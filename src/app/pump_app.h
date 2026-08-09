/**
 * @file pump_app.h
 * @brief 泵应用主程序（初始化编排 + 事件循环 + 状态机驱动）。
 * 依据《固件详细设计说明书》§3.1、§8 初始化序列。
 */
#ifndef PUMPILOT_PUMP_APP_H
#define PUMPILOT_PUMP_APP_H

#include "hal_flash.h"
#include "pump_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 应用事件（ISR 可投递） */
typedef enum {
    APP_EV_FILL_WAKE    = 1,   /* 灌注唤醒中断 */
    APP_EV_HALL_RISE    = 2,   /* 霍尔上升沿（转子转动） */
    APP_EV_PIN_IN       = 3,   /* 针到位 K65 */
    APP_EV_LOCK         = 4,   /* 丝杆锁止 K66 */
    APP_EV_TIMER_3MIN   = 5,   /* 基础率 3 分钟槽 */
    APP_EV_TIMER_1H     = 6,   /* 对时 1 小时 */
    APP_EV_TIMER_LPM    = 7,   /* 低功耗唤醒窗口 */
    APP_EV_HALL_FAULT   = 8,   /* 机械故障 */
    APP_EV_ADC_LOW      = 9,   /* 电量低 */
    APP_EV_START_INFUSE = 10,  /* 开始输注(START) */
    APP_EV_PAUSE        = 11,  /* 暂停 */
    APP_EV_RESUME       = 12,  /* 恢复 */
    APP_EV_ABANDON      = 13   /* 废止 */
} app_event_t;

/**
 * @brief 初始化固件（引导序列）：
 * 时钟/电源 → Flash 载入出厂信息 → GPIO_ISR → 各服务/驱动初始化 → 状态机初始态。
 * @param fi 出厂信息（可从 hal_flash 读取，亦可外部注入用于测试）
 */
void pump_app_init(const factory_info_t *fi);

/** 主循环：处理事件队列（含低功耗调度） */
void pump_app_run(void);

/**
 * @brief 处理一次事件队列 + 低频轮询（非阻塞，单步）。
 *
 * host 由 pump_app_run() 的 for(;;) 调用；SoftDevice 构建由 main_sdk.c
 * 在 sd_app_evt_wait 循环中调用（与 SoftDevice 事件处理交替）。
 */
void pump_app_process_once(void);

/** 投递事件（ISR 上下文安全，简单队列） */
void pump_app_event_push(app_event_t ev);

#ifdef __cplusplus
}
#endif

#endif /* PUMPILOT_PUMP_APP_H */
