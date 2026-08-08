/**
 * @file bolus_scheduler.h
 * @brief 大剂量调度器（快速/扩展/复合）。
 *
 * 依据《固件软件需求说明书 FW-SRS》FW-DRIVE-03、《固件详细设计说明书》§4.2、§6：
 * - 快速：脉冲 = round(剂量/0.00339)，一次连续输出；
 * - 扩展：指定时间内按基础率方式分布（每 3 分钟槽匀距）；
 * - 复合：前半按快速，后半按扩展；切换点脉冲去重防叠加（RSK-04）；
 * - op_id 幂等去重（RSK-06）：已执行命令重发不重复输注。
 */
#ifndef PUMPILOT_BOLUS_SCHEDULER_H
#define PUMPILOT_BOLUS_SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 大剂量类型 */
typedef enum {
    BOLUS_FAST = 0,   /* 快速 */
    BOLUS_EXT  = 1,   /* 扩展 */
    BOLUS_DUAL = 2    /* 复合 */
} bolus_type_t;

/** 大剂量规格（来自 APP 命令 payload） */
typedef struct {
    uint32_t  op_id;          /* 幂等去重 */
    bolus_type_t type;        /* 类型 */
    float     dose_iu;        /* 总剂量(IU) */
    uint16_t  extension_min;  /* 扩展/复合的扩展时长(分钟) */
    uint8_t   bolus_ratio;    /* 复合前半比例(0..100) */
} bolus_spec_t;

/** 调度器运行状态 */
typedef enum {
    BOLUS_IDLE      = 0,
    BOLUS_RUNNING   = 1,  /* 正在执行 */
    BOLUS_FAULT     = 2   /* 异常中止 */
} bolus_state_t;

/** 停止原因 */
typedef enum {
    BOLUS_STOP_DONE     = 0,  /* 正常完成 */
    BOLUS_STOP_USER     = 1,  /* 用户中止 */
    BOLUS_STOP_FAULT    = 2,  /* 机械/报警中止 */
    BOLUS_STOP_ALERT_L1 = 3   /* 一级报警停止 */
} bolus_stop_reason_t;

/** 状态回调：某阶段输注完成/大剂量完成/中止 */
typedef void (*bolus_event_cb_t)(bolus_stop_reason_t reason);

/**
 * @brief 启动一个大剂量。
 * @param spec 规格
 * @return true=已接受并开始；false=拒绝（冲突/非法/重复op_id）
 */
bool bolus_scheduler_start(const bolus_spec_t *spec);

/** 停止当前大剂量 */
void bolus_scheduler_stop(bolus_stop_reason_t r);

/** 注册完成/中止回调 */
void bolus_scheduler_set_event_cb(bolus_event_cb_t cb);

/** 当前状态 */
bolus_state_t bolus_scheduler_state(void);

/** 当前是否忙 */
bool bolus_scheduler_busy(void);

/**
 * @brief 当前应输出的下一批脉冲数（调用方经 PWM 输出后再继续）。
 *        返回 >0 表示有脉冲可输出；返回 0 且 RUNNING 表示等待下一时隙。
 * @param pulses_out 输出应执行的脉冲数
 * @param final  输出是否为最后一次（本次后完成）
 * @return true=有待执行动作
 */
bool bolus_scheduler_poll(uint32_t *pulses_out, bool *final);

/**
 * @brief 扩展/复合扩展部分的单 3 分钟槽脉冲（Bresenham 匀距）。
 * @param total_ext_pulses 扩展总脉冲数
 * @param idx 时段内第几个 3 分钟槽(从0)
 * @param total_slots 扩展总槽数
 * @return 该槽脉冲
 */
uint32_t bolus_scheduler_ext_slot_pulses(uint32_t total_ext_pulses,
                                         uint32_t idx, uint32_t total_slots);

/** 初始化（清零状态与 op_id 记录） */
void bolus_scheduler_init(void);

#ifdef __cplusplus
}
#endif

#endif /* PUMPILOT_BOLUS_SCHEDULER_H */
