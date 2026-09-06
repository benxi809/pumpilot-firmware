/**
 * @file test_pulse_calc.c
 * @brief FW-D1 纯逻辑 PC 单元测试（host gcc 可编译运行）。
 *
 * 覆盖：脉冲换算（快速/扩展/基础率/复合）、格栅药量映射、蛇形序表完整性、
 * CRC16-CCITT、状态机转换、环形缓冲。不含板载依赖。
 */
#include <stdio.h>
#include <string.h>
#include "pulse_calc.h"
#include "crc16.h"
#include "ringbuf.h"
#include "pump_state.h"
#include "basal_scheduler.h"
#include "bolus_scheduler.h"
#include "ieee11073.h"
#include "alarm_engine.h"
#include "alarm_app.h"
#include "device_info.h"
#include "log_manager.h"
#include "power_mgr.h"
#include "algo_params.h"

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  [FAIL] %s (line %d)\n", msg, __LINE__); g_fail++; } \
    else { printf("  [ ok ] %s\n", msg); } \
} while (0)

extern const uint8_t grid_seq[];

static void test_crc16(void)
{
    printf("[CRC16-CCITT]\n");
    uint8_t data[] = "123456789";
    uint16_t crc = crc16_ccitt(data, 9);
    CHECK(crc == 0x29B1u, "crc16('123456789') == 0x29B1");
    uint16_t incr = CRC16_INIT;
    size_t i;
    for (i = 0; i < 9; ++i) incr = crc16_ccitt_byte(incr, data[i]);
    CHECK(incr == crc, "incremental == block");
}

static void test_pulse_calc(void)
{
    printf("[PulseCalc]\n");
    CHECK(pulse_calc_fast_bolus(0.5f) == 147u, "0.5IU fast -> 147");
    CHECK(pulse_calc_fast_bolus(1.0f) == 295u, "1.0IU fast -> 295");
    CHECK(pulse_calc_fast_bolus(2.5f) == 737u, "2.5IU fast -> 737");
    CHECK(pulse_calc_basal_segment(0.5f) == 73u, "basal 0.5 IU/h seg -> 73 (floor)");
    CHECK(pulse_calc_basal_segment(1.0f) == 147u, "basal 1.0 IU/h seg -> 147");
    CHECK(pulse_calc_basal_segment(2.0f) == 294u, "basal 2.0 IU/h seg -> 294");
    /* 扩展：1.0IU/60min, slot3 → 每槽 0.05IU/0.00339 = 14.75 → floor 14 */
    CHECK(pulse_calc_ext_slot(1.0f, 60u, 3u) == 14u, "ext 1IU/60min slot3 -> 14");
    /* 复合：2.0IU, 50% → bolus 1.0IU → 295 */
    CHECK(pulse_calc_dual_bolus_part(2.0f, 50u) == 295u, "dual 2IU 50% -> 295");
    CHECK(pulse_calc_dual_bolus_part(2.0f, 100u) == pulse_calc_fast_bolus(2.0f),
          "dual 100% == fast");
    CHECK(pulse_calc_seg_of_minute(0u) == 0u, "seg(00:00) -> 0");
    CHECK(pulse_calc_seg_of_minute(30u) == 1u, "seg(00:30) -> 1");
    CHECK(pulse_calc_seg_of_minute(1439u) == 47u, "seg(23:59) -> 47 (clamp)");
}

static void test_reservoir(void)
{
    printf("[Reservoir+Grid]\n");
    CHECK(pulse_calc_reservoir_ml(1u) == 0.0f, "seq1(K7) -> 0ml");
    CHECK(pulse_calc_reservoir_ml(64u) == 2.0f, "seq64(K57) -> 2ml");
    CHECK(pulse_calc_reservoir_ml(32u) > 0.98f && pulse_calc_reservoir_ml(32u) < 0.99f,
          "seq32 -> ~0.984ml");
    /* 蛇形序表完整性：恰好覆盖 K1..K64 各一次 */
    {
        int seen[65] = { 0 };
        int i, ok = 1;
        for (i = 0; i < 64; ++i) {
            uint8_t k = grid_seq[i];
            if (k == 0 || k > 64 || seen[k]) { ok = 0; break; }
            seen[k] = 1;
        }
        CHECK(ok == 1, "grid_seq covers K1..K64 uniquely");
        CHECK(grid_seq[0] == 7 && grid_seq[63] == 57,
              "grid_seq[0]=K7(0ml), grid_seq[63]=K57(2ml)");
    }
}

