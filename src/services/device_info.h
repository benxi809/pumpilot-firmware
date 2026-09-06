/**
 * device_info.h — 出厂信息管理（SDK 无关）
 *
 * FW-D4 / FW-SRS §2.6 FW-INFO
 * 存储并上报：泵型号、唯一识别号、生产批号、有效期、组装厂编号、
 * 泵结构信息（机械模块/PCB/MCU）、固件版本。
 *
 * 出厂时一次性设定（生产烧录），固件只读。
 * 对应《数据库详表》§2 出厂信息区（FI_*）。
 *
 * 工厂结构 factory_info_t 由 hal/hal_flash.h 统一定义（只读烧录区布局）。
 */
#ifndef DEVICE_INFO_H
#define DEVICE_INFO_H

#include <stdint.h>
#include "hal_flash.h"   /* factory_info_t */

#ifdef __cplusplus
extern "C" {
#endif

void   device_info_init(const factory_info_t *fi);
const factory_info_t *device_info_get(void);

/* 上报负载：将出厂信息打包为 IEEE_RPT_FACTORY(0x84) payload；返回长度 */
uint16_t device_info_export(uint8_t *buf, uint16_t max);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_INFO_H */
