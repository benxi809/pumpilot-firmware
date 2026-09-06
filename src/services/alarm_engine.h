/**
 * alarm_engine.h — 报警引擎（SDK 无关）
 *
 * FW-D4 / FW-SRS §2.3 FW-ALM
 * 报警位分级管理 + 周期阈值巡检。
 *
 * 三级报警：
 *   一级：管路堵塞、药液泄露、电池耗尽、药液用尽、自检出错、机械故障
 *   二级：泵电池电量低、剩余药液量低
 *   三级：系统时间误差过大、泵与APP失联超时
 *
 * 报警一旦产生立即上报（不等待休眠周期）。
 */
#ifndef ALARM_ENGINE_H
#define ALARM_ENGINE_H

#include <stdbool.h>
#include <stdint.h>
/* 报警级别 alert_level_t 由 beeper 驱动统一定义（一级/二级/三级） */
#include "beeper.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------- 报警位定义 ---------------------------- */
enum {
    /* 一级 */
    ALARM_BIT_OCLUSION   = 0,   /* 管路堵塞（理论输注 >3.5IU）        */
    ALARM_BIT_LEAK       = 1,   /* 药液泄露                           */
    ALARM_BIT_BATT_DEAD  = 2,   /* 电池耗尽（<1.8V）                  */
    ALARM_BIT_RESV_EMPTY = 3,   /* 药液用尽（K7=0ml）                 */
    ALARM_BIT_SELFTEST   = 4,   /* 自检出错                           */
    ALARM_BIT_MECH       = 5,   /* 机械故障（连续10脉冲无霍尔）        */

    /* 二级 */
    ALARM_BIT_BATT_LOW   = 6,   /* 泵电池电量低（<2.0V）              */
    ALARM_BIT_RESV_LOW   = 7,   /* 剩余药液量低（K1）                 */

    /* 三级 */
    ALARM_BIT_TIME_DRIFT = 8,   /* 系统时间误差过大                   */
    ALARM_BIT_LOST       = 9,   /* 泵与APP失联超时                    */

    ALARM_BIT_COUNT      = 10
};

/* ---------------------------- 报警级别 ----------------------------
 * alert_level_t（ALERT_LEVEL_1/2/3）在 drivers/beeper.h 统一定义。
 */

/* ---------------------------- 报警状态 ---------------------------- */
typedef struct {
    uint16_t active;          /* 活跃报警位掩码（bit0..9）       */
    alert_level_t levels[ALARM_BIT_COUNT]; /* 每位的级别        */
    uint32_t raised_flag;     /* 1=自上次消费后有新报警产生 */
} alarm_state_t;

/* ---------------------------- 巡检输入 ---------------------------- */
typedef struct {
    bool occlusion;           /* 堵塞（理论输注>3.5IU）   */
    bool leak;                /* 泄露                     */
    bool batt_dead;           /* 电池<1.8V                */
    bool batt_low;            /* 电池<2.0V                */
    bool resv_empty;          /* 药量用尽 (K7)            */
    bool resv_low;            /* 药量低 (K1)              */
    bool mech_fault;          /* 机械故障                 */
    bool selftest_fail;       /* 自检出错                 */
    bool time_drift;          /* 时间误差过大             */
    bool lost;                /* 与APP失联               */
} alarm_input_t;

/* ---------------------------- 接口 ---------------------------- */
void     alarm_engine_init(void);

/* 直接置位/清除单个报警位（应用/驱动事件驱动调用） */
void     alarm_engine_raise(uint16_t bit);
void     alarm_engine_clear(uint16_t bit);

/* 根据输入阈值巡检，自动置位/清除对应位（周期调用） */
void     alarm_engine_check(const alarm_input_t *in);

/* 查询某位是否活跃 */
bool     alarm_engine_is_active(uint16_t bit);

/* 自上次调用后是否有新产生的报警（用于立即上报 IEEE_RPT_ALERT 0x81） */
bool     alarm_engine_take_new(void);

/* 当前活跃报警位掩码 */
uint16_t alarm_engine_get_active(void);

/* 某活跃位的级别（用于驱动蜂鸣）；无该位返回 0 */
alert_level_t alarm_engine_level(uint16_t bit);

/* 活跃报警数量 */
uint8_t  alarm_engine_active_count(void);

#ifdef __cplusplus
}
#endif

#endif /* ALARM_ENGINE_H */