static void test_state_machine(void)
{
    printf("[StateMachine]\n");
    state_machine_init();
    CHECK(state_machine_get() == ST_WAREHOUSE, "init -> WAREHOUSE");
    CHECK(state_machine_transit(EV_FILL_WAKE), "WAREHOUSE + FILL_WAKE -> INIT_SLEEP");
    CHECK(state_machine_get() == ST_INIT_SLEEP, "now INIT_SLEEP");
    CHECK(state_machine_transit(EV_APPLY_START), "INIT_SLEEP + APPLY_START -> INITIALIZING");
    CHECK(state_machine_transit(EV_LOCK_DONE), "INITIALIZING + LOCK_DONE -> READY");
    CHECK(state_machine_get() == ST_READY, "now READY");
    CHECK(state_machine_transit(EV_START_INFUSE), "READY + START_INFUSE -> RUNNING");
    CHECK(state_machine_transit(EV_PAUSE), "RUNNING + PAUSE -> PAUSED");
    CHECK(state_machine_transit(EV_RESUME), "PAUSED + RESUME -> RUNNING");
    CHECK(state_machine_transit(EV_ABANDON), "RUNNING + ABANDON -> ABANDONED");
    CHECK(state_machine_get() == ST_ABANDONED, "now ABANDONED");
    /* 非法转换 */
    CHECK(!state_machine_transit(EV_START_INFUSE), "ABANDONED + START_INFUSE invalid");
    /* 一级报警 */
    state_machine_init();
    state_machine_transit(EV_FILL_WAKE);
    state_machine_transit(EV_APPLY_START);
    state_machine_transit(EV_LOCK_DONE);
    state_machine_transit(EV_START_INFUSE);
    state_machine_transit(EV_ALERT_L1);
    CHECK(state_machine_get() == ST_PAUSED, "RUNNING + ALERT_L1 -> PAUSED (stop infusion)");
}

static void test_ringbuf(void)
{
    printf("[RingBuf]\n");
    uint8_t buf[8];
    ringbuf_t rb;
    ringbuf_init(&rb, buf, 8);
    uint8_t b;
    CHECK(ringbuf_empty(&rb), "empty initially");
    CHECK(ringbuf_push(&rb, 0x11), "push 0x11");
    CHECK(ringbuf_push(&rb, 0x73), "push 0x73");
    CHECK(ringbuf_readable(&rb) == 2u, "readable 2");
    CHECK(ringbuf_pop(&rb, &b) && b == 0x11, "pop 0x11");
    CHECK(ringbuf_pop(&rb, &b) && b == 0x73, "pop 0x73");
    CHECK(ringbuf_empty(&rb), "empty after pop");
    /* 回绕写入 */
    {
        uint8_t data[8] = {1,2,3,4,5,6};
        CHECK(ringbuf_write(&rb, data, 6) == 6u, "bulk write 6");
        uint8_t out[8];
        CHECK(ringbuf_read(&rb, out, 6) == 6u && out[5] == 6u, "bulk read 6");
    }
}

