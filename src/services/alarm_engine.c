/**
 * alarm_engine.c — 报警引擎实现（SDK 无关）
 *
 * 位表维护 + 巡检阈值映射 + 新报警标记。
 * 不依赖任何硬件，纯逻辑，可 host 单测。
 */
#include "alarm_engine.h"

static alarm_state_t s;

void alarm_engine_init(void)
{
    s.active = 0;
    s.raised_flag = 0;

    /* 各级别映射（按 FW-SRS §2.3） */
    s.levels[ALARM_BIT_OCLUSION]   = ALERT_LEVEL_1;
    s.levels[ALARM_BIT_LEAK]       = ALERT_LEVEL_1;
    s.levels[ALARM_BIT_BATT_DEAD]  = ALERT_LEVEL_1;
    s.levels[ALARM_BIT_RESV_EMPTY] = ALERT_LEVEL_1;
    s.levels[ALARM_BIT_SELFTEST]   = ALERT_LEVEL_1;
    s.levels[ALARM_BIT_MECH]       = ALERT_LEVEL_1;
    s.levels[ALARM_BIT_BATT_LOW]   = ALERT_LEVEL_2;
    s.levels[ALARM_BIT_RESV_LOW]   = ALERT_LEVEL_2;
    s.levels[ALARM_BIT_TIME_DRIFT] = ALERT_LEVEL_3;
    s.levels[ALARM_BIT_LOST]       = ALERT_LEVEL_3;
}

static void set_bit_active(uint16_t bit)
{
    if (!(s.active & (1u << bit))) {
        s.active |= (1u << bit);
        s.raised_flag = 1;     /* 新报警 → 标记需立即上报 */
    }
}

static void clear_bit_active(uint16_t bit)
{
    s.active &= ~(1u << bit);
}

void alarm_engine_raise(uint16_t bit)
{
    if (bit < ALARM_BIT_COUNT)
        set_bit_active(bit);
}

void alarm_engine_clear(uint16_t bit)
{
    if (bit < ALARM_BIT_COUNT)
        clear_bit_active(bit);
}

void alarm_engine_check(const alarm_input_t *in)
{
    /* 一级 */
    in->occlusion   ? set_bit_active(ALARM_BIT_OCLUSION)   : clear_bit_active(ALARM_BIT_OCLUSION);
    in->leak        ? set_bit_active(ALARM_BIT_LEAK)       : clear_bit_active(ALARM_BIT_LEAK);
    in->batt_dead   ? set_bit_active(ALARM_BIT_BATT_DEAD)  : clear_bit_active(ALARM_BIT_BATT_DEAD);
    in->resv_empty  ? set_bit_active(ALARM_BIT_RESV_EMPTY) : clear_bit_active(ALARM_BIT_RESV_EMPTY);
    in->selftest_fail? set_bit_active(ALARM_BIT_SELFTEST)  : clear_bit_active(ALARM_BIT_SELFTEST);
    in->mech_fault  ? set_bit_active(ALARM_BIT_MECH)       : clear_bit_active(ALARM_BIT_MECH);

    /* 二级 */
    in->batt_low    ? set_bit_active(ALARM_BIT_BATT_LOW)   : clear_bit_active(ALARM_BIT_BATT_LOW);
    in->resv_low    ? set_bit_active(ALARM_BIT_RESV_LOW)   : clear_bit_active(ALARM_BIT_RESV_LOW);

    /* 三级 */
    in->time_drift  ? set_bit_active(ALARM_BIT_TIME_DRIFT) : clear_bit_active(ALARM_BIT_TIME_DRIFT);
    in->lost        ? set_bit_active(ALARM_BIT_LOST)       : clear_bit_active(ALARM_BIT_LOST);
}

bool alarm_engine_is_active(uint16_t bit)
{
    return (bit < ALARM_BIT_COUNT) && (s.active & (1u << bit));
}

bool alarm_engine_take_new(void)
{
    if (s.raised_flag) {
        s.raised_flag = 0;
        return true;
    }
    return false;
}

uint16_t alarm_engine_get_active(void)
{
    return s.active;
}

alert_level_t alarm_engine_level(uint16_t bit)
{
    return (bit < ALARM_BIT_COUNT) ? s.levels[bit] : (alert_level_t)0;
}

uint8_t alarm_engine_active_count(void)
{
    uint8_t n = 0;
    for (uint16_t b = 0; b < ALARM_BIT_COUNT; b++)
        if (s.active & (1u << b))
            n++;
    return n;
}
