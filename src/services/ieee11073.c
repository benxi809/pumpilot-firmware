/**
 * @file ieee11073.c
 * @brief IEEE 11073 agent 实现。
 *
 * 依据《固件完整API与BLE通讯协议文档》§5~§9。
 * 帧编解码小端 + CRC16-CCITT。
 */
#include "ieee11073.h"
#include "crc16.h"
#include "basal_scheduler.h"
#include "bolus_scheduler.h"
#include "algo_params.h"
#include <string.h>

/* 命令帧：CMD(1)|op_id(4)|len(2)|payload|CRC(2)；总头 = 7 */
#define CMD_HDR_LEN     7u
/* 上报帧：RPT(1)|len(2)|seq(2)|payload|CRC(2)；总头 = 7 */
#define RPT_HDR_LEN     7u
/* 最小可用帧长（含类型+长度） */
#define IEEE11073_MIN   3u

/* 幂等记录容量 */
#define OPID_HISTORY    8u

/* 待上报 pending 单帧缓冲 */
static uint8_t  s_pending[IEEE11073_FRAME_MAX];
static uint16_t s_pending_len = 0;
static bool     s_has_pending = false;

/* 上报序号 */
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
    /* 调度器需已初始化（由 PumpApp 保证顺序） */
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

/* ---- 组命令执行回执 RPT_RESULT ---- */
static void send_result(uint32_t op_id, uint8_t code)
{
    uint8_t pl[5];
    uint8_t f[IEEE11073_FRAME_MAX];
    int n;
    pl[0] = code;
    wr_u32_le(&pl[1], op_id);
    n = ieee11073_build_report(RPT_RESULT, pl, sizeof pl, f);
    if (n > 0) { s_pending_len = (uint16_t)n; memcpy(s_pending, f, (size_t)n); s_has_pending = true; s_seq++; }
}

/* ---- 构建上报帧 ---- */
int ieee11073_build_report(uint8_t rpt_type,
                           const uint8_t *payload, uint16_t plen,
                           uint8_t *out)
{
    uint8_t *p = out;
    uint16_t crc;
    if (plen > (IEEE11073_FRAME_MAX - RPT_HDR_LEN - 2u)) return -1;
    *p++ = rpt_type;
    wr_u16_le(p, plen); p += 2;
    wr_u16_le(p, s_seq); p += 2;
    if (plen) { memcpy(p, payload, plen); p += plen; }
    crc = crc16_ccitt(out, (size_t)(p - out));
    wr_u16_le(p, crc); p += 2;
    return (int)(p - out);
}