/* ================= FW-D2：基础率调度测试 ================= */
static void test_basal_scheduler(void)
{
    printf("[BasalScheduler]\n");
    float table[PUMP_BASAL_SEG_COUNT] = {0}; /* 先载全 0 */
    uint32_t i, total, sum = 0, maxdiff = 0;

    /* 载入：全部 1.0 IU/h */
    for (i = 0; i < PUMP_BASAL_SEG_COUNT; ++i) table[i] = 1.0f;
    basal_scheduler_load(table);
    CHECK(basal_scheduler_loaded(), "loaded");
    CHECK(basal_scheduler_get_seg_rate(0) == 1.0f, "seg0 rate 1.0");
    /* 1.0 IU/h 段总脉冲 = floor(1.0*0.5/0.00339)=147 */
    CHECK(basal_scheduler_seg_pulses(0) == 147u, "seg0 total pulses 147");

    /* Bresenham 匀距：10 槽和 = 总脉冲，各槽差 ≤1 */
    total = basal_scheduler_seg_pulses(0);
    for (i = 0; i < BASAL_SLOTS_PER_SEG; ++i) {
        uint32_t p = basal_scheduler_slot_pulses(0, (uint8_t)i);
        sum += p;
    }
    CHECK(sum == total, "10 slots sum == seg total");
    for (i = 1; i < BASAL_SLOTS_PER_SEG; ++i) {
        uint32_t a = basal_scheduler_slot_pulses(0, (uint8_t)(i - 1));
        uint32_t b = basal_scheduler_slot_pulses(0, (uint8_t)i);
        uint32_t d = (a > b) ? (a - b) : (b - a);
        if (d > maxdiff) maxdiff = d;
    }
    CHECK(maxdiff <= 1u, "slot pulse diff <=1 (uniform)");

    /* pulses_at：分钟定位 */
    {
        uint8_t seg, slot;
        uint32_t p = basal_scheduler_pulses_at(0, &seg, &slot);  /* 00:00, seg0 slot0 */
        CHECK(seg == 0 && slot == 0, "00:00 -> seg0 slot0");
        p = basal_scheduler_pulses_at(90, &seg, &slot);          /* 01:30 -> seg3 slot0 */
        CHECK(seg == 3 && slot == 0, "01:30 -> seg3 slot0");
        p = basal_scheduler_pulses_at(95, &seg, &slot);          /* 01:35 -> seg3 slot1(5/3) */
        CHECK(seg == 3 && slot == 1u, "01:35 -> seg3 slot1");
        (void)p;
    }

    /* 启停激活 */
    basal_scheduler_start();
    CHECK(basal_scheduler_is_active(), "start -> active");
    basal_scheduler_pause();
    CHECK(!basal_scheduler_is_active(), "pause -> inactive");
    basal_scheduler_resume();
    CHECK(basal_scheduler_is_active(), "resume -> active");
}

