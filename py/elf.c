/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
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

#ifdef MICROPY_MODULE_ELF

#include "py/elf.h"
#include "py/reader.h"
#include "py/runtime.h"
#include "py/misc.h"
#include <string.h>
#include <stdio.h> // TODO remove (after printf is removed)

#define EI_NIDENT 16
typedef uint32_t elf32_addr_t;
typedef uint16_t elf32_half_t;
typedef uint32_t elf32_off_t;
typedef uint32_t elf32_word_t;
typedef int32_t  elf32_sword_t;

#define ELFCLASS32 1
#define ELFCLASS64 1

#define ELFDATA2LSB 1
#define ELFDATA2MSB 2

#define ET_NONE 0
#define ET_REL  1
#define ET_EXEC 2
#define ET_DYN  3
#define ET_CORE 4

#define SHT_NULL     0
#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_RELA     4
#define SHT_HASH     5
#define SHT_DYNAMIC  6
#define SHT_NOTE     7
#define SHT_NOBITS   8
#define SHT_REL      9
#define SHT_SHLIB    10
#define SHT_DYNSYM   11
#define SHT_LOPROC   0x70000000
#define SHT_HIPROC   0x7fffffff
#define SHT_LOUSER   0x80000000
#define SHT_HIUSER   0xffffffff

#define SHF_WRITE     0x1
#define SHF_ALLOC     0x2
#define SHF_EXECINSTR 0x4
#define SHF_MASKPROC  0xf0000000

#define STB_LOCAL  0
#define STB_GLOBAL 1
#define STB_WEAK   2
#define STB_LOPROC 13
#define STB_HIPROC 15

#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2
#define STT_SECTION 3
#define STT_FILE    4
#define STT_LOPROC  13
#define STT_HIPROC  15

#define INSTRUCTION_SET_XTENSA 0x5e

// http://wiki.linux-xtensa.org/index.php/ELF_Relocation_Notes
#define R_XTENSA_RTLD     2
#define R_XTENSA_GLOB_DAT 3
#define R_XTENSA_JMP_SLOT 4
#define R_XTENSA_RELATIVE 5

typedef struct {
    byte mag0;
    byte mag1;
    byte mag2;
    byte mag3;
    byte class;
    byte data;
    byte version;
    byte pad[9];
} elf32_ident_t;

typedef struct {
    elf32_ident_t e_ident;
    elf32_half_t e_type;
    elf32_half_t e_machine;
    elf32_word_t e_version;
    elf32_addr_t e_entry;
    elf32_off_t e_phoff;
    elf32_off_t e_shoff;
    elf32_word_t e_flags;
    elf32_half_t e_ehsize;
    elf32_half_t e_phentsize;
    elf32_half_t e_phnum;
    elf32_half_t e_shentsize;
    elf32_half_t e_shnum;
    elf32_half_t e_shstrndx;
} elf32_ehdr_t;

typedef struct {
    elf32_word_t sh_name;
    elf32_word_t sh_type;
    elf32_word_t sh_flags;
    elf32_addr_t sh_addr;
    elf32_off_t sh_offset;
    elf32_word_t sh_size;
    elf32_word_t sh_link;
    elf32_word_t sh_info;
    elf32_word_t sh_addralign;
    elf32_word_t sh_entsize;
} elf32_shdr_t;

typedef struct {
    elf32_word_t st_name;
    elf32_addr_t st_value;
    elf32_word_t st_size;
    unsigned char st_info;
    unsigned char st_other;
    elf32_half_t st_shndx;
} elf32_sym_t;

#define ELF32_ST_BIND(i) ((i)>>4)
#define ELF32_ST_TYPE(i) ((i)&0xf)
#define ELF32_ST_INFO(b,t) (((b)<<4)+((t)&0xf))

typedef struct {
    elf32_addr_t r_offset;
    elf32_word_t r_info;
    elf32_sword_t r_addend;
} elf32_rela_t;

