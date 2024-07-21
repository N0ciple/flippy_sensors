#include "circular_buffer.h"
#include <stdlib.h>
#include <string.h>

void circular_buffer_init(CircularBuffer* cbuf, size_t size) {
    cbuf->buffer = (Measurement*)malloc(size * sizeof(Measurement));
    cbuf->max_size = size;
    circular_buffer_reset(cbuf);
}

void circular_buffer_free(CircularBuffer* cbuf) {
    free(cbuf->buffer);
}

void circular_buffer_reset(CircularBuffer* cbuf) {
    cbuf->head = 0;
    cbuf->tail = 0;
    cbuf->full = false;
    memset(cbuf->buffer, 0, cbuf->max_size * sizeof(Measurement));
}

void circular_buffer_put(CircularBuffer* cbuf, const Measurement* data) {
    cbuf->buffer[cbuf->head] = *data;
    if(cbuf->full) {
        cbuf->tail = (cbuf->tail + 1) % cbuf->max_size;
    }
    cbuf->head = (cbuf->head + 1) % cbuf->max_size;
    cbuf->full = (cbuf->head == cbuf->tail);
}

bool circular_buffer_get(CircularBuffer* cbuf, Measurement* data) {
    if(circular_buffer_empty(cbuf)) {
        return false;
    }
    *data = cbuf->buffer[cbuf->tail];
    cbuf->tail = (cbuf->tail + 1) % cbuf->max_size;
    cbuf->full = false;
    return true;
}

size_t circular_buffer_size(CircularBuffer* cbuf) {
    size_t size = cbuf->max_size;
    if(!cbuf->full) {
        if(cbuf->head >= cbuf->tail) {
            size = cbuf->head - cbuf->tail;
        } else {
            size = cbuf->max_size + cbuf->head - cbuf->tail;
        }
    }
    return size;
}

bool circular_buffer_empty(CircularBuffer* cbuf) {
    return (!cbuf->full && (cbuf->head == cbuf->tail));
}

bool circular_buffer_full(CircularBuffer* cbuf) {
    return cbuf->full;
}
