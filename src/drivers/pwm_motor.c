/**
 * @file pwm_motor.c
 * @brief 步进电机 PWM 驱动实现。
 */
#include "pwm_motor.h"
#include "hal_pwm.h"
#include "hal_gpio.h"
#include "hall_sensor.h"
#include "algo_params.h"

/* 霍尔缺失计数阈值（连续脉冲无反馈 → 机械故障） */
#define HALL_MISS_LIMIT   PUMP_HALL_MISS_MAX

static pwm_hall_fault_cb_t s_fault_cb  = 0;
static uint32_t            s_hall_miss = 0u;
static volatile bool       s_busy      = false;
static pwm_phase_t         s_phase     = PWM_PHASE_1;

/* 霍尔反馈由 hal 提供（占位：此处不直接访问寄存器的裸机接口，见 hall_sensor） */
static bool hall_check_feedback(void);

void pwm_motor_init(void)
{
    hal_pwm_init();
    /* 霍尔引脚初始化为输入（中断由 hall_sensor 配置） */
    hal_gpio_config(PIN_HALL, GPIO_DIR_INPUT, GPIO_PULL_NONE);
    s_hall_miss = 0u;
    s_busy      = false;
    s_phase     = PWM_PHASE_1;
}

void pwm_motor_set_hall_fault_cb(pwm_hall_fault_cb_t cb)
{
    s_fault_cb = cb;
}

bool pwm_motor_pulse_once(pwm_phase_t phase)
{
    bool ok;
    /* 脉冲前清零霍尔计数，脉冲后判定是否有转子反馈 */
    hall_reset_pulse_count();
    /* 当前相位输出单脉冲（阻塞 20ms + 30ms 内完成） */
    hal_pwm_pulse_once(phase);
    /* 霍尔校验：该脉冲期间是否有转子转动反馈 */
    ok = hall_check_feedback();
    if (!ok) {
        ++s_hall_miss;
        if (s_hall_miss >= HALL_MISS_LIMIT) {
            s_hall_miss = 0u;
            if (s_fault_cb != 0) {
                s_fault_cb();   /* 机械故障 */
            }
            return false;
        }
    } else {
        s_hall_miss = 0u;
    }
    return true;
}

bool pwm_motor_busy(void)
{
    return s_busy;
}

uint32_t pwm_motor_pulse_burst(uint32_t count)
{
    uint32_t i;
    uint32_t delivered = 0u;
    s_busy = true;
    for (i = 0; i < count; ++i) {
        /* 交替相位避免单线圈持续激励 */
        if (!pwm_motor_pulse_once(s_phase)) {
            break;   /* 机械故障，提前停止 */
        }
        s_phase = (s_phase == PWM_PHASE_1) ? PWM_PHASE_2 : PWM_PHASE_1;
        ++delivered;
    }
    s_busy = false;
    return delivered;
}

void pwm_motor_stop(void)
{
    hal_pwm_stop();
    s_busy = false;
}

void pwm_motor_clear_hall_fault(void)
{
    s_hall_miss = 0u;
}

/* 霍尔反馈检测：脉冲期间霍尔计数有增量，视为转子转动反馈 */
static bool hall_check_feedback(void)
{
    return hall_get_pulse_count() > 0u;
}
