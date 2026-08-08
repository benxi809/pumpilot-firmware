/**
 * @file hal_base.h
 * @brief HAL 层基础类型定义（板级引脚映射、开关/状态位、事件）。
 *
 * 依据《固件架构与概要设计 v1.0》§1 硬件平台、《固件详细设计说明书 v1.0》§5。
 * 引脚定义为板级约束；开关 K1~K64 为滑动格栅键，K65/K66 为到位/锁止开关。
 * HAL 层为 SDK 无关抽象，SDK 接入时在 hal_impl 中给出真实寄存器实现。
 */
#ifndef PUMPILOT_HAL_BASE_H
#define PUMPILOT_HAL_BASE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================= 板级引脚编号（nRF52832 GPIO） ================= */
/* 依据《固件架构与概要设计 v1.0》§1 */
#define PIN_BEEP            5u    /* 蜂鸣器针5 */
#define PIN_PWM1            6u    /* 步进电机 PWM1 针6 */
#define PIN_PWM2            7u    /* 步进电机 PWM2 针7 */
#define PIN_HALL            24u   /* 霍尔信号 针24 */
#define PIN_ZLCD1           4u    /* 针到位 K65 开关（ZLCD1） */
#define PIN_ON_BYP          37u   /* 电源旁路 ON/BYP 针37 */
#define PIN_LOCK            0u    /* 丝杆锁止 K66 开关（占位，见 board） */

/* 滑动格栅 8×8 矩阵行引脚（8~12,23,27,28） */
#define GRID_ROW_PIN_0      8u
#define GRID_ROW_PIN_1      9u
#define GRID_ROW_PIN_2      10u
#define GRID_ROW_PIN_3      11u
#define GRID_ROW_PIN_4      12u
#define GRID_ROW_PIN_5      23u
#define GRID_ROW_PIN_6      27u
#define GRID_ROW_PIN_7      28u

/* 滑动格栅 8×8 矩阵列引脚（15~22） */
#define GRID_COL_PIN_0      15u
#define GRID_COL_PIN_1      16u
#define GRID_COL_PIN_2      17u
#define GRID_COL_PIN_3      18u
#define GRID_COL_PIN_4      19u
#define GRID_COL_PIN_5      20u
#define GRID_COL_PIN_6      21u
#define GRID_COL_PIN_7      22u

/* ================= GPIO 方向 / 电平 ================= */
typedef enum {
    GPIO_DIR_INPUT  = 0,
    GPIO_DIR_OUTPUT = 1
} gpio_dir_t;

typedef enum {
    GPIO_LEVEL_LOW  = 0,
    GPIO_LEVEL_HIGH = 1
} gpio_level_t;

/* 输入内部上下拉 */
typedef enum {
    GPIO_PULL_NONE = 0,
    GPIO_PULL_UP   = 1,
    GPIO_PULL_DOWN = 2
} gpio_pull_t;

/* ================= 外部中断触发沿 ================= */
typedef enum {
    IRQ_EDGE_NONE   = 0,
    IRQ_EDGE_RISING = 1,
    IRQ_EDGE_FALLING = 2,
    IRQ_EDGE_BOTH   = 3
} gpio_irq_edge_t;

/** 外部中断回调（ISR 上下文），param 为注册时传入上下文 */
typedef void (*gpio_isr_cb_t)(uint32_t pin, void *param);

/* ================= PWM 相位（电机线圈） ================= */
typedef enum {
    PWM_PHASE_1 = 0,   /* PWM1 */
    PWM_PHASE_2 = 1    /* PWM2 */
} pwm_phase_t;

/* ================= RTC 日期时间 ================= */
typedef struct {
    uint16_t year;   /* 完整年份，如 2026 */
    uint8_t  month;  /* 1..12 */
    uint8_t  day;    /* 1..31 */
    uint8_t  hour;   /* 0..23 */
    uint8_t  minute; /* 0..59 */
    uint8_t  second; /* 0..59 */
} rtc_datetime_t;

/** 分钟自零点换算（0..1439） */
uint16_t rtc_to_minute_of_day(const rtc_datetime_t *t);

#ifdef __cplusplus
}
#endif

#endif /* PUMPILOT_HAL_BASE_H */
