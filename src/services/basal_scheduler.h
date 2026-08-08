/**
 * @file basal_scheduler.h
 * @brief 基础率调度器。
 *
 * 依据《固件软件需求说明书 FW-SRS》FW-DRIVE-02、《固件详细设计说明书》§4.1、§6：
 * - 48 段基础率（30min/段），单位 IU/h；
 * - 段总脉冲 = floor(段速率 × 0.5 / 0.00339)；
 * - 每 3 分钟输注一次（一段含 3min×10=30min）；
 * - 计量不足每 3 分钟时按均匀间隔分配（Bresenham 匀距，各槽差 ≤1）。
 */
#ifndef PUMPILOT_BASAL_SCHEDULER_H
#define PUMPILOT_BASAL_SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>
#include "algo_params.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 段内 3 分钟槽数（30min / 3min） */
#define BASAL_SLOTS_PER_SEG   10u

/**
 * @brief 加载 48 段基础率表（IU/h）。
 * @param table 48 个元素，值 >=0
 */
void basal_scheduler_load(const float *table);

/** 获取指定段基础率（IU/h） */
float basal_scheduler_get_seg_rate(uint8_t seg);

/**
 * @brief 计算指定段总脉冲数 = floor(速率×0.5/0.00339)。
 * @param seg 段索引 0..47
 * @return 该段 0.5h 内整数脉冲数
 */
uint32_t basal_scheduler_seg_pulses(uint8_t seg);

/**
 * @brief 计算指定段的第 slot 个 3 分钟槽应输出脉冲数（Bresenham 匀距）。
 * @param seg  段索引 0..47
 * @param slot 槽索引 0..9（段内第几个 3 分钟）
 * @return 该槽脉冲数
 */
uint32_t basal_scheduler_slot_pulses(uint8_t seg, uint8_t slot);

/**
 * @brief 由分钟自零点+当前段/槽推算当前应执行的脉冲序列。
 *        供 PumpApp 的 3 分钟 tick 调用。返回当前整数分钟对应段的第几个槽。
 * @param minute_of_day 0..1439
 * @param seg_out 输出段索引
 * @param slot_out 输出槽索引(0..9)
 * @return 该槽应输出脉冲数
 */
uint32_t basal_scheduler_pulses_at(uint16_t minute_of_day,
                                   uint8_t *seg_out, uint8_t *slot_out);

/** 调度是否已激活（运行中） */
bool basal_scheduler_is_active(void);

/** 启动基础率调度（进入 ST_RUNNING 时调用） */
void basal_scheduler_start(void);

/** 暂停 */
void basal_scheduler_pause(void);

/** 恢复 */
void basal_scheduler_resume(void);

/** 累计已注射基础率（IU） */
float basal_scheduler_delivered_iu(void);

/** 追加已输注脉冲到累计（PWM 实际输出后由 PumpApp/Scheduler 调用） */
void basal_scheduler_add_delivered_pulses(uint32_t pulses);

/** 复位累计 */
void basal_scheduler_reset_delivered(void);

/** 基础率表是否已加载 */
bool basal_scheduler_loaded(void);

#ifdef __cplusplus
}
#endif

#endif /* PUMPILOT_BASAL_SCHEDULER_H */
