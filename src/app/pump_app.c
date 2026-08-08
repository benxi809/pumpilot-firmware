/**
 * @file pump_app.c
 * @brief 泵应用主程序实现（FW-D1 引导骨架）。
 *
 * FW-D1 范围：初始化各 HAL/驱动、载入出厂信息、进入状态机初始态、主循环事件分发。
 * FW-D2 将在此接入基础率/大剂量调度；FW-D3 接入 BLE/IEEE11073。
 */
#include "pump_app.h"
#include "pump_state.h"
#include "hal_gpio.h"
#include "hal_rtc.h"
#include "hal_flash.h"
#include "pwm_motor.h"
#include "grid_scan.h"
#include "hall_sensor.h"
#include "adc_battery.h"
#include "beeper.h"
#include "ringbuf.h"

/* 事件队列（ISR→主循环，单生产单消费） */
#define APP_EVT_Q_SIZE  32u
static uint8_t          s_evt_buf[APP_EVT_Q_SIZE];
static ringbuf_t        s_evt_q;

static factory_info_t   s_factory;

static void on_hall_fault(void)
{
    /* 机械故障 → 一级报警事件 */
    beeper_alert(ALERT_LEVEL_1);
    pump_app_event_push(APP_EV_HALL_FAULT);
}

void pump_app_init(const factory_info_t *fi)
{
    rtc_datetime_t seed = { 2026, 1, 1, 0, 0, 0 };

    /* 1. 时钟/电源初始化 */
    hal_rtc_init(&seed);

    /* 2. Flash 载入出厂信息（外部注入优先） */
    if (fi != 0) {
        s_factory = *fi;
    } else if (!hal_flash_read_factory(&s_factory)) {
        /* 出厂信息缺失：用空结构（调试/测试场景允许） */
        s_factory = (factory_info_t){ 0 };
    }

    /* 3. GPIO_ISR 配置（灌注唤醒、霍尔、K65/K66） */
    hal_gpio_init();
    hall_sensor_init();
    grid_scan_init();

    /* 4. 驱动初始化 */
    pwm_motor_init();
    adc_battery_init();
    beeper_init();
    pwm_motor_set_hall_fault_cb(on_hall_fault);

    /* 5. 状态机初始态 */
    state_machine_init();

    /* 6. 事件队列 */
    ringbuf_init(&s_evt_q, s_evt_buf, APP_EVT_Q_SIZE);

    /* 出厂信息载入：FIRMWARE 骨架就绪 */
    (void)s_factory;
}

void pump_app_event_push(app_event_t ev)
{
    ringbuf_push(&s_evt_q, (uint8_t)ev);
}

void pump_app_run(void)
{
    uint8_t ev;
    for (;;) {
        if (ringbuf_pop(&s_evt_q, &ev)) {
            switch ((app_event_t)ev) {
            case APP_EV_HALL_FAULT:
                /* 机械故障：转入一级报警停止 */
                state_machine_transit(EV_ALERT_L1);
                break;
            case APP_EV_FILL_WAKE:
                state_machine_transit(EV_FILL_WAKE);
                break;
            case APP_EV_PIN_IN:
                state_machine_transit(EV_APPLY_START);
                break;
            case APP_EV_LOCK:
                state_machine_transit(EV_LOCK_DONE);
                break;
            case APP_EV_ADC_LOW:
                beeper_alert(ALERT_LEVEL_2);
                break;
            default:
                /* 计时器/对时/低功耗等由 FW-D2/D4 处理 */
                break;
            }
        } else {
            /* 低频轮询：电池/蜂鸣 */
            adc_battery_poll();
            beeper_tick(50u);
            /* 低功耗调度由 FW-D4 实现 */
        }
    }
}
