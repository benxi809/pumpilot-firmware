/**
 * @file hal_gpio.h
 * @brief GPIO 与外部中断 HAL 接口（SDK 无关抽象）。
 * 实际寄存器实现由 SDK 接入层（nRF5 gpio）填充。
 */
#ifndef PUMPILOT_HAL_GPIO_H
#define PUMPILOT_HAL_GPIO_H

#include "hal_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 GPIO 子系统 */
void hal_gpio_init(void);

/** 配置引脚方向 */
void hal_gpio_config(uint32_t pin, gpio_dir_t dir, gpio_pull_t pull);

/** 写输出电平 */
void hal_gpio_write(uint32_t pin, gpio_level_t level);

/** 读输入电平 */
gpio_level_t hal_gpio_read(uint32_t pin);

/**
 * @brief 注册外部中断（边沿触发）并使能。
 * @param pin  引脚
 * @param edge 触发沿
 * @param cb   回调（ISR）
 * @param param 传给回调的上下文
 */
void hal_gpio_irq_register(uint32_t pin, gpio_irq_edge_t edge,
                           gpio_isr_cb_t cb, void *param);

/** 使能/关闭某引脚外部中断 */
void hal_gpio_irq_enable(uint32_t pin, bool enable);

#ifdef __cplusplus
}
#endif

#endif /* PUMPILOT_HAL_GPIO_H */
