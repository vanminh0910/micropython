/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2016 Damien P. George
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

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#ifdef __linux__
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h> // DEBUG
#endif

#include "py/reader.h"
#include "py/emitglue.h"
#include "py/persistentcode.h"
#include "py/bc.h"

#if MICROPY_PERSISTENT_CODE_LOAD || MICROPY_PERSISTENT_CODE_SAVE

#include "py/smallint.h"

// The current version of .mpy files
#define MPY_VERSION (2)

// The feature flags byte encodes the compile-time config options that
// affect the generate bytecode.
#define MPY_BYTECODE_FEATURE_FLAGS ( \
    ((MICROPY_OPT_CACHE_MAP_LOOKUP_IN_BYTECODE) << 0) \
    | ((MICROPY_PY_BUILTINS_STR_UNICODE) << 1) \
    )
// This is a version of the flags that can be configured at runtime.
#define MPY_FEATURE_FLAGS_DYNAMIC ( \
    ((MICROPY_OPT_CACHE_MAP_LOOKUP_IN_BYTECODE_DYNAMIC) << 0) \
    | ((MICROPY_PY_BUILTINS_STR_UNICODE_DYNAMIC) << 1) \
    )

// High bit is to mark this file as a native file.
#define MPY_NATIVE_FEATURE_FLAGS ( \
    (0x80) \
    )

#if MICROPY_PERSISTENT_CODE_LOAD || (MICROPY_PERSISTENT_CODE_SAVE && !MICROPY_DYNAMIC_COMPILER)
// The bytecode will depend on the number of bits in a small-int, and
// this function computes that (could make it a fixed constant, but it
// would need to be defined in mpconfigport.h).
STATIC int mp_small_int_bits(void) {
    mp_int_t i = MP_SMALL_INT_MAX;
    int n = 1;
    while (i != 0) {
        i >>= 1;
        ++n;
    }
    return n;
}
#endif

// See
// https://en.wikipedia.org/wiki/Executable_and_Linkable_Format#File_header
#define ISA_X86_64 (0x3E) // 64-bit Intel architecture
#define ISA_ARM    (0x28) // Tensilica Xtensa processors, e.g. ESP8266
#define ISA_XTENSA (0x5E) // ARM, e.g. Raspberry Pi

#if defined(__x86_64__)
#define MPY_ISA (ISA_X86_64)

#elif defined(__arm__)
#define MPY_ISA (ISA_ARM)

#elif defined(__XTENSA__)
#define MPY_ISA (ISA_XTENSA)

#else
#error unknown machine architecture
#endif


typedef struct _bytecode_prelude_t {
    uint n_state;
    uint n_exc_stack;
    uint scope_flags;
    uint n_pos_args;
    uint n_kwonly_args;
    uint n_def_pos_args;
    uint code_info_size;
} bytecode_prelude_t;

// ip will point to start of opcodes
// ip2 will point to simple_name, source_file qstrs
STATIC void extract_prelude(const byte **ip, const byte **ip2, bytecode_prelude_t *prelude) {
    prelude->n_state = mp_decode_uint(ip);
    prelude->n_exc_stack = mp_decode_uint(ip);
    prelude->scope_flags = *(*ip)++;
    prelude->n_pos_args = *(*ip)++;
    prelude->n_kwonly_args = *(*ip)++;
    prelude->n_def_pos_args = *(*ip)++;
    *ip2 = *ip;
    prelude->code_info_size = mp_decode_uint(ip2);
    *ip += prelude->code_info_size;
    while (*(*ip)++ != 255) {
    }
}

#endif // MICROPY_PERSISTENT_CODE_LOAD || MICROPY_PERSISTENT_CODE_SAVE

#if MICROPY_PERSISTENT_CODE_LOAD

#include "py/parsenum.h"
#include "py/bc0.h"

STATIC int read_byte(mp_reader_t *reader) {
    return reader->readbyte(reader->data);
}

STATIC void read_bytes(mp_reader_t *reader, byte *buf, size_t len) {
    while (len-- > 0) {
        *buf++ = reader->readbyte(reader->data);
    }
}

STATIC size_t read_uint(mp_reader_t *reader) {
    size_t unum = 0;
    for (;;) {
        byte b = reader->readbyte(reader->data);
        unum = (unum << 7) | (b & 0x7f);
        if ((b & 0x80) == 0) {
            break;
        }
    }
    return unum;
}

