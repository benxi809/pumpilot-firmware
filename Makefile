# Pumpilot 固件 Makefile (FW-D1 骨架)
#
# 目标:
#   make test        - host gcc 编译并运行 PC 单元测试（纯逻辑/驱动 stub）
#   make clean       - 清理构建产物
#   make arm         - (占位) 未来 nRF5 SDK/SoftDevice 交叉编译入口
#
# host 测试用 GDB 友好、-Wall -Wextra。

CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -O0 -g
INCLUDES := -Isrc -Isrc/app -Isrc/services -Isrc/drivers -Isrc/hal -Isrc/config -Ilib -Itests

# 被测固件源（不含各测试 main；两个测试二进制共享）
UT_SRCS := \
    tests/mock_hal.c \
    lib/crc16.c \
    lib/ringbuf.c \
    lib/pulse_calc.c \
    src/app/pump_state.c \
    src/services/basal_scheduler.c \
    src/services/bolus_scheduler.c \
    src/services/ieee11073.c \
    src/services/alarm_engine.c \
    src/services/alarm_app.c \
    src/services/device_info.c \
    src/services/log_manager.c \
    src/services/power_mgr.c \
    src/drivers/pwm_motor.c \
    src/drivers/hall_sensor.c \
    src/drivers/grid_scan.c \
    src/drivers/adc_battery.c \
    src/drivers/beeper.c

TEST_SRCS := tests/test_pulse_calc.c $(UT_SRCS)

# IEEE11073 0x11/0x73 字节级对齐测试（独立 main，与上面同源链接）
ALIGN_SRCS := tests/test_ieee11073_align.c $(UT_SRCS)

TEST_BIN := build/fw_tests
ALIGN_BIN := build/fw_align

.PHONY: all test clean arm

all: test

test: $(TEST_BIN) $(ALIGN_BIN)
	@echo "--- running (legacy test_pulse_calc) ---"
	./$(TEST_BIN)
	@echo "--- running (ieee11073 0x11/0x73 align) ---"
	./$(ALIGN_BIN)

$(TEST_BIN): $(TEST_SRCS)
	@mkdir -p build
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

$(ALIGN_BIN): $(ALIGN_SRCS)
	@mkdir -p build
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

# 未来 ARM/SoftDevice 构建（FW-D3 时补充具体示例 makefile）
arm:
	@echo "ARM target not yet wired (FW-D3 fills nRF5 SDK path)"

clean:
	rm -rf build
