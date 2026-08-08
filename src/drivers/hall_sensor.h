/**
 * @file hall_sensor.h
 * @brief 霍尔信号检测驱动。
 * 依据《固件详细设计说明书》§5.3：针24 霍尔信号，中断检测转子转动（输注反馈）。
 */
#ifndef PUMPILOT_HALL_SENSOR_H
#define PUMPILOT_HALL_SENSOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化霍尔（输入 + 边沿中断计数） */
void hall_sensor_init(void);

/** 读取累计霍尔脉冲数（自初始化/清零以来） */
uint32_t hall_get_pulse_count(void);

/** 清零累计计数（脉冲前调用以检测单脉冲反馈） */
void hall_reset_pulse_count(void);

#ifdef __cplusplus
}
#endif

#endif /* PUMPILOT_HALL_SENSOR_H */