/*STATIC int read_sint(mp_reader_t *reader) {
    size_t unum = read_uint(reader);
    if (unum & 1) { // negative
        return -(int)(unum >> 1);
    } else { // positive
        return ((int)unum) >> 1;
    }
}*/

STATIC qstr load_qstr(mp_reader_t *reader) {
    size_t len = read_uint(reader);
    char *str = m_new(char, len);
    read_bytes(reader, (byte*)str, len);
    qstr qst = qstr_from_strn(str, len);
    m_del(char, str, len);
    return qst;
}

STATIC mp_obj_t load_obj(mp_reader_t *reader) {
    byte obj_type = read_byte(reader);
    if (obj_type == 'e') {
        return MP_OBJ_FROM_PTR(&mp_const_ellipsis_obj);
    } else {
        size_t len = read_uint(reader);
        vstr_t vstr;
        vstr_init_len(&vstr, len);
        read_bytes(reader, (byte*)vstr.buf, len);
        if (obj_type == 's' || obj_type == 'b') {
            return mp_obj_new_str_from_vstr(obj_type == 's' ? &mp_type_str : &mp_type_bytes, &vstr);
        } else if (obj_type == 'i') {
            return mp_parse_num_integer(vstr.buf, vstr.len, 10, NULL);
        } else {
            assert(obj_type == 'f' || obj_type == 'c');
            return mp_parse_num_decimal(vstr.buf, vstr.len, obj_type == 'c', false, NULL);
        }
    }
}

STATIC void load_bytecode_qstrs(mp_reader_t *reader, byte *ip, byte *ip_top) {
    while (ip < ip_top) {
        size_t sz;
        uint f = mp_opcode_format(ip, &sz);
        if (f == MP_OPCODE_QSTR) {
            qstr qst = load_qstr(reader);
            ip[1] = qst;
            ip[2] = qst >> 8;
        }
        ip += sz;
    }
}

STATIC mp_raw_code_t *load_raw_code_bytecode(mp_reader_t *reader) {
    // load bytecode
    size_t bc_len = read_uint(reader);
    byte *bytecode = m_new(byte, bc_len);
    read_bytes(reader, bytecode, bc_len);

    // extract prelude
    const byte *ip = bytecode;
    const byte *ip2;
    bytecode_prelude_t prelude;
    extract_prelude(&ip, &ip2, &prelude);

    // load qstrs and link global qstr ids into bytecode
    qstr simple_name = load_qstr(reader);
    qstr source_file = load_qstr(reader);
    ((byte*)ip2)[0] = simple_name; ((byte*)ip2)[1] = simple_name >> 8;
    ((byte*)ip2)[2] = source_file; ((byte*)ip2)[3] = source_file >> 8;
    load_bytecode_qstrs(reader, (byte*)ip, bytecode + bc_len);

    // load constant table
    size_t n_obj = read_uint(reader);
    size_t n_raw_code = read_uint(reader);
    mp_uint_t *const_table = m_new(mp_uint_t, prelude.n_pos_args + prelude.n_kwonly_args + n_obj + n_raw_code);
    mp_uint_t *ct = const_table;
    for (size_t i = 0; i < prelude.n_pos_args + prelude.n_kwonly_args; ++i) {
        *ct++ = (mp_uint_t)MP_OBJ_NEW_QSTR(load_qstr(reader));
    }
    for (size_t i = 0; i < n_obj; ++i) {
        *ct++ = (mp_uint_t)load_obj(reader);
    }
    for (size_t i = 0; i < n_raw_code; ++i) {
        *ct++ = (mp_uint_t)(uintptr_t)load_raw_code_bytecode(reader);
    }

    // create raw_code and return it
    mp_raw_code_t *rc = mp_emit_glue_new_raw_code();
    mp_emit_glue_assign_bytecode(rc, bytecode, bc_len, const_table,
        #if MICROPY_PERSISTENT_CODE_SAVE
        n_obj, n_raw_code,
        #endif
        prelude.scope_flags);
    return rc;
}

