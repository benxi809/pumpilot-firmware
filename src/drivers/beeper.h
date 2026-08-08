/**
 * @file beeper.h
 * @brief 蜂鸣/震动驱动。
 * 依据《固件详细设计说明书》§5.5、架构设计 §4.3：按报警级别输出不同模式。
 */
#ifndef PUMPILOT_BEEPER_H
#define PUMPILOT_BEEPER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 报警级别（对应警报分级） */
typedef enum {
    ALERT_LEVEL_NONE = 0,
    ALERT_LEVEL_1    = 1,  /* 一级：持续蜂鸣 */
    ALERT_LEVEL_2    = 2,  /* 二级：断续 30s 循环 */
    ALERT_LEVEL_3    = 3   /* 三级：3 次循环 */
} alert_level_t;

/** 初始化蜂鸣器引脚 */
void beeper_init(void);

/** 触发某级别报警蜂鸣 */
void beeper_alert(alert_level_t level);

/** 停止蜂鸣 */
void beeper_stop(void);

/**
 * @brief 蜂鸣器周期轮询（需在定时中断/主循环调用）。
 * 二级按 30s 循环断续，三级 3 次后自动停止。
 * @param elapsed_ms 距上次调用的毫秒数
 */
void beeper_tick(uint32_t elapsed_ms);

#ifdef __cplusplus
}
#endif

#endif /* PUMPILOT_BEEPER_H */
