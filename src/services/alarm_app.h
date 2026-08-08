/**
 * alarm_app.h — 报警应用（SDK 无关）
 *
 * FW-D4 / FW-SRS §2.3 FW-ALM-02
 * 接收 alarm_engine 的分级报警，驱动蜂鸣 + 触发立即上报。
 *
 * 驱动模式（由 beeper 实现注入）：
 *   一级：持续鸣叫（直到取下泵/电池耗尽）
 *   二级：断续 30s 循环
 *   三级：3 次循环，3 分钟后复报
 */
#ifndef ALARM_APP_H
#define ALARM_APP_H

#include <stdbool.h>
#include <stdint.h>
#include "alarm_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 蜂鸣驱动注入（底层由 beeper.c 实现，host 测试可 mock） */
typedef void (*alarm_beeper_fn)(alert_level_t level, bool active);

/* 上报注入（底层经 ieee11073 → ble_service notiry RPT_ALERT） */
typedef void (*alarm_report_fn)(uint16_t active_mask, alert_level_t level);

typedef struct {
    alarm_beeper_fn beeper;   /* 驱动蜂鸣（可空） */
    alarm_report_fn report;   /* 触发上报（可空） */
} alarm_app_config_t;

void alarm_app_init(const alarm_app_config_t *cfg);

/* 应用层收到 AlarmEngine 的分级报警，周期调用驱动驱动蜂鸣/触发上报 */
void alarm_app_tick(void);

/* 立即上报当前活跃报警（报警一旦产生即调用） */
void alarm_app_report_now(void);

#ifdef __cplusplus
}
#endif

#endif /* ALARM_APP_H */