STATIC mp_raw_code_t *load_raw_code_native(mp_reader_t *reader) {
    // Load native code header
    size_t len_code = read_uint(reader);
    size_t len_data = read_uint(reader);
    size_t num_relocs = read_uint(reader);
    size_t start_index = read_uint(reader); // Load start address index (offset from code start)
    printf("num relocs:  %d\n", (int)num_relocs);
    printf("start index: %x\n", (unsigned int)start_index);

#if defined(__arm__)
    if (len_code >= 0x1f00000) { // if code >= 31M
        // Heuristic: the code size should be smaller than ~32M
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ImportError,
            "code too big: %d bytes\n", len_code));
    }
#endif

    printf("len code:   %x\n", (unsigned int)len_code);
    printf("len data:   %x\n", (unsigned int)len_data);

#if defined(__unix__)
    // Unix-like (does that include OSX?)

#if defined(__x86_64__)
    size_t len_plt = (14 * num_relocs);
    if ((len_code + len_plt) % 8 != 0) {
        // align end of len_plt for the start of data
        len_plt += 8 - (len_code + len_plt) % 8;
    }
    // Hint to the kernel that we want a region after where our program is
    // loaded. Currently using 32MB after the address of a random
    // function, but this may change. It also clears the lower 20 bits, or
    // ~1MB, to align on a page boundary.
    // TODO integrate with ports/unix/alloc.c? This is currently mostly a
    // hack.
    byte *code = 0;
    uintptr_t try_addr = ((uintptr_t)mp_store_attr + 0x02000000) >> 20 << 20;
    int len = len_code + len_plt + len_data;
    for (int i=0; i<100; i++) { // TODO magic number of tries
        // Work around the compiler disallowing pointer arithmetic.
        code = mmap((void*)try_addr, len, PROT_EXEC|PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
        if (code == MAP_FAILED) {
            perror("mmap");
            code = NULL;
        }
        printf("  try=%lx code=%p\n", try_addr, code);
        if (code == (void*)try_addr) {
            break;
        }
        munmap((void*)try_addr, len);
        code = 0;
        printf("! trying mmap again...\n");
        try_addr += 0x100000; // add 1MB
    }
    if (code == 0) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_MemoryError,
            "cannot find room for code + data"));
    }
    byte *plt = code + len_code;
    byte *data = plt + len_plt;
    printf("code:   %p (extra bits %lx)\n", code, (uintptr_t)code >> 32);
    printf("plt:    %p\n", plt);
    printf("data:   %p\n", data);

