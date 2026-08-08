/**
 * @file basal_scheduler.c
 * @brief 基础率调度器实现。
 */
#include "basal_scheduler.h"
#include "pulse_calc.h"

/* 每段 30min / 3min = 10 个槽 */
#define SLOTS_PER_SEG   10u
/* 段时长分钟 */
#define SEG_MIN         30u

static float            s_table[PUMP_BASAL_SEG_COUNT];
static bool             s_loaded  = false;
static bool             s_active  = false;
static float            s_delivered_iu = 0.0f;

void basal_scheduler_load(const float *table)
{
    uint8_t i;
    for (i = 0; i < PUMP_BASAL_SEG_COUNT; ++i) {
        float r = (table) ? table[i] : 0.0f;
        if (r < 0.0f) r = 0.0f;
        if (r > 25.0f) r = 25.0f; /* 基础率安全上限 IU/h */
        s_table[i] = r;
    }
    s_loaded = true;
}

float basal_scheduler_get_seg_rate(uint8_t seg)
{
    if (seg >= PUMP_BASAL_SEG_COUNT) return 0.0f;
    return s_table[seg];
}

uint32_t basal_scheduler_seg_pulses(uint8_t seg)
{
    if (seg >= PUMP_BASAL_SEG_COUNT) return 0u;
    /* floor(rate × 0.5 / 0.00339) */
    return pulse_calc_basal_segment(s_table[seg]);
}

uint32_t basal_scheduler_slot_pulses(uint8_t seg, uint8_t slot)
{
    uint32_t N;
    uint32_t a, b;
    if (slot >= SLOTS_PER_SEG) slot = SLOTS_PER_SEG - 1u;
    N = basal_scheduler_seg_pulses(seg);
    /* Bresenham 匀距：第 slot 槽输出 floor((slot+1)*N/10) - floor(slot*N/10) */
    a = (uint32_t)(((uint64_t)(slot + 1u) * N) / SLOTS_PER_SEG);
    b = (uint32_t)(((uint64_t)slot * N) / SLOTS_PER_SEG);
    return a - b;
}

uint32_t basal_scheduler_pulses_at(uint16_t minute_of_day,
                                   uint8_t *seg_out, uint8_t *slot_out)
{
    uint8_t seg  = pulse_calc_seg_of_minute(minute_of_day);   /* 0..47 */
    uint8_t slot = (uint8_t)((minute_of_day % SEG_MIN) / PUMP_BASAL_PERIOD_MIN); /* 0..9 */
    if (seg_out)  *seg_out  = seg;
    if (slot_out) *slot_out = slot;
    return basal_scheduler_slot_pulses(seg, slot);
}

bool basal_scheduler_is_active(void) { return s_active; }

void basal_scheduler_start(void)   { s_active = true; }
void basal_scheduler_pause(void)   { s_active = false; }
void basal_scheduler_resume(void)  { s_active = true; }

float basal_scheduler_delivered_iu(void) { return s_delivered_iu; }

void basal_scheduler_add_delivered_pulses(uint32_t pulses)
{
    s_delivered_iu += (float)pulses * PUMP_IU_PER_PULSE;
}

void basal_scheduler_reset_delivered(void) { s_delivered_iu = 0.0f; }

bool basal_scheduler_loaded(void) { return s_loaded; }
