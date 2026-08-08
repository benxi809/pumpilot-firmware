/**
 * @file pump_state.c
 * @brief 泵状态机实现（FW-D1 基础骨架）。
 *
 * 依据《固件详细设计说明书》§3.2 状态转换表。完整调度/输注逻辑在 FW-D2 接入。
 * 约束：治疗位(run/pause)在 op-ready 时置位（对齐 11073 OpStat 契约）。
 */
#include "pump_state.h"

static pump_state_t s_state = ST_WAREHOUSE;

void state_machine_init(void)
{
    s_state = ST_WAREHOUSE;
}

pump_state_t state_machine_get(void)
{
    return s_state;
}

/* 转换表：transitions[state][event] -> 目标状态；PST_INVALID 表示非法 */
#define PST_INVALID ((pump_state_t)0xFF)

static const pump_state_t s_trans[7][8] = {
    /* ST_WAREHOUSE */
    { PST_INVALID, ST_INIT_SLEEP,   PST_INVALID,    PST_INVALID,    PST_INVALID, PST_INVALID, PST_INVALID, PST_INVALID },
    /* ST_INIT_SLEEP */
    { PST_INVALID, PST_INVALID,     ST_INITIALIZING, PST_INVALID,   PST_INVALID, PST_INVALID, PST_INVALID, PST_INVALID },
    /* ST_INITIALIZING */
    { PST_INVALID, PST_INVALID,     PST_INVALID,    ST_READY,       PST_INVALID, PST_INVALID, PST_INVALID, ST_ABANDONED },
    /* ST_READY */
    { PST_INVALID, PST_INVALID,     PST_INVALID,    PST_INVALID,    ST_RUNNING,  PST_INVALID, PST_INVALID, PST_INVALID },
    /* ST_RUNNING */
    { PST_INVALID, PST_INVALID,     PST_INVALID,    PST_INVALID,    PST_INVALID, ST_PAUSED,   ST_RUNNING,  ST_ABANDONED },
    /* ST_PAUSED */
    { PST_INVALID, PST_INVALID,     PST_INVALID,    PST_INVALID,    PST_INVALID, PST_INVALID, ST_RUNNING,  ST_ABANDONED },
    /* ST_ABANDONED */
    { PST_INVALID, PST_INVALID,     PST_INVALID,    PST_INVALID,    PST_INVALID, PST_INVALID, PST_INVALID, PST_INVALID }
};

/* 一级报警在运行/暂停/就绪态均停止输注 → 权威要求：任何运行态一级报警停止输注 */
static const pump_state_t s_alert_map[7] = {
    ST_WAREHOUSE, ST_INIT_SLEEP, ST_ABANDONED, ST_PAUSED, ST_PAUSED, ST_PAUSED, ST_ABANDONED
};

bool state_machine_transit(pump_event_t ev)
{
    if (ev == EV_ALERT_L1) {
        /* 一级报警：进入对应安全处理（运行/暂停→暂停停止输注；初始化→废止） */
        s_state = s_alert_map[s_state];
        return true;
    }
    if (s_state > ST_ABANDONED || ev > EV_ABANDON) {
        return false;
    }
    {
        pump_state_t next = s_trans[s_state][ev];
        if (next == PST_INVALID) {
            return false;
        }
        s_state = next;
        return true;
    }
}

const char *state_machine_name(pump_state_t s)
{
    static const char *names[] = {
        "WAREHOUSE", "INIT_SLEEP", "INITIALIZING", "READY",
        "RUNNING", "PAUSED", "ABANDONED"
    };
    if (s > ST_ABANDONED) {
        return "?";
    }
    return names[s];
}
