/**
 * @file pulse_calc.c
 * @brief 输注/药量纯算法实现。
 */
#include "pulse_calc.h"
#include "algo_params.h"

/* 0.5h 内应输注的 IU */
#define IU_PER_HALF_HOUR(rate) ((rate) * PUMP_SEG_DURATION_H)

uint32_t pulse_calc_fast_bolus(float dose_iu)
{
    float pulses = dose_iu / PUMP_IU_PER_PULSE;
    uint32_t r = (uint32_t)(pulses + 0.5f); /* 四舍五入 */
    return (r == 0u) ? 1u : r;
}

uint32_t pulse_calc_basal_segment(float seg_rate_iu_h)
{
    float iu_half = IU_PER_HALF_HOUR(seg_rate_iu_h);
    float pulses  = iu_half / PUMP_IU_PER_PULSE;
    return (uint32_t)pulses; /* floor */
}

uint32_t pulse_calc_ext_slot(float extension_iu, uint16_t extension_min, uint8_t slot_min)
{
    float iu_per_slot;
    float pulses;
    if (extension_min == 0u || slot_min == 0u) {
        return 0u;
    }
    /* 单个子槽应输注 IU = 总扩展量 × (slot/extension) */
    iu_per_slot = extension_iu * ((float)slot_min / (float)extension_min);
    pulses      = iu_per_slot / PUMP_IU_PER_PULSE;
    return (uint32_t)pulses; /* floor */
}

float pulse_calc_reservoir_ml(uint8_t seq_index)
{
    float idx;
    float ml;
    if (seq_index == 0u) {
        return 0.0f;
    }
    if (seq_index > PUMP_GRID_STEPS) {
        seq_index = PUMP_GRID_STEPS;
    }
    idx = (float)(seq_index - 1u);
    ml  = idx / 63.0f * PUMP_RESERVOIR_FULL_ML;
    if (ml < 0.0f) {
        ml = 0.0f;
    }
    return ml;
}

bool pulse_calc_is_occluded(float theoretical_total_iu, float measured_remaining_iu)
{
    float consumed = theoretical_total_iu;
    float expected_total_iu = measured_remaining_iu + consumed;
    /* 理论累计(含本次) 与 初始药量对比。此处按契约：差 > 阈值即堵塞 */
    /* expected_total_iu 越最接近理论初始量；此处简化为：输注量与格栅反馈差 */
    float diff = theoretical_total_iu - measured_remaining_iu;
    (void)expected_total_iu;
    return (diff > PUMP_OCCLUSION_TOLERANCE_IU);
}

uint8_t pulse_calc_seg_of_minute(uint16_t minutes_since_midnight)
{
    uint16_t seg = (uint16_t)(minutes_since_midnight / 30u);
    if (seg >= PUMP_BASAL_SEG_COUNT) {
        seg = PUMP_BASAL_SEG_COUNT - 1u;
    }
    return (uint8_t)seg;
}

uint32_t pulse_calc_dual_bolus_part(float dose_iu, uint8_t bolus_ratio)
{
    float bolus_iu = dose_iu * ((float)bolus_ratio / 100.0f);
    float pulses;
    uint32_t r;
    if (bolus_ratio >= 100u) {
        return pulse_calc_fast_bolus(dose_iu);
    }
    pulses = bolus_iu / PUMP_IU_PER_PULSE;
    r = (uint32_t)(pulses + 0.5f);
    return (r == 0u) ? 1u : r;
}
