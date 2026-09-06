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
#include "basal_scheduler.h"
#include "bolus_scheduler.h"
#include "ieee11073.h"
#include "ble_service.h"
#include "alarm_engine.h"
#include "alarm_app.h"
#include "device_info.h"
#include "log_manager.h"
#include "power_mgr.h"

/* 事件队列（ISR→主循环，单生产单消费） */
#define APP_EVT_Q_SIZE  32u
static uint8_t          s_evt_buf[APP_EVT_Q_SIZE];
static ringbuf_t        s_evt_q;

static factory_info_t   s_factory;

/* forward decl（pump_app_init 中使用） */
static void ble_on_write(const uint8_t *data, uint16_t len);

static void on_hall_fault(void)
{
    /* 机械故障 → 一级报警事件 */
    beeper_alert(ALERT_LEVEL_1);
    pump_app_event_push(APP_EV_HALL_FAULT);
}

/* ---- FW-D4 报警桥接 ---- */
/* 报警上报：IEEE_RPT_ALERT(0x81)，
 * payload = level(1) + alarm_type(1) + 文本(0..N)
 * 这里沿用内部分级信息打包：level 在前，type 由 mask 低位推导（0..5 一级/6..7 二级/8..9 三级
 * → 直接映射 §10 报警码并不准确 —— 先框架保留 mask 低字节做 type 占位）。 */
static void on_alarm_report(uint16_t mask, alert_level_t level)
{
    uint8_t pl[3];
    pl[0] = (uint8_t)level;                    /* level(1) */
    pl[1] = (uint8_t)(mask & 0xFF);            /* alarm_type 低字节(占位) */
    pl[2] = (uint8_t)((mask >> 8) & 0xFF);     /* 次级 */
    uint8_t f[IEEE11073_FRAME_MAX];
    int n = ieee11073_build_report(IEEE_RPT_ALERT, pl, sizeof(pl), f);
    if (n > 0)
        ble_service_notify(f, (uint16_t)n);
}

/* 报警蜂鸣：映射到 beeper 驱动 (active=true→alert, false→stop) */
static void on_alarm_beeper(alert_level_t level, bool active)
{
    if (active) beeper_alert(level);
    else        beeper_stop();
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

    /* 7. 调度器初始化（FW-D2） */
    bolus_scheduler_init();
    /* 基础率默认空表（APP 首次绑定/被替换曲线命令时 load） */
    {
        float zero[PUMP_BASAL_SEG_COUNT];
        uint8_t i;
        for (i = 0; i < PUMP_BASAL_SEG_COUNT; ++i) zero[i] = 0.0f;
        basal_scheduler_load(zero);
        basal_scheduler_reset_delivered();
    }

    /* 8. IEEE11073 agent + BLE 服务（FW-D3） */
    ieee11073_init();
    ble_service_init(ble_on_write, 0);

    /* 出厂信息载入：FIRMWARE 骨架就绪 */
    if (!hal_flash_read_factory(&s_factory)) {
        /* 出厂信息缺失时保留外部注入；仍空则用默认 */
    }

    /* 9. FW-D4：报警 / 出厂信息 / 日志 / 低功耗 */
    alarm_engine_init();
    {
        alarm_app_config_t acfg;
        acfg.beeper = on_alarm_beeper;
        acfg.report = on_alarm_report;
        alarm_app_init(&acfg);
    }
    device_info_init(&s_factory);
    log_manager_init();
    power_mgr_init(PWR_MODE_REGULAR);
}

void pump_app_event_push(app_event_t ev)
{
    ringbuf_push(&s_evt_q, (uint8_t)ev);
}

/* ---- FW-D2 调度辅助 ---- */

/* BLE 2A20 写入回调：数据交由 IEEE11073 agent 处理 */
static void ble_on_write(const uint8_t *data, uint16_t len)
{
    ieee11073_on_data(data, len);
}

