/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Mark Shannon
 * Copyright (c) 2017 Ayke van Laethem
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

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>

#include "microbitfs.h"
#include "hal/hal_nvmc.h"
#include "py/nlr.h"
#include "py/obj.h"
#include "py/stream.h"
#include "py/runtime.h"
#include "extmod/vfs.h"
#include "mpconfigport.h"

#if MICROPY_PY_UOS_MICROBITFS

#define DEBUG_FILE 0
#if DEBUG_FILE
#define DEBUG(s) printf s
#else
#define DEBUG(s) (void)0
#endif

/**  How it works:
 * The File System consists of up to MAX_CHUNKS_IN_FILE_SYSTEM chunks of CHUNK_SIZE each,
 * plus one spare page which holds persistent configuration data and is used. for bulk erasing.
 * The spare page is either the first or the last page and will be switched by a bulk erase.
 * The exact number of chunks will depend on the amount of flash available.
 *
 * Each chunk consists of a one byte marker and a one byte tail
 * The marker shows whether this chunk is the start of a file, the midst of a file
 * (in which case it refers to the previous chunk in the file) or whether it is UNUSED
 * (and erased) or FREED (which means it is unused, but not erased).
 * Chunks are selected in a randomised round-robin fashion to even out wear on the flash
 * memory as much as possible.
 * A file consists of a linked list of chunks. The first chunk in a file contains its name
 * as well as the end chunk and offset.
 * Files are found by linear search of the chunks, this means that no meta-data needs to be stored
 * outside of the file, which prevents wear hot-spots. Since there are fewer than 250 chunks,
 * the search is fast enough.
 *
 * Chunks are numbered from 1 as we need to reserve 0 as the FREED marker.
 *
 * Writing to files relies on the persistent API which is high-level wrapper on top of the Nordic SDK.
 */


// Page indexes count down from the end of ROM.
STATIC uint8_t first_page_index;
STATIC uint8_t last_page_index;
// The number of useable chunks in the file system.
STATIC uint8_t chunks_in_file_system;
// Index of chunk to start searches. This is randomised to even out wear.
STATIC uint8_t start_index;
STATIC file_chunk *file_system_chunks;

// Defined by the linker
extern byte _flash_user_start[];
extern byte _flash_user_end[];

STATIC_ASSERT((sizeof(file_chunk) == CHUNK_SIZE));

// From micro:bit memory.h
STATIC inline byte *rounddown(byte *addr, uint32_t align) {
    return (byte*)(((uint32_t)addr)&(-align));
}

// From micro:bit memory.h
STATIC inline byte *roundup(byte *addr, uint32_t align) {
    return (byte*)((((uint32_t)addr)+align-1)&(-align));
}


STATIC inline void *first_page(void) {
    return _flash_user_end - HAL_NVMC_PAGESIZE * first_page_index;
}

STATIC inline void *last_page(void) {
    return _flash_user_end - HAL_NVMC_PAGESIZE * last_page_index;
}

STATIC void init_limits(void) {
    // First determine where to end
    byte *end = _flash_user_end;
    end = rounddown(end, HAL_NVMC_PAGESIZE)-HAL_NVMC_PAGESIZE;
    last_page_index = (_flash_user_end - end)/HAL_NVMC_PAGESIZE;

    // Now find the start
    byte *start = roundup(end - CHUNK_SIZE*MAX_CHUNKS_IN_FILE_SYSTEM, HAL_NVMC_PAGESIZE);
    while (start < _flash_user_start) {
        start += HAL_NVMC_PAGESIZE;
    }
    first_page_index = (_flash_user_end - start)/HAL_NVMC_PAGESIZE;
    chunks_in_file_system = (end-start)>>LOG_CHUNK_SIZE;
}

STATIC void randomise_start_index(void) {
    uint8_t new_index; // 0 based index.
    NRF_RNG->TASKS_START = 1;
    // Wait for valid number
    do {
        NRF_RNG->EVENTS_VALRDY = 0;
        while(NRF_RNG->EVENTS_VALRDY == 0);
        new_index = NRF_RNG->VALUE&255;
    } while (new_index >= chunks_in_file_system);
    start_index = new_index + 1;  // Adjust index to 1 based.
    NRF_RNG->TASKS_STOP = 1;
}

