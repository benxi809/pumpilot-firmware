/**
 * @file hall_sensor.c
 * @brief 霍尔信号检测实现（边沿中断计数）。
 */
#include "hall_sensor.h"
#include "hal_base.h"
#include "hal_gpio.h"

static volatile uint32_t s_hall_count = 0u;

static void hall_isr(uint32_t pin, void *param)
{
    (void)pin;
    (void)param;
    ++s_hall_count;
}

void hall_sensor_init(void)
{
    s_hall_count = 0u;
    /* 霍尔边沿上升沿触发计数（转子每转触发一次） */
    hal_gpio_config(PIN_HALL, GPIO_DIR_INPUT, GPIO_PULL_NONE);
    hal_gpio_irq_register(PIN_HALL, IRQ_EDGE_RISING, hall_isr, 0);
    hal_gpio_irq_enable(PIN_HALL, true);
}

uint32_t hall_get_pulse_count(void)
{
    return s_hall_count;
}

void hall_reset_pulse_count(void)
{
    s_hall_count = 0u;
}
