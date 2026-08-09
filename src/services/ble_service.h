/**
 * @file ble_service.h
 * @brief BLE GATT 服务抽象接口（SDK 无关）。
 *
 * 依据《固件完整API与BLE通讯协议文档》§4、§7：
 * - Service `0000180F`（Drug Delivery / IEEE 11073）
 * - Char `00002A20`（IEEE 11073 Data：write / notify）
 * - Char `00002A21`（IEEE 11073 Status：read）
 * - Config-ID 0x076C / data-proto-id 0x5069（IEEE 11073-10419 / PHD 优化交换）
 *
 * 本层仅定义契约，SoftDevice(GATT)/nRF5 SDK 接入时在 sdk_impl 提供真实实现；
 * host 测试用 mock 验证逻辑。应用层只经 notify/on_write 交互。
 */
#ifndef PUMPILOT_BLE_SERVICE_H
#define PUMPILOT_BLE_SERVICE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============ IEEE 11073 GATT UUID（128-bit） ============ */
#define BLE_UUID_SVC_11073    0x180F   /* Drug Delivery / IEEE 11073 */
#define BLE_UUID_DATA_11073   0x2A20   /* write + notify */
#define BLE_UUID_STATUS_11073 0x2A21   /* read */

/* Config-ID / data-proto-id（IEEE 11073 协商） */
#define IEEE11073_CONFIG_ID       0x076Cu /* 1900, 11073-10419 */
#define IEEE11073_DATA_PROTO_ID   0x5069u /* PHD Optimized Exchange */

/** 2A20 写入数据到达回调（BLE 栈线程） */
typedef void (*ble_write_cb_t)(const uint8_t *data, uint16_t len);

/** 连接状态回调 */
typedef void (*ble_conn_cb_t)(bool connected);

/**
 * @brief 初始化并注册 GATT 服务（180F/2A20/2A21）并启动广播。
 * @param on_write 2A20 写入回调
 * @param on_conn  连接/断开回调
 */
void ble_service_init(ble_write_cb_t on_write, ble_conn_cb_t on_conn);

/**
 * @brief 经 2A20 向对端 notify 一条数据（IEEE11073 数据字节流）。
 * @param data 数据
 * @param len  长度
 * @return true=已入队通知
 */
bool ble_service_notify(const uint8_t *data, uint16_t len);

/** 读取 2A21 状态值（对端读时返回当前 IEEE11073 状态） */
uint16_t ble_service_read_status(void);
void     ble_service_set_status(uint16_t status);

/** 是否已连接 */
bool ble_service_is_connected(void);

/**
 * @brief 绑定判断接口：是否已与某 APP 绑定过。
 * 用于决定广播模式（首连广播 vs 绑定后定向广播）。
 */
bool ble_service_is_bound(void);
void ble_service_set_bound(bool bound);

/** 设置广播模式：true=定向(绑定后), false=普通 */
void ble_service_set_directed(bool directed);

/**
 * @brief 设置定向广播目标（绑定 peer 地址，6字节）。
 * @param addr peer BLE 地址；NULL 清除。
 */
void ble_service_set_target(const uint8_t addr[6]);

/** 读取定向广播目标（内部指针） */
const uint8_t *ble_service_get_target(void);

/** 读取定向广播使能标志 */
bool ble_service_directed_enabled(void);

/** 2A20 Data 特征句柄（SoftDevice 构建用） */
uint16_t ble_service_data_handle(void);

/**
 * @brief 经 2A20 写入回调分发（BLE 栈调用；host 测试可直接调用验证）。
 */
void ble_service_dispatch_write(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* PUMPILOT_BLE_SERVICE_H */