STATIC void microbit_filesystem_init(void) {
    init_limits();
    randomise_start_index();
    file_chunk *base = first_page();
    if (base->marker == PERSISTENT_DATA_MARKER) {
        file_system_chunks = &base[(HAL_NVMC_PAGESIZE>>LOG_CHUNK_SIZE)-1];
    } else if (((file_chunk *)last_page())->marker == PERSISTENT_DATA_MARKER) {
        file_system_chunks = &base[-1];
    } else {
        hal_nvmc_write_byte(&((file_chunk *)last_page())->marker, PERSISTENT_DATA_MARKER);
        file_system_chunks = &base[-1];
    }
}

STATIC void copy_page(void *dest, void *src) {
    DEBUG(("FILE DEBUG: Copying page from %lx to %lx.\r\n", (uint32_t)src, (uint32_t)dest));
    hal_nvmc_erase_page((uint32_t)dest);
    file_chunk *src_chunk = src;
    file_chunk *dest_chunk = dest;
    uint32_t chunks = HAL_NVMC_PAGESIZE>>LOG_CHUNK_SIZE;
    for (uint32_t i = 0; i < chunks; i++) {
        if (src_chunk[i].marker != FREED_CHUNK) {
            hal_nvmc_write_buffer(&dest_chunk[i], &src_chunk[i], CHUNK_SIZE);
        }
    }
}

// Move entire file system up or down one page, copying all used chunks
// Freed chunks are not copied, so become erased.
// There should be no erased chunks before the sweep (or it would be unnecessary)
// but if there are this should work correctly.
//
// The direction of the sweep depends on whether the persistent data is in the first or last page
// The persistent data is copied to RAM, leaving its page unused.
// Then all the pages are copied, one by one, into the adjacent newly unused page.
// Finally, the persistent data is saved back to the opposite end of the filesystem from whence it came.
//
STATIC void filesystem_sweep(void) {
    persistent_config_t config;
    uint8_t *page;
    uint8_t *end_page;
    int step;
    uint32_t page_size = HAL_NVMC_PAGESIZE;
    DEBUG(("FILE DEBUG: Sweeping file system\r\n"));
    if (((file_chunk *)first_page())->marker == PERSISTENT_DATA_MARKER) {
        config = *(persistent_config_t *)first_page();
        page = first_page();
        end_page = last_page();
        step = page_size;
    } else {
        config = *(persistent_config_t *)last_page();
        page = last_page();
        end_page = first_page();
        step = -page_size;
    }
    while (page != end_page) {
        uint8_t *next_page = page+step;
        hal_nvmc_erase_page((uint32_t)page);
        copy_page(page, next_page);
        page = next_page;
    }
    hal_nvmc_erase_page((uint32_t)end_page);
    hal_nvmc_write_buffer(end_page, &config, sizeof(config));
    microbit_filesystem_init();
}


STATIC inline byte *seek_address(file_descriptor_obj *self) {
    return (byte*)&(file_system_chunks[self->seek_chunk].data[self->seek_offset]);
}

STATIC uint8_t microbit_find_file(const char *name, int name_len) {
    for (uint8_t index = 1; index <= chunks_in_file_system; index++) {
        const file_chunk *p = &file_system_chunks[index];
        if (p->marker != FILE_START)
            continue;
        if (p->header.name_len != name_len)
            continue;
        if (memcmp(name, &p->header.filename[0], name_len) == 0) {
            DEBUG(("FILE DEBUG: File found. index %d\r\n", index));
            return index;
        }
    }
    DEBUG(("FILE DEBUG: File not found.\r\n"));
    return FILE_NOT_FOUND;
}

