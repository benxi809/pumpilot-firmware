/**
 * @file hal_flash.h
 * @brief Flash 掉电持久化 HAL（FDS 抽象）与出厂信息结构。
 *
 * 依据《固件数据库详表 v1.0》（闪存布局）：
 * - 出厂信息区 0x70000（4KB，只读，烧录时写入，整区 CRC 自检）
 * - 可写数据区 0x71000（~60KB，FDS 键值：曲线/状态/时钟/日志，掉电保护）
 * SDK 接入层用 nRF FDS / NVMC 实现具体读写。
 */
#ifndef PUMPILOT_HAL_FLASH_H
#define PUMPILOT_HAL_FLASH_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============ 分区地址（最终以链接脚本为准，此处仅文档基线） ============ */
#define FLASH_FACTORY_BASE      0x70000u
#define FLASH_DATA_BASE         0x71000u

/* ================= 出厂信息结构（只读，烧录时写入） ================= */
#define FI_MODEL_LEN        4u
#define FI_SERIAL_LEN       10u
#define FI_BT_PWD_LEN       8u
#define FI_PROD_BATCH_LEN   10u
#define FI_EXPIRY_LEN       8u
#define FI_FACTORY_LEN      2u
#define FI_MECH_MODEL_LEN   3u
#define FI_MECH_VENDOR_LEN  3u
#define FI_MECH_BATCH_LEN   8u
#define FI_PCB_MODEL_LEN    8u
#define FI_PCB_VENDOR_LEN   3u
#define FI_PCB_BATCH_LEN    8u
#define FI_MCU_MODEL_LEN    16u
#define FI_MCU_ID_LEN       16u
#define FI_FW_VERSION_LEN   16u

typedef struct {
    char model[FI_MODEL_LEN + 1];        /* 泵型号 */
    char serial[FI_SERIAL_LEN + 1];      /* 唯一识别号 2字母+8数字 */
    char bt_password[FI_BT_PWD_LEN + 1]; /* 蓝牙密码 */
    char prod_batch[FI_PROD_BATCH_LEN + 1]; /* 生产批号 */
    char expiry[FI_EXPIRY_LEN + 1];      /* 有效期 */
    char factory[FI_FACTORY_LEN + 1];    /* 组装厂编号 */
    char mech_model[FI_MECH_MODEL_LEN + 1];
    char mech_vendor[FI_MECH_VENDOR_LEN + 1];
    char mech_batch[FI_MECH_BATCH_LEN + 1];
    char pcb_model[FI_PCB_MODEL_LEN + 1];
    char pcb_vendor[FI_PCB_VENDOR_LEN + 1];
    char pcb_batch[FI_PCB_BATCH_LEN + 1];
    char mcu_model[FI_MCU_MODEL_LEN + 1]; /* nRF52832 */
    char mcu_id[FI_MCU_ID_LEN + 1];      /* FICR 读取 */
    char fw_version[FI_FW_VERSION_LEN + 1]; /* e.g. "1.0.0" */
    uint16_t crc;                        /* 整区校验 */
} factory_info_t;

/** 出厂信息区大小（含 CRC），4KB 页内 */
#define FACTORY_INFO_SIZE   256u

/* ================= 可写数据区记录密钥（FDS key） ================= */
#define FLASH_KEY_BASAL_PROFILE  0x0001u  /* 基础率曲线 48 段 */
#define FLASH_KEY_THER_ICR       0x0002u  /* 碳水化合物系数 */
#define FLASH_KEY_THER_ISF       0x0003u  /* 胰岛素敏感系数 */
#define FLASH_KEY_CUR_STATE      0x0004u  /* 当前状态机状态/药量 */
#define FLASH_KEY_RTC_LAST       0x0005u  /* 最后校准时间戳 */
#define FLASH_KEY_LOG_BASE       0x0100u  /* 日志条目基址（+n） */

/* ================= HAL Flash 接口 ================= */

/** 初始化 Flash（加载出厂信息 + 挂载可写区） */
void hal_flash_init(void);

/** 读取出厂信息；返回 false=CRC 校验失败/信息缺失 */
bool hal_flash_read_factory(factory_info_t *out);

/** 写入出厂信息（生产烧录用，附 CRC） */
void hal_flash_write_factory(const factory_info_t *fi);

/**
 * @brief 读取一个键值记录。
 * @param key 记录密钥
 * @param out 输出缓冲
 * @param len 读取长度
 * @return true=命中
 */
bool hal_flash_read(uint16_t key, uint8_t *out, uint16_t len);

/**
 * @brief 写入/更新一个键值记录（掉电保护）。
 * @param key 记录密钥
 * @param data 数据
 * @param len  长度
 * @return true=成功
 */
bool hal_flash_write(uint16_t key, const uint8_t *data, uint16_t len);

/** 删除一个记录 */
void hal_flash_delete(uint16_t key);

/** 数据区是否已就绪可写 */
bool hal_flash_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* PUMPILOT_HAL_FLASH_H */
