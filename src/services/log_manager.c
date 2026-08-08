/**
 * log_manager.c — 工作状态日志实现（SDK 无关）
 */
#include "log_manager.h"
#include <string.h>

static log_entry_t s_log[LOG_CAPACITY];
static uint16_t s_head;   /* 下一个写入位（环形） */
static uint16_t s_count;  /* 当前条数 */

void log_manager_init(void)
{
    s_head = 0;
    s_count = 0;
}

void log_manager_append_entry(const log_entry_t *e)
{
    if (!e)
        return;
    s_log[s_head] = *e;
    s_head = (uint16_t)((s_head + 1) % LOG_CAPACITY);
    if (s_count < LOG_CAPACITY)
        s_count++;
}

void log_manager_append(uint32_t ts, uint16_t event_code, uint16_t value)
{
    log_entry_t e;
    e.timestamp  = ts;
    e.event_code = event_code;
    e.value      = value;
    log_manager_append_entry(&e);
}

uint16_t log_manager_count(void)
{
    return s_count;
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}
static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

uint16_t log_manager_export(uint8_t *buf, uint16_t max)
{
    if (!buf || s_count == 0)
        return 0;

    /* pkg: count(2) + 8B*n */
    uint16_t need = (uint16_t)(2u + (uint32_t)s_count * 8u);
    if (max < need)
        return 0;

    put_u16(buf, s_count);
    uint16_t o = 2;
    /* 环形读取：从最旧（s_head - s_count 模容量）开始 */
    uint16_t start = (s_count < LOG_CAPACITY) ? 0u
                     : (uint16_t)((s_head + LOG_CAPACITY - s_count) % LOG_CAPACITY);
    for (uint16_t i = 0; i < s_count; i++) {
        const log_entry_t *e = &s_log[(start + i) % LOG_CAPACITY];
        put_u32(buf + o, e->timestamp); o += 4;
        put_u16(buf + o, e->event_code); o += 2;
        put_u16(buf + o, e->value);      o += 2;
    }
    return o;
}
