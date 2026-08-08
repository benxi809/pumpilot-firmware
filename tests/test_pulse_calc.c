/**
 * @file test_pulse_calc.c
 * @brief FW-D1 纯逻辑 PC 单元测试（host gcc 可编译运行）。
 *
 * 覆盖：脉冲换算（快速/扩展/基础率/复合）、格栅药量映射、蛇形序表完整性、
 * CRC16-CCITT、状态机转换、环形缓冲。不含板载依赖。
 */
#include <stdio.h>
#include <string.h>
#include "pulse_calc.h"
#include "crc16.h"
#include "ringbuf.h"
#include "pump_state.h"
#include "algo_params.h"

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  [FAIL] %s (line %d)\n", msg, __LINE__); g_fail++; } \
    else { printf("  [ ok ] %s\n", msg); } \
} while (0)

extern const uint8_t grid_seq[];

static void test_crc16(void)
{
    printf("[CRC16-CCITT]\n");
    uint8_t data[] = "123456789";
    uint16_t crc = crc16_ccitt(data, 9);
    CHECK(crc == 0x29B1u, "crc16('123456789') == 0x29B1");
    uint16_t incr = CRC16_INIT;
    size_t i;
    for (i = 0; i < 9; ++i) incr = crc16_ccitt_byte(incr, data[i]);
    CHECK(incr == crc, "incremental == block");
}

static void test_pulse_calc(void)
{
    printf("[PulseCalc]\n");
    CHECK(pulse_calc_fast_bolus(0.5f) == 147u, "0.5IU fast -> 147");
    CHECK(pulse_calc_fast_bolus(1.0f) == 295u, "1.0IU fast -> 295");
    CHECK(pulse_calc_fast_bolus(2.5f) == 737u, "2.5IU fast -> 737");
    CHECK(pulse_calc_basal_segment(0.5f) == 73u, "basal 0.5 IU/h seg -> 73 (floor)");
    CHECK(pulse_calc_basal_segment(1.0f) == 147u, "basal 1.0 IU/h seg -> 147");
    CHECK(pulse_calc_basal_segment(2.0f) == 294u, "basal 2.0 IU/h seg -> 294");
    /* 扩展：1.0IU/60min, slot3 → 每槽 0.05IU/0.00339 = 14.75 → floor 14 */
    CHECK(pulse_calc_ext_slot(1.0f, 60u, 3u) == 14u, "ext 1IU/60min slot3 -> 14");
    /* 复合：2.0IU, 50% → bolus 1.0IU → 295 */
    CHECK(pulse_calc_dual_bolus_part(2.0f, 50u) == 295u, "dual 2IU 50% -> 295");
    CHECK(pulse_calc_dual_bolus_part(2.0f, 100u) == pulse_calc_fast_bolus(2.0f),
          "dual 100% == fast");
    CHECK(pulse_calc_seg_of_minute(0u) == 0u, "seg(00:00) -> 0");
    CHECK(pulse_calc_seg_of_minute(30u) == 1u, "seg(00:30) -> 1");
    CHECK(pulse_calc_seg_of_minute(1439u) == 47u, "seg(23:59) -> 47 (clamp)");
}

static void test_reservoir(void)
{
    printf("[Reservoir+Grid]\n");
    CHECK(pulse_calc_reservoir_ml(1u) == 0.0f, "seq1(K7) -> 0ml");
    CHECK(pulse_calc_reservoir_ml(64u) == 2.0f, "seq64(K57) -> 2ml");
    CHECK(pulse_calc_reservoir_ml(32u) > 0.98f && pulse_calc_reservoir_ml(32u) < 0.99f,
          "seq32 -> ~0.984ml");
    /* 蛇形序表完整性：恰好覆盖 K1..K64 各一次 */
    {
        int seen[65] = { 0 };
        int i, ok = 1;
        for (i = 0; i < 64; ++i) {
            uint8_t k = grid_seq[i];
            if (k == 0 || k > 64 || seen[k]) { ok = 0; break; }
            seen[k] = 1;
        }
        CHECK(ok == 1, "grid_seq covers K1..K64 uniquely");
        CHECK(grid_seq[0] == 7 && grid_seq[63] == 57,
              "grid_seq[0]=K7(0ml), grid_seq[63]=K57(2ml)");
    }
}

static void test_state_machine(void)
{
    printf("[StateMachine]\n");
    state_machine_init();
    CHECK(state_machine_get() == ST_WAREHOUSE, "init -> WAREHOUSE");
    CHECK(state_machine_transit(EV_FILL_WAKE), "WAREHOUSE + FILL_WAKE -> INIT_SLEEP");
    CHECK(state_machine_get() == ST_INIT_SLEEP, "now INIT_SLEEP");
    CHECK(state_machine_transit(EV_APPLY_START), "INIT_SLEEP + APPLY_START -> INITIALIZING");
    CHECK(state_machine_transit(EV_LOCK_DONE), "INITIALIZING + LOCK_DONE -> READY");
    CHECK(state_machine_get() == ST_READY, "now READY");
    CHECK(state_machine_transit(EV_START_INFUSE), "READY + START_INFUSE -> RUNNING");
    CHECK(state_machine_transit(EV_PAUSE), "RUNNING + PAUSE -> PAUSED");
    CHECK(state_machine_transit(EV_RESUME), "PAUSED + RESUME -> RUNNING");
    CHECK(state_machine_transit(EV_ABANDON), "RUNNING + ABANDON -> ABANDONED");
    CHECK(state_machine_get() == ST_ABANDONED, "now ABANDONED");
    /* 非法转换 */
    CHECK(!state_machine_transit(EV_START_INFUSE), "ABANDONED + START_INFUSE invalid");
    /* 一级报警 */
    state_machine_init();
    state_machine_transit(EV_FILL_WAKE);
    state_machine_transit(EV_APPLY_START);
    state_machine_transit(EV_LOCK_DONE);
    state_machine_transit(EV_START_INFUSE);
    state_machine_transit(EV_ALERT_L1);
    CHECK(state_machine_get() == ST_PAUSED, "RUNNING + ALERT_L1 -> PAUSED (stop infusion)");
}

static void test_ringbuf(void)
{
    printf("[RingBuf]\n");
    uint8_t buf[8];
    ringbuf_t rb;
    ringbuf_init(&rb, buf, 8);
    uint8_t b;
    CHECK(ringbuf_empty(&rb), "empty initially");
    CHECK(ringbuf_push(&rb, 0x11), "push 0x11");
    CHECK(ringbuf_push(&rb, 0x73), "push 0x73");
    CHECK(ringbuf_readable(&rb) == 2u, "readable 2");
    CHECK(ringbuf_pop(&rb, &b) && b == 0x11, "pop 0x11");
    CHECK(ringbuf_pop(&rb, &b) && b == 0x73, "pop 0x73");
    CHECK(ringbuf_empty(&rb), "empty after pop");
    /* 回绕写入 */
    {
        uint8_t data[8] = {1,2,3,4,5,6};
        CHECK(ringbuf_write(&rb, data, 6) == 6u, "bulk write 6");
        uint8_t out[8];
        CHECK(ringbuf_read(&rb, out, 6) == 6u && out[5] == 6u, "bulk read 6");
    }
}

int main(void)
{
    printf("=== Pumpilot FW-D1 host tests ===\n");
    test_crc16();
    test_pulse_calc();
    test_reservoir();
    test_state_machine();
    test_ringbuf();
    if (g_fail == 0) {
        printf("\nALL TESTS PASSED\n");
        return 0;
    }
    printf("\n%d TEST(S) FAILED\n", g_fail);
    return 1;
}