// Return a free, erased chunk.
// Search the chunks:
// 1  If an UNUSED chunk is found, then return that.
// 2. If an entire page of FREED chunks is found, then erase the page and return the first chunk
// 3. If the number of FREED chunks is >= MIN_FREE_CHUNKS_FOR_SWEEP, then
// 3a. Sweep the filesystem and restart.
// 3b. Fail and return FILE_NOT_FOUND
//
STATIC uint8_t find_chunk_and_erase(void) {
    // Start search at a random chunk to spread the wear more evenly.
    // Search for unused chunk
    uint8_t index = start_index;
    do {
        const file_chunk *p = &file_system_chunks[index];
        if (p->marker == UNUSED_CHUNK) {
            DEBUG(("FILE DEBUG: Unused chunk found: %d\r\n", index));
            return index;
        }
        index++;
        if (index == chunks_in_file_system+1) index = 1;
    } while (index != start_index);

    // Search for FREED page, and total up FREED chunks
    uint32_t freed_chunks = 0;
    index = start_index;
    uint32_t chunks_per_page = HAL_NVMC_PAGESIZE>>LOG_CHUNK_SIZE;
    do {
        const file_chunk *p = &file_system_chunks[index];
        if (p->marker == FREED_CHUNK) {
            freed_chunks++;
        }
        if (HAL_NVMC_IS_PAGE_ALIGNED(p)) {
            uint32_t i;
            for (i = 0; i < chunks_per_page; i++) {
                if (p[i].marker != FREED_CHUNK)
                    break;
            }
            if (i == chunks_per_page) {
                DEBUG(("FILE DEBUG: Found freed page of chunks: %d\r\n", index));
                hal_nvmc_erase_page((uint32_t)&file_system_chunks[index]);
                return index;
            }
        }
        index++;
        if (index == chunks_in_file_system+1) index = 1;
    } while (index != start_index);
    DEBUG(("FILE DEBUG: %lu free chunks\r\n", freed_chunks));
    if (freed_chunks < MIN_CHUNKS_FOR_SWEEP) {
        return FILE_NOT_FOUND;
    }
    // No freed pages, so sweep file system.
    filesystem_sweep();
    // This is guaranteed to succeed.
    return find_chunk_and_erase();
}

STATIC mp_obj_t microbit_file_name(file_descriptor_obj *fd) {
    return mp_obj_new_str(&(file_system_chunks[fd->start_chunk].header.filename[0]), file_system_chunks[fd->start_chunk].header.name_len, false);
}

STATIC file_descriptor_obj *microbit_file_descriptor_new(uint8_t start_chunk, bool write, bool binary);

STATIC void clear_file(uint8_t chunk) {
    do {
        hal_nvmc_write_byte(&(file_system_chunks[chunk].marker), FREED_CHUNK);
        DEBUG(("FILE DEBUG: Freeing chunk %d.\n", chunk));
        chunk = file_system_chunks[chunk].next_chunk;
    } while (chunk <= chunks_in_file_system);
}

STATIC file_descriptor_obj *microbit_file_open(const char *name, size_t name_len, bool write, bool binary) {
    if (name_len > MAX_FILENAME_LENGTH) {
        return NULL;
    }
    uint8_t index = microbit_find_file(name, name_len);
    if (write) {
        if (index != FILE_NOT_FOUND) {
            // Free old file
            clear_file(index);
        }
        index = find_chunk_and_erase();
        if (index == FILE_NOT_FOUND) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "No more storage space"));
        }
        hal_nvmc_write_byte(&(file_system_chunks[index].marker), FILE_START);
        hal_nvmc_write_byte(&(file_system_chunks[index].header.name_len), name_len);
        hal_nvmc_write_buffer(&(file_system_chunks[index].header.filename[0]), name, name_len);
    } else {
        if (index == FILE_NOT_FOUND) {
            return NULL;
        }
    }
    return microbit_file_descriptor_new(index, write, binary);
}

