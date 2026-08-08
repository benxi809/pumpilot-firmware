/**
 * @file bolus_scheduler.c
 * @brief 大剂量调度器实现。
 */
#include "bolus_scheduler.h"
#include "pulse_calc.h"
#include "algo_params.h"

/* op_id 幂等记录容量 */
#define OPID_HISTORY      8u

/* 内部子阶段（用于复合大剂量两段推进） */
typedef enum {
    PHASE_NONE   = 0,
    PHASE_BOLUS  = 1,   /* 快速或复合前半（连续输出） */
    PHASE_EXT    = 2    /* 扩展或复合后半（按槽分布） */
} phase_t;

static bolus_state_t   s_state   = BOLUS_IDLE;
static bolus_event_cb_t s_cb     = 0;

static uint32_t    s_opid_hist[OPID_HISTORY];
static uint8_t     s_opid_idx = 0;
static uint8_t     s_opid_cnt = 0;

static bolus_spec_t s_spec;
static phase_t     s_phase  = PHASE_NONE;
static uint32_t    s_total_pulses = 0;   /* 当前阶段总脉冲 */
static uint32_t    s_sent     = 0;      /* 当前阶段已发送 */
static uint32_t    s_slot_idx = 0;      /* 扩展阶段当前槽 */
static uint32_t    s_slot_total = 0;    /* 扩展阶段总槽数 */

void bolus_scheduler_init(void)
{
    s_state = BOLUS_IDLE;
    s_cb    = 0;
    s_phase = PHASE_NONE;
    s_opid_idx = 0;
    s_opid_cnt = 0;
    uint8_t i;
    for (i = 0; i < OPID_HISTORY; ++i) s_opid_hist[i] = 0;
}

void bolus_scheduler_set_event_cb(bolus_event_cb_t cb) { s_cb = cb; }

/* op_id 是否已记录（幂等去重） */
static bool opid_seen(uint32_t op_id)
{
    uint8_t i;
    for (i = 0; i < s_opid_cnt; ++i) {
        if (s_opid_hist[i] == op_id) return true;
    }
    return false;
}

static void opid_record(uint32_t op_id)
{
    s_opid_hist[s_opid_idx] = op_id;
    s_opid_idx = (uint8_t)((s_opid_idx + 1u) % OPID_HISTORY);
    if (s_opid_cnt < OPID_HISTORY) s_opid_cnt++;
}

/* 全部脉冲（快速或复合前半 bolus 部分） */
static uint32_t phase_bolus_pulses(const bolus_spec_t *sp)
{
    if (sp->type == BOLUS_DUAL) {
        return pulse_calc_dual_bolus_part(sp->dose_iu, sp->bolus_ratio);
    }
    return pulse_calc_fast_bolus(sp->dose_iu);
}

/* 扩展部分总脉冲（扩展或复合后半） */
static uint32_t phase_ext_pulses(const bolus_spec_t *sp)
{
    if (sp->type == BOLUS_DUAL) {
        uint8_t rem = (sp->bolus_ratio >= 100u) ? 0u : (uint8_t)(100u - sp->bolus_ratio);
        return pulse_calc_dual_bolus_part(sp->dose_iu, rem);
    }
    /* 纯扩展：总剂量 */
    return pulse_calc_fast_bolus(sp->dose_iu);
}

bool bolus_scheduler_start(const bolus_spec_t *spec)
{
    if (!spec) return false;
    if (s_state == BOLUS_RUNNING) return false;          /* 忙 */
    if (spec->dose_iu <= 0.0f) return false;
    if (spec->type == BOLUS_DUAL && spec->bolus_ratio > 100u) return false;
    if (opid_seen(spec->op_id)) return false;            /* 幂等去重 */

    s_spec    = *spec;
    opid_record(spec->op_id);

    if (spec->type == BOLUS_FAST) {
        s_phase = PHASE_BOLUS;
        s_total_pulses = phase_bolus_pulses(&s_spec);
    } else if (spec->type == BOLUS_EXT) {
        s_phase = PHASE_EXT;
        s_total_pulses = phase_ext_pulses(&s_spec);
        s_slot_total = (spec->extension_min > 0u)
                     ? (uint32_t)((spec->extension_min + PUMP_BASAL_PERIOD_MIN - 1u) / PUMP_BASAL_PERIOD_MIN)
                     : 1u;
        s_slot_idx = 0u;
    } else { /* DUAL */
        s_phase = PHASE_BOLUS;
        s_total_pulses = phase_bolus_pulses(&s_spec);
    }

    s_sent   = 0u;
    s_state  = BOLUS_RUNNING;
    return true;
}

