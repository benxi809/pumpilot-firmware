/**
 * @file mock_hal.c
 * @brief Host 测试用 HAL stub（板载依赖模拟）。
 *
 * 仅为 PC 单测提供可链接的 hal_* 空实现；板载编译使用 SDK 接入层的真实实现。
 */
#include "hal_base.h"
#include "hal_gpio.h"
#include "hal_pwm.h"
#include "hal_rtc.h"
#include "hal_adc.h"
#include "hal_flash.h"
#include <string.h>

/* ---- GPIO ---- */
void hal_gpio_init(void) {}
void hal_gpio_config(uint32_t pin, gpio_dir_t dir, gpio_pull_t pull) { (void)pin;(void)dir;(void)pull; }
void hal_gpio_write(uint32_t pin, gpio_level_t level) { (void)pin;(void)level; }
gpio_level_t hal_gpio_read(uint32_t pin) { (void)pin; return GPIO_LEVEL_HIGH; }
void hal_gpio_irq_register(uint32_t pin, gpio_irq_edge_t edge, gpio_isr_cb_t cb, void *param)
{ (void)pin;(void)edge;(void)cb;(void)param; }
void hal_gpio_irq_enable(uint32_t pin, bool enable) { (void)pin;(void)enable; }

/* ---- PWM ---- */
void hal_pwm_init(void) {}
void hal_pwm_pulse_once(pwm_phase_t phase) { (void)phase; }
bool hal_pwm_busy(void) { return false; }
void hal_pwm_stop(void) {}

/* ---- RTC ---- */
static rtc_datetime_t s_mock_rtc = { 2026, 1, 1, 0, 0, 0 };
void hal_rtc_init(const rtc_datetime_t *seed) { if (seed) s_mock_rtc = *seed; }
void hal_rtc_get(rtc_datetime_t *t) { *t = s_mock_rtc; }
void hal_rtc_set(const rtc_datetime_t *t) { if (t) s_mock_rtc = *t; }
uint32_t hal_rtc_minutes_since_set(void) { return 0u; }

/* ---- ADC ---- */
void hal_adc_init(void) {}
uint16_t hal_adc_read_battery_mv(void) { return 3000u; } /* 模拟满电 */

/* ---- Flash ---- */
void hal_flash_init(void) {}
bool hal_flash_read_factory(factory_info_t *out)
{
    /* 提供一份默认出厂信息便于 host 测试 */
    if (!out) return false;
    *out = (factory_info_t){0};
    memcpy(out->model, "RP10", 4);
    strcpy(out->serial, "AB12345678");
    strcpy(out->mcu_model, "nRF52832");
    strcpy(out->fw_version, "0.1.0");
    return true;
}
void hal_flash_write_factory(const factory_info_t *fi) { (void)fi; }
bool hal_flash_read(uint16_t key, uint8_t *out, uint16_t len) { (void)key;(void)out;(void)len; return false; }
bool hal_flash_write(uint16_t key, const uint8_t *data, uint16_t len) { (void)key;(void)data;(void)len; return true; }
void hal_flash_delete(uint16_t key) { (void)key; }
bool hal_flash_ready(void) { return true; }