/* 将 IEEE11073 待上报帧经 BLE notify 发出 */
static void pump_app_flush_reports(void)
{
    uint8_t f[IEEE11073_FRAME_MAX];
    uint16_t fl;
    if (ieee11073_has_pending()) {
        if (ieee11073_get_pending(f, &fl)) {
            ble_service_notify(f, fl);
        }
    }
}

/* 基础率 3 分钟槽：读取时钟，计算本槽脉冲并输出 */
static void pump_app_basal_tick(void)
{
    uint16_t mom;
    uint8_t  seg, slot;
    uint32_t pulses;
    rtc_datetime_t now;

    if (!basal_scheduler_loaded() || !basal_scheduler_is_active()) {
        return;
    }
    hal_rtc_get(&now);
    mom = (uint16_t)(now.hour * 60u + now.minute);
    pulses = basal_scheduler_pulses_at(mom, &seg, &slot);
    if (pulses > 0u) {
        uint32_t done = pwm_motor_pulse_burst(pulses);
        basal_scheduler_add_delivered_pulses(done);
    }
}

/* 大剂量服务：驱动 BolusScheduler 输出脉冲（每次输出一批） */
static void pump_app_service_bolus(void)
{
    uint32_t pulses;
    bool     final;
    if (!bolus_scheduler_busy()) {
        return;
    }
    if (pwm_motor_busy()) {
        return; /* 防叠加：前一批脉冲未输出完，等待 */
    }
    if (!bolus_scheduler_poll(&pulses, &final)) {
        return;
    }
    if (pulses > 0u) {
        uint32_t done = pwm_motor_pulse_burst(pulses);
        /* 复合切换点 buf 清空后由 poll 内部推进；此处大剂量累计由后续上报 */
        (void)done;
        (void)final;
    }
}

/**
 * @brief 处理一次事件队列 + 低频轮询（非阻塞）。
 *
 * host 构建：由 pump_app_run() 的 for(;;) 反复调用；
 * SoftDevice 构建：由 main_sdk.c 在 sd_app_evt_wait 循环中调用。
 * 处理完一个事件（或无可处理时做一次低频轮询）即返回，供外部调度。
 */
void pump_app_process_once(void)
{
    uint8_t ev;
    if (ringbuf_pop(&s_evt_q, &ev)) {
        switch ((app_event_t)ev) {
        case APP_EV_HALL_FAULT:
            /* 机械故障：转入一级报警停止，中止大剂量 */
            state_machine_transit(EV_ALERT_L1);
            bolus_scheduler_stop(BOLUS_STOP_ALERT_L1);
            basal_scheduler_pause();
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
        case APP_EV_TIMER_3MIN:
            /* 基础率 3 分钟槽输注 */
            pump_app_basal_tick();
            break;
        case APP_EV_START_INFUSE:
            if (state_machine_transit(EV_START_INFUSE)) {
                basal_scheduler_start();
            }
            break;
        case APP_EV_PAUSE:
            if (state_machine_transit(EV_PAUSE)) {
                basal_scheduler_pause();
                bolus_scheduler_stop(BOLUS_STOP_USER);
            }
            break;
        case APP_EV_RESUME:
            if (state_machine_transit(EV_RESUME)) {
                basal_scheduler_resume();
            }
            break;
        case APP_EV_ABANDON:
            if (state_machine_transit(EV_ABANDON)) {
                basal_scheduler_pause();
                bolus_scheduler_stop(BOLUS_STOP_USER);
            }
            break;
        default:
            break;
        }
    } else {
        /* 无可处理事件：低频轮询：电池/蜂鸣/报警 + 大剂量服务 + 上报刷新 */
        adc_battery_poll();
        beeper_tick(50u);
        alarm_app_tick();           /* 分级报警蜂鸣驱动（FW-D4） */
        pump_app_service_bolus();
        pump_app_flush_reports();
    }
}

void pump_app_run(void)
{
    for (;;) {
        pump_app_process_once();
    }
}
