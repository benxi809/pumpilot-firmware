/**
 * @file hal_adc.h
 * @brief ADC 电池电压 HAL。
 * 依据《固件详细设计说明书》§5.4：<1.8V 耗尽(一级)、<2.0V 电量低(二级)、<2.0V 进旁路。
 * SDK 接入层用 nRF SAADC 实现。
 */
#ifndef PUMPILOT_HAL_ADC_H
#define PUMPILOT_HAL_ADC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 SAADC 通道 */
void hal_adc_init(void);

/** 读取电池电压（mV） */
uint16_t hal_adc_read_battery_mv(void);

#ifdef __cplusplus
}
#endif

#endif /* PUMPILOT_HAL_ADC_H */