void bolus_scheduler_stop(bolus_stop_reason_t r)
{
    if (s_state != BOLUS_RUNNING) return;
    s_state = BOLUS_IDLE;
    s_phase = PHASE_NONE;
    if (s_cb) s_cb(r);
}

bool bolus_scheduler_busy(void) { return s_state == BOLUS_RUNNING; }
bolus_state_t bolus_scheduler_state(void) { return s_state; }

uint32_t bolus_scheduler_ext_slot_pulses(uint32_t total, uint32_t idx, uint32_t slots)
{
    uint32_t a, b;
    if (slots == 0u) return total;
    if (idx >= slots) idx = slots - 1u;
    a = (uint32_t)(((uint64_t)(idx + 1u) * total) / slots);
    b = (uint32_t)(((uint64_t)idx * total) / slots);
    return a - b;
}

/* 推进扩展阶段一槽；返回是否还有（<0 waiting, 0 done, >0 pulses） */
static int32_t advance_ext(void)
{
    uint32_t pulses;
    if (s_slot_idx >= s_slot_total) {
        return 0;   /* 扩展完成 */
    }
    pulses = bolus_scheduler_ext_slot_pulses(s_total_pulses, s_slot_idx, s_slot_total);
    s_slot_idx++;
    s_sent += pulses;
    return (int32_t)pulses;
}

bool bolus_scheduler_poll(uint32_t *pulses_out, bool *final)
{
    if (pulses_out) *pulses_out = 0u;
    if (final) *final = false;
    if (s_state != BOLUS_RUNNING) return false;

    if (s_phase == PHASE_BOLUS) {
        /* 快速或复合前半：一次输出全部 */
        uint32_t remain = s_total_pulses - s_sent;
        if (remain == 0u) {
            /* bolus 部分完成 */
            if (s_spec.type == BOLUS_DUAL) {
                /* 切换到扩展部分（切点点脉冲去重由调用方用 pump_busy 判断） */
                s_phase = PHASE_EXT;
                s_total_pulses = phase_ext_pulses(&s_spec);
                s_slot_total = (s_spec.extension_min > 0u)
                    ? (uint32_t)((s_spec.extension_min + PUMP_BASAL_PERIOD_MIN - 1u) / PUMP_BASAL_PERIOD_MIN)
                    : 1u;
                s_slot_idx = 0u;
                s_sent = 0u;
                return bolus_scheduler_poll(pulses_out, final);
            }
            /* 纯快速完成 */
            s_state = BOLUS_IDLE;
            if (final) *final = true;
            if (s_cb) s_cb(BOLUS_STOP_DONE);
            return true;
        }
        if (pulses_out) *pulses_out = remain;
        s_sent = s_total_pulses;
        if (s_spec.type != BOLUS_DUAL) {
            /* 纯快速大剂量：本批即完成 */
            s_state = BOLUS_IDLE;
            if (final) *final = true;
            if (s_cb) s_cb(BOLUS_STOP_DONE);
        } else {
            /* 复合前半 bolus 部分本批发完，ext 部分下次 poll 切 phase */
            if (final) *final = true;
        }
        return true;
    }

    /* PHASE_EXT */
    {
        int32_t r = advance_ext();
        if (r == 0) {
            s_state = BOLUS_IDLE;
            if (final) *final = true;
            if (s_cb) s_cb(BOLUS_STOP_DONE);
            return true;
        }
        if (pulses_out) *pulses_out = (uint32_t)r;
        return true;
    }
}
