/**
 * log_manager.h — 工作状态日志（SDK 无关）
 *
 * FW-D4 / FW-SRS §2.7 FW-LOG
 * 环形缓冲保存工作状态（时间戳+事件码+参数），FlashStore 持久化，支持 BLE 导出。
 * 对应《数据库详表》§3.5 LOG_ENTRY_n。
 */
#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 单条日志：timestamp(4) + event_code(2) + value(2) = 8B */
typedef struct {
    uint32_t timestamp;   /* UNIX 秒   */
    uint16_t event_code;  /* 事件码    */
    uint16_t value;       /* 事件参数  */
} log_entry_t;

/* 日志容量（按数据库详表 ≤800 条） */
#ifndef LOG_CAPACITY
#define LOG_CAPACITY 256
#endif

void log_manager_init(void);

/* 追加一条日志；满则覆盖最旧 */
void log_manager_append(uint32_t ts, uint16_t event_code, uint16_t value);
void log_manager_append_entry(const log_entry_t *e);

/* 当前已存条数 */
uint16_t log_manager_count(void);

/* 导出全部（按时间正序）：pkg_count(2) + n*(ts,code,value)。返回总字节，0=空 */
uint16_t log_manager_export(uint8_t *buf, uint16_t max);

#ifdef __cplusplus
}
#endif

#endif /* LOG_MANAGER_H */
