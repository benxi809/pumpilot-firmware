/**
 * @file grid_scan.c
 * @brief 滑动格栅扫描实现。
 *
 * 蛇形物理次序表依据《固件软件需求说明书 FW-SRS》FW-SENSE-01：
 * 从活塞全推出(0ml)到完全回退(2ml)实际排列次序。64 个物理位置恰好覆盖 K1~K64。
 * @note 原文档 FW-SRS 该序列有笔误（重复了一个 K56）；此处按 64 个唯一位序修正。
 */
#include "grid_scan.h"
#include "hal_gpio.h"
#include "pulse_calc.h"
#include "algo_params.h"

/* 蛇形物理次序：下标 i(0..63) = 物理序号(i+1)，值为对应的格键 K */
/* 0ml(序号1,K7) -> 2ml(序号64,K57) */
const uint8_t grid_seq[GRID_STEPS] = {
    7,  6,  5,  4,  3,  2,  1,  8,
    9,  10, 11, 12, 13, 14, 15, 16,
    17, 18, 19, 20, 21, 22, 23, 24,
    32, 31, 30, 29, 28, 27, 26, 25,
    41, 42, 43, 44, 45, 46, 47, 48,
    40, 39, 38, 37, 36, 35, 34, 33,
    49, 50, 51, 52, 53, 54, 55, 56,
    64, 63, 62, 61, 60, 59, 58, 57
};

/* 行引脚表 */
static const uint8_t s_row_pins[8] = {
    GRID_ROW_PIN_0, GRID_ROW_PIN_1, GRID_ROW_PIN_2, GRID_ROW_PIN_3,
    GRID_ROW_PIN_4, GRID_ROW_PIN_5, GRID_ROW_PIN_6, GRID_ROW_PIN_7
};

/* 列引脚表 */
static const uint8_t s_col_pins[8] = {
    GRID_COL_PIN_0, GRID_COL_PIN_1, GRID_COL_PIN_2, GRID_COL_PIN_3,
    GRID_COL_PIN_4, GRID_COL_PIN_5, GRID_COL_PIN_6, GRID_COL_PIN_7
};

/* 由行列还原格键号 K：(row*8 + col) + 1 */
static uint8_t key_of(uint8_t row, uint8_t col)
{
    return (uint8_t)(row * 8u + col + 1u);
}

void grid_scan_init(void)
{
    uint8_t i;
    /* 行：输出，默认拉低 */
    for (i = 0; i < 8; ++i) {
        hal_gpio_config(s_row_pins[i], GPIO_DIR_OUTPUT, GPIO_PULL_NONE);
        hal_gpio_write(s_row_pins[i], GPIO_LEVEL_LOW);
    }
    /* 列：输入上拉 */
    for (i = 0; i < 8; ++i) {
        hal_gpio_config(s_col_pins[i], GPIO_DIR_INPUT, GPIO_PULL_UP);
    }
    /* 到位/锁止开关：输入上拉 */
    hal_gpio_config(PIN_ZLCD1, GPIO_DIR_INPUT, GPIO_PULL_UP);
}

/* 扫描 8×8：逐行选通，列检测。返回唯一接通键号(1..64)，多键/无键返回0 */
static uint8_t grid_scan_key(void)
{
    uint8_t r, c;
    uint8_t hit = 0u;
    for (r = 0; r < 8; ++r) {
        /* 选通行：拉低该行 */
        hal_gpio_write(s_row_pins[r], GPIO_LEVEL_LOW);
        for (c = 0; c < 8; ++c) {
            if (hal_gpio_read(s_col_pins[c]) == GPIO_LEVEL_LOW) {
                if (hit != 0u) {
                    return 0u; /* 多键同时接通，异常 */
                }
                hit = key_of(r, c);
            }
        }
        /* 取消选通 */
        hal_gpio_write(s_row_pins[r], GPIO_LEVEL_HIGH);
    }
    return hit;
}

uint8_t grid_scan_position(void)
{
    uint8_t k = grid_scan_key();
    uint8_t i;
    if (k == 0u) {
        return 0u;
    }
    /* 在蛇形表中定位 k 的物理序号 */
    for (i = 0; i < GRID_STEPS; ++i) {
        if (grid_seq[i] == k) {
            return (uint8_t)(i + 1u); /* 物理序号 1..64 */
        }
    }
    return 0u; /* 键号不在表中（异常） */
}

bool grid_is_key(uint8_t k)
{
    return grid_scan_key() == k;
}

bool grid_is_pin_in(void)
{
    /* K65 针到位：ZLCD1 引脚，低有效（触点闭合） */
    return hal_gpio_read(PIN_ZLCD1) == GPIO_LEVEL_LOW;
}

bool grid_is_lock(void)
{
    /* K66 丝杆锁止：锁定引脚，低有效（占位） */
    return hal_gpio_read(PIN_LOCK) == GPIO_LEVEL_LOW;
}

float grid_reservoir_ml(void)
{
    uint8_t pos = grid_scan_position();
    return pulse_calc_reservoir_ml(pos);
}
