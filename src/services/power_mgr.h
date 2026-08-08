/**
 * power_mgr.h — 电源/低功耗管理（SDK 无关）
 *
 * FW-D4 / FW-SRS §2.7 FW-PWR
 *  - 正常工作 10 天（目标 2 周）低能耗；
 *  - 仓储状态深度睡眠，灌注触发中断唤醒。
 */
#ifndef POWER_MGR_H
#define POWER_MGR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PWR_MODE_REGULAR = 0,   /* 常规低功耗运行 */
    PWR_MODE_DEEP_SLEEP = 1 /* 仓储深度睡眠   */
} pwr_mode_t;

typedef enum {
    PWR_WAKE_GPIO  = 1,     /* 灌注/针到位中断唤醒 */
    PWR_WAKE_RTC   = 2      /* 定时唤醒（呼吸连接） */
} pwr_wake_src_t;

void   power_mgr_init(pwr_mode_t initial);

/* 请求进入仓储深度睡眠（灌注将中断唤醒） */
void   power_mgr_enter_deep_sleep(void);

/* 唤醒（由 GPIO_ISR 或 RTC 中断调用），返回唤醒源 */
pwr_wake_src_t power_mgr_event_wake(void);

/* 当前模式 */
pwr_mode_t power_mgr_mode(void);

/* 通知主循环：本周期是否需要休眠（无任务/无报警/无通信时允许） */
bool   power_mgr_allow_sleep(void);

/* 常规功耗调度计数（主机测试/统计用），累计进入睡眠次数 */
uint32_t power_mgr_sleep_count(void);

#ifdef __cplusplus
}
#endif

#endif /* POWER_MGR_H */
