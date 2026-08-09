# Pumpilot 固件 ARM/SoftDevice 构建 (nRF52832 + S132)
#
# 用法:
#   make            - 默认构建 app 固件 .hex
#   make app_hex    - 构建 app_hex（含 merge softdevice 可选）
#   make clean
#
# 工具链: xPack GCC 12.2.1 (见 SDK Makefile.posix)
# SDK   : /opt/data/firmware/nRF5_SDK_17.1.0_ddde560

PROJECT_NAME     := pumpilot_firmware
TARGETS          := nrf52832_xxaa
OUTPUT_DIRECTORY := build_arm

# 工具链 / SDK 根（可在环境变量覆盖）
GNU_INSTALL_ROOT ?= /opt/data/firmware/xpack-arm-none-eabi-gcc-12.2.1-1.1/bin/
GNU_PREFIX       ?= arm-none-eabi
GNU_VERSION      ?= 12.2.1
SDK_ROOT         ?= /opt/data/firmware/nRF5_SDK_17.1.0_ddde560

# 项目源目录
PROJ_ROOT := ..
PROJ_SRC  := $(PROJ_ROOT)/src
PROJ_LIB  := $(PROJ_ROOT)/lib
CONFIG    := ../config

$(OUTPUT_DIRECTORY)/$(TARGETS).out: \
  LINKER_SCRIPT  := pumpilot_gcc_nrf52.ld

# ---------------- SDK 源文件 ----------------
SRC_FILES += \
  $(SDK_ROOT)/modules/nrfx/mdk/gcc_startup_nrf52.S \
  $(SDK_ROOT)/modules/nrfx/mdk/system_nrf52.c \
  $(SDK_ROOT)/components/softdevice/common/nrf_sdh.c \
  $(SDK_ROOT)/components/softdevice/common/nrf_sdh_ble.c \
  $(SDK_ROOT)/components/softdevice/common/nrf_sdh_soc.c \
  $(SDK_ROOT)/components/libraries/util/app_error.c \
  $(SDK_ROOT)/components/libraries/util/app_error_handler_gcc.c \
  $(SDK_ROOT)/components/libraries/util/app_error_weak.c \
  $(SDK_ROOT)/components/libraries/util/app_util_platform.c \
  $(SDK_ROOT)/components/libraries/util/nrf_assert.c \
  $(SDK_ROOT)/components/libraries/atomic_fifo/nrf_atfifo.c \
  $(SDK_ROOT)/components/libraries/atomic_flags/nrf_atflags.c \
  $(SDK_ROOT)/components/libraries/atomic/nrf_atomic.c \
  $(SDK_ROOT)/components/libraries/balloc/nrf_balloc.c \
  $(SDK_ROOT)/components/libraries/memobj/nrf_memobj.c \
  $(SDK_ROOT)/components/libraries/ringbuf/nrf_ringbuf.c \
  $(SDK_ROOT)/components/libraries/sortlist/nrf_sortlist.c \
  $(SDK_ROOT)/components/libraries/experimental_section_vars/nrf_section_iter.c \
  $(SDK_ROOT)/components/libraries/strerror/nrf_strerror.c \
  $(SDK_ROOT)/components/libraries/timer/app_timer2.c \
  $(SDK_ROOT)/components/libraries/timer/drv_rtc.c \
  $(SDK_ROOT)/components/libraries/pwr_mgmt/nrf_pwr_mgmt.c \
  $(SDK_ROOT)/components/libraries/hardfault/hardfault_implementation.c \
  $(SDK_ROOT)/components/libraries/log/src/nrf_log_backend_rtt.c \
  $(SDK_ROOT)/components/libraries/log/src/nrf_log_frontend.c \
  $(SDK_ROOT)/components/libraries/log/src/nrf_log_str_formatter.c \
  $(SDK_ROOT)/external/fprintf/nrf_fprintf.c \
  $(SDK_ROOT)/external/fprintf/nrf_fprintf_format.c \
  $(SDK_ROOT)/external/segger_rtt/SEGGER_RTT.c \
  $(SDK_ROOT)/external/segger_rtt/SEGGER_RTT_printf.c \
  $(SDK_ROOT)/external/segger_rtt/SEGGER_RTT_Syscalls_GCC.c \
  $(SDK_ROOT)/integration/nrfx/legacy/nrf_drv_clock.c \
  $(SDK_ROOT)/modules/nrfx/drivers/src/nrfx_clock.c \
  $(SDK_ROOT)/modules/nrfx/hal/nrf_nvmc.c \
  $(SDK_ROOT)/components/ble/common/ble_advdata.c \
  $(SDK_ROOT)/components/ble/common/ble_srv_common.c \
  $(SDK_ROOT)/components/ble/common/ble_conn_state.c \
  $(SDK_ROOT)/components/ble/nrf_ble_gatt/nrf_ble_gatt.c \

