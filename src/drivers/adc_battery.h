/**
 * @file adc_battery.h
 * @brief 电池电压检测驱动。
 * 依据《固件详细设计说明书》§5.4：<1.8V 耗尽(一级)、<2.0V 电量低(二级)、<2.0V 进旁路(ON/BYP 针37)。
 */
#ifndef PUMPILOT_ADC_BATTERY_H
#define PUMPILOT_ADC_BATTERY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化电池电压采集 */
void adc_battery_init(void);

/** 读取电池电压（mV） */
uint16_t adc_battery_read_mv(void);

/** 是否电池耗尽（<1.8V） */
int adc_battery_is_depleted(uint16_t mv);

/** 是否电量低（<2.0V） */
int adc_battery_is_low(uint16_t mv);

/** 读电压并更新旁路输出（<2.0V 拉低 ON/BYP 进旁路） */
void adc_battery_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* PUMPILOT_ADC_BATTERY_H */
