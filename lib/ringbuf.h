/**
 * @file ringbuf.h
 * @brief 定长字节环形缓冲。用于事件队列、串口/命令流接收缓冲等。
 * 线程/中断安全：单生产者单消费者场景无需锁（head/tail 各自维护）。
 */
#ifndef PUMPILOT_RINGBUF_H
#define PUMPILOT_RINGBUF_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *buf;      /* 存储区 */
    uint16_t capacity; /* 容量（必须是 2 的幂以支持掩码） */
    uint16_t head;     /* 写指针 */
    uint16_t tail;     /* 读指针 */
} ringbuf_t;

/** 初始化，capacity 必须为 2 的幂 */
void ringbuf_init(ringbuf_t *rb, uint8_t *buf, uint16_t capacity);

/** 可写字节数 */
uint16_t ringbuf_writable(const ringbuf_t *rb);

/** 可读字节数 */
uint16_t ringbuf_readable(const ringbuf_t *rb);

/** 写入单字节；成功返回 1，满返回 0 */
int ringbuf_push(ringbuf_t *rb, uint8_t byte);

/** 读取单字节；成功返回 1，空返回 0 */
int ringbuf_pop(ringbuf_t *rb, uint8_t *byte);

/** 读取但不移出；成功返回 1 */
int ringbuf_peek(const ringbuf_t *rb, uint16_t offset, uint8_t *byte);

/** 丢弃 n 字节（用于已消费部分） */
void ringbuf_discard(ringbuf_t *rb, uint16_t n);

/** 是否为空 */
int ringbuf_empty(const ringbuf_t *rb);

/** 写满一批数据；返回实际写入字节数 */
uint16_t ringbuf_write(ringbuf_t *rb, const uint8_t *data, uint16_t len);

/** 读出一批数据（同时消费）；返回实际读出字节数 */
uint16_t ringbuf_read(ringbuf_t *rb, uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* PUMPILOT_RINGBUF_H */
