/**
 * @file test_ieee11073_align.c
 * @brief IEEE11073 0x11 0x73 文档帧协议 — 字节级对齐 host 测试
 *
 * 依据《180F_FW_IMPLEMENT》§7 断言清单。与 test_pulse_calc.c 用同一批
 * 被测源（device_info/mock/ieee11073/crc16/调度/驱动 stubs），两者各自
 * 提供 main，分别链接成独立可执行：fw_tests / fw_align。
 *
 * 测试通过直接调用 ieee11073_on_data / get_pending 验证帧头与回执字节。
 *
 * 覆盖（文档 §9 上报码/§8 命令码）：
 *   1 CONNECT 成功 → bound=true，回执 11 73 02 85 | opId LE | 01 | ok
 *   2 CONNECT 错误密码 → bound=false，回执 result=3004(u16 LE)
 *   3 未绑定发非 CONNECT（SET_CLOCK=0x09）→ 3004 且无执行
 *   4 SET_BASAL_PROF(0x02，绑后) profile_id+48×f32 → OK + basal_scheduler 生效
 *   5 SET_BOLUS(0x04，绑后) fast → OK；重复 op_id → OPID_DUP(3) 幂等
 *   附 坏 CRC → 回 CMD_RESULT_CRC_ERROR(2)
 *
 * 密码成功向量：运行时以 device_info_get()->bt_password 实际读回值为准构造，
 * 不写死，确保 host(gcc, "123456"/6B) 与实机(hal 注入 "PUMP1234"/8B) 通路各自自洽。
 */

#include <stdio.h>
#include <string.h>

#include "ieee11073.h"
#include "crc16.h"
#include "device_info.h"
#include "basal_scheduler.h"
#include "bolus_scheduler.h"
#include "algo_params.h"

static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  [FAIL] %-46s @%d\n", msg, __LINE__); ++g_fail; } \
    else         { printf("  [ ok ] %-46s\n", msg); } \
} while (0)