#elif defined(__arm__)
    size_t len_veneer = (8 * num_relocs); // can be smaller, many relocs require 4 or 0 bytes of veneer
    printf("len veneer: %x\n", len_veneer);
    byte *code = mmap(NULL, len_code + len_veneer, PROT_EXEC|PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    byte *data = m_new(byte, len_data);
    uint32_t *veneer = (uint32_t*)(code + len_code);
    printf("code:   %p\n", code);
    printf("veneer: %p\n", veneer);
    printf("data:   %p\n", data);

#else
#error "Unix that isn't x64 or ARM - I don't know how to allocate memory"
#endif

#else
    // Assuming no OS, running on bare metal

#if defined(__xtensa__)
    byte *code = m_new(byte, len_code);
    byte *data = m_new(byte, len_data);
    printf("code:   %p\n", code);
    printf("data:   %p\n", data);

#else
#error Unknown MCU platform
#endif

#endif

#ifdef MP_PLAT_COMMIT_EXEC
    // Where code is stored in the end. This is relevant for systems using
    // the Harvard architecture (e.g. Xtensa).
    byte *code_stored = MP_PLAT_COMMIT_EXEC(code, 0);
    printf("code 2: %p\n", code_stored);
#else
    byte *code_stored = code;
#endif

    // Load native code
    read_bytes(reader, code, len_code);

    // Load initialized data
    read_bytes(reader, data, len_data);

    #if 1
    // Load relocations
    size_t offset = 0;
    for (size_t i=0; i<num_relocs; i++) {
        size_t target = read_uint(reader);
        offset = read_uint(reader);
        //int    addend = read_sint(reader);
        //printf("relocation: target=%d offset=%x\n", (int)target, (int)offset);

        //printf("  old bytes for address: %02x %02x %02x %02x\n", code[offset+0], code[offset+1], code[offset+2], code[offset+3]);

        uintptr_t address = 0;
        switch (target) {
            //case 1:
            //    address = (uintptr_t)mp_obj_new_int;
            //    break;
            //case 2:
            //    address = (uintptr_t)&mp_type_fun_builtin_0;
            //    break;
            //case 3:
            //    address = (uintptr_t)mp_const_none;
            //    break;
            //case 4:
            //    address = (uintptr_t)mp_const_true;
            //    break;
            //case 5:
            //    address = (uintptr_t)mp_const_false;
            //    break;
            //case 6:
            //    address = (uintptr_t)mp_store_attr;
            //    break;
            //case 7:
            //    address = (uintptr_t)qstr_from_str;
            //    break;
            //case 8:
            //    address = (uintptr_t)mp_obj_get_int;
            //    break;
            //case 9:
            //    address = (uintptr_t)&mp_type_fun_builtin_2;
            //    break;
            //case 10:
            //    address = (uintptr_t)&mp_type_fun_builtin_3;
            //    break;
            //case 11:
            //    address = (uintptr_t)&mp_obj_dict_store;
            //    break;
            case 126:
                address = (uintptr_t)data;
                break;
            case 127:
                address = (uintptr_t)code_stored;
                break;
            default:
                address = (uintptr_t)mp_fun_table[target];
                //nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ImportError,
                //    "relocation failed: unknown symbol %d", target));
                break;
        }
        //printf("  orig address:  %lx\n", (long unsigned int)address);


        // works for little-endian systems
        //printf("  bytes for address: %02x %02x %02x %02x\n", ((byte*)address)[0], ((byte*)address)[1], ((byte*)address)[2], ((byte*)address)[3]);

#if defined(__x86_64__)
        // For Intel 64-bit (also called amd64, x86-64 and x64)

        uint_fast8_t type = offset & 0b111;
        size_t offset_x64 = offset >> 3;
        //printf("  type:   %u\n", (unsigned int)type);
        //printf("  offset: %lx\n", offset_x64);

        if (type & 0b001) { // special relocation
            if (type == 0b001) { // jump to function
                // This load may be unaligned, but doing it this way is a lot
                // easier and possibly even faster than doing it per byte.
                int32_t addend = *(int32_t*)&code[offset_x64];
                //printf("  addend: %x\n", addend);

                uintptr_t reladdress = address - ((uintptr_t)code + offset_x64) - 4 + addend;
                if ((reladdress >> 32) == 0 || (reladdress >> 32) == 0xffffffff) {
                    // The target address is within range.
                    //printf("  not using a PLT!\n");
                    address = reladdress & 0xffffffff;
                } else {
                    // Jump to PLT
                    // Program counter is always at the end of the instruction,
                    // i.e. 4 bytes ahead of the offset. So subtract those 4 bytes
                    // from the address to the PLT entry.
                    reladdress = (uintptr_t)plt - ((uintptr_t)code + offset_x64) - 4;

                    // Create PLT entry
                    // Assembly:
                    //    jmp    QWORD PTR [rip+0]
                    // Source: http://www.ragestorm.net/blogs/?p=107 (method 4)
                    // And I took a look at the PLT entries for a .so file
                    // generated by the GNU linker.
                    // The absolute 64-bit target address is put directly in the
                    // PLT, after the JMP instruction.
                    *(uint16_t*)&plt[0] = 0x25ff;           // jmp QWORD PTR
                    *(uint32_t*)&plt[2] = 0x00000000;       // [rip+0]
                    *(uint64_t*)&plt[6] = address + addend; // literal value for address (this is at [rip+0])
                    plt += 14; // 2 + 4 + 8

                    address = reladdress;
                }

                //if ((address >> 32) != 0) {
                //    // Not in 32-bit range
                //    nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ImportError,
                //        "relocation failed: relative pointer %08p (extra bits %p) out of range for target %d", address, address >> 32, target));
                //}

                *(uint32_t*)&code[offset_x64] = (uint32_t)address;

            } else if (type == 0b011) { // without 'data' flag
                int32_t addend = *(int32_t*)&code[offset_x64];
                //printf("  addend: %x\n", addend);
                *(uint64_t*)&code[offset_x64] = address + addend;
            } else if (type == 0b111) { // with 'data' flag
                int32_t addend = *(int32_t*)&data[offset_x64];
                //printf("  addend: %x\n", addend);
                *(uint64_t*)&data[offset_x64] = address + addend;
            } else {
                //nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ImportError,
                //    "unknown relocation type: %d", type));
            }
        } else {
            // Normal relocation: address to relative offset

            // Select the place to store the address
            byte *dest = code;
            if (type & 0b100) {
                dest = data;
            }

            int32_t addend = *(int32_t*)&code[offset_x64];
            //printf("  addend: %x\n", addend);

            *(uint32_t*)&dest[offset_x64] = address - ((uintptr_t)dest + offset_x64) + addend;
        }

#elif defined(__arm__)
        // for ARM (non-Thumb), also known as A32.

        uint_fast8_t type = offset & 0b111;
        size_t offset_arm = offset >> 3;

        //printf("  type:    %d\n", type);
        //printf("  offset:  %x\n", offset_arm);

        //if (offset_arm & 0b11) {
        //    nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ImportError,
        //        "relocation failed: unaligned offset %x\n", offset_arm));
        //}

        if (type == 0b001) {
            // This is a 26-bit branch instruction, branching via the
            // veneer.

            // Address to the jump in the veneer below.
            // -8 for the PC offset
            uint32_t reladdress = (uint32_t)veneer - ((uint32_t)code + offset_arm) - 8;

            // Due to things that made sense around the time ARM was
            // designed the PC is always 8 bits (2 instructions) ahead of
            // the current instruction. That means we have to load the
            // target address from PC - 4, not PC + 4.
            *veneer++ = 0xe51ff004; // LDR pc, [pc, #-4]  -- jump to absolute address
            *veneer++ = address;
            //printf("  branch to veneer: %x\n", reladdress);
            //printf("  veneer branches to %x\n", address);

            // Branch to the veneer - the veneer is in range while the
            // destination address probably isn't (on systems with >32M of
            // RAM).
            // This is a 26-bit address of which the lowest 2 bits aren't
            // stored in the immediate.
            code[offset_arm+0] = (uint32_t)reladdress >> 2;
            code[offset_arm+1] = (uint32_t)reladdress >> 10;
            code[offset_arm+2] = (uint32_t)reladdress >> 18;

        // Not necessary in non-PIC code.
        //} else if (type == 0b011) {
        //    // Put relative 32-bit address at offset (relative to the
        //    // start of code) and target absolute address in the veneer.

        //    uint32_t addend = *(uint32_t*)&code[offset_arm];
        //    printf("  addend:  %x\n", addend);
        //    address += addend;

        //    uint32_t reladdress = (uint32_t)veneer - (uint32_t)code;
        //    *veneer++ = address;
        //    printf("  reladdress: %x\n", reladdress);
        //    printf("  inserting into: %p\n", &code[offset_arm]);
        //    *(uint32_t*)&code[offset_arm] = reladdress;

        } else { // normal 32-bit relocation
            byte *dest = code;
            if (type & 0b010) { // is the offset from the data storage (and not from code)?
                dest = data;
                //printf("  dest:    data\n");
            } else {
                //printf("  dest:    code\n");
            }

            if (type & 0b100) { // is this address relative?
                address = address - ((uint32_t)dest + offset_arm);
                //printf("  reladdr: %x\n", address);
            }

            uint32_t addend = *(uint32_t*)&dest[offset_arm];
            //printf("  addend:  %x (%d)\n", addend, (int)addend);
            address += addend;
            //printf("  address: %x\n", address);


            *(uint32_t*)&dest[offset_arm] = address;
        }

#elif defined(__xtensa__)
        uint_fast8_t type = offset & 0b1;
        size_t offset_xtensa = offset >> 1;

        //if (offset_xtensa & 0b11) {
        //    nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ImportError,
        //        "relocation failed: unaligned offset %x\n", offset_xtensa));
        //}

        //printf("  type:    %d\n", type);
        //printf("  offset:  %x\n", offset_xtensa);

        byte *dest = code;
        if (type == 0b1) { // is the offset from the data storage (and not from code)?
            dest = data;
            //printf("  dest:    data\n");
        } else {
            //printf("  dest:    code\n");
        }
        //printf("  data 0: ");
        //for (int i=0; i<16; i++) {
        //    printf(" %02x", data[i]);
        //}
        //printf("\n");

        uint32_t addend = *(uint32_t*)&dest[offset_xtensa];
        //printf("  addend: %02x %02x %02x %02x\n", dest[offset_xtensa+0], dest[offset_xtensa+1], dest[offset_xtensa+2], dest[offset_xtensa+3]);
        //printf("  addend:  %x (%d)\n", addend, (int)addend);
        address += addend;
        //printf("  address: %x\n", address);


        *(uint32_t*)&dest[offset_xtensa] = address;

#else
#error Unknown architecture for relocations
#endif
    }
    #endif

#if defined(MP_PLAT_COMMIT_EXEC)
    byte *newcode = MP_PLAT_COMMIT_EXEC(code, len_code);
    if (newcode != code) {
        m_free(code);
        code = newcode;
    }
    if (newcode != code_stored) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_ImportError,
                "code address after commit is wrong"));
    }