#define ELF32_R_SYM(i) ((i)>>8)
#define ELF32_R_TYPE(i) ((unsigned char)(i))
#define ELF32_R_INFO(s,t) (((s)<<8)+(unsigned char)(t))

typedef enum {
    TYPE_UNDEFINED,
    TYPE_FUNC_VAR,
    TYPE_FUNC_2_INT,
    TYPE_CONST_INT,
} member_type_t;

typedef struct mp_dyn_module_t {
    member_type_t type;
    uint32_t addr;
} mp_dyn_module_t;

STATIC void raise_err() {
    mp_raise_msg(&mp_type_ImportError, "invalid ELF file");
}

STATIC void read_obj(void *buf, size_t len, mp_reader_t *reader) {
    byte *bytesbuf = (byte*)buf;
    while (len--) {
        mp_uint_t b = reader->readbyte(reader->data);
        if (b == MP_READER_EOF) {
            raise_err();
        }
        *bytesbuf++ = b;
    }
}

typedef struct {
    byte *buf;
    uintptr_t addr_src;
    uintptr_t addr_dst;
    uintptr_t size;
} memorybuf_t;

STATIC uintptr_t relocate_address(uintptr_t addr_src, uintptr_t rel_size, memorybuf_t *memories, size_t len, bool final_address) {
    for (size_t i=0; i<len; i++) {
        if (addr_src >= memories[i].addr_src && addr_src + rel_size <= memories[i].addr_src + memories[i].size) {
            if (final_address) {
                // Address in flash memory
                return memories[i].addr_dst + (addr_src - memories[i].addr_src);
            } else {
                // Address currently still in RAM
                return (uintptr_t)memories[i].buf + (addr_src - memories[i].addr_src);
            }
        }
    }
    return 0;
}

STATIC mp_obj_t fun_elf_call_2(mp_obj_t self_in, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    assert(MP_OBJ_IS_TYPE(self_in, &mp_type_fun_elf_2));
    mp_obj_fun_elf_t *self = MP_OBJ_TO_PTR(self_in);
    mp_arg_check_num(n_args, n_kw, 2, 2, false);
    int a = mp_obj_get_int(args[0]);
    int b = mp_obj_get_int(args[1]);
    int result = self->fun._2(a, b);
    return mp_obj_new_int(result);
}

STATIC mp_obj_t fun_elf_call_var(mp_obj_t self_in, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    assert(MP_OBJ_IS_TYPE(self_in, &mp_type_fun_elf_var));
    mp_obj_fun_elf_t *self = MP_OBJ_TO_PTR(self_in);
    mp_arg_check_num(n_args, n_kw, 0, 16 /* TODO */, false);
    mp_obj_t result = self->fun._var(n_args, args);
    return result;
}

const mp_obj_type_t mp_type_fun_elf_2 = {
    { &mp_type_type },
    .name = MP_QSTR_function,
    .call = fun_elf_call_2,
    .unary_op = mp_generic_unary_op,
};

const mp_obj_type_t mp_type_fun_elf_var = {
    { &mp_type_type },
    .name = MP_QSTR_function,
    .call = fun_elf_call_var,
    .unary_op = mp_generic_unary_op,
};

/*STATIC void print_assembly(memorybuf_t *text) {
    // Print instructions
    // (reverse engineerd - that seemed easier than actually looking in
    // the source of xtensa-lx106-elf-objdump)
    printf("\n.text segment:\n");
    for (int i=0x358-text->addr_src; i<text->size; ) {
        printf("%4p:   ", (void*)(i+text->addr_src));
        uint32_t instr = text->buf[i++];
        instr |= text->buf[i++] << 8;
        if ((instr & 0xf) < 8) {
            instr |= text->buf[i++] << 16;
            printf("%06x  %p\n", instr, &text->buf[i-3]);
        } else {
            printf("%04x    %p\n", instr, &text->buf[i-2]);
        }
    }
}*/

// Internal for GCC
// https://gcc.gnu.org/onlinedocs/gccint/Soft-float-library-routines.html
extern float __floatsisf (int i);
extern float __mulsf3 (float a, float b);
extern float __addsf3 (float a, float b);
extern unsigned int __fixunssfsi (float a);


