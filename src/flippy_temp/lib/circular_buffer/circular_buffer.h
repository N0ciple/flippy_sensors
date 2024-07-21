#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "../../measurement.h"

typedef struct {
    Measurement* buffer;
    size_t head;
    size_t tail;
    size_t max_size;
    bool full;
} CircularBuffer;

void circular_buffer_init(CircularBuffer* cbuf, size_t size);
void circular_buffer_free(CircularBuffer* cbuf);
void circular_buffer_put(CircularBuffer* cbuf, const Measurement* data);
bool circular_buffer_get(CircularBuffer* cbuf, Measurement* data);
size_t circular_buffer_size(CircularBuffer* cbuf);
bool circular_buffer_empty(CircularBuffer* cbuf);
bool circular_buffer_full(CircularBuffer* cbuf);
void circular_buffer_reset(CircularBuffer* cbuf);
