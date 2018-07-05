/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 Ayke van Laethem
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "py/mpconfig.h"
#include "py/misc.h"
#include "py/mpstate.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

enum { BUF_SIZE, BUF_NEXT, BUF_PREV, BUF_HEADER };

void * malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    // An allocation is a buffer with 3 header fields: the size of the
    // allocation, and the prev and next pointers for a linked list.
    // It is stored in root pointers to avoid garbage collection.
    void **buf = m_malloc(size + sizeof(void*) * BUF_HEADER);
    // chain this buffer in the list of allocated buffers
    buf[BUF_SIZE] = (void*)size;
    buf[BUF_NEXT] = MP_STATE_PORT(malloc_list);
    MP_STATE_PORT(malloc_list)[BUF_PREV] = buf;
    MP_STATE_PORT(malloc_list) = buf; // move list
    return &buf[3];
}

void free(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    void **buf = (void**)ptr - BUF_HEADER;
    if (buf == MP_STATE_PORT(malloc_list)) {
        MP_STATE_PORT(malloc_list) = buf[BUF_NEXT];
    } else {
        printf("TODO: free %p\n", ptr);
    }
}

void * realloc(void *oldptr, size_t newsize) {
    if (oldptr == NULL) {
        return malloc(newsize);
    }
    if (newsize == 0) {
        free(oldptr);
        return NULL;
    }
    void **buf = (void**)oldptr - BUF_HEADER;
    size_t oldsize = (size_t)buf[BUF_SIZE];
    if (newsize < oldsize) {
        return oldptr; // nothing to do
    }
    void *newptr = malloc(newsize);
    memcpy(newptr, oldptr, oldsize);
    free(oldptr);
    return newptr;
}

static uint32_t rand_seed = 0x4c2f21ff; // generated with random.org

int rand(void) {
    // rand() that repeats after 2**32 - 1.
    // Copied from: https://en.wikipedia.org/wiki/Xorshift
    // This is the credit in the source:
    //   Algorithm "xor" from p. 4 of Marsaglia, "Xorshift RNGs"
    rand_seed ^= rand_seed << 13;
    rand_seed ^= rand_seed >> 17;
    rand_seed ^= rand_seed << 5;
    return rand_seed;
}

void srand(unsigned int n) {
    rand_seed = n;
}