/* ================= FW-D2：大剂量调度测试 ================= */
static void test_bolus_scheduler(void)
{
    printf("[BolusScheduler]\n");
    bolus_scheduler_init();

    /* 快速大剂量：2.5 IU 一次输出 737 脉冲 */
    {
        bolus_spec_t sp = { 100u, BOLUS_FAST, 2.5f, 0u, 0u };
        uint32_t pulses; bool final = false;
        CHECK(bolus_scheduler_start(&sp), "fast start accepted");
        CHECK(bolus_scheduler_busy(), "busy while running");
        CHECK(bolus_scheduler_poll(&pulses, &final), "fast poll has output");
        CHECK(pulses == 737u, "fast 2.5IU -> 737 pulses");
        CHECK(bolus_scheduler_state() == BOLUS_IDLE, "fast completes -> IDLE");
    }

    /* 幂等：同 op_id 重复 → 拒绝 */
    {
        bolus_spec_t sp = { 100u, BOLUS_FAST, 1.0f, 0u, 0u };
        CHECK(!bolus_scheduler_start(&sp), "duplicate op_id rejected (idempotent)");
    }
    /* 不同 op_id 可执行 */
    {
        bolus_spec_t sp = { 101u, BOLUS_FAST, 1.0f, 0u, 0u };
        uint32_t pulses; bool final;
        CHECK(bolus_scheduler_start(&sp), "new op_id accepted");
        bolus_scheduler_poll(&pulses, &final);
        (void)pulses; (void)final;
    }

    /* 扩展大剂量：1.0 IU/60min → 每3分钟槽(20槽) 匀距，总和=295 */
    {
        bolus_spec_t sp = { 200u, BOLUS_EXT, 1.0f, 60u, 0u };
        uint32_t pulses, total = 0, slots = 0; bool final;
        bolus_scheduler_init();
        CHECK(bolus_scheduler_start(&sp), "ext start");
        while (bolus_scheduler_busy()) {
            if (bolus_scheduler_poll(&pulses, &final)) {
                total += pulses; slots++;
            } else break;
        }
        CHECK(total == 295u, "ext total pulses 295");
        /* 60min/3min = 20 slots */
        (void)slots;
    }

    /* 复合大剂量：2.0 IU, 50% → 前半295 + 后半295，两次阶段 */
    {
        bolus_spec_t sp = { 300u, BOLUS_DUAL, 2.0f, 60u, 50u };
        uint32_t pulses, a = 0, b = 0; bool final;
        bolus_scheduler_init();
        CHECK(bolus_scheduler_start(&sp), "dual start");
        /* 第一次 poll：bolus 部分 295 */
        CHECK(bolus_scheduler_poll(&pulses, &final) && pulses == 295u, "dual bolus part 295");
        a = pulses;
        /* 之后推进扩展部分，累计到总完成 */
        while (bolus_scheduler_busy()) {
            if (bolus_scheduler_poll(&pulses, &final)) b += pulses;
            else break;
        }
        CHECK(a == 295u && b == 295u, "dual bolus 295 + ext 295");
    }
}

/* ============ FW-D3：IEEE11073 agent 测试（0x11 0x73 文档帧） ============ */
/* 构造一条 0x11 0x73 文档命令帧：
 *   11 73 | 01 | cmd | opId(LE4) | payload | crc(LE2)
 */
static uint16_t build_cmd(uint8_t *buf, uint8_t cmd, uint32_t op_id,
                          const uint8_t *payload, uint16_t plen)
{
    uint16_t i;
    buf[0] = 0x11; buf[1] = 0x73; buf[2] = 0x01; buf[3] = cmd;
    buf[4] = (uint8_t)op_id; buf[5] = (uint8_t)(op_id >> 8);
    buf[6] = (uint8_t)(op_id >> 16); buf[7] = (uint8_t)(op_id >> 24);
    for (i = 0; i < plen; ++i) buf[8 + i] = payload[i];
    {
        uint16_t crc = crc16_ccitt(buf, (size_t)(8 + plen));
        buf[8 + plen] = (uint8_t)crc;
        buf[9 + plen] = (uint8_t)(crc >> 8);
    }
    return (uint16_t)(8 + plen + 2);
}

