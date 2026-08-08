/**
 * @file ringbuf.c
 * @brief 定长字节环形缓冲实现。
 */
#include "ringbuf.h"

void ringbuf_init(ringbuf_t *rb, uint8_t *buf, uint16_t capacity)
{
    rb->buf      = buf;
    rb->capacity = capacity;
    rb->head     = 0;
    rb->tail     = 0;
}

uint16_t ringbuf_writable(const ringbuf_t *rb)
{
    /* capacity 为 2 的幂，用位与简化取模 */
    return (uint16_t)(rb->capacity - 1u - ((rb->head - rb->tail) & (rb->capacity - 1u)));
}

uint16_t ringbuf_readable(const ringbuf_t *rb)
{
    return (uint16_t)((rb->head - rb->tail) & (rb->capacity - 1u));
}

int ringbuf_push(ringbuf_t *rb, uint8_t byte)
{
    if (ringbuf_writable(rb) == 0u) {
        return 0;
    }
    rb->buf[rb->head] = byte;
    rb->head = (uint16_t)((rb->head + 1u) & (rb->capacity - 1u));
    return 1;
}

int ringbuf_pop(ringbuf_t *rb, uint8_t *byte)
{
    if (ringbuf_readable(rb) == 0u) {
        return 0;
    }
    *byte = rb->buf[rb->tail];
    rb->tail = (uint16_t)((rb->tail + 1u) & (rb->capacity - 1u));
    return 1;
}

int ringbuf_peek(const ringbuf_t *rb, uint16_t offset, uint8_t *byte)
{
    if (ringbuf_readable(rb) <= offset) {
        return 0;
    }
    *byte = rb->buf[(rb->tail + offset) & (rb->capacity - 1u)];
    return 1;
}

void ringbuf_discard(ringbuf_t *rb, uint16_t n)
{
    uint16_t avail = ringbuf_readable(rb);
    if (n > avail) {
        n = avail;
    }
    rb->tail = (uint16_t)((rb->tail + n) & (rb->capacity - 1u));
}

int ringbuf_empty(const ringbuf_t *rb)
{
    return rb->head == rb->tail;
}

uint16_t ringbuf_write(ringbuf_t *rb, const uint8_t *data, uint16_t len)
{
    uint16_t w = ringbuf_writable(rb);
    uint16_t i;
    if (len > w) {
        len = w;
    }
    for (i = 0; i < len; ++i) {
        rb->buf[(rb->head + i) & (rb->capacity - 1u)] = data[i];
    }
    rb->head = (uint16_t)((rb->head + len) & (rb->capacity - 1u));
    return len;
}

uint16_t ringbuf_read(ringbuf_t *rb, uint8_t *data, uint16_t len)
{
    uint16_t r = ringbuf_readable(rb);
    uint16_t i;
    if (len > r) {
        len = r;
    }
    for (i = 0; i < len; ++i) {
        data[i] = rb->buf[(rb->tail + i) & (rb->capacity - 1u)];
    }
    rb->tail = (uint16_t)((rb->tail + len) & (rb->capacity - 1u));
    return len;
}