int ieee11073_push_report(uint8_t rpt_type)
{
    uint8_t f[IEEE11073_FRAME_MAX];
    int n;
    /* 默认载荷：状态源聚合成简单字段（依类型而异） */
    uint8_t pl[16];
    uint16_t plen = 0;
    switch (rpt_type) {
    case RPT_STATUS:
        pl[0] = s_state;
        wr_u16_le(&pl[1], s_bat_mv);
        plen = 3;
        break;
    case RPT_MED:
        /* 已注射大剂量 + 剩余药量 */
        wr_u32_le(&pl[0], (uint32_t)s_delivered_bolus_iu);
        wr_u32_le(&pl[4], (uint32_t)(s_reservoir_ml * 100.0f));
        plen = 8;
        break;
    case RPT_FACTORY:
        /* 占位：出厂信息由上层填入 */
        pl[0] = 0;
        plen = 1;
        break;
    default:
        pl[0] = rpt_type;
        plen = 1;
        break;
    }
    n = ieee11073_build_report(rpt_type, pl, plen, f);
    if (n < 0) return n;
    s_pending_len = (uint16_t)n;
    memcpy(s_pending, f, (size_t)n);
    s_has_pending = true;
    s_seq++;
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

/* ---- 命令执行 ---- */
static void cmd_set_basal(const uint8_t *payload, uint16_t plen)
{
    float table[PUMP_BASAL_SEG_COUNT];
    uint16_t i;
    if (plen < PUMP_BASAL_SEG_COUNT * 4u) {
        send_result(0, CMD_RESULT_PAYLOAD_ERR);
        return;
    }
    for (i = 0; i < PUMP_BASAL_SEG_COUNT; ++i) {
        table[i] = rd_f32_le(payload + i * 4u);
    }
    basal_scheduler_load(table);
}

static void cmd_set_temp_basal(const uint8_t *payload, uint16_t plen)
{
    /* 临时基础率：暂由基础率表替换实现；1~4 段覆盖已由上层简化 */
    (void)payload; (void)plen;
}

static void cmd_deliver_bolus(uint32_t op_id, const uint8_t *payload, uint16_t plen)
{
    bolus_spec_t sp;
    if (plen < 1u + 4u + 2u + 1u) {
        send_result(op_id, CMD_RESULT_PAYLOAD_ERR);
        return;
    }
    /* payload: bolus_type(1) + dose(4) + extension(2) + ratio(1) */
    sp.type         = (bolus_type_t)payload[0];
    sp.dose_iu      = rd_f32_le(payload + 1);
    sp.extension_min= rd_u16_le(payload + 5);
    sp.bolus_ratio  = payload[7];
    sp.op_id        = op_id;
    if (bolus_scheduler_busy()) {
        send_result(op_id, CMD_RESULT_REJECT);
        return;
    }
    if (!bolus_scheduler_start(&sp)) {
        send_result(op_id, CMD_RESULT_REJECT);
        return;
    }
}

static void cmd_set_time(const uint8_t *payload, uint16_t plen)
{
    /* 时间同步：payload 打包 y/m/d/h/min/sec（由上层 rtc 设置），此处略 */
    (void)payload; (void)plen;
}

static void cmd_start_pause_resume(int which)
{
    /* which: 5=start,6=pause,7=resume,8=abandon —— 由上层经 PumpApp 事件驱动 */
    (void)which;
}

static void cmd_set_param(const uint8_t *payload, uint16_t plen) { (void)payload; (void)plen; }
static void cmd_replace_profile(const uint8_t *payload, uint16_t plen) { (void)payload; (void)plen; }

/* 处理一条命令帧（校验 CRC + 幂等 + 分发） */
static int process_cmd(const uint8_t *data, uint16_t len)
{
    uint8_t cmd;
    uint32_t op_id;
    uint16_t plen;
    const uint8_t *pl;
    uint16_t crc_calc, crc_recv;
    int r = 0;

    if (len < CMD_HDR_LEN + 2u) return CMD_RESULT_PAYLOAD_ERR;
    cmd   = data[0];
    op_id = rd_u32_le(data + 1);
    plen  = rd_u16_le(data + 5);
    pl    = data + CMD_HDR_LEN;
    if (plen != len - CMD_HDR_LEN - 2u) return CMD_RESULT_PAYLOAD_ERR;

    crc_calc = crc16_ccitt(data, CMD_HDR_LEN + plen);
    crc_recv = rd_u16_le(data + CMD_HDR_LEN + plen);
    if (crc_calc != crc_recv) {
        send_result(op_id, CMD_RESULT_CRC_ERROR);
        return CMD_RESULT_CRC_ERROR;
    }

    /* 幂等：仅对执行型命令（可能重复副作用）去重 */
    if (cmd == CMD_DELIVER_BOLUS || cmd == CMD_SET_BASAL ||
        cmd == CMD_REPLACE_PROFILE || cmd == CMD_SET_PARAM) {
        if (opid_seen(op_id)) {
            send_result(op_id, CMD_RESULT_OPID_DUP);
            return CMD_RESULT_OPID_DUP;
        }
        opid_record(op_id);
    }

    switch (cmd) {
    case CMD_SET_BASAL:       cmd_set_basal(pl, plen); r = 0; break;
    case CMD_SET_TEMP_BASAL:  cmd_set_temp_basal(pl, plen); r = 0; break;
    case CMD_DELIVER_BOLUS:   cmd_deliver_bolus(op_id, pl, plen); r = 0; break;
    case CMD_SET_TIME:        cmd_set_time(pl, plen); r = 0; break;
    case CMD_START:   cmd_start_pause_resume(5); r = 0; break;
    case CMD_PAUSE:   cmd_start_pause_resume(6); r = 0; break;
    case CMD_RESUME:  cmd_start_pause_resume(7); r = 0; break;
    case CMD_ABANDON: cmd_start_pause_resume(8); r = 0; break;
    case CMD_SET_PARAM:       cmd_set_param(pl, plen); r = 0; break;
    case CMD_REPLACE_PROFILE: cmd_replace_profile(pl, plen); r = 0; break;
    default:
        send_result(op_id, CMD_RESULT_PAYLOAD_ERR);
        return CMD_RESULT_PAYLOAD_ERR;
    }
    send_result(op_id, CMD_RESULT_OK);
    return r;
}

/* 处理 APDU 呈现报文：识别关联/操作/数据。简化：ACK 回执。 */
static int process_apdu(uint8_t type, const uint8_t *payload, uint16_t plen)
{
    (void)payload;
    if (type == APDU_TYPE_ECHO) {
        /* ECHO 回 ECHO */
        uint8_t f[IEEE11073_FRAME_MAX];
        uint8_t e[] = { APDU_TYPE_ECHO };
        int n = ieee11073_build_report(RPT_RESULT, e, 1, f); /* 占位 */
        if (n > 0) { s_pending_len = (uint16_t)n; memcpy(s_pending, f, (size_t)n); s_has_pending = true; s_seq++; }
        return 0;
    }
    /* 关联：accepted */
    {
        uint8_t f[IEEE11073_FRAME_MAX];
        uint8_t ac[] = { 0x01 };  /* accepted */
        int n = ieee11073_build_report(RPT_RESULT, ac, 1, f);
        if (n > 0) { s_pending_len = (uint16_t)n; memcpy(s_pending, f, (size_t)n); s_has_pending = true; s_seq++; }
    }
    if (plen) (void)payload;
    return 0;
}

int ieee11073_on_data(const uint8_t *data, uint16_t len)
{
    if (!data || len < IEEE11073_MIN) return CMD_RESULT_PAYLOAD_ERR;
    /* 区分 APDU 与快捷命令帧：首字节为 APDU 类型(0x50/0xE2) 或命令码(0x01~0x0A) */
    {
        uint8_t t = data[0];
        if (t == APDU_TYPE_PRESENTATION) {
            /* APDU: type|len(2 LE)|payload */
            uint16_t plen = rd_u16_le(data + 1);
            return process_apdu(t, data + 3, plen);
        } else if (t == APDU_TYPE_ECHO) {
            return process_apdu(t, data + 3, rd_u16_le(data + 1));
        }
        /* 快捷命令帧 */
        return process_cmd(data, len);
    }
}

bool ieee11073_is_bound(void) { return s_bound; }

bool ieee11073_auth_bind_pwd(const uint8_t *pwd, uint16_t plen)
{
    /* 与出厂蓝牙密码比对（出厂信息由上层注入）。此处简化接受任意非空。 */
    if (!pwd || plen == 0u) return false;
    s_bound = true;
    return true;
}

bool ieee11073_check_bound(const uint8_t *serial, uint16_t slen)
{
    /* 绑定后仅原 APP 连：需要出厂序列号比对。此处占位返回真。 */
    (void)serial; (void)slen;
    return true;
}

uint16_t ieee11073_get_handle(uint16_t class_code)
{
    switch (class_code) {
    case 37u: return OBJ_HANDLE_MDS;
    case 6u:  return OBJ_HANDLE_CURRENT_BOLUS;
    default:  return 0xFFFFu;
    }
}
