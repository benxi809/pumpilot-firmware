/**
 * alarm_app.c — 报警应用实现（SDK 无关）
 */
#include "alarm_app.h"

static alarm_app_config_t s_cfg;

/* 二级断续计时 */
static uint32_t s_l2_cycle;
static uint32_t s_l2_on;
static uint32_t s_l2_off;
static uint32_t s_l2_off_count;

/* 三级计数 */
static uint32_t s_l3_beeps;

void alarm_app_init(const alarm_app_config_t *cfg)
{
    s_cfg = cfg ? *cfg : (alarm_app_config_t){0};
    s_l2_cycle = 0;
    s_l2_on = 0;
    s_l2_off = 0;
    s_l2_off_count = 0;
    s_l3_beeps = 0;
}

void alarm_app_report_now(void)
{
    uint16_t m = alarm_engine_get_active();
    if (m && s_cfg.report) {
        /* 取最高级别（数字越大越关键时；此处一级最严重，取最小 level 值作为主级） */
        alert_level_t top = ALERT_LEVEL_3;
        for (uint16_t b = 0; b < ALARM_BIT_COUNT; b++) {
            if (m & (1u << b)) {
                alert_level_t lv = alarm_engine_level(b);
                if (lv < top)
                    top = lv;
            }
        }
        s_cfg.report(m, top);
    }
}

/*
 * 每 1s 调用一次（由主循环 1s tick 驱动）。
 *  一级：持续鸣叫（beeper active=true 连续）
 *  二级：30s 鸣叫 → 30min 休眠 → 再报，循环至应答
 *  三级：3 次温和 → 3min 后复报，循环至消除/应答
 */
void alarm_app_tick(void)
{
    /* 一次性判定当前最高的活跃层级 */
    static const uint16_t all = 0x3FFu; /* bit0..9 */
    uint16_t m = alarm_engine_get_active();
    bool has_l1 = (m & 0x003Fu) != 0;   /* bit0..5 */
    bool has_l2 = (m & 0x00C0u) != 0;   /* bit6..7 */
    bool has_l3 = (m & 0x0300u) != 0;   /* bit8..9 */
    (void)all;

    /* 一级：持续鸣叫 */
    if (has_l1) {
        if (s_cfg.beeper) s_cfg.beeper(ALERT_LEVEL_1, true);
        /* 二级/三级逻辑暂停（一级优先） */
        s_l2_on = 0; s_l3_beeps = 0;
        return;
    }

    /* 无一级，处理二级 */
    if (has_l2) {
        /* 周期 30s on / 30min off */
        s_l2_cycle++;
        if (s_l2_cycle <= 30) {
            s_l2_on = 1;
        } else {
            s_l2_on = 0;
            if (s_l2_off < 30u * 60u) {
                s_l2_off++;
            } else {
                /* 30min 后再报一轮，循环 */
                s_l2_cycle = 0;
                s_l2_off = 0;
                s_l2_off_count++;
                alarm_app_report_now();
            }
        }
        if (s_cfg.beeper) s_cfg.beeper(ALERT_LEVEL_2, s_l2_on);
        s_l3_beeps = 0;
        return;
    }

    /* 无一级/二级，处理三级：3 次温和 */
    if (has_l3) {
        s_l3_beeps++;
        if (s_l3_beeps <= 3) {
            if (s_cfg.beeper) s_cfg.beeper(ALERT_LEVEL_3, true);
        } else if (s_l3_beeps == 4) {
            if (s_cfg.beeper) s_cfg.beeper(ALERT_LEVEL_3, false);
        } else if (s_l3_beeps >= 3u * 60u + 4u) {
            /* 3min 后复报，循环 */
            s_l3_beeps = 0;
            alarm_app_report_now();
        }
        return;
    }

    /* 无报警：关闭蜂鸣 */
    if (s_cfg.beeper) s_cfg.beeper(ALERT_LEVEL_1, false);
    (void)s_l2_off;
}