static void test_ieee11073(void)
{
    printf("[IEEE11073]\n");
    ieee11073_init();

    /* 上报帧构建 + CRC 校验：11 73 | 02 | rpt | payload... | crc
     * 3 字节载荷 → 总长 4+3+2 = 9 */
    {
        uint8_t f[IEEE11073_FRAME_MAX];
        int n = ieee11073_build_report(IEEE_RPT_MED, (const uint8_t[]){1, 0, 0, 0x80}, 4, f);
        CHECK(n == 10, "RPT frame len 10 (4 hdr + 4 pl + 2 crc)");
        CHECK(f[0] == 0x11 && f[1] == 0x73, "sync 11 73");
        CHECK(f[2] == 0x02, "msgtype 0x02");
        CHECK(f[3] == IEEE_RPT_MED, "rpt type 0x83");
        /* 校验整帧 CRC（从首字节到 payload 末，不含 CRC 两字节） */
        {
            uint16_t len = (uint16_t)(n - 2);
            uint16_t crc = crc16_ccitt(f, len);
            uint16_t crcRd = (uint16_t)((uint16_t)f[len] | ((uint16_t)f[len+1] << 8));
            CHECK(crc == crcRd, "RPT frame CRC valid");
        }
    }

    /* 结合 CONNECT：未绑定时非 CONNECT 命令一律 3004 拒绝 (gate) */
    {
        uint8_t b[32];
        uint16_t n = build_cmd(b, IEEE_CMD_SET_CLOCK, 0x01, (const uint8_t[]){0}, 0);
        CHECK(ieee11073_on_data(b, n) == 0, "unbound SET_CLOCK handler returns 0");
        CHECK(ieee11073_has_pending(), "gate result pending");
        uint8_t out[64]; uint16_t ol;
        CHECK(ieee11073_get_pending(out, &ol), "get pending");
        /* 帧: 11 73 02 85 | opId(4)+cmd(1)+result(u16 LE)=3004 → 总长 4+7+2=13 */
        CHECK(ol == 13, "gate confirm frame len 13");
        CHECK(out[0] == 0x11 && out[1] == 0x73 && out[2] == 0x02, "gate frame hdr 11 73 02");
        CHECK(out[3] == IEEE_RPT_CONFIRM, "gate rpt=0x85");
        CHECK(out[4]==0x01 && out[5]==0x00 && out[6]==0x00 && out[7]==0x00, "opId LE");
        CHECK(out[8] == IEEE_CMD_SET_CLOCK, "cmd echoed");
        uint16_t code = out[9] | (uint16_t)(out[10] << 8);
        CHECK(code == 3004u, "result u16 LE == 3004 (unauthorized)");
    }

    /* CONNECT 成功 → bound=true, 回执 result=0 */
    {
        uint8_t pwd[8];
        const factory_info_t *fi = device_info_get();
        size_t pwlen = strlen(fi->bt_password);
        bool use_known = (pwlen <= sizeof pwd);
        const char *known = use_known ? fi->bt_password : "PUMP1234";
        memcpy(pwd, known, sizeof pwd);
        size_t plen2 = (use_known) ? pwlen : 8u;
        uint8_t b[64];
        uint16_t n = build_cmd(b, IEEE_CMD_CONNECT, 0xABCDu, pwd, (uint16_t)plen2);
        CHECK(ieee11073_on_data(b, n) == 0, "CONNECT accepted");
        CHECK(ieee11073_is_bound(), "bound after CONNECT");
        CHECK(ieee11073_has_pending(), "CONNECT result pending");
        uint8_t out[64]; uint16_t ol;
        CHECK(ieee11073_get_pending(out, &ol), "get connect pending");
        CHECK(out[3] == IEEE_RPT_CONFIRM, "rpt=0x85 in connect confirm");
        /* opId = out[4..7] LE */
        uint32_t opid = (uint32_t)out[4] | ((uint32_t)out[5] << 8)
                      | ((uint32_t)out[6] << 16) | ((uint32_t)out[7] << 24);
        CHECK(opid == 0xABCDu, "opId echoed");
        CHECK(out[8] == IEEE_CMD_CONNECT && out[9] == 0 && out[10] == 0,
              "connect cmd=0x01 result=0 (u16LE)");
    }

    /* 错误 CRC → 回 CRC_ERROR (2)，仍有收到 cmd (SET_CLOCK) */
    {
        uint8_t b[32];
        uint16_t n = build_cmd(b, IEEE_CMD_SET_CLOCK, 0x02, 0, 0);
        b[n-1] ^= 0xFF; /* 破坏 CRC */
        CHECK(ieee11073_on_data(b, n) == CMD_RESULT_CRC_ERROR, "bad CRC rejected");
        uint8_t out[64]; uint16_t ol;
        if (ieee11073_get_pending(out, &ol)) {
            CHECK(out[3] == IEEE_RPT_CONFIRM, "crc err rpt=0x85");
            uint16_t code = out[9] | (uint16_t)(out[10] << 8);
            CHECK(code == CMD_RESULT_CRC_ERROR, "crc err result=2");
        }
    }

    /* SET_BASAL_PROFILE (绑后，payload 含 profile_id + 48×f32) */
    {
        uint8_t pl[1 + 48*4];
        uint8_t b[256];
        uint16_t i, n;
        float t[PUMP_BASAL_SEG_COUNT];
        pl[0] = 1; /* profile_id */
        for (i = 0; i < PUMP_BASAL_SEG_COUNT; ++i) t[i] = 1.0f;
        for (i = 0; i < PUMP_BASAL_SEG_COUNT; ++i) {
            memcpy(pl + 1 + i*4, &t[i], 4);
        }
        n = build_cmd(b, IEEE_CMD_SET_BASAL_PROF, 0x20, pl, sizeof pl);
        CHECK(ieee11073_on_data(b, n) == 0, "SET_BASAL_PROF accepted");
        CHECK(basal_scheduler_get_seg_rate(0) == 1.0f, "basal table loaded via BLE");
        CHECK(ieee11073_has_pending(), "basal result pending");
        uint8_t out[64]; uint16_t ol;
        if (ieee11073_get_pending(out, &ol)) {
            uint16_t code = out[9] | (uint16_t)(out[10] << 8);
            CHECK(code == CMD_RESULT_OK, "basal confirm result=0");
        }
    }

    /* SET_BOLUS → 启动大剂量；幂等拒绝重复 op_id */
    {
        uint8_t pl[8];
        uint8_t b[64];
        uint16_t n;
        bolus_scheduler_init();
        ieee11073_init(); /* 清 pending + 幂等历史 + 绑定态 */
        /* 需先 reconnect → 绑定 */
        {
            uint8_t pwd[8];
            const factory_info_t *fi = device_info_get();
            size_t ln = strlen(fi->bt_password);
            memcpy(pwd, fi->bt_password, ln > 8 ? 8 : ln);
            uint8_t bc[64];
            uint16_t n2 = build_cmd(bc, IEEE_CMD_CONNECT, 0xCCC1u, pwd, (uint16_t)ln);
            ieee11073_on_data(bc, n2);
        }
        pl[0] = 0; /* fast */
        pl[1]=0; pl[2]=0; pl[3]=0x80; pl[4]=0x3F; /* dose 1.0 IU */
        pl[5]=0; pl[6]=0; /* ext 0 */
        pl[7]=0; /* ratio 0 */
        n = build_cmd(b, IEEE_CMD_SET_BOLUS, 0x10, pl, 8);
        uint8_t out[64]; uint16_t ol;
        /* 第一次：启动 */
        CHECK(ieee11073_on_data(b, n) == 0, "first SET_BOLUS accepted");
        CHECK(bolus_scheduler_busy(), "SET_BOLUS -> bolus running");
        /* 幂等：同 op_id 重发 → 返回 OPID_DUP，不重启 */
        CHECK(ieee11073_on_data(b, n) == CMD_RESULT_OPID_DUP,
              "dup op_id -> OPID_DUP (idempotent)");
        if (ieee11073_has_pending() && ieee11073_get_pending(out, &ol)) {
            uint16_t code = out[9] | (uint16_t)(out[10] << 8);
            CHECK(code == CMD_RESULT_OPID_DUP, "dup result=3");
        }
    }

    /* 鉴权绑定 */
    ieee11073_init();
    CHECK(!ieee11073_is_bound(), "not bound initially");
    CHECK(!ieee11073_auth_bind_pwd((const uint8_t*)"", 0), "empty pwd rejected");
    {
        const factory_info_t *fi = device_info_get();
        size_t ln = strlen(fi->bt_password);
        uint8_t pwd[8]; memcpy(pwd, fi->bt_password, ln > 8 ? 8 : ln);
        CHECK(ieee11073_auth_bind_pwd(pwd, (uint16_t)ln), "pwd accepted");
    }
    CHECK(ieee11073_is_bound(), "bound after auth");
}

