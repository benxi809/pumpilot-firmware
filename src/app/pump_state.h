/**
 * @file pump_state.h
 * @brief 泵状态机定义（FW-D1 定义枚举与接口，FW-D2 实现完整转换逻辑）。
 *
 * 依据《固件详细设计说明书》§3.2、《固件架构与概要设计》§3。
 */
#ifndef PUMPILOT_PUMP_STATE_H
#define PUMPILOT_PUMP_STATE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 泵状态机状态 */
typedef enum {
    ST_WAREHOUSE    = 0,  /* 仓储：MCU 深睡，格栅簧片 K3，灌注唤醒 */
    ST_INIT_SLEEP   = 1,  /* 初始休眠：排空初始化前的休眠态 */
    ST_INITIALIZING = 2,  /* 初始化中：贴敷进针、排空 */
    ST_READY        = 3,  /* 就绪(等待)：可开始输注 */
    ST_RUNNING      = 4,  /* 运行中：基础率/大剂量/临时基础率执行 */
    ST_PAUSED       = 5,  /* 治疗暂停 */
    ST_ABANDONED    = 6   /* 废止：终止使用 */
} pump_state_t;

/** 泵事件 */
typedef enum {
    EV_NONE         = 0,
    EV_FILL_WAKE    = 1,  /* 灌注唤醒（推杆回退中断） */
    EV_APPLY_START  = 2,  /* 贴敷+启用（进针 K65） */
    EV_LOCK_DONE    = 3,  /* 丝杆锁止 K66（排空完成） */
    EV_START_INFUSE = 4,  /* 开始输注 */
    EV_PAUSE        = 5,  /* 暂停 */
    EV_RESUME       = 6,  /* 恢复 */
    EV_ABANDON      = 7,  /* 废止 */
    EV_ALERT_L1     = 8   /* 一级报警（堵塞/机械/耗尽/用尽） */
} pump_event_t;

/** 获取当前状态 */
pump_state_t state_machine_get(void);

/**
 * @brief 驱动状态转换。
 * @param ev 事件
 * @return true=转换成功；false=该状态下事件非法
 */
bool state_machine_transit(pump_event_t ev);

/** 初始化状态机（默认 ST_WAREHOUSE） */
void state_machine_init(void);

/** 状态名（调试/上报用） */
const char *state_machine_name(pump_state_t s);

#ifdef __cplusplus
}
#endif

#endif /* PUMPILOT_PUMP_STATE_H */