STATIC void mp_elf_load(mp_obj_t module_obj, mp_reader_t *reader) {
    elf32_ehdr_t header;
    read_obj(&header, sizeof(header), reader);
    if (header.e_ident.mag0 != 0x7f ||
        header.e_ident.mag1 != 'E' ||
        header.e_ident.mag2 != 'L' ||
        header.e_ident.mag3 != 'F') {
        // not an ELF file
        raise_err();
    }
    if (header.e_ident.class != ELFCLASS32 || // 32-bit
        header.e_ident.data != ELFDATA2LSB || // little-endian
        header.e_ident.version != 1 ||
        header.e_type != ET_DYN || // not a .so file
        header.e_machine != INSTRUCTION_SET_XTENSA ||
        header.e_version != 1) {
        raise_err();
    }

    if (header.e_shoff == 0 || header.e_shnum <= 1) {
        // must have sections
        raise_err();
    }

    elf32_shdr_t sections[header.e_shnum];
    reader->seek(reader->data, header.e_shoff);
    read_obj(sections, sizeof(elf32_shdr_t)*header.e_shnum, reader);

    // Look for the .text, .dynsym and .dynstr sections.
    // This is mostly guesswork.
    elf32_shdr_t *text_section = NULL;
    elf32_shdr_t *dynsym_section = NULL;
    elf32_shdr_t *dynstr_section = NULL;

    // Parts of the memory
    memorybuf_t memories[2] = {
        {NULL, 0, 0, 0}, // text
        {NULL, 0, 0, 0}, // readonly
    };
    memorybuf_t *text = &memories[0];
    memorybuf_t *readonly = &memories[1];

    size_t ro_start = 0;
    for (size_t i=1; i<header.e_shnum; i++) {
        elf32_shdr_t *section = &sections[i];

        // Determine read-only part. This is mostly guesswork.
        if (text_section != NULL && ro_start == 0 && section->sh_type == SHT_PROGBITS) {
            // start read-only part (after .text)
            ro_start = section->sh_offset;
            readonly->addr_src = section->sh_addr;
        }
        if (ro_start != 0 && readonly->size == 0 && section->sh_type == SHT_PROGBITS) {
            // continue read-only part
            if (readonly->addr_src + (section->sh_offset - ro_start) != section->sh_addr) {
                raise_err(); // non-contiguous read-only section
            }
        }
        if (ro_start != 0 && readonly->size == 0 && section->sh_type != SHT_PROGBITS) {
            // end read-only part
            readonly->size = section->sh_offset - ro_start;
        }

        // Determine .text section.
        if (section->sh_type == SHT_PROGBITS && (section->sh_flags & SHF_EXECINSTR)) {
            text_section = section;
        }

        if (section->sh_type == SHT_DYNSYM) {
            dynsym_section = section;
            if (dynsym_section->sh_link >= header.e_shnum) {
                raise_err();
            }
            dynstr_section = &sections[dynsym_section->sh_link];
            if (dynstr_section->sh_type != SHT_STRTAB) {
                raise_err();
            }
        }
    }
    if (text_section == NULL || dynsym_section == NULL || dynstr_section == NULL) {
        raise_err();
    }

    // Read the dynsym segment.
    elf32_sym_t dynsym[dynsym_section->sh_size / sizeof(elf32_sym_t)];
    reader->seek(reader->data, dynsym_section->sh_offset);
    read_obj(dynsym, dynsym_section->sh_size, reader);

    // Read the dynstr segment.
    char dynstr[dynstr_section->sh_size];
    reader->seek(reader->data, dynstr_section->sh_offset);
    read_obj(dynstr, dynstr_section->sh_size, reader);

    // Read the text segment.
    text->buf = m_new(byte, text_section->sh_size);
    text->addr_src = (uintptr_t)text_section->sh_addr, // not a real pointer
    text->size = text_section->sh_size,
    reader->seek(reader->data, text_section->sh_offset);
    read_obj(text->buf, text_section->sh_size, reader);

    // Get the relocation addresses.
    #ifdef MP_PLAT_COMMIT_EXEC
    text->addr_dst = (uintptr_t)MP_PLAT_COMMIT_EXEC(text->buf, 0); // only take the next pointer, don't commit
    #else
    text->addr_dst = (uintptr_t)text->buf;
    #endif

    // Read the readonly part.
    readonly->buf = m_new(byte, readonly->size);
    reader->seek(reader->data, ro_start);
    read_obj(readonly->buf, readonly->size, reader);
    readonly->addr_dst = (uintptr_t)readonly->buf;

    // Find the module_* table.
    elf32_sym_t *module_sym = NULL;
    for (size_t i=1; i < dynsym_section->sh_size / sizeof(elf32_sym_t); i++) {
        if (ELF32_ST_BIND(dynsym[i].st_info) != STB_GLOBAL) {
            continue; // not accessible for us
        }
        if (ELF32_ST_TYPE(dynsym[i].st_info) != STT_OBJECT) {
            continue; // not an 'object' (constant/variable)
        }
        if (strncmp("module_", &dynstr[dynsym[i].st_name], 7) != 0) {
            continue; // does not start with "module_"
        }
        // TODO: check whether this is the right object (name is
        // "module_<name>")
        module_sym = &dynsym[i];
    }
    if (module_sym == NULL || module_sym->st_shndx >= header.e_shnum) {
        raise_err(); // TODO: valid ELF (usually), but doesn't have module entries.
    }

    // Find the (hopefully!) already loaded address of the module_* table.
    mp_dyn_module_t *module_entries = (void*)relocate_address(module_sym->st_value, module_sym->st_size, memories, sizeof(memories)/sizeof(memorybuf_t), false);
    if (module_entries == NULL) {
        raise_err();
    }

    // Apply all relocations and look up exported module names.
    for (size_t i=1; i<header.e_shnum; i++) {
        elf32_shdr_t *section = &sections[i];
        if (section->sh_type != SHT_RELA) {
            continue;
        }
        // Read relocations
        elf32_rela_t relocations[section->sh_size / sizeof(elf32_rela_t)];
        reader->seek(reader->data, section->sh_offset);
        read_obj(relocations, section->sh_size, reader);
        for (int j=0; j<section->sh_size / sizeof(elf32_rela_t); j++) {
            elf32_rela_t *relocation = &relocations[j];
            size_t sym = ELF32_R_SYM(relocation->r_info);
            elf32_sym_t *symbol = NULL;
            if (sym != 0 && sym < dynsym_section->sh_size / sizeof(elf32_sym_t)) {
                symbol = &dynsym[sym];
                printf("relocating %4x %s...\n", relocation->r_offset, &dynstr[symbol->st_name]);
            } else {
                printf("relocating %4x ???...\n", relocation->r_offset);
            }


            // See for more details:
            // http://wiki.linux-xtensa.org/index.php/ELF_Relocation_Notes
            uintptr_t rel_addr = relocate_address(relocation->r_offset, sizeof(elf32_addr_t), memories, sizeof(memories)/sizeof(memorybuf_t), false);
            unsigned char r_type = ELF32_R_TYPE(relocation->r_info);
            uintptr_t rel_value;

            if (r_type == R_XTENSA_GLOB_DAT) {
                if (!symbol) {
                    raise_err();
                }
                rel_value = relocate_address(symbol->st_value, symbol->st_size, memories, sizeof(memories)/sizeof(memorybuf_t), true);

            } else if (r_type == R_XTENSA_JMP_SLOT) {
                if (!symbol) {
                    raise_err();
                }
                if (symbol->st_value != 0) {
                    // Relocation of our own symbol (calling a function
                    // within the library).
                    rel_value = relocate_address(symbol->st_value, symbol->st_size, memories, sizeof(memories)/sizeof(memorybuf_t), true);
                } else {
                    // Relocation of unknown symbol. We must provide this
                    // symbol, or fail.
                    switch (qstr_find_strn(&dynstr[symbol->st_name], strlen(&dynstr[symbol->st_name]))) {
                        case MP_QSTR_mp_obj_new_int:
                            rel_value = (uintptr_t)mp_obj_new_int + relocation->r_addend;
                            break;
                        case MP_QSTR_mp_obj_get_int:
                            rel_value = (uintptr_t)mp_obj_get_int + relocation->r_addend;
                            break;
                        case MP_QSTR_mp_obj_get_float:
                            rel_value = (uintptr_t)mp_obj_get_float + relocation->r_addend;
                            break;
                        //case MP_QSTR___floatsisf:
                        //    rel_value = (uintptr_t)__floatsisf + relocation->r_addend;
                        //    break;
                        //case MP_QSTR___mulsf3:
                        //    rel_value = (uintptr_t)__mulsf3 + relocation->r_addend;
                        //    break;
                        //case MP_QSTR___addsf3:
                        //    rel_value = (uintptr_t)__addsf3 + relocation->r_addend;
                        //    break;
                        //case MP_QSTR___fixunssfsi:
                        //    rel_value = (uintptr_t)__fixunssfsi + relocation->r_addend;
                        //    break;
                        default:
                            nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ImportError,
                                "relocation failed: unknown symbol %s", &dynstr[symbol->st_name]));
                            break;
                    }
                }
            } else if (r_type == R_XTENSA_RTLD) {
                // Apparently not necessary.
                continue;
            //} else if (r_type == R_XTENSA_RELATIVE) {
                // TODO
                //continue;
            } else {
                raise_err(); // unknown relocation
            }

            if (rel_addr == 0 || rel_value == 0) {
                raise_err();
            }

            // Apply relocation.
            // Hopefully all relocations are aligned...
            *(uint32_t*)(void*)rel_addr = rel_value;

            // Is this relocation in the module table?
            if (r_type == R_XTENSA_GLOB_DAT && relocation->r_offset >= module_sym->st_value && relocation->r_offset < module_sym->st_value + module_sym->st_size) {
                // Find the address of the module entry from the
                // relocated address.
                mp_dyn_module_t *module_entry = (mp_dyn_module_t*)(((uint32_t*)rel_addr) - 1);
                qstr name = qstr_from_str(&dynstr[symbol->st_name]);
                mp_obj_t obj;
                switch (module_entry->type) {
                    case TYPE_CONST_INT:
                        obj = mp_obj_new_int(*(mp_int_t*)rel_value);
                        break;
                    case TYPE_FUNC_VAR:
                        {
                            mp_obj_fun_elf_t *funobj = m_new(mp_obj_fun_elf_t, 1);
                            funobj->base.type = &mp_type_fun_elf_var;
                            funobj->fun._var = (void*)rel_value;
                            obj = funobj;
                        }
                        break;
                    case TYPE_FUNC_2_INT:
                        {
                            mp_obj_fun_elf_t *funobj = m_new(mp_obj_fun_elf_t, 1);
                            funobj->base.type = &mp_type_fun_elf_2;
                            funobj->fun._2 = (void*)rel_value;
                            obj = funobj;
                        }
                        break;
                    default:
                        obj = mp_const_none;
                        break;
                }
                mp_store_attr(module_obj, name, obj);
            }
        }
    }

    #ifdef MP_PLAT_COMMIT_EXEC
    uintptr_t addr_dst = (uintptr_t)MP_PLAT_COMMIT_EXEC(text->buf, text->size);
    if ((void*)addr_dst != text->buf) {
        m_free(text->buf);
    }
    if (addr_dst != text->addr_dst) {
        mp_raise_msg(&mp_type_RuntimeError, NULL);
    }
    #endif
}

void mp_elf_load_file(mp_obj_t module_obj, char *filename) {
    mp_reader_t reader;
    mp_reader_new_file(&reader, filename);
    mp_elf_load(module_obj, &reader);
    reader.close(reader.data);
}

#endif // MICROPY_MODULE_ELF
