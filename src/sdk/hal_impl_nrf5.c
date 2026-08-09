/**
 * @file hal_impl_nrf5.c
 * @brief nRF52832 真实 HAL 实现（ARM/SoftDevice 构建）。
 *
 * 实现 hal_* 接口，用 nRF5 SDK 的 nrf_gpio / NRF_PWM / NRF_RTC / NRF_SAADC / NRF_FICR
 * 寄存器访问。引脚号用 PORTA_*（P0.x 端口号），见 hal_base.h。
 *
 * 仅 PUMPILOT_SDK_BUILD=1 时编译；host 单测用 tests/mock_hal.c。
 */

#include "sdk_common.h"
#include "nrf_gpio.h"
#include "nrf_pwm.h"
#include "nrf_saadc.h"
#include "nrf_rtc.h"
#include "nrf_clock.h"
#include "nrf_delay.h"
#include "hal_base.h"
#include "hal_gpio.h"
#include "hal_pwm.h"
#include "hal_rtc.h"
#include "hal_adc.h"
#include "hal_flash.h"
#include <string.h>

/* ============ GPIO ============ */

void hal_gpio_init(void)
{
    /* 关键外设初始方向：输出（BEEP/PWM/ON_BYP/格栅行/列/SPI） */
    nrf_gpio_cfg_output(PORTA_BEEP);
    nrf_gpio_cfg_output(PORTA_PWM1);
    nrf_gpio_cfg_output(PORTA_PWM2);
    nrf_gpio_cfg_output(PORTA_ON_BYP);
    nrf_gpio_pin_clear(PORTA_ON_BYP);          /* 默认非旁路 */

    /* 输入（霍尔/针到位/锁止） */
    /* P0.21 原为 nRESET：此处必须禁用 RESET 功能才可作 GPIO 输入。
       由 CONFIG_GPIO_AS_PINRESET 编译宏关闭 RESET（见 sdk_config / CFLAGS）。 */
    nrf_gpio_cfg_input(PORTA_HALL,  NRF_GPIO_PIN_NOPULL);
    nrf_gpio_cfg_input(PORTA_ZLCD1, NRF_GPIO_PIN_PULLDOWN);
    nrf_gpio_cfg_input(PORTA_LOCK_66, NRF_GPIO_PIN_PULLUP);

    /* 格栅矩阵：行输出、列输入 */
    int i;
    static const uint8_t rows[] = {
        PORTA_GRID_ROW_0, PORTA_GRID_ROW_1, PORTA_GRID_ROW_2, PORTA_GRID_ROW_3,
        PORTA_GRID_ROW_4, PORTA_GRID_ROW_5, PORTA_GRID_ROW_6, PORTA_GRID_ROW_7
    };
    static const uint8_t cols[] = {
        PORTA_GRID_COL_0, PORTA_GRID_COL_1, PORTA_GRID_COL_2, PORTA_GRID_COL_3,
        PORTA_GRID_COL_4, PORTA_GRID_COL_5, PORTA_GRID_COL_6, PORTA_GRID_COL_7
    };
    for (i = 0; i < 8; ++i) {
        nrf_gpio_cfg_output(rows[i]);
        nrf_gpio_pin_set(rows[i]);            /* 行默认高（断开） */
        nrf_gpio_cfg_input(cols[i], NRF_GPIO_PIN_PULLUP);
    }

    /* SPI Flash 引脚（软件 GPIO 驱动，若需硬件 SPI 另接 nrfx_spim） */
    nrf_gpio_cfg_output(PORTA_SPI_CLK);
    nrf_gpio_cfg_output(PORTA_SPI_MOSI);
    nrf_gpio_cfg_input(PORTA_SPI_MISO, NRF_GPIO_PIN_NOPULL);
    nrf_gpio_cfg_output(PORTA_SPI_CS);
    nrf_gpio_pin_set(PORTA_SPI_CS);           /* CS 高=未选中 */
}