#endif

    // create raw_code and return it
    mp_raw_code_t *rc = mp_emit_glue_new_raw_code();
    mp_emit_glue_assign_loadable_native(rc, code, len_code, start_index);
    return rc;
}

mp_raw_code_t *mp_raw_code_load(mp_reader_t *reader) {
    byte header[4];
    read_bytes(reader, header, sizeof(header));
    mp_raw_code_t *rc;
    if (header[0] == 'M'
        && header[1] == MPY_VERSION
        && header[2] == MPY_BYTECODE_FEATURE_FLAGS
        && header[3] <= mp_small_int_bits()) {
        rc = load_raw_code_bytecode(reader);
        reader->close(reader->data);
    } else if (header[0] == 'M'
        && header[1] == MPY_VERSION
        && header[2] == MPY_NATIVE_FEATURE_FLAGS
        && header[3] == MPY_ISA) {
        rc = load_raw_code_native(reader);
    } else {
        printf("header: %02x %02x %02x %02x\n", header[0], header[1], header[2], header[3]);
        mp_raise_ValueError("incompatible .mpy file");
    }
    return rc;
}

mp_raw_code_t *mp_raw_code_load_mem(const byte *buf, size_t len) {
    mp_reader_t reader;
    mp_reader_new_mem(&reader, buf, len, 0);
    return mp_raw_code_load(&reader);
}