/* ================= FW-D4：报警 / 出厂信息 / 日志 / 低功耗 ================= */

static void test_alarm_engine(void)
{
    printf("[FW-D4 AlarmEngine]\n");
    alarm_engine_init();

    /* 初始无报警 */
    CHECK(alarm_engine_get_active() == 0, "no alarm initially");
    CHECK(alarm_engine_active_count() == 0, "count 0");

    /* 巡检：堵塞置一级 */
    alarm_input_t in = {0};
    in.occlusion = true;
    alarm_engine_check(&in);
    CHECK(alarm_engine_is_active(ALARM_BIT_OCLUSION), "occlusion active");
    CHECK(alarm_engine_level(ALARM_BIT_OCLUSION) == ALERT_LEVEL_1, "occlusion lvl1");
    CHECK(alarm_engine_take_new(), "new-alarm flag set");

    /* 消费后不再重复标记（同状态不变） */
    CHECK(!alarm_engine_take_new(), "no new alarm after consume");

    /* 电池耗尽一级 / 电量低二级 */
    alarm_input_t in2 = {0};
    in2.occlusion = true;
    in2.batt_low  = true;
    in2.batt_dead = false;
    alarm_engine_check(&in2);
    CHECK(alarm_engine_is_active(ALARM_BIT_BATT_LOW), "batt_low active");
    CHECK(alarm_engine_level(ALARM_BIT_BATT_LOW) == ALERT_LEVEL_2, "batt_low lvl2");

    /* 三级失联 */
    alarm_input_t in3 = {0};
    in3.occlusion = false;
    in3.batt_low  = false;
    in3.lost      = true;
    alarm_engine_check(&in3);
    CHECK(alarm_engine_is_active(ALARM_BIT_LOST), "lost active");
    CHECK(alarm_engine_level(ALARM_BIT_LOST) == ALERT_LEVEL_3, "lost lvl3");
    CHECK(!alarm_engine_is_active(ALARM_BIT_OCLUSION), "occlusion cleared");
    CHECK(alarm_engine_take_new(), "lost is new alarm");

    /* 清除 */
    alarm_engine_clear(ALARM_BIT_LOST);
    CHECK(!alarm_engine_is_active(ALARM_BIT_LOST), "lost cleared");
    CHECK(alarm_engine_active_count() == 0, "count 0 after clear");
}

