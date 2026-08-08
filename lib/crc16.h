/**
 * @file crc16.h
 * @brief CRC16-CCITT (polynomial 0x1021) 实现接口。
 * 依据《固件完整API与BLE通讯协议文档 v1.0》§8：CRC16-CCITT, poly 0x1021, 小端。
 */
#ifndef PUMPILOT_CRC16_H
#define PUMPILOT_CRC16_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** CRC16-CCITT 生成多项式（MSB-first, poly 0x1021） */
#define CRC16_CCITT_POLY  0x1021u
/** 初始值 */
#define CRC16_INIT        0xFFFFu

/**
 * @brief 计算一段数据的 CRC16-CCITT。
 * @param data 数据指针
 * @param len   字节长度
 * @return 16 位 CRC 值（小端存放时低字节在前）
 */
uint16_t crc16_ccitt(const uint8_t *data, size_t len);

/**
 * @brief 增量 CRC 计算（用于分帧接收场景）。
 * @param crc 上一次返回值（首次传 CRC16_INIT）
 * @param byte 当前字节
 * @return 更新后的 CRC
 */
uint16_t crc16_ccitt_byte(uint16_t crc, uint8_t byte);

#ifdef __cplusplus
}
#endif

#endif /* PUMPILOT_CRC16_H */
