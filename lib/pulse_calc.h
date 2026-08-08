/**
 * @file pulse_calc.h
 * @brief 输注/药量纯算法（PC 可单元测试，不依赖板载）。
 *
 * 依据《固件详细设计说明书 v1.0》§6、§4.2/§4.3。
 * - 基础率 3 分钟槽脉冲换算
 * - 快速/扩展/复合大剂量脉冲换算
 * - 格栅物理序号 → 药量线性映射
 * - 堵塞判断辅助
 */
#ifndef PUMPILOT_PULSE_CALC_H
#define PUMPILOT_PULSE_CALC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 快速/普通大剂量脉冲数 = round(dose_IU / 0.00339)
 * @param dose_iu 剂量（IU，IQ>0）
 * @return 脉冲数（>=1）
 */
uint32_t pulse_calc_fast_bolus(float dose_iu);

/**
 * @brief 基础率某段在 0.5h 内应输出整数脉冲数
 *        = floor(seg_rate_IUh * 0.5 / 0.00339)
 * @param seg_rate_iu_h 该段基础率（IU/h）
 * @return 该段 0.5h 内整数脉冲数
 */
uint32_t pulse_calc_basal_segment(float seg_rate_iu_h);

/**
 * @brief 扩展大剂量在单 3 分钟子槽内的脉冲数
 *        = floor((extension_iu * (3/60)) / 0.00339)
 * @param extension_iu  扩展部分总剂量（IU）
 * @param extension_min 扩展总时长（分钟）>0
 * @param slot_min      子槽时长（分钟，基础率为 3）
 * @return 单子槽脉冲数
 */
uint32_t pulse_calc_ext_slot(float extension_iu, uint16_t extension_min, uint8_t slot_min);

/**
 * @brief 格栅物理序号 → 药量线性映射
 *        reservoir_ml = (grid_seq_index - 1) / 63.0 * 2.0
 * @param seq_index 物理序号（1..64，蛇形次序定位）
 * @return 剩余药量（ml）
 */
float pulse_calc_reservoir_ml(uint8_t seq_index);

/**
 * @brief 药量差堵塞判断：理论累计输注量(IU) 与 格栅实测药量差(IU)
 *        是否超过阈值 3.5 IU。
 * @param theoretical_total_iu 理论累计已输注（IU）
 * @param measured_remaining_iu 格栅实测剩余（IU，换算自 ml）
 * @return true = 判定堵塞（一级报警）
 */
bool pulse_calc_is_occluded(float theoretical_total_iu, float measured_remaining_iu);

/**
 * @brief 当前时间(分钟自零点) → 基础率段索引 seg = (h*60+m)/30，范围 0..47
 * @param minutes_since_midnight 0..1439
 * @return 0..47
 */
uint8_t pulse_calc_seg_of_minute(uint16_t minutes_since_midnight);

/**
 * @brief 复合大剂量前半（立即输注 bolus 部分）脉冲数
 *        = round(dose_iu * bolus_ratio% / 100 / 0.00339)
 * @param dose_iu    总剂量（IU）
 * @param bolus_ratio 前半比例（0..100）
 * @return bolus 部分脉冲数
 */
uint32_t pulse_calc_dual_bolus_part(float dose_iu, uint8_t bolus_ratio);

#ifdef __cplusplus
}
#endif

#endif /* PUMPILOT_PULSE_CALC_H */