STATIC file_descriptor_obj *microbit_file_descriptor_new(uint8_t start_chunk, bool write, bool binary) {
    file_descriptor_obj *res = m_new_obj(file_descriptor_obj);
    if (binary) {
        res->base.type = &mbfs_fileio_type;
    } else {
        res->base.type = &mbfs_textio_type;
    }
    res->start_chunk = start_chunk;
    res->seek_chunk = start_chunk;
    res->seek_offset = file_system_chunks[start_chunk].header.name_len+2;
    res->writable = write;
    res->open = true;
    res->binary = binary;
    return res;
}

STATIC mp_obj_t microbit_remove(mp_obj_t filename) {
    mp_uint_t name_len;
    const char *name = mp_obj_str_get_data(filename, &name_len);
    mp_uint_t index = microbit_find_file(name, name_len);
    if (index == 255) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "file not found"));
    }
    clear_file(index);
    return mp_const_none;
}

STATIC void check_file_open(file_descriptor_obj *self) {
    if (!self->open) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "I/O operation on closed file"));
    }
}

STATIC int advance(file_descriptor_obj *self, uint32_t n, bool write) {
    DEBUG(("FILE DEBUG: Advancing from chunk %d, offset %d.\r\n", self->seek_chunk, self->seek_offset));
    self->seek_offset += n;
    if (self->seek_offset == DATA_PER_CHUNK) {
        self->seek_offset = 0;
        if (write) {
            uint8_t next_chunk = find_chunk_and_erase();
            if (next_chunk == FILE_NOT_FOUND) {
                clear_file(self->start_chunk);
                self->open = false;
                return ENOSPC;
            }
            // Link next chunk to this one
            hal_nvmc_write_byte(&(file_system_chunks[self->seek_chunk].next_chunk), next_chunk);
            hal_nvmc_write_byte(&(file_system_chunks[next_chunk].marker), self->seek_chunk);
        }
        self->seek_chunk = file_system_chunks[self->seek_chunk].next_chunk;
    }
    DEBUG(("FILE DEBUG: Advanced to chunk %d, offset %d.\r\n", self->seek_chunk, self->seek_offset));
    return 0;
}

STATIC mp_uint_t microbit_file_read(mp_obj_t obj, void *buf, mp_uint_t size, int *errcode) {
    file_descriptor_obj *self = (file_descriptor_obj *)obj;
    check_file_open(self);
    if (self->writable || file_system_chunks[self->start_chunk].marker == FREED_CHUNK) {
        *errcode = EBADF;
        return MP_STREAM_ERROR;
    }
    uint32_t bytes_read = 0;
    uint8_t *data = buf;
    while (1) {
        mp_uint_t to_read = DATA_PER_CHUNK - self->seek_offset;
        if (file_system_chunks[self->seek_chunk].next_chunk == UNUSED_CHUNK) {
            uint8_t end_offset = file_system_chunks[self->start_chunk].header.end_offset;
            if (end_offset == UNUSED_CHUNK) {
                to_read = 0;
            } else {
                to_read = min(to_read, (mp_uint_t)end_offset-self->seek_offset);
            }
        }
        to_read = min(to_read, size-bytes_read);
        if (to_read == 0) {
            break;
        }
        memcpy(data+bytes_read, seek_address(self), to_read);
        advance(self, to_read, false);
        bytes_read += to_read;
    }
    return bytes_read;
}

STATIC mp_uint_t microbit_file_write(mp_obj_t obj, const void *buf, mp_uint_t size, int *errcode) {
    file_descriptor_obj *self = (file_descriptor_obj *)obj;
    check_file_open(self);
    if (!self->writable || file_system_chunks[self->start_chunk].marker == FREED_CHUNK) {
        *errcode = EBADF;
        return MP_STREAM_ERROR;
    }
    uint32_t len = size;
    const uint8_t *data = buf;
    while (len) {
        uint32_t to_write = min(((uint32_t)(DATA_PER_CHUNK - self->seek_offset)), len);
        hal_nvmc_write_buffer(seek_address(self), data, to_write);
        int err = advance(self, to_write, true);
        if (err) {
            *errcode = err;
            return MP_STREAM_ERROR;
        }
        data += to_write;
        len -= to_write;
    }
    return size;
}

