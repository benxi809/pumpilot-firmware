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
/* 依据《固件架构与概要设计 v1.0》§1、《泵控制PCBA与固件开发需求.docx》、《注射器_V3.0.0.pdf》
 * 引脚值 = QFN48 封装引脚号。P0.x 端口号见下方 P0_* 映射表（SDK 接入用）。
 * 权威映射文档：docs/PIN_MAPPING_authoritative.md（待硬件方核对） */
#define PIN_BEEP            5u    /* 蜂鸣器 封装pin5  P0.03/AIN1 */
#define PIN_PWM1            6u    /* 步进电机 PWM1 pin6  P0.04/AIN2 */
#define PIN_PWM2            7u    /* 步进电机 PWM2 pin7  P0.05/AIN3 */
#define PIN_HALL            24u   /* 霍尔信号 pin24  P0.21/RESET（硬件确认，固件禁RESET当GPIO输入） */
#define PIN_ZLCD1           4u    /* 针到位 K65 (ZLCD1) pin4  P0.02/AIN0 */
#define PIN_ON_BYP          37u   /* 电源旁路 ON/BYP pin37  P0.25 */
#define PIN_LOCK            14u   /* 丝杆锁止 K66 (WLCD) pin14  P0.11（硬件确认） */

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

/* ================= 封装引脚号 → P0.x 端口号 映射（SDK 接入权威） ================= */
/*
 * nRF52832 QFN48 封装引脚号 → 实际 GPIO 端口(P0.x)。
 * 依据《注射器_V3.0.0.pdf》原理图 U1 标注，与 Nordic 官方定义一致。
 * 供 SDK(nrf_gpio/nrf_gpiote/nrf_ppi)寄存器操作使用：GPIO 端口 = pin 值。
 * 警告：芯片标准 QFN48 中 引脚号 与 P0.x 的对应关系如下（非连续），
 *       关键外设引脚务必使用下方 BOARD_PIN_* 宏，勿用 pin 号直接当端口。
 */
#define PIN_QFN_TO_P0(qfn)  /* 查表 */ _pin_qfn_to_p0(qfn)
uint8_t _pin_qfn_to_p0(uint8_t qfn_pin);  /* 实现在 sdk/hal_impl_gpio.c */

/* 关键外设 P0.x 端口（SDK 操作直接用的端口号） */
#define PORTA_PWM1          4u     /* PWM1  → P0.04 */
#define PORTA_PWM2          5u     /* PWM2  → P0.05 */
#define PORTA_BEEP          3u     /* BEEP  → P0.03 */
#define PORTA_ZLCD1         2u     /* ZLCD1 → P0.02 (k65) */
#define PORTA_HALL          21u    /* HALL  → P0.21 (nRESET,需在GPIO配置禁用RESET) */
#define PORTA_LOCK_66       11u    /* k66   → P0.11 (WLCD) 丝杆锁止 */
#define PORTA_ON_BYP        25u    /* ON/BYP→ P0.25 */
#define PORTA_GRID_ROW_0    6u     /* KY1   → P0.06 */
#define PORTA_GRID_ROW_1    7u     /* KY2   → P0.07 */
#define PORTA_GRID_ROW_2    8u     /* KY3   → P0.08 */
#define PORTA_GRID_ROW_3    9u     /* KY4   → P0.09 */
#define PORTA_GRID_ROW_4    10u    /* KY5   → P0.10 */
#define PORTA_GRID_ROW_5    20u    /* KY6   → P0.20 */
#define PORTA_GRID_ROW_6    22u    /* KY7   → P0.22 */
#define PORTA_GRID_ROW_7    23u    /* KY8   → P0.23 */
#define PORTA_GRID_COL_0    12u    /* KX1   → P0.12 */
#define PORTA_GRID_COL_1    13u    /* KX2   → P0.13 */
#define PORTA_GRID_COL_2    14u    /* KX3   → P0.14 */
#define PORTA_GRID_COL_3    15u    /* KX4   → P0.15 */
#define PORTA_GRID_COL_4    16u    /* KX5   → P0.16 */
#define PORTA_GRID_COL_5    17u    /* KX6   → P0.17 */
#define PORTA_GRID_COL_6    18u    /* KX7   → P0.18 */
#define PORTA_GRID_COL_7    19u    /* KX8   → P0.19 */

/* SPI Flash (U3 W25X40CL) —— 已由原理图坐标确认 */
#define PORTA_SPI_CLK       26u    /* SPI_CLK  → P0.26 */
#define PORTA_SPI_MOSI      27u    /* SPI_MOSI → P0.27 */
#define PORTA_SPI_MISO      28u    /* SPI_MISO → P0.28 */
#define PORTA_SPI_CS        29u    /* SPI_CS   → P0.29 */

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