mp_raw_code_t *mp_raw_code_load_file(const char *filename) {
    mp_reader_t reader;
    mp_reader_new_file(&reader, filename);
    return mp_raw_code_load(&reader);
}

#endif // MICROPY_PERSISTENT_CODE_LOAD

#if MICROPY_PERSISTENT_CODE_SAVE

#include "py/objstr.h"

STATIC void mp_print_bytes(mp_print_t *print, const byte *data, size_t len) {
    print->print_strn(print->data, (const char*)data, len);
}

#define BYTES_FOR_INT ((BYTES_PER_WORD * 8 + 6) / 7)
STATIC void mp_print_uint(mp_print_t *print, size_t n) {
    byte buf[BYTES_FOR_INT];
    byte *p = buf + sizeof(buf);
    *--p = n & 0x7f;
    n >>= 7;
    for (; n != 0; n >>= 7) {
        *--p = 0x80 | (n & 0x7f);
    }
    print->print_strn(print->data, (char*)p, buf + sizeof(buf) - p);
}

STATIC void save_qstr(mp_print_t *print, qstr qst) {
    size_t len;
    const byte *str = qstr_data(qst, &len);
    mp_print_uint(print, len);
    mp_print_bytes(print, str, len);
}

STATIC void save_obj(mp_print_t *print, mp_obj_t o) {
    if (MP_OBJ_IS_STR_OR_BYTES(o)) {
        byte obj_type;
        if (MP_OBJ_IS_STR(o)) {
            obj_type = 's';
        } else {
            obj_type = 'b';
        }
        mp_uint_t len;
        const char *str = mp_obj_str_get_data(o, &len);
        mp_print_bytes(print, &obj_type, 1);
        mp_print_uint(print, len);
        mp_print_bytes(print, (const byte*)str, len);
    } else if (MP_OBJ_TO_PTR(o) == &mp_const_ellipsis_obj) {
        byte obj_type = 'e';
        mp_print_bytes(print, &obj_type, 1);
    } else {
        // we save numbers using a simplistic text representation
        // TODO could be improved
        byte obj_type;
        if (MP_OBJ_IS_TYPE(o, &mp_type_int)) {
            obj_type = 'i';
        #if MICROPY_PY_BUILTINS_COMPLEX
        } else if (MP_OBJ_IS_TYPE(o, &mp_type_complex)) {
            obj_type = 'c';
        #endif
        } else {
            assert(mp_obj_is_float(o));
            obj_type = 'f';
        }
        vstr_t vstr;
        mp_print_t pr;
        vstr_init_print(&vstr, 10, &pr);
        mp_obj_print_helper(&pr, o, PRINT_REPR);
        mp_print_bytes(print, &obj_type, 1);
        mp_print_uint(print, vstr.len);
        mp_print_bytes(print, (const byte*)vstr.buf, vstr.len);
        vstr_clear(&vstr);
    }
}