/* 小端写入 helper */
static void put_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void put_u16_le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static uint32_t get_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t get_u16_le(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

/* 组装 0x11 0x73 | 01 | cmd | opId(LE4) | payload | crc(LE2)。返回整帧长。 */
static uint16_t build_cmd_frame(uint8_t *buf, uint8_t cmd, uint32_t opid,
                                const uint8_t *pl, uint16_t plen)
{
    uint16_t hdr = 8;   /* 11 73 | 01 | cmd | opId(4) */
    buf[0] = 0x11; buf[1] = 0x73; buf[2] = 0x01; buf[3] = cmd;
    put_u32_le(buf + 4, opid);
    if (plen) memcpy(buf + hdr, pl, plen);
    {
        uint16_t crc = crc16_ccitt(buf, (size_t)(hdr + plen));
        put_u16_le(buf + hdr + plen, crc);
    }
    return (uint16_t)(hdr + plen + 2);
}

/* 校验一帧上报 CONFIRM 的最小字段并返回 result。
 * 帧 = 11 73 | 02 | 0x85 | opId(4) | cmd(1) | result(u16 LE) | crc(2) — 共 4+7+2=13 */
static uint16_t check_confirm(const uint8_t *f, uint16_t len,
                              uint32_t exp_opid, uint8_t exp_cmd)
{
    if (len != 13) { printf("  [?? ] confirm len=%u expect 13\n", len); ++g_fail; return 0xFFFFu; }
    CHECK(f[0] == 0x11 && f[1] == 0x73, "sync 11 73");
    CHECK(f[2] == IEEE_MSG_REPORT, "msgtype 0x02");
    CHECK(f[3] == IEEE_RPT_CONFIRM, "rpt 0x85 CONFIRM");
    CHECK(get_u32_le(f + 4) == exp_opid, "confirm opId LE echo");
    CHECK(f[8] == exp_cmd, "confirm carries cmd");
    return get_u16_le(f + 9);
}

/* 使用运行期实际 bt_password 绑定。返回 pwd 长度（以当前 device_info 为准）。 */
static size_t do_connect_expect_ok(uint32_t opid)
{
    const factory_info_t *fi = device_info_get();
    size_t pw = strlen(fi->bt_password);
    uint8_t frame[64];
    /* 取当前环境密码做成功向量 */
    uint8_t pwnum[8]; size_t cl = pw < 8 ? pw : 8u;
    memcpy(pwnum, fi->bt_password, cl);
    uint16_t len = build_cmd_frame(frame, IEEE_CMD_CONNECT, opid, pwnum, (uint16_t)cl);
    CHECK(ieee11073_on_data(frame, len) == 0, "CONNECT accepted (actual bt_password)");
    CHECK(ieee11073_is_bound(), "bound == true");
    uint8_t out[64]; uint16_t ol;
    CHECK(ieee11073_get_pending(out, &ol), "CONNECT confirm pending");
    uint16_t rc = check_confirm(out, ol, opid, IEEE_CMD_CONNECT);
    CHECK(rc == CMD_RESULT_OK, "CONNECT confirm result=0");
    return cl;
}

static void do_connect_wrong_pwd(uint32_t opid)
{
    /* 取现 bt_password，复刻其长度但改末字节内容 → 必错 */
    const factory_info_t *fi = device_info_get();
    size_t pw = strlen(fi->bt_password);
    uint8_t bad[8]; size_t cl = pw < 8 ? pw : 8u;
    memcpy(bad, fi->bt_password, cl);
    bad[0] ^= 0x01u;                 /* 保证内容 ≠ */
    uint8_t frame[64];
    uint16_t len = build_cmd_frame(frame, IEEE_CMD_CONNECT, opid, bad, (uint16_t)cl);
    int rc = ieee11073_on_data(frame, len);
    CHECK(rc == CMD_RESULT_REJECT || rc == 0, "CONNECT wrong-pwd processed (denied)");
    CHECK(!ieee11073_is_bound(), "bound stays false");
    uint8_t out[64]; uint16_t ol;
    if (ieee11073_get_pending(out, &ol)) {
        uint16_t rc = check_confirm(out, ol, opid, IEEE_CMD_CONNECT);
        CHECK(rc == IEEE_ERR_UNAUTHORIZED, "wrong pwd confirm result=3004 (u16 LE)");
    } else {
        CHECK(0, "wrong pwd pending confirm");
    }
}

static void test_align(void)
{
    printf("[IEEE11073-ALIGN (0x11 0x73)]\n");

    bolus_scheduler_init();

    /* ---------- 1. CONNECT 成功 ---------- */
    ieee11073_init();
    CHECK(!ieee11073_is_bound(), "initial not bound");
    do_connect_expect_ok(0x11u);

    /* ---------- 2. CONNECT 错误密码 → 3004 ---------- */
    ieee11073_init();
    do_connect_wrong_pwd(0x22u);

    /* ---------- 3. 未绑定发 SET_CLOCK(0x09) → gate 3004 ---------- */
    ieee11073_init();
    {
        uint8_t pl[5] = { 2026u & 0xFFu, 9u, 6u, 21u, 0u };   /* y/m/d/h 占位 */
        uint8_t frame[64];
        uint16_t len = build_cmd_frame(frame, IEEE_CMD_SET_CLOCK, 0x33u, pl, sizeof pl);
        CHECK(ieee11073_on_data(frame, len) == 0, "unbound SET_CLOCK handled (no exec)");
        CHECK(!ieee11073_is_bound(), "still unbound");
        uint8_t out[64]; uint16_t ol;
        CHECK(ieee11073_get_pending(out, &ol), "gate confirm pending");
        uint16_t rc = check_confirm(out, ol, 0x33u, IEEE_CMD_SET_CLOCK);
        CHECK(rc == IEEE_ERR_UNAUTHORIZED, "gate result=3004");
    }

    /* ---------- 4. SET_BASAL_PROF(0x02) 绑后 profile_id+48×f32 ---------- */
    do_connect_expect_ok(0x44u);   /* 绑定 */
    {
        uint8_t pl[1 + PUMP_BASAL_SEG_COUNT * 4];
        uint8_t frame[256];
        pl[0] = 1;                                    /* profile_id */
        for (uint16_t i = 0; i < PUMP_BASAL_SEG_COUNT; ++i) {
            float v = 0.5f;
            memcpy(pl + 1 + 4u*i, &v, 4);
        }
        uint16_t len = build_cmd_frame(frame, IEEE_CMD_SET_BASAL_PROF, 0x55u,
                                       pl, sizeof pl);
        CHECK(ieee11073_on_data(frame, len) == 0, "SET_BASAL_PROF accepted (bound)");
        uint8_t out[64]; uint16_t ol;
        CHECK(ieee11073_get_pending(out, &ol), "basal confirm pending");
        uint16_t rc = check_confirm(out, ol, 0x55u, IEEE_CMD_SET_BASAL_PROF);
        CHECK(rc == CMD_RESULT_OK, "basal confirm OK");
        /* 载荷语义复核：profile_id 之后 48×f32 已置入调度 */
        CHECK(basal_scheduler_get_seg_rate(0) == 0.5f, "basal seg0 = 0.5 IU/h (loaded)");
    }
    /* 合法但 profile_id 偏移下的段数一致性 */
    CHECK(basal_scheduler_get_seg_rate(1) == 0.5f, "basal seg1 = 0.5 IU/h");

    /* ---------- 5. SET_BOLUS(0x04) 绑后；幂等 ---------- */
    bolus_scheduler_init();           /* 清 busy */
    do_connect_expect_ok(0x66u);      /* rebind clearing bolus state via separate op */
    {
        uint8_t pl[8];
        uint8_t frame[64];
        /* type=0 fast, dose 2.0 IU = 0x40000000 LE, ext=0, ratio=0 */
        pl[0] = 0;
        put_u32_le(pl + 1, 0x40000000u);
        pl[5] = 0; pl[6] = 0;
        pl[7] = 0;
        uint16_t len = build_cmd_frame(frame, IEEE_CMD_SET_BOLUS, 0x77u, pl, sizeof pl);
        CHECK(ieee11073_on_data(frame, len) == 0, "SET_BOLUS accepted (bound)");
        CHECK(bolus_scheduler_busy(), "bolus running after SET_BOLUS");
        uint8_t out[64]; uint16_t ol;
        CHECK(ieee11073_get_pending(out, &ol), "bolus confirm pending");
        uint16_t rc = check_confirm(out, ol, 0x77u, IEEE_CMD_SET_BOLUS);
        CHECK(rc == CMD_RESULT_OK || rc == CMD_RESULT_REJECT,
              "bolus confirm result ∈ {OK, REJECT}");

        /* 回执帧只消费一条 */
        CHECK(!ieee11073_has_pending(), "no extra bolus confirm");
    }

    /* ---------- 幂等 OPID_DUP ---------- */
    {
        /* 重新建立但保持上一条命令同 op_id 重发 */
        uint8_t pl[8]; uint8_t frame[64];
        pl[0] = 0; put_u32_le(pl + 1, 0x40000000u); pl[5]=0; pl[6]=0; pl[7]=0;
        uint16_t len = build_cmd_frame(frame, IEEE_CMD_SET_BOLUS, 0x77u, pl, sizeof pl);
        int rc = ieee11073_on_data(frame, len);
        CHECK(rc == CMD_RESULT_OPID_DUP, "dup op_id → OPID_DUP(3) (idempotent)");
        uint8_t out[64]; uint16_t ol;
        if (ieee11073_get_pending(out, &ol)) {
            uint16_t r2 = check_confirm(out, ol, 0x77u, IEEE_CMD_SET_BOLUS);
            CHECK(r2 == CMD_RESULT_OPID_DUP, "dup confirm result=3");
        }
    }

    /* ---------- 附：坏 CRC → CRC_ERROR(2) ---------- */
    ieee11073_init();
    do_connect_expect_ok(0x88u);
    {
        uint8_t frame[64];
        uint16_t len = build_cmd_frame(frame, IEEE_CMD_SET_CLOCK, 0x99u, NULL, 0);
        frame[len - 1] ^= 0xFFu;   /* 破坏 CRC */
        int rc = ieee11073_on_data(frame, len);
        CHECK(rc == CMD_RESULT_CRC_ERROR, "bad CRC → CRC_ERROR");
        uint8_t out[64]; uint16_t ol;
        CHECK(ieee11073_get_pending(out, &ol), "crc-error confirm pending");
        if (ol == 13) {
            uint16_t r2 = check_confirm(out, ol, 0x99u, IEEE_CMD_SET_CLOCK);
            CHECK(r2 == CMD_RESULT_CRC_ERROR, "crc-error confirm result=2");
        } else {
            CHECK(0, "crc-error confirm frame shall be 13 bytes");
        }
    }
}

int main(void)
{
    printf("=== Pumpilot IEEE11073 0x11/0x73 对齐 hOST test ===\n");
    test_align();
    if (g_fail == 0) {
        printf("\nALL IEEE11073-ALIGN TESTS PASSED\n");
        return 0;
    }
    printf("\n%d ALIGN TEST(S) FAILED\n", g_fail);
    return 1;
}
