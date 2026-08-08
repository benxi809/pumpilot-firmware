/**
 * power_mgr.c — 电源/低功耗实现（SDK 无关）
 */
#include "power_mgr.h"

static pwr_mode_t s_mode;
static uint32_t   s_sleep_count;

void power_mgr_init(pwr_mode_t initial)
{
    s_mode = initial;
    s_sleep_count = 0;
}

void power_mgr_enter_deep_sleep(void)
{
    /* 仓储深度睡眠：断电等待灌注中断（GPIO_ISR 推杆回退）唤醒 */
    s_mode = PWR_MODE_DEEP_SLEEP;
}

pwr_wake_src_t power_mgr_event_wake(void)
{
    pwr_wake_src_t src = PWR_WAKE_GPIO;
    if (s_mode == PWR_MODE_DEEP_SLEEP)
        s_mode = PWR_MODE_REGULAR;     /* 从深度睡眠恢复 */
    s_sleep_count++;
    return src;
}

pwr_mode_t power_mgr_mode(void)
{
    return s_mode;
}

bool power_mgr_allow_sleep(void)
{
    /* 深度睡眠下不参与常规调度。常规模式默认允许（由主程序补充 */
    /* 任务/报警/通信判定，这里仅提供语义钩子） */
    return (s_mode == PWR_MODE_REGULAR);
}

uint32_t power_mgr_sleep_count(void)
{
    return s_sleep_count;
}
