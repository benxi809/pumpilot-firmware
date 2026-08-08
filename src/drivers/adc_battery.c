/**
 * @file adc_battery.c
 * @brief 电池电压检测实现。
 */
#include "adc_battery.h"
#include "hal_adc.h"
#include "hal_gpio.h"
#include "algo_params.h"

void adc_battery_init(void)
{
    hal_adc_init();
    /* 旁路控制输出（ON/BYP 针37） */
    hal_gpio_config(PIN_ON_BYP, GPIO_DIR_OUTPUT, GPIO_PULL_NONE);
    hal_gpio_write(PIN_ON_BYP, GPIO_LEVEL_HIGH);
}

uint16_t adc_battery_read_mv(void)
{
    return hal_adc_read_battery_mv();
}

int adc_battery_is_depleted(uint16_t mv)
{
    return mv < PUMP_BAT_DEPLETED_MV;
}

int adc_battery_is_low(uint16_t mv)
{
    return mv < PUMP_BAT_LOW_MV;
}

void adc_battery_poll(void)
{
    uint16_t mv = hal_adc_read_battery_mv();
    if (mv < PUMP_BAT_BYPASS_MV) {
        /* 电压<2.0V：ON/BYP 拉低进旁路模式（停止输注前提） */
        hal_gpio_write(PIN_ON_BYP, GPIO_LEVEL_LOW);
    } else {
        hal_gpio_write(PIN_ON_BYP, GPIO_LEVEL_HIGH);
    }
}