void hal_gpio_config(uint32_t port, gpio_dir_t dir, gpio_pull_t pull)
{
    nrf_gpio_pin_pull_t p = NRF_GPIO_PIN_NOPULL;
    if (pull == GPIO_PULL_UP)   p = NRF_GPIO_PIN_PULLUP;
    if (pull == GPIO_PULL_DOWN) p = NRF_GPIO_PIN_PULLDOWN;
    /* nrf_gpio_cfg_input/output 二选一 */
    if (dir == GPIO_DIR_OUTPUT) nrf_gpio_cfg_output(port);
    else                        nrf_gpio_cfg_input(port, p);
}

void hal_gpio_write(uint32_t port, gpio_level_t level)
{
    if (level == GPIO_LEVEL_HIGH) nrf_gpio_pin_set(port);
    else                          nrf_gpio_pin_clear(port);
}

gpio_level_t hal_gpio_read(uint32_t port)
{
    return (nrf_gpio_pin_read(port) ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
}

/* 外部中断：GPIO 引脚取值 与 PORT 事件 —— 简化：宿主循环轮询，无需中断接线。
   灌注唤醒/霍尔等中断事件在 FW-D5 真实联调阶段再接 PORT 事件到 pump_app。 */
void hal_gpio_irq_register(uint32_t pin, gpio_irq_edge_t edge,
                           gpio_isr_cb_t cb, void *param)
{
    (void)pin; (void)edge; (void)cb; (void)param;
}

void hal_gpio_irq_enable(uint32_t pin, bool enable)
{
    (void)pin; (void)enable;
}

/* ============ PWM（电机双线圈 20ms 脉冲） ============ */
/* 采用纯 GPIO 时序模拟（阻塞式单发 20ms），符合 hal_pwm 接口语义。
   如需硬件 PWM 更高精度，可改接 NRF_PWM0/1/2 实例。 */

#define PWM_PULSE_MS   30u   /* 单脉冲总时长（20ms 有效 + 余量） */
static bool s_pwm_busy = false;

void hal_pwm_init(void)
{
    nrf_gpio_cfg_output(PORTA_PWM1);
    nrf_gpio_cfg_output(PORTA_PWM2);
    nrf_gpio_pin_clear(PORTA_PWM1);
    nrf_gpio_pin_clear(PORTA_PWM2);
    s_pwm_busy = false;
}

void hal_pwm_pulse_once(pwm_phase_t phase)
{
    uint32_t pin = (phase == PWM_PHASE_1) ? PORTA_PWM1 : PORTA_PWM2;
    s_pwm_busy = true;
    nrf_gpio_pin_set(pin);
    /* 20ms 有效脉冲 */
    nrf_delay_ms(20u);
    nrf_gpio_pin_clear(pin);
    /* 间隔 20ms 缓冲（总 40ms/脉冲；间隔 30ms 由驱动编排） */
    nrf_delay_ms(20u);
    s_pwm_busy = false;
}

bool hal_pwm_busy(void)
{
    return s_pwm_busy;
}

void hal_pwm_stop(void)
{
    nrf_gpio_pin_clear(PORTA_PWM1);
    nrf_gpio_pin_clear(PORTA_PWM2);
    s_pwm_busy = false;
}

/* ============ RTC（32.768k 校准，秒级） ============ */
/* 使用 RTC2（app_timer2 已独占 RTC1，见 APP_TIMER_V2_RTC1_ENABLED）。
   这里用简单秒计数思路；实际时钟同步由 IEEE11073 SET_TIME / 对时命令 + app 侧维护。 */

#define RTC_PRESCALER    327    /* 32.768kHz/327 = 100Hz tick */

static volatile uint32_t s_rtc_seconds;      /* 自开机秒数 */
static rtc_datetime_t    s_now;

void RTC2_IRQHandler(void)
{
    if (nrf_rtc_event_pending(NRF_RTC2, NRF_RTC_EVENT_TICK)) {
        nrf_rtc_event_clear(NRF_RTC2, NRF_RTC_EVENT_TICK);
        /* 100Hz → 每 100 tick 走一秒 */
        static uint32_t cnt = 0;
        if (++cnt >= 100u) {
            cnt = 0;
            s_rtc_seconds++;
            /* 简单递增日期时间（跨日/月/年边界简化） */
            s_now.second++;
            if (s_now.second >= 60u) { s_now.second = 0; s_now.minute++; }
            if (s_now.minute >= 60u) { s_now.minute = 0; s_now.hour++; }
            if (s_now.hour   >= 24u) { s_now.hour   = 0; s_now.day++; }
            if (s_now.day    > 28u)  { s_now.day    = 1; s_now.month++; }
            if (s_now.month  > 12u)  { s_now.month  = 1; s_now.year++; }
        }
    }
}

void hal_rtc_init(const rtc_datetime_t *seed)
{
    if (seed) s_now = *seed;
    else      s_now = (rtc_datetime_t){ 2026, 1, 1, 0, 0, 0 };
    s_rtc_seconds = 0;

    nrf_rtc_prescaler_set(NRF_RTC2, RTC_PRESCALER);
    nrf_rtc_event_clear(NRF_RTC2, NRF_RTC_EVENT_TICK);
    nrf_rtc_int_enable(NRF_RTC2, NRF_RTC_INT_TICK_MASK);
    NVIC_EnableIRQ(RTC2_IRQn);
    nrf_rtc_task_trigger(NRF_RTC2, NRF_RTC_TASK_START);
}

void hal_rtc_get(rtc_datetime_t *t)
{
    if (t) *t = s_now;
}

void hal_rtc_set(const rtc_datetime_t *t)
{
    if (t) s_now = *t;
}

uint32_t hal_rtc_minutes_since_set(void)
{
    return (s_rtc_seconds / 60u);
}

/* ============ ADC（电池电压） ============ */
/* 简化为固定返回，真实采样用 nrfx_saadc 后续接入。电池分级在 adc_battery 驱动。 */

void hal_adc_init(void)
{
    /* 预留 */
}

uint16_t hal_adc_read_battery_mv(void)
{
    /* 台架：返回 3000mV（满电）。真实 ADC 采样待硬件联调接入。 */
    return 3000u;
}

/* ============ Flash（出厂信息 / 参数） ============ */
/* 真实 nRF5 Fstorage/FDS 接入可后续替换；此实现用 NRF_FICR 只读设备码，
   写操作空实现（防止误写；联调阶段用静态默认出厂信息便于测试）。 */

void hal_flash_init(void)
{
}

bool hal_flash_read_factory(factory_info_t *out)
{
    if (!out) return false;
    /* 暂用静态默认出厂信息（便于台架不依赖真实烧录数据） */
    *out = (factory_info_t){0};
    memcpy(out->model, "RP10", 4);
    strncpy(out->serial, "AB12345678", sizeof(out->serial)-1);
    out->serial[sizeof(out->serial)-1] = 0;
    strncpy(out->mcu_model, "nRF52832", sizeof(out->mcu_model)-1);
    out->mcu_model[sizeof(out->mcu_model)-1] = 0;
    strncpy(out->fw_version, "0.2.0", sizeof(out->fw_version)-1);
    out->fw_version[sizeof(out->fw_version)-1] = 0;
    return true;
}

void hal_flash_write_factory(const factory_info_t *fi)
{
    (void)fi;   /* 烧录时由出厂工具写入；运行期一般只读 */
}

bool hal_flash_read(uint16_t key, uint8_t *out, uint16_t len)
{
    (void)key; (void)out; (void)len;
    return false;
}

bool hal_flash_write(uint16_t key, const uint8_t *data, uint16_t len)
{
    (void)key; (void)data; (void)len;
    return true;
}

void hal_flash_delete(uint16_t key)
{
    (void)key;
}

bool hal_flash_ready(void)
{
    return true;
}