# ---------------- 项目固件源文件 ----------------
# 注意: 不编译 src/main.c (host 入口)，改用 src/sdk/main_sdk.c
SRC_FILES += \
  $(PROJ_LIB)/crc16.c \
  $(PROJ_LIB)/pulse_calc.c \
  $(PROJ_LIB)/ringbuf.c \
  $(PROJ_SRC)/app/pump_app.c \
  $(PROJ_SRC)/app/pump_state.c \
  $(PROJ_SRC)/drivers/adc_battery.c \
  $(PROJ_SRC)/drivers/beeper.c \
  $(PROJ_SRC)/drivers/grid_scan.c \
  $(PROJ_SRC)/drivers/hall_sensor.c \
  $(PROJ_SRC)/drivers/pwm_motor.c \
  $(PROJ_SRC)/services/alarm_app.c \
  $(PROJ_SRC)/services/alarm_engine.c \
  $(PROJ_SRC)/services/basal_scheduler.c \
  $(PROJ_SRC)/services/bolus_scheduler.c \
  $(PROJ_SRC)/services/device_info.c \
  $(PROJ_SRC)/services/ieee11073.c \
  $(PROJ_SRC)/services/log_manager.c \
  $(PROJ_SRC)/services/power_mgr.c \
  $(PROJ_SRC)/sdk/ble_service_sdk.c \
  $(PROJ_SRC)/sdk/hal_impl_nrf5.c \
  $(PROJ_SRC)/sdk/main_sdk.c \

# ---------------- Include 路径 ----------------
INC_FOLDERS += \
  $(SDK_ROOT)/components \
  $(SDK_ROOT)/components/softdevice/s132/headers \
  $(SDK_ROOT)/components/softdevice/s132/headers/nrf52 \
  $(SDK_ROOT)/components/softdevice/common \
  $(SDK_ROOT)/components/ble/common \
  $(SDK_ROOT)/components/ble/ble_advertising \
  $(SDK_ROOT)/components/ble/nrf_ble_gatt \
  $(SDK_ROOT)/components/libraries/util \
  $(SDK_ROOT)/components/libraries/atomic \
  $(SDK_ROOT)/components/libraries/atomic_fifo \
  $(SDK_ROOT)/components/libraries/atomic_flags \
  $(SDK_ROOT)/components/libraries/balloc \
  $(SDK_ROOT)/components/libraries/memobj \
  $(SDK_ROOT)/components/libraries/ringbuf \
  $(SDK_ROOT)/components/libraries/sortlist \
  $(SDK_ROOT)/components/libraries/section_vars \
  $(SDK_ROOT)/components/libraries/strerror \
  $(SDK_ROOT)/components/libraries/timer \
  $(SDK_ROOT)/components/libraries/delay \
  $(SDK_ROOT)/components/libraries/experimental_section_vars \
  $(SDK_ROOT)/components/libraries/pwr_mgmt \
  $(SDK_ROOT)/components/libraries/mutex \
  $(SDK_ROOT)/components/libraries/hardfault \
  $(SDK_ROOT)/components/libraries/log \
  $(SDK_ROOT)/components/libraries/log/src \
  $(SDK_ROOT)/components/libraries/memobj \
  $(SDK_ROOT)/components/libraries/experimental_task_manager \
  $(SDK_ROOT)/components/boards \
  $(SDK_ROOT)/integration/nrfx \
  $(SDK_ROOT)/integration/nrfx/legacy \
  $(SDK_ROOT)/modules/nrfx \
  $(SDK_ROOT)/modules/nrfx/mdk \
  $(SDK_ROOT)/modules/nrfx/hal \
  $(SDK_ROOT)/modules/nrfx/drivers/include \
  $(SDK_ROOT)/external/fprintf \
  $(SDK_ROOT)/external/segger_rtt \
  $(SDK_ROOT)/components/toolchain/cmsis/include \
  $(SDK_ROOT)/modules/nrfx \
  $(CONFIG) \
  $(PROJ_SRC) \
  $(PROJ_SRC)/app \
  $(PROJ_SRC)/services \
  $(PROJ_SRC)/drivers \
  $(PROJ_SRC)/hal \
  $(PROJ_SRC)/config \
  $(PROJ_LIB) \
  $(PROJ_SRC)/sdk \

