/**
 * @file hal_rtc.h
 * @brief 连续时钟 RTC HAL（日误差 ≤1s）。
 * 依据《固件详细设计说明书》§5.6、§4.4。SDK 接入层用 nRF RTC2/RTC1 实现。
 */
#ifndef PUMPILOT_HAL_RTC_H
#define PUMPILOT_HAL_RTC_H

#include "hal_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 RTC（起始时间由 FlashStore 或出厂默认载入） */
void hal_rtc_init(const rtc_datetime_t *seed);

/** 读取当前时间 */
void hal_rtc_get(rtc_datetime_t *t);

/** 设置当前时间（APP 通过 SET_TIME 同步） */
void hal_rtc_set(const rtc_datetime_t *t);

/**
 * @brief 距上次设置时间已过的分钟数（用于"每小时对时"判断）
 * @return 距上次 set 的分钟数
 */
uint32_t hal_rtc_minutes_since_set(void);

#ifdef __cplusplus
}
#endif

#endif /* PUMPILOT_HAL_RTC_H */