STATIC void microbit_file_close(file_descriptor_obj *fd) {
    if (fd->writable) {
        hal_nvmc_write_byte(&(file_system_chunks[fd->start_chunk].header.end_offset), fd->seek_offset);
    }
    fd->open = false;
}

STATIC mp_obj_t mbfs_file_name(mp_obj_t self) {
    file_descriptor_obj *fd = (file_descriptor_obj*)self;
    return microbit_file_name(fd);
}
MP_DEFINE_CONST_FUN_OBJ_1(mbfs_file_name_obj, mbfs_file_name);

STATIC mp_obj_t mbfs_file_close(mp_obj_t self) {
    file_descriptor_obj *fd = (file_descriptor_obj*)self;
    microbit_file_close(fd);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(mbfs_file_close_obj, mbfs_file_close);

extern const mp_obj_type_t mbfs_type;


STATIC mp_obj_t mbfs_mount(mp_obj_t self_in, mp_obj_t readonly, mp_obj_t mkfs) {
    // TODO readonly flag

    STATIC bool mounted = false;
    if (!mounted) {
        microbit_filesystem_init();
    }
    mounted = true;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(mbfs_mount_obj, mbfs_mount);

STATIC mp_obj_t mbfs_remove(mp_obj_t self, mp_obj_t name) {
    return microbit_remove(name);
}
MP_DEFINE_CONST_FUN_OBJ_2(mbfs_remove_obj, mbfs_remove);

typedef struct {
    mp_obj_base_t base;
    mp_fun_1_t iternext;
    bool is_str;
    uint8_t index;
} mbfs_ilistdir_it_t;


STATIC mp_obj_t mbfs_ilistdir_it_iternext(mp_obj_t self_in) {
    mbfs_ilistdir_it_t *self = MP_OBJ_TO_PTR(self_in);

    // Read until the next FILE_START chunk.
    for (; self->index <= chunks_in_file_system; self->index++) {
        if (file_system_chunks[self->index].marker != FILE_START) {
            continue;
        }

        // Get the file name as str/bytes object.
        mp_obj_t name;
        if (self->is_str) {
            name = mp_obj_new_str(&file_system_chunks[self->index].header.filename[0], file_system_chunks[self->index].header.name_len, false);
        } else {
            name = mp_obj_new_bytes((const byte*)&file_system_chunks[self->index].header.filename[0], file_system_chunks[self->index].header.name_len);
        }

        // make 3-tuple with info about this entry
        mp_obj_tuple_t *t = MP_OBJ_TO_PTR(mp_obj_new_tuple(3, NULL));
        t->items[0] = name;
        t->items[1] = MP_OBJ_NEW_SMALL_INT(MP_S_IFREG); // all entries are files
        t->items[2] = MP_OBJ_NEW_SMALL_INT(0); // no inode number

        self->index++;
        return MP_OBJ_FROM_PTR(t);
    }

    return MP_OBJ_STOP_ITERATION;
}

STATIC mp_obj_t mbfs_ilistdir(size_t n_args, const mp_obj_t *args) {
    mbfs_ilistdir_it_t *iter = m_new_obj(mbfs_ilistdir_it_t);
    iter->base.type = &mp_type_polymorph_iter;
    iter->iternext = mbfs_ilistdir_it_iternext;
    iter->is_str = true;
    iter->index = 1;

    if (n_args == 2) {
        if (mp_obj_get_type(args[1]) == &mp_type_bytes) {
            iter->is_str = false;
        }

        // Check for allowed paths
        size_t pathlen;
        const char *path = mp_obj_str_get_data(args[1], &pathlen);
        if (pathlen > 1 || (pathlen && path[0] != '/')) {
            // No directories allowed.
            mp_raise_OSError(MP_ENOENT);
        }
    }

    return MP_OBJ_FROM_PTR(iter);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mbfs_ilistdir_obj, 1, 2, mbfs_ilistdir);

STATIC mp_obj_t microbit_file_writable(mp_obj_t self) {
    return mp_obj_new_bool(((file_descriptor_obj *)self)->writable);
}
MP_DEFINE_CONST_FUN_OBJ_1(microbit_file_writable_obj, microbit_file_writable);

STATIC const mp_map_elem_t mbfs_file_locals_dict_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR_close), (mp_obj_t)&mbfs_file_close_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_name), (mp_obj_t)&mbfs_file_name_obj },
    //{ MP_ROM_QSTR(MP_QSTR___enter__), (mp_obj_t)&mp_identity_obj },
    //{ MP_ROM_QSTR(MP_QSTR___exit__), (mp_obj_t)&file___exit___obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_writable), (mp_obj_t)&microbit_file_writable_obj },
    /* Stream methods */
    { MP_OBJ_NEW_QSTR(MP_QSTR_read), (mp_obj_t)&mp_stream_read_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_readinto), (mp_obj_t)&mp_stream_readinto_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_readline), (mp_obj_t)&mp_stream_unbuffered_readline_obj},
    { MP_OBJ_NEW_QSTR(MP_QSTR_write), (mp_obj_t)&mp_stream_write_obj},
};
STATIC MP_DEFINE_CONST_DICT(mbfs_file_locals_dict, mbfs_file_locals_dict_table);


