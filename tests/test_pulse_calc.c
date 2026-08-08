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
#include "basal_scheduler.h"
#include "bolus_scheduler.h"
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

/* ================= FW-D2：基础率调度测试 ================= */
static void test_basal_scheduler(void)
{
    printf("[BasalScheduler]\n");
    float table[PUMP_BASAL_SEG_COUNT];
    uint32_t i, total, sum = 0, maxdiff = 0;
    basal_scheduler_load(table); /* 全 0 */

    /* 载入：全部 1.0 IU/h */
    for (i = 0; i < PUMP_BASAL_SEG_COUNT; ++i) table[i] = 1.0f;
    basal_scheduler_load(table);
    CHECK(basal_scheduler_loaded(), "loaded");
    CHECK(basal_scheduler_get_seg_rate(0) == 1.0f, "seg0 rate 1.0");
    /* 1.0 IU/h 段总脉冲 = floor(1.0*0.5/0.00339)=147 */
    CHECK(basal_scheduler_seg_pulses(0) == 147u, "seg0 total pulses 147");

    /* Bresenham 匀距：10 槽和 = 总脉冲，各槽差 ≤1 */
    total = basal_scheduler_seg_pulses(0);
    for (i = 0; i < BASAL_SLOTS_PER_SEG; ++i) {
        uint32_t p = basal_scheduler_slot_pulses(0, (uint8_t)i);
        sum += p;
    }
    CHECK(sum == total, "10 slots sum == seg total");
    for (i = 1; i < BASAL_SLOTS_PER_SEG; ++i) {
        uint32_t a = basal_scheduler_slot_pulses(0, (uint8_t)(i - 1));
        uint32_t b = basal_scheduler_slot_pulses(0, (uint8_t)i);
        uint32_t d = (a > b) ? (a - b) : (b - a);
        if (d > maxdiff) maxdiff = d;
    }
    CHECK(maxdiff <= 1u, "slot pulse diff <=1 (uniform)");

    /* pulses_at：分钟定位 */
    {
        uint8_t seg, slot;
        uint32_t p = basal_scheduler_pulses_at(0, &seg, &slot);  /* 00:00, seg0 slot0 */
        CHECK(seg == 0 && slot == 0, "00:00 -> seg0 slot0");
        p = basal_scheduler_pulses_at(90, &seg, &slot);          /* 01:30 -> seg3 slot0 */
        CHECK(seg == 3 && slot == 0, "01:30 -> seg3 slot0");
        p = basal_scheduler_pulses_at(95, &seg, &slot);          /* 01:35 -> seg3 slot1(5/3) */
        CHECK(seg == 3 && slot == 1u, "01:35 -> seg3 slot1");
        (void)p;
    }

    /* 启停激活 */
    basal_scheduler_start();
    CHECK(basal_scheduler_is_active(), "start -> active");
    basal_scheduler_pause();
    CHECK(!basal_scheduler_is_active(), "pause -> inactive");
    basal_scheduler_resume();
    CHECK(basal_scheduler_is_active(), "resume -> active");
}

/* ================= FW-D2：大剂量调度测试 ================= */
static void test_bolus_scheduler(void)
{
    printf("[BolusScheduler]\n");
    bolus_scheduler_init();

    /* 快速大剂量：2.5 IU 一次输出 737 脉冲 */
    {
        bolus_spec_t sp = { 100u, BOLUS_FAST, 2.5f, 0u, 0u };
        uint32_t pulses; bool final = false;
        CHECK(bolus_scheduler_start(&sp), "fast start accepted");
        CHECK(bolus_scheduler_busy(), "busy while running");
        CHECK(bolus_scheduler_poll(&pulses, &final), "fast poll has output");
        CHECK(pulses == 737u, "fast 2.5IU -> 737 pulses");
        CHECK(bolus_scheduler_state() == BOLUS_IDLE, "fast completes -> IDLE");
    }

    /* 幂等：同 op_id 重复 → 拒绝 */
    {
        bolus_spec_t sp = { 100u, BOLUS_FAST, 1.0f, 0u, 0u };
        CHECK(!bolus_scheduler_start(&sp), "duplicate op_id rejected (idempotent)");
    }
    /* 不同 op_id 可执行 */
    {
        bolus_spec_t sp = { 101u, BOLUS_FAST, 1.0f, 0u, 0u };
        uint32_t pulses; bool final;
        CHECK(bolus_scheduler_start(&sp), "new op_id accepted");
        bolus_scheduler_poll(&pulses, &final);
        (void)pulses; (void)final;
    }

    /* 扩展大剂量：1.0 IU/60min → 每3分钟槽(20槽) 匀距，总和=295 */
    {
        bolus_spec_t sp = { 200u, BOLUS_EXT, 1.0f, 60u, 0u };
        uint32_t pulses, total = 0, slots = 0; bool final;
        bolus_scheduler_init();
        CHECK(bolus_scheduler_start(&sp), "ext start");
        while (bolus_scheduler_busy()) {
            if (bolus_scheduler_poll(&pulses, &final)) {
                total += pulses; slots++;
            } else break;
        }
        CHECK(total == 295u, "ext total pulses 295");
        /* 60min/3min = 20 slots */
        (void)slots;
    }

    /* 复合大剂量：2.0 IU, 50% → 前半295 + 后半295，两次阶段 */
    {
        bolus_spec_t sp = { 300u, BOLUS_DUAL, 2.0f, 60u, 50u };
        uint32_t pulses, a = 0, b = 0; bool final;
        bolus_scheduler_init();
        CHECK(bolus_scheduler_start(&sp), "dual start");
        /* 第一次 poll：bolus 部分 295 */
        CHECK(bolus_scheduler_poll(&pulses, &final) && pulses == 295u, "dual bolus part 295");
        a = pulses;
        /* 之后推进扩展部分，累计到总完成 */
        while (bolus_scheduler_busy()) {
            if (bolus_scheduler_poll(&pulses, &final)) b += pulses;
            else break;
        }
        CHECK(a == 295u && b == 295u, "dual bolus 295 + ext 295");
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
    test_basal_scheduler();
    test_bolus_scheduler();
    if (g_fail == 0) {
        printf("\nALL TESTS PASSED\n");
        return 0;
    }
    printf("\n%d TEST(S) FAILED\n", g_fail);
    return 1;
}
