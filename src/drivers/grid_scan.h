/**
 * @file grid_scan.h
 * @brief 滑动格栅 8×8 开关矩阵扫描驱动。
 *
 * 依据《固件详细设计说明书》§5.2：8×8 矩阵（行 8~12,23,27,28；列 15~22），K1~K64。
 * 关键：物理位置 → 序号映射用**蛇形物理次序表**（0ml→2ml 端），不可按格号相减。
 * 另含到位开关 K65、锁止开关 K66 检测。
 */
#ifndef PUMPILOT_GRID_SCAN_H
#define PUMPILOT_GRID_SCAN_H

#include <stdint.h>
#include <stdbool.h>
#include "hal_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 格栅步数（64 位） */
#define GRID_STEPS  64u

/** 蛇形物理次序表：下标 0..63 为物理序号 1..64 对应的格键 Kx（0ml→2ml 端） */
extern const uint8_t grid_seq[GRID_STEPS];

/**
 * @brief 扫描格栅，定位当前唯一接通的物理序号（1..64）。
 * @return 物理序号；0=未检测到任何键（可能异常）
 */
uint8_t grid_scan_position(void);

/** 初始化格栅扫描（行列 GPIO 方向） */
void grid_scan_init(void);

/** 读取针到位开关 K65 状态 */
bool grid_is_pin_in(void);

/** 读取丝杆锁止开关 K66 状态 */
bool grid_is_lock(void);

/** 判断某格键 K 是否接通（K=1..64） */
bool grid_is_key(uint8_t k);

/**
 * @brief 计算当前剩余药量（ml），依据当前物理序号。
 * @return ml（调用 grid_scan_position 后）
 */
float grid_reservoir_ml(void);

#ifdef __cplusplus
}
#endif

#endif /* PUMPILOT_GRID_SCAN_H */