STATIC const mp_stream_p_t textio_stream_p = {
    .read = microbit_file_read,
    .write = microbit_file_write,
    .is_text = true,
};

const mp_obj_type_t mbfs_textio_type = {
    { &mp_type_type },
    .name = MP_QSTR_TextIO,
    .protocol = &textio_stream_p,
    .locals_dict = (mp_obj_dict_t*)&mbfs_file_locals_dict,
};


STATIC const mp_stream_p_t fileio_stream_p = {
    .read = microbit_file_read,
    .write = microbit_file_write,
};

const mp_obj_type_t mbfs_fileio_type = {
    { &mp_type_type },
    .name = MP_QSTR_FileIO,
    .protocol = &fileio_stream_p,
    .locals_dict = (mp_obj_dict_t*)&mbfs_file_locals_dict,
};

// From fileobj.c
STATIC mp_obj_t mbfs_open(size_t n_args, const mp_obj_t *args) {
    /// -1 means default; 0 explicitly false; 1 explicitly true.
    int read = -1;
    int text = -1;
    if (n_args == 3) {
        mp_uint_t len;
        const char *mode = mp_obj_str_get_data(args[2], &len);
        for (mp_uint_t i = 0; i < len; i++) {
            if (mode[i] == 'r' || mode[i] == 'w') {
                if (read >= 0) {
                    goto mode_error;
                }
                read = (mode[i] == 'r');
            } else if (mode[i] == 'b' || mode[i] == 't') {
                if (text >= 0) {
                    goto mode_error;
                }
                text = (mode[i] == 't');
            } else {
                goto mode_error;
            }
        }
    }
    mp_uint_t name_len;
    const char *filename = mp_obj_str_get_data(args[1], &name_len);
    file_descriptor_obj *res = microbit_file_open(filename, name_len, read == 0, text == 0);
    if (res == NULL) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "file not found"));
    }
    return res;
mode_error:
    nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "illegal mode"));
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mbfs_open_obj, 2, 3, mbfs_open);


STATIC const mp_rom_map_elem_t mbfs_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_mount), MP_ROM_PTR(&mbfs_mount_obj) },
    { MP_ROM_QSTR(MP_QSTR_ilistdir), MP_ROM_PTR(&mbfs_ilistdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_open), MP_ROM_PTR(&mbfs_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_remove), MP_ROM_PTR(&mbfs_remove_obj) },
};
STATIC MP_DEFINE_CONST_DICT(mbfs_locals_dict, mbfs_locals_dict_table);

const mp_obj_type_t mbfs_type = {
    { &mp_type_type },
    .name = MP_QSTR_MicroBitFS,
    .locals_dict = (mp_obj_dict_t*)&mbfs_locals_dict,
};

const mbfs_obj_t mbfs_obj = {
    { &mbfs_type },
};

#endif // MICROPY_PY_UOS_MICROBITFS
