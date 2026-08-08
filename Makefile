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

TEST_SRCS := \
    tests/test_pulse_calc.c \
    tests/mock_hal.c \
    lib/crc16.c \
    lib/ringbuf.c \
    lib/pulse_calc.c \
    src/app/pump_state.c \
    src/services/basal_scheduler.c \
    src/services/bolus_scheduler.c \
    src/services/ieee11073.c \
    src/drivers/pwm_motor.c \
    src/drivers/hall_sensor.c \
    src/drivers/grid_scan.c \
    src/drivers/adc_battery.c \
    src/drivers/beeper.c

TEST_BIN := build/fw_tests

.PHONY: all test clean arm

all: test

test: $(TEST_BIN)
	@echo "--- running ---"
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRCS)
	@mkdir -p build
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

# 未来 ARM/SoftDevice 构建（FW-D3 时补充具体示例 makefile）
arm:
	@echo "ARM target not yet wired (FW-D3 fills nRF5 SDK path)"

clean:
	rm -rf build