/* alarm_app 蜂鸣驱动记录 */
static int g_beep_level = 0;
static int g_beep_active = 0;
static int g_report_mask = 0;
static int g_report_level = 0;
static int g_report_count = 0;
static void mock_beeper(alert_level_t lvl, bool active)
{
    g_beep_level = lvl;
    g_beep_active = active ? 1 : 0;
}
static void mock_report(uint16_t mask, alert_level_t lvl)
{
    g_report_mask = mask;
    g_report_level = lvl;
    g_report_count++;
}

static void test_alarm_app(void)
{
    printf("[FW-D4 AlarmApp]\n");
    alarm_app_config_t cfg = { mock_beeper, mock_report };
    alarm_app_init(&cfg);
    alarm_engine_init();

    /* 无报警：蜂鸣关闭 */
    alarm_app_tick();
    CHECK(g_beep_active == 0, "no alarm → beeper off");

    /* 一级：持续鸣叫 */
    alarm_input_t in = {0};
    in.occlusion = true;
    alarm_engine_check(&in);
    alarm_app_report_now();
    CHECK(g_report_count == 1, "report triggered on alarm");
    CHECK(g_report_level == ALERT_LEVEL_1, "report level 1");
    alarm_app_tick();
    CHECK(g_beep_active == 1, "lvl1 → beeper continuous on");

    /* 清除一级并设三级 */
    alarm_input_t in3 = {0};
    in3.lost = true;
    alarm_engine_check(&in3);
    alarm_app_tick();
    CHECK(g_beep_active == 1, "lvl3 beeps on");
}

