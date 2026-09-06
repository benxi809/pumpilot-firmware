/**
 * @file ieee11073.c
 * @brief IEEE 11073 agent 实现（0x11 0x73 自定义帧协议）。
 *
 * 依据《API与BLE通讯协议文档》§8~§9、《180F_FW_IMPLEMENT》Phase A。
 *
 * 命令帧: 11 73 | 01 | cmd | opId(LE4) | payload  | crc(LE2)
 * 上报帧: 11 73 | 02 | rpt | payload ... | crc(LE2)
 * CRC16-CCITT-FALSE 覆盖整帧（同步字起）到 payload 末（不含末尾 CRC）。
 */
#include "ieee11073.h"
#include "device_info.h"      /* factory_info_t via hal_flash.h, device_info_get() */
#include "crc16.h"
#include "basal_scheduler.h"
#include "bolus_scheduler.h"
#include "algo_params.h"
#include <string.h>

/* 命令帧前缀 = 11 73 | 01 | cmd | opId(4) = 8 字节，绕过数据=data+8 */
#define CMD_HDR_LEN     8u
/* 上报帧前缀 = 11 73 | 02 | rpt = 4 字节 */
#define RPT_HDR_LEN     4u
/* CRC 尾巴 */
#define CRC_LEN         2u

/* 最小可接收的帧长（命令帧最小 8 头 + 0 载荷 + 2 CRC = 10） */
#define CMD_FRAME_MIN   10u
/* 上报帧最小 = 11 73 02 rpt crc(2) = 6 字节（不需要，APP 一般不发送） */
#define RPT_FRAME_MIN   6u

/* 幂等记录容量 */
#define OPID_HISTORY    8u

/* 待上报 pending 单帧缓冲 */
static uint8_t  s_pending[IEEE11073_FRAME_MAX];
static uint16_t s_pending_len = 0;
static bool     s_has_pending = false;

/* 上报序号（文档帧不携带；保留仅日志用） */
static uint16_t s_seq = 0;

/* 绑定状态 */
static bool s_bound = false;

/* 幂等 op_id 历史 */
static uint32_t s_opid_hist[OPID_HISTORY];
static uint8_t  s_opid_idx = 0;
static uint8_t  s_opid_cnt = 0;

/* 状态源缓存 */
static uint8_t  s_state      = 0;
static uint16_t s_bat_mv     = 0;
static float    s_delivered_bolus_iu = 0.0f;
static float    s_reservoir_ml        = 0.0f;

