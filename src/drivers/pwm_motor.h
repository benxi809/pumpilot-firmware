/**
 * @file pwm_motor.h
 * @brief 步进电机 PWM 驱动（PulseDriver）。
 *
 * 依据《固件详细设计说明书》§5.1：PWM1/PWM2 轮流输出 20ms 宽脉冲，间隔 30ms；
 * 每脉冲转子转 180°，输注 0.00339 IU；含霍尔校验（连续 N 脉冲无反馈 → 机械故障）。
 */
#ifndef PUMPILOT_PWM_MOTOR_H
#define PUMPILOT_PWM_MOTOR_H

#include <stdint.h>
#include <stdbool.h>
#include "hal_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 机械故障回调（连续 PUMP_HALL_MISS_MAX 脉冲无霍尔反馈时触发） */
typedef void (*pwm_hall_fault_cb_t)(void);

/** 初始化电机 PWM + 霍尔反馈 */
void pwm_motor_init(void);

/** 注册霍尔连续缺失触发的机械故障回调 */
void pwm_motor_set_hall_fault_cb(pwm_hall_fault_cb_t cb);

/**
 * @brief 输出单个脉冲（阻塞式：20ms 脉冲 + 30ms 间隔）。
 * @return true=霍尔反馈正常；false=本次脉冲无霍尔反馈（计数累积，达阈值触发故障回调）
 */
bool pwm_motor_pulse_once(pwm_phase_t phase);

/** 当前是否正忙（PulseDriver 防叠加用） */
bool pwm_motor_busy(void);

/**
 * @brief 连续输出 n 个脉冲（快速大剂量/基础率子槽；每间隔 30ms）。
 * @param phase  起始相位（交替翻转）
 * @param count  脉冲数
 * @return 实际输出脉冲数（若中途机械故障则提前停止）
 */
uint32_t pwm_motor_pulse_burst(uint32_t count);

/** 停止电机输出 */
void pwm_motor_stop(void);

/** 复位霍尔缺失计数（大剂量切换点调用） */
void pwm_motor_clear_hall_fault(void);

#ifdef __cplusplus
}
#endif

#endif /* PUMPILOT_PWM_MOTOR_H */