static void test_device_info(void)
{
    printf("[FW-D4 DeviceInfo]\n");
    factory_info_t fi = {
        .model = "PLT1", .serial = "PL00000001", .bt_password = "654321",
        .prod_batch = "20260801AB", .expiry = "20290801", .factory = "SH",
        .mech_model = "M2Y", .mech_vendor = "V02", .mech_batch = "MB000002",
        .pcb_model = "PCB0002", .pcb_vendor = "P02", .pcb_batch = "PB000002",
        .mcu_model = "nRF52832", .mcu_id = "FICR-ABCDEF01", .fw_version = "1.4.0",
    };
    device_info_init(&fi);
    const factory_info_t *got = device_info_get();
    CHECK(strcmp(got->serial, "PL00000001") == 0, "serial readback");
    CHECK(strcmp(got->fw_version, "1.4.0") == 0, "fw version readback");

    uint8_t buf[200];
    uint16_t n = device_info_export(buf, sizeof(buf));
    CHECK(n > 100, "factory export non empty");
    /* 魔数校验：字段按序紧贴，起始即 model */
    CHECK(memcmp(buf, "PLT1", 4) == 0, "export starts with model");
    /* serial=固定偏移 5 */
    CHECK(memcmp(buf + 5, "PL00000001", 10) == 0, "export serial offset");
}

static void test_log_manager(void)
{
    printf("[FW-D4 LogManager]\n");
    log_manager_init();
    CHECK(log_manager_count() == 0, "log empty init");

    log_manager_append(1000, 0x01, 5);
    log_manager_append(1001, 0x02, 6);
    CHECK(log_manager_count() == 2, "log append 2");

    uint8_t buf[100];
    uint16_t n = log_manager_export(buf, sizeof(buf));
    CHECK(n == 2 + 2 * 8, "export size 18");
    CHECK(buf[0] == 2 && buf[1] == 0, "pkg count 2");
    /* 第一条 ts=1000 */
    CHECK(buf[2] == 0xE8 && buf[3] == 0x03, "first ts=1000");

    /* 环形覆盖：填 256+5 条 */
    for (uint32_t i = 0; i < LOG_CAPACITY + 5; i++)
        log_manager_append((uint32_t)(2000 + i), 0x03, (uint16_t)i);
    CHECK(log_manager_count() == LOG_CAPACITY, "log capped");
    /* 最旧被覆盖：导出起始 ts=2005 */
    uint8_t buf2[3000];
    uint16_t n2 = log_manager_export(buf2, sizeof(buf2));
    CHECK(n2 == 2 + LOG_CAPACITY * 8, "full export after ring full");
    uint32_t first_ts = buf2[2] | (buf2[3] << 8) | (buf2[4] << 16) | ((uint32_t)buf2[5] << 24);
    CHECK(first_ts == 2005, "ring overwrite oldest");
}

static void test_power_mgr(void)
{
    printf("[FW-D4 PowerMgr]\n");
    power_mgr_init(PWR_MODE_REGULAR);
    CHECK(power_mgr_mode() == PWR_MODE_REGULAR, "regular mode");
    CHECK(power_mgr_allow_sleep(), "allow sleep in regular");

    power_mgr_enter_deep_sleep();
    CHECK(power_mgr_mode() == PWR_MODE_DEEP_SLEEP, "deep sleep entered");

    pwr_wake_src_t src = power_mgr_event_wake();
    CHECK(src == PWR_WAKE_GPIO, "woke by gpio");
    CHECK(power_mgr_mode() == PWR_MODE_REGULAR, "back to regular after wake");
    CHECK(power_mgr_sleep_count() == 1, "sleep counted once");
}

int main(void)
{
    printf("=== Pumpilot FW-D1 host tests ===\n");
    test_crc16();
    test_pulse_calc();
    test_reservoir();
    test_state_machine();
    test_ringbuf();
    test_basal_scheduler();
    test_bolus_scheduler();
    test_ieee11073();
    test_alarm_engine();
    test_alarm_app();
    test_device_info();
    test_log_manager();
    test_power_mgr();
    if (g_fail == 0) {
        printf("\nALL TESTS PASSED\n");
        return 0;
    }
    printf("\n%d TEST(S) FAILED\n", g_fail);
    return 1;
}