# ---------------- 编译选项 ----------------
CFLAGS += -O1
CFLAGS += -DBOARD_CUSTOM
CFLAGS += -DAPP_TIMER_V2
CFLAGS += -DAPP_TIMER_V2_RTC1_ENABLED
CFLAGS += -DCONFIG_GPIO_AS_PINRESET
CFLAGS += -DFLOAT_ABI_HARD
CFLAGS += -DNRF52
CFLAGS += -DNRF52832_XXAA
CFLAGS += -DNRF_SD_BLE_API_VERSION=7
CFLAGS += -DS132
CFLAGS += -DSOFTDEVICE_PRESENT
CFLAGS += -DPUMPILOT_SDK_BUILD=1
CFLAGS += -mcpu=cortex-m4
CFLAGS += -mthumb -mabi=aapcs
CFLAGS += -Wall -Werror -Wno-error=array-bounds -Wno-error=stringop-overflow
CFLAGS += -mfloat-abi=hard -mfpu=fpv4-sp-d16
CFLAGS += -ffunction-sections -fdata-sections -fno-strict-aliasing
CFLAGS += -fno-builtin -fshort-enums
CFLAGS += -D__HEAP_SIZE=8192
CFLAGS += -D__STACK_SIZE=8192

LDFLAGS += -mthumb -mabi=aapcs -L$(SDK_ROOT)/modules/nrfx/mdk -T$(LINKER_SCRIPT)
LDFLAGS += -mcpu=cortex-m4
LDFLAGS += -mfloat-abi=hard -mfpu=fpv4-sp-d16
LDFLAGS += -Wl,--gc-sections
LDFLAGS += --specs=nano.specs

# ---------------- 工具链 ---------------- 
CC      = $(GNU_INSTALL_ROOT)$(GNU_PREFIX)-gcc
AS      = $(GNU_INSTALL_ROOT)$(GNU_PREFIX)-as
AR      = $(GNU_INSTALL_ROOT)$(GNU_PREFIX)-ar
LD      = $(GNU_INSTALL_ROOT)$(GNU_PREFIX)-ld
NM      = $(GNU_INSTALL_ROOT)$(GNU_PREFIX)-nm
OBJCOPY = $(GNU_INSTALL_ROOT)$(GNU_PREFIX)-objcopy
OBJDUMP = $(GNU_INSTALL_ROOT)$(GNU_PREFIX)-objdump

# ---------------- 编译规则 ----------------
INC_PATHS = $(addprefix -I, $(INC_FOLDERS))

OBJECT_DIRECTORY = $(OUTPUT_DIRECTORY)/$(TARGETS)
LIST = $(OBJECT_DIRECTORY)/listing

OBJECT_DIRECTORY_TARGET = $(OBJECT_DIRECTORY)

# 收集对象文件
OBJECT_DIRECTORIES := $(sort $(dir $(addprefix $(OBJECT_DIRECTORY)/, $(SRC_FILES))))
OBJECT_FILES := $(addprefix $(OBJECT_DIRECTORY)/, $(addsuffix .o, $(basename $(notdir $(SRC_FILES)))))

vpath %.c $(sort $(dir $(SRC_FILES)))
vpath %.S $(sort $(dir $(SRC_FILES)))

all: clean_objects app_hex

clean_objects:
	@rm -rf $(OBJECT_DIRECTORIES)

.PHONY: clean app_hex all

clean:
	rm -rf $(OBJECT_DIRECTORY) $(OUTPUT_DIRECTORY)

# 主目标: 生成可烧录 app hex
app_hex: $(OUTPUT_DIRECTORY)/$(TARGETS).hex

$(OUTPUT_DIRECTORY)/$(TARGETS).out: $(OBJECT_FILES) $(LINKER_SCRIPT)
	@mkdir -p $(OBJECT_DIRECTORY)
	@echo "Linking $@"
	$(CC) $(LDFLAGS) -Wl,-Map=$(@:.out=.map) -o $@ $(filter %.o, $^) \
	    -lgcc -lc -lnosys

$(OBJECT_DIRECTORY)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "Compiling $<"
	$(CC) $(CFLAGS) $(INC_PATHS) -c -o $@ $<

$(OBJECT_DIRECTORY)/%.o: %.S
	@mkdir -p $(dir $@)
	@echo "Assembling $<"
	$(CC) $(CFLAGS) $(INC_PATHS) -c -o $@ $<

$(OUTPUT_DIRECTORY)/$(TARGETS).hex: $(OUTPUT_DIRECTORY)/$(TARGETS).out
	@echo "Creating hex"
	$(OBJCOPY) -O ihex $< $@
	@echo "=== 输入 .hex: $@ 大小:"
	@wc -c $@

# 合并 softdevice + app（可选，产物含 S132 的单一烧录镜像）
app_merged.hex: $(OUTPUT_DIRECTORY)/$(TARGETS).hex
	@echo "Merge SoftDevice + App -> pumpilot_merged.hex"
	$(SDK_ROOT)/components/toolchain/mergehex/mergehex \
	    --merge \
	    $(SDK_ROOT)/components/softdevice/s132/hex/s132_nrf52_7.2.0_softdevice.hex \
	    $(OUTPUT_DIRECTORY)/$(TARGETS).hex --output pumpilot_merged.hex
	@wc -c pumpilot_merged.hex