STATIC void save_bytecode_qstrs(mp_print_t *print, const byte *ip, const byte *ip_top) {
    while (ip < ip_top) {
        size_t sz;
        uint f = mp_opcode_format(ip, &sz);
        if (f == MP_OPCODE_QSTR) {
            qstr qst = ip[1] | (ip[2] << 8);
            save_qstr(print, qst);
        }
        ip += sz;
    }
}

STATIC void save_raw_code(mp_print_t *print, mp_raw_code_t *rc) {
    if (rc->kind != MP_CODE_BYTECODE) {
        mp_raise_ValueError("can only save bytecode");
    }

    // save bytecode
    mp_print_uint(print, rc->data.u_byte.bc_len);
    mp_print_bytes(print, rc->data.u_byte.bytecode, rc->data.u_byte.bc_len);

    // extract prelude
    const byte *ip = rc->data.u_byte.bytecode;
    const byte *ip2;
    bytecode_prelude_t prelude;
    extract_prelude(&ip, &ip2, &prelude);

    // save qstrs
    save_qstr(print, ip2[0] | (ip2[1] << 8)); // simple_name
    save_qstr(print, ip2[2] | (ip2[3] << 8)); // source_file
    save_bytecode_qstrs(print, ip, rc->data.u_byte.bytecode + rc->data.u_byte.bc_len);

    // save constant table
    mp_print_uint(print, rc->data.u_byte.n_obj);
    mp_print_uint(print, rc->data.u_byte.n_raw_code);
    const mp_uint_t *const_table = rc->data.u_byte.const_table;
    for (uint i = 0; i < prelude.n_pos_args + prelude.n_kwonly_args; ++i) {
        mp_obj_t o = (mp_obj_t)*const_table++;
        save_qstr(print, MP_OBJ_QSTR_VALUE(o));
    }
    for (uint i = 0; i < rc->data.u_byte.n_obj; ++i) {
        save_obj(print, (mp_obj_t)*const_table++);
    }
    for (uint i = 0; i < rc->data.u_byte.n_raw_code; ++i) {
        save_raw_code(print, (mp_raw_code_t*)(uintptr_t)*const_table++);
    }
}

void mp_raw_code_save(mp_raw_code_t *rc, mp_print_t *print) {
    // header contains:
    //  byte  'M'
    //  byte  version
    //  byte  feature flags
    //  byte  number of bits in a small int
    byte header[4] = {'M', MPY_VERSION, MPY_FEATURE_FLAGS_DYNAMIC,
        #if MICROPY_DYNAMIC_COMPILER
        mp_dynamic_compiler.small_int_bits,
        #else
        mp_small_int_bits(),
        #endif
    };
    mp_print_bytes(print, header, sizeof(header));

    save_raw_code(print, rc);
}

// here we define mp_raw_code_save_file depending on the port
// TODO abstract this away properly

#if defined(__i386__) || defined(__x86_64__) || (defined(__arm__) && (defined(__unix__)))

#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

STATIC void fd_print_strn(void *env, const char *str, size_t len) {
    int fd = (intptr_t)env;
    ssize_t ret = write(fd, str, len);
    (void)ret;
}

void mp_raw_code_save_file(mp_raw_code_t *rc, const char *filename) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    mp_print_t fd_print = {(void*)(intptr_t)fd, fd_print_strn};
    mp_raw_code_save(rc, &fd_print);
    close(fd);
}

#else
#error mp_raw_code_save_file not implemented for this platform
#endif

#endif // MICROPY_PERSISTENT_CODE_SAVE
