/**
 * @file hal_pwm.h
 * @brief PWM 电机驱动 HAL：输出 20ms 宽单脉冲。
 * 依据《固件详细设计说明书》§5.1：PWM1/PWM2 轮流输出 20ms 宽脉冲，间隔 30ms。
 * 实现为阻塞式单脉冲输出（由 PulseDriver 编排时序），SDK 接入层填充寄存器。
 */
#ifndef PUMPILOT_HAL_PWM_H
#define PUMPILOT_HAL_PWM_H

#include "hal_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 PWM1/PWM2 引脚为输出 */
void hal_pwm_init(void);

/**
 * @brief 输出一个 20ms 宽脉冲。
 * @param phase 选择 PWM1 或 PWM2 线圈相位
 * @param level 暂略（HAL 统一高电平脉冲）
 */
void hal_pwm_pulse_once(pwm_phase_t phase);

/** 当前是否正忙（正在输出脉冲） */
bool hal_pwm_busy(void);

/** 关闭 PWM 输出（停止电机） */
void hal_pwm_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* PUMPILOT_HAL_PWM_H */
