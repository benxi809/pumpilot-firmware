/**
 * @file beeper.c
 * @brief 蜂鸣/震动实现。
 */
#include "beeper.h"
#include "hal_base.h"
#include "hal_gpio.h"

/* 二级断续：30s 周期内 on/off */
#define BEEP_L2_PERIOD_MS     30000u
#define BEEP_L2_TOGGLE_MS     1500u
/* 三级 3 次循环：每次 on 300ms off 700ms，3 次后停 */
#define BEEP_L3_ON_MS         300u
#define BEEP_L3_OFF_MS        700u
#define BEEP_L3_CYCLES        3u

static alert_level_t s_level     = ALERT_LEVEL_NONE;
static uint32_t      s_elapsed   = 0u;   /* 当前模式运行毫秒 */
static uint32_t      s_cycle_cnt = 0u;
static int           s_on        = 0;

void beeper_init(void)
{
    hal_gpio_config(PIN_BEEP, GPIO_DIR_OUTPUT, GPIO_PULL_NONE);
    hal_gpio_write(PIN_BEEP, GPIO_LEVEL_LOW);
    s_level     = ALERT_LEVEL_NONE;
    s_elapsed   = 0u;
    s_cycle_cnt = 0u;
    s_on        = 0;
}

void beeper_alert(alert_level_t level)
{
    s_level   = level;
    s_elapsed = 0u;
    s_cycle_cnt = 0u;
    if (level == ALERT_LEVEL_1) {
        hal_gpio_write(PIN_BEEP, GPIO_LEVEL_HIGH); /* 一级持续 */
        s_on = 1;
    } else {
        hal_gpio_write(PIN_BEEP, GPIO_LEVEL_LOW);
        s_on = 0;
    }
}

void beeper_stop(void)
{
    beeper_alert(ALERT_LEVEL_NONE);
    hal_gpio_write(PIN_BEEP, GPIO_LEVEL_LOW);
    s_on = 0;
}

void beeper_tick(uint32_t elapsed_ms)
{
    if (s_level == ALERT_LEVEL_NONE) {
        return;
    }
    s_elapsed += elapsed_ms;

    if (s_level == ALERT_LEVEL_2) {
        /* 30s 内断续 */
        uint32_t phase = s_elapsed % BEEP_L2_PERIOD_MS;
        int want = (phase < BEEP_L2_TOGGLE_MS) ? 1 : 0;
        if (want != s_on) {
            s_on = want;
            hal_gpio_write(PIN_BEEP, s_on ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
        }
    } else if (s_level == ALERT_LEVEL_3) {
        /* 3 次循环后自动停 */
        uint32_t cyc_len = BEEP_L3_ON_MS + BEEP_L3_OFF_MS;
        uint32_t total = BEEP_L3_CYCLES * cyc_len;
        if (s_elapsed >= total) {
            beeper_stop();
            return;
        }
        {
            uint32_t phase = s_elapsed % cyc_len;
            int want = (phase < BEEP_L3_ON_MS) ? 1 : 0;
            if (want != s_on) {
                s_on = want;
                hal_gpio_write(PIN_BEEP, s_on ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
            }
        }
    }
}
