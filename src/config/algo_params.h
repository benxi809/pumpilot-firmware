/**
 * @file algo_params.h
 * @brief 固件权威算法参数（nRF52832 方案）
 *
 * 依据《固件软件需求说明书(FW-SRS) v1.0》《接口边界与契约文档 v1.0》。
 * 每脉冲输注量、驱动时序、基础率周期、格栅药量、电池分级、堵塞阈值等。
 * 这些参数是 APP 与固件共用的权威值，修改须经双仓库同步评审。
 */
#ifndef PUMPILOT_ALGO_PARAMS_H
#define PUMPILOT_ALGO_PARAMS_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================= 输注核心参数 ================= */
/** 每脉冲输注量（IU）。权威值 0.00339 IU/脉冲（nRF52832 方案）。 */
#define PUMP_IU_PER_PULSE            0.00339f

/** 排空/快注时每脉冲参考转子转动角度 */
#define PUMP_ROTOR_DEG_PER_PULSE     180.0f

/** 驱动波形单脉冲宽度（ms） */
#define PUMP_PWM_PULSE_MS            20
/** 两次脉冲间隔（ms） */
#define PUMP_PWM_INTERVAL_MS         30

/** 基础率唤醒周期（分钟） */
#define PUMP_BASAL_PERIOD_MIN        3
/** 基础率表段时长（小时/段，48 段覆盖 24h，每段 30min） */
#define PUMP_SEG_DURATION_H          0.5f
/** 基础率段数（24h / 0.5h） */
#define PUMP_BASAL_SEG_COUNT         48

/** 大剂量脉冲换算：round(dose_IU / 0.00339) */
#define PUMP_MAX_DOSE_IU             25.0f  /* 单次大剂量上限（IU），安全钳制 */

/* ================= 药量/格栅 ================= */
/** 满药量（ml）：滑动格栅 64 位，序号 1=K7=0ml → 序号64=K57=2ml */
#define PUMP_RESERVOIR_FULL_ML       2.0f
/** 格栅物理位序数 */
#define PUMP_GRID_STEPS              64
/** 针到位开关 K65 编号（非格栅键） */
#define PUMP_SW_PIN_IN              65
/** 丝杆锁止开关 K66 编号（非格栅键） */
#define PUMP_SW_LOCK                66

/* ================= 电池 / 电压 ================= */
/** 电池耗尽阈值（mV）—— 一级报警 + 旁路 + 停止输注 */
#define PUMP_BAT_DEPLETED_MV        1800
/** 电池电量低阈值（mV）—— 二级报警 */
#define PUMP_BAT_LOW_MV             2000
/** 进旁路阈值（mV），<2.0V 进旁路模式（ON/BYP 针37） */
#define PUMP_BAT_BYPASS_MV          2000

/* ================= 报警阈值 ================= */
/** 堵塞检测：理论累计输注量与格栅实测药量差 > 此值(IU) → 管路堵塞（一级） */
#define PUMP_OCCLUSION_TOLERANCE_IU 3.5f
/** 机械故障：连续 N 个 PWM 脉冲无霍尔反馈信号 → 机械故障（一级） */
#define PUMP_HALL_MISS_MAX          10

/* ================= 时钟 / 低功耗 ================= */
/** 时钟日误差要求（秒），连续 RTC 日误差 ≤1s */
#define PUMP_CLOCK_DAILY_ERR_MAX_S   1
/** 与 APP 对时周期（分钟），首次绑定设置，之后每小时对时 */
#define PUMP_CLOCK_SYNC_PERIOD_MIN   60
/** 低功耗休眠窗口（分钟），3~30 分钟，唤醒后上报状态 */
#define PUMP_LPM_WIN_MIN             3
#define PUMP_LPM_WIN_MAX             30

/* ================= 通信 ================= */
/** IEEE 11073 GATT Service UUID 0x180F (Drug Delivery Service) */
#define PUMP_GATT_SVC_UUID           0x180F
/** Data 特征：写/notify 0x2A20 */
#define PUMP_GATT_DATA_UUID          0x2A20
/** Status 特征：读 0x2A21 */
#define PUMP_GATT_STATUS_UUID        0x2A21

#ifdef __cplusplus
}
#endif

#endif /* PUMPILOT_ALGO_PARAMS_H */
