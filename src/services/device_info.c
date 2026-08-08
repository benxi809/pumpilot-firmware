/**
 * device_info.c — 出厂信息实现（SDK 无关）
 */
#include "device_info.h"

/* 出厂信息区默认值（生产烧录时覆盖）。字段与 hal_flash.h factory_info_t 对齐。 */
static factory_info_t s_fi = {
    .model       = "PLT1",
    .serial      = "PL00000001",
    .bt_password = "123456",
    .prod_batch  = "20260801AA",
    .expiry      = "20290801",
    .factory     = "CT",
    .mech_model  = "M1X",
    .mech_vendor = "V01",
    .mech_batch  = "MB000001",
    .pcb_model   = "PCB0001",
    .pcb_vendor  = "P01",
    .pcb_batch   = "PB000001",
    .mcu_model   = "nRF52832",
    .mcu_id      = "FICR-00000001",
    .fw_version  = "1.0.0",
    .crc         = 0,
};

void device_info_init(const factory_info_t *fi)
{
    if (fi)
        s_fi = *fi;
}

const factory_info_t *device_info_get(void)
{
    return &s_fi;
}

/*
 * RPT_FACTORY(0x86) payload 打包。
 * 顺序固定（与接口契约 §4.3 一致）：
 *   model(5) serial(11) prod_batch(11) expiry(9) factory(3)
 *   mech_model(4) mech_vendor(4) mech_batch(9)
 *   pcb_model(9) pcb_vendor(4) pcb_batch(9)
 *   mcu_model(17) mcu_id(17) fw_version(17)
 */
uint16_t device_info_export(uint8_t *buf, uint16_t max)
{
    if (!buf || max < 128)
        return 0;

    uint16_t o = 0;
#define PUT(field, size) do { for (unsigned i = 0; i < (size); i++) buf[o++] = (uint8_t)s_fi.field[i]; } while (0)

    PUT(model,      FI_MODEL_LEN + 1);
    PUT(serial,     FI_SERIAL_LEN + 1);
    PUT(prod_batch, FI_PROD_BATCH_LEN + 1);
    PUT(expiry,     FI_EXPIRY_LEN + 1);
    PUT(factory,    FI_FACTORY_LEN + 1);
    PUT(mech_model, FI_MECH_MODEL_LEN + 1);
    PUT(mech_vendor,FI_MECH_VENDOR_LEN + 1);
    PUT(mech_batch, FI_MECH_BATCH_LEN + 1);
    PUT(pcb_model,  FI_PCB_MODEL_LEN + 1);
    PUT(pcb_vendor, FI_PCB_VENDOR_LEN + 1);
    PUT(pcb_batch,  FI_PCB_BATCH_LEN + 1);
    PUT(mcu_model,  FI_MCU_MODEL_LEN + 1);
    PUT(mcu_id,     FI_MCU_ID_LEN + 1);
    PUT(fw_version, FI_FW_VERSION_LEN + 1);
#undef PUT

    return o;
}