/* ---- 小端读写 helpers ---- */
static uint32_t rd_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd_u16_le(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
static float rd_f32_le(const uint8_t *p) {
    uint32_t u = rd_u32_le(p);
    float f; memcpy(&f, &u, sizeof f); return f;
}
static void wr_u16_le(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void wr_u32_le(uint8_t *p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }

void ieee11073_init(void)
{
    uint8_t i;
    s_has_pending = false;
    s_pending_len = 0;
    s_seq = 0;
    s_bound = false;
    for (i = 0; i < OPID_HISTORY; ++i) s_opid_hist[i] = 0;
    s_opid_idx = 0;
    s_opid_cnt = 0;
    s_state = 0; s_bat_mv = 0; s_delivered_bolus_iu = 0.0f; s_reservoir_ml = 0.0f;
}

void ieee11073_update_sources(uint8_t state, uint16_t bat_mv,
                              float delivered_bolus_iu, float reservoir_ml)
{
    s_state = state;
    s_bat_mv = bat_mv;
    s_delivered_bolus_iu = delivered_bolus_iu;
    s_reservoir_ml = reservoir_ml;
}

/* ---- op_id 幂等 ---- */
static bool opid_seen(uint32_t id)
{
    uint8_t i;
    for (i = 0; i < s_opid_cnt; ++i) if (s_opid_hist[i] == id) return true;
    return false;
}
static void opid_record(uint32_t id)
{
    s_opid_hist[s_opid_idx] = id;
    s_opid_idx = (uint8_t)((s_opid_idx + 1u) % OPID_HISTORY);
    if (s_opid_cnt < OPID_HISTORY) s_opid_cnt++;
}

/* ---- 保存一条待 notify 的帧 ---- */
static void stash_frame(const uint8_t *f, int n)
{
    if (n <= 0) return;
    s_pending_len = (uint16_t)n;
    memcpy(s_pending, f, (size_t)n);
    s_has_pending = true;
    s_seq++;
}

/* ---- 组命令执行回执 IEEE_RPT_CONFIRM ----
 * 载荷 = opId(4 LE) + cmd(1) + result(unsigned16 LE)
 */
static void send_result(uint32_t op_id, uint8_t cmd, uint16_t code)
{
    uint8_t pl[7];
    uint8_t f[IEEE11073_FRAME_MAX];
    int n;
    wr_u32_le(pl, op_id);
    pl[4] = cmd;
    wr_u16_le(pl + 5, code);
    n = ieee11073_build_report(IEEE_RPT_CONFIRM, pl, sizeof pl, f);
    if (n > 0) stash_frame(f, n);
}

/* ---- 构建上报帧 ---- */
int ieee11073_build_report(uint8_t rpt_type,
                           const uint8_t *payload, uint16_t plen,
                           uint8_t *out)
{
    uint8_t *p = out;
    uint16_t crc;
    if (!out) return -1;
    if (plen > (IEEE11073_FRAME_MAX - RPT_HDR_LEN - CRC_LEN)) return -1;
    *p++ = IEEE_SYNC0;
    *p++ = IEEE_SYNC1;
    *p++ = IEEE_MSG_REPORT;
    *p++ = rpt_type;
    if (plen) { memcpy(p, payload, plen); p += plen; }
    crc = crc16_ccitt(out, (size_t)(p - out));   /* 同步字起..payload 末 */
    wr_u16_le(p, crc); p += CRC_LEN;
    return (int)(p - out);
}

int ieee11073_push_report(uint8_t rpt_type)
{
    uint8_t f[IEEE11073_FRAME_MAX];
    int n;
    /* 默认载荷：仅供 agent 内部状态源 -- 与文档 §9 对齐简化 */
    uint8_t pl[16];
    uint16_t plen = 0;
    switch (rpt_type) {
    case IEEE_RPT_MED:
        /* 药量：物理序号(1=占位) + 剩余(IU f32 LE) */
        pl[0] = 0;
        {
            uint32_t r = 0;
            memcpy(&r, &s_reservoir_ml, sizeof r);
            wr_u32_le(&pl[1], r);
        }
        plen = 5;
        break;
    case IEEE_RPT_STATE:
        /* OpStat(2占位)+InsPumpStat(4占位)+电量%(1) = 7 */
        wr_u16_le(pl, 0);        /* OpStat 由上层填 --- 0 */
        wr_u32_le(pl + 2, 0);    /* InsPumpStat 占位 */
        pl[6] = (uint8_t)s_bat_mv;   /* 用 mv 低 8 位占位（上层应算 %） */
        plen = 7;
        break;
    case IEEE_RPT_FACTORY:
        /* 占位 */
        pl[0] = 0;
        plen = 1;
        break;
    case IEEE_RPT_TIME:
        pl[0] = 0; pl[1] = 0; pl[2] = 0; pl[3] = 0; pl[4] = 0;
        plen = 5;
        break;
    default:
        pl[0] = (uint8_t)rpt_type;
        plen = 1;
        break;
    }
    n = ieee11073_build_report(rpt_type, pl, plen, f);
    if (n > 0) stash_frame(f, n);
    return n;
}

bool ieee11073_has_pending(void) { return s_has_pending; }

bool ieee11073_get_pending(uint8_t *out, uint16_t *len_out)
{
    if (!s_has_pending) return false;
    memcpy(out, s_pending, s_pending_len);
    *len_out = s_pending_len;
    s_has_pending = false;
    s_pending_len = 0;
    return true;
}

/* ---- 物理命令执行（复用现 scheduler/算法主体；仅按新载荷字节切法） ---- */

/* 基础率 48 段：跳过 profile_id(1 字节)再读 48×f32 */
static void cmd_set_basal_prof(uint32_t op_id, const uint8_t *pl, uint16_t plen)
{
    float table[PUMP_BASAL_SEG_COUNT];
    uint16_t i;
    if (plen < 1u + PUMP_BASAL_SEG_COUNT * 4u) {
        send_result(op_id, IEEE_CMD_SET_BASAL_PROF, CMD_RESULT_PAYLOAD_ERR);
        return;
    }
    /* pl[0]=profile_id, pl[1..] = 48×f32 */
    for (i = 0; i < PUMP_BASAL_SEG_COUNT; ++i) {
        table[i] = rd_f32_le(pl + 1u + (uint32_t)i * 4u);
    }
    basal_scheduler_load(table);
    send_result(op_id, IEEE_CMD_SET_BASAL_PROF, CMD_RESULT_OK);
}

/* 临时基础率 —— 先框架：仅校验 plen >= 1 即 ACCEPT(OK) */
static void cmd_set_temp_basal(uint32_t op_id, const uint8_t *pl, uint16_t plen)
{
    if (!pl || plen < 1u) {
        send_result(op_id, IEEE_CMD_SET_TEMP_BASAL, CMD_RESULT_PAYLOAD_ERR);
        return;
    }
    /* 帧架构阶段仅学习不计入调度：回执 OK */
    send_result(op_id, IEEE_CMD_SET_TEMP_BASAL, CMD_RESULT_OK);
}

static void cmd_deliver_bolus(uint32_t op_id, const uint8_t *pl, uint16_t plen)
{
    bolus_spec_t sp;
    /* 载荷: type(1)+dose(f32 4)+extMin(2)+[ratio(1) 可选] → 最低 7 字节 */
    if (plen < 1u + 4u + 2u) {
        send_result(op_id, IEEE_CMD_SET_BOLUS, CMD_RESULT_PAYLOAD_ERR);
        return;
    }
    sp.type         = (bolus_type_t)pl[0];
    sp.dose_iu      = rd_f32_le(pl + 1);
    sp.extension_min= rd_u16_le(pl + 5);
    sp.bolus_ratio  = (plen >= 8u) ? pl[7] : 0u;
    sp.op_id        = op_id;
    if (bolus_scheduler_busy()) {
        send_result(op_id, IEEE_CMD_SET_BOLUS, CMD_RESULT_REJECT);
        return;
    }
    if (!bolus_scheduler_start(&sp)) {
        send_result(op_id, IEEE_CMD_SET_BOLUS, CMD_RESULT_REJECT);
        return;
    }
    send_result(op_id, IEEE_CMD_SET_BOLUS, CMD_RESULT_OK);
}

static void cmd_set_time(uint32_t op_id, uint8_t cmd, const uint8_t *pl, uint16_t plen)
{
    /* 文档 SET_CLOCK: y/m/d/h/min(5)；先框架：仅校验长度(≥5) 即 ACCEPT */
    (void)pl;
    if (plen < 5u) {
        send_result(op_id, cmd, CMD_RESULT_PAYLOAD_ERR);
        return;
    }
    send_result(op_id, cmd, CMD_RESULT_OK);
}

static void cmd_noop_accept(uint32_t op_id, uint8_t cmd)
{
    /* START/STOP/RESUME/SWITCH_CURVE 等"先框架"仅回 OK */
    send_result(op_id, cmd, CMD_RESULT_OK);
}

static int cmd_connect(const uint8_t *pl, uint16_t plen, uint32_t opid)
{
    bool ok;
    uint16_t res;
    if (!pl || plen == 0) {
        send_result(opid, IEEE_CMD_CONNECT, CMD_RESULT_PAYLOAD_ERR);
        return CMD_RESULT_PAYLOAD_ERR;
    }
    /* auth_bind_pwd 内部按当前 device_info bt_password 严格等长比对并置 s_bound */
    ok = ieee11073_auth_bind_pwd(pl, plen);
    res = ok ? (uint16_t)CMD_RESULT_OK : IEEE_ERR_UNAUTHORIZED;
    /* 无论成败都回执 cmd=0x01 result 如上；帧已处理 → 返回 0（调用侧不再重发） */
    send_result(opid, IEEE_CMD_CONNECT, res);
    return 0;
}

/* ---- process_cmd：分发 ---- */
static int process_cmd(uint8_t cmd, uint32_t opid,
                       const uint8_t *pl, uint16_t plen)
{
    /* gate：未绑定只允许 CONNECT */
    if (!s_bound && cmd != IEEE_CMD_CONNECT) {
        send_result(opid, cmd, IEEE_ERR_UNAUTHORIZED);
        return 0;
    }

    /* 幂等：仅对执行型命令（SET_BASAL_PROF=0x02 / SET_BOLUS=0x04）去重 */
    if (cmd == IEEE_CMD_SET_BASAL_PROF || cmd == IEEE_CMD_SET_BOLUS) {
        if (opid_seen(opid)) {
            send_result(opid, cmd, CMD_RESULT_OPID_DUP);
            return CMD_RESULT_OPID_DUP;
        }
        opid_record(opid);
    }

    switch (cmd) {
    case IEEE_CMD_CONNECT:
        return cmd_connect(pl, plen, opid);
    case IEEE_CMD_SET_BASAL_PROF:
        cmd_set_basal_prof(opid, pl, plen);
        return 0;
    case IEEE_CMD_SET_TEMP_BASAL:
        cmd_set_temp_basal(opid, pl, plen);
        return 0;
    case IEEE_CMD_SET_BOLUS:
        cmd_deliver_bolus(opid, pl, plen);
        return 0;
    case IEEE_CMD_START:        /* 开始输注 — 复用调度层由 PumpApp 最终驱动；先框架回 OK */
        cmd_noop_accept(opid, cmd);
        return 0;
    case IEEE_CMD_PAUSE:        /* 载荷 target(1)：校验 ≥1 即回 OK */
        if (plen < 1u) {
            send_result(opid, cmd, CMD_RESULT_PAYLOAD_ERR);
            return 0;
        }
        cmd_noop_accept(opid, cmd);
        return 0;
    case IEEE_CMD_RESUME:
        cmd_noop_accept(opid, cmd);
        return 0;
    case IEEE_CMD_STOP:
        cmd_noop_accept(opid, cmd);
        return 0;
    case IEEE_CMD_SET_CLOCK:
        cmd_set_time(opid, cmd, pl, plen);
        return 0;
    case IEEE_CMD_SWITCH_CURVE:
        if (plen < 1u) {
            send_result(opid, cmd, CMD_RESULT_PAYLOAD_ERR);
            return 0;
        }
        cmd_noop_accept(opid, cmd);
        return 0;
    case IEEE_CMD_CONFIRM:      /* 回执保留仅解析 */
        send_result(opid, cmd, CMD_RESULT_OK);
        return 0;
    default:
        send_result(opid, cmd, CMD_RESULT_PAYLOAD_ERR);
        return CMD_RESULT_PAYLOAD_ERR;
    }
}

/* 处理一条 0x11 0x73 命令帧（不含上报类型） */
static int process_cmd_frame(const uint8_t *data, uint16_t len)
{
    uint8_t cmd;
    uint32_t opid;
    const uint8_t *pl;
    uint16_t plen;
    uint16_t crc_calc, crc_recv;

    if (len < CMD_FRAME_MIN) return CMD_RESULT_PAYLOAD_ERR;
    if (data[2] != IEEE_MSG_CMD) return CMD_RESULT_PAYLOAD_ERR;
    if (data[0] != IEEE_SYNC0 || data[1] != IEEE_SYNC1) return CMD_RESULT_PAYLOAD_ERR;

    cmd  = data[3];
    opid = rd_u32_le(data + 4);
    /* payload 从 data+8 起到（不含）data+len-2；plen = (len-2)-8 = len-10 */
    pl = data + 8;
    plen = (uint16_t)(len - CMD_FRAME_MIN);

    /* CRC 覆盖 data[0..len-3]（同步字到 payload 末） */
    crc_calc = crc16_ccitt(data, (size_t)(len - CRC_LEN));
    crc_recv = rd_u16_le(data + len - CRC_LEN);
    if (crc_calc != crc_recv) {
        /* cmd 已从 data[3] 取得，仍然回执 CRC_ERROR */
        send_result(opid, cmd, CMD_RESULT_CRC_ERROR);
        return CMD_RESULT_CRC_ERROR;
    }

    return process_cmd(cmd, opid, pl, plen);
}

int ieee11073_on_data(const uint8_t *data, uint16_t len)
{
    if (!data) return CMD_RESULT_PAYLOAD_ERR;
    if (len < RPT_FRAME_MIN) return CMD_RESULT_PAYLOAD_ERR;
    /* 非 0x11 0x73 帧 → 一律 PAYLOAD_ERR（不再走旧 APDU） */
    if (data[0] != IEEE_SYNC0 || data[1] != IEEE_SYNC1) return CMD_RESULT_PAYLOAD_ERR;

    if (data[2] == IEEE_MSG_CMD) {
        return process_cmd_frame(data, len);
    }
    /* data[2]==0x02 上报/0x03 查询 — APP 一般不发，不支持回 PAYLOAD_ERR */
    return CMD_RESULT_PAYLOAD_ERR;
}

bool ieee11073_is_bound(void) { return s_bound; }

bool ieee11073_auth_bind_pwd(const uint8_t *pwd, uint16_t plen)
{
    if (!pwd || plen == 0u) return false;

    const factory_info_t *fi = device_info_get();
    if (!fi) return false;

    /* 密码长度必须严格匹配到当前 bt_password */
    if (plen != strlen(fi->bt_password)) return false;

    /* 常时安全比对 */
    volatile uint8_t diff = 0;
    uint16_t i;
    for (i = 0; i < plen; ++i) {
        diff |= pwd[i] ^ (uint8_t)fi->bt_password[i];
    }
    s_bound = (diff == 0);
    return s_bound;
}

bool ieee11073_check_bound(const uint8_t *serial, uint16_t slen)
{
    (void)serial; (void)slen;
    return true;
}

uint16_t ieee11073_get_handle(uint16_t class_code)
{
    switch (class_code) {
    case 37u: return 0x0000u;   /* MDS */
    case 6u:  return 0x000Au;   /* 当前大剂量 */
    default:  return 0xFFFFu;
    }
}
