"""
This tool is ultimately intended to convert a .elf to a .mpy file.
But for now all it does is patch the text size within a .mpy file.

Usage (after building modx.mpy):

    $ python elftompy.py modx.mpy

This will make modx.mpy ready for importing.
"""

import sys
import struct

SHT_NULL     = 0
SHT_PROGBITS = 1
SHT_SYMTAB   = 2
SHT_STRTAB   = 3
SHT_RELA     = 4
SHT_HASH     = 5
SHT_DYNAMIC  = 6
SHT_NOTE     = 7
SHT_NOBITS   = 8
SHT_REL      = 9
SHT_SHLIB    = 10
SHT_DYNSYM   = 11

ISA_X86_64 = 0x3E
ISA_ARM    = 0x28
ISA_XTENSA = 0x5E

R_XTENSA_32         = 1
R_XTENSA_RTLD       = 2
R_XTENSA_GLOB_DAT   = 3
R_XTENSA_JMP_SLOT   = 4
R_XTENSA_RELATIVE   = 5
R_XTENSA_PLT        = 6
R_XTENSA_ASM_EXPAND = 11
R_XTENSA_SLOT0_OP   = 20

def to_uint(n):
    if n < 0:
        raise ValueError('unsigned integer cannot be < 0: %r' % n)
    b = bytearray()
    while n:
        b.insert(0, (n & 0x7f) | 0x80)
        n >>= 7
    if len(b) == 0:
        return b'\x00'
    b[-1] &= 0x7f # clear high bit: it's the last byte
    return bytes(b)

class Ident:
    def __init__(self, ident):
        self.data = ident
        self.i_magic   = ident[0]
        self.i_class   = ident[1]
        self.i_data    = ident[2]
        self.i_version = ident[3]
        self.i_osabi   = ident[4]

    @property
    def is_64bit(self):
        return self.i_class == 2

class Header:
    def __init__(self, header, ident):
        self.data = header
        self.ident = ident
        self.e_type      = header[0]
        self.e_machine   = header[1]
        self.e_version   = header[2]
        self.e_entry     = header[3]
        self.e_phoff     = header[4]
        self.e_shoff     = header[5]
        self.e_flags     = header[6]
        self.e_ehsize    = header[7]
        self.e_phentsize = header[8]
        self.e_phnum     = header[9]
        self.e_shentsize = header[10]
        self.e_shnum     = header[11]
        self.e_shstrndx  = header[12]

    @property
    def is_64bit(self):
        return self.ident.is_64bit

class Section:
    def __init__(self, section):
        self.relocations = None
        self.data = section
        self.sh_name      = section[0]
        self.sh_type      = section[1]
        self.sh_flags     = section[2]
        self.sh_addr      = section[3]
        self.sh_offset    = section[4]
        self.sh_size      = section[5]
        self.sh_link      = section[6]
        self.sh_info      = section[7]
        self.sh_addralign = section[8]
        self.sh_entsize   = section[9]

    def read_strtab(self, f):
        if self.sh_type != 3: # SHT_STRTAB
            raise ValueError('string section has wrong type')
        if self.sh_offset == 0: # sh_offset
            raise ValueError('string section has no sh_offset')
        if self.sh_size == 0: # sh_size
            raise ValueError('string section has no sh_size')
        f.seek(self.sh_offset)
        string_buf = bytearray(f.read(self.sh_size))
        strings = {}
        last_index = 1
        dots = []
        for i in range(1, len(string_buf)):
            if string_buf[i] == 0:
                strings[last_index] = string_buf[last_index:i].decode('ascii')
                last_index = i+1
                for dot in dots:
                    strings[dot] = string_buf[dot:i].decode('ascii')
                dots = []
            if string_buf[i] == ord('.'):
                dots.append(i)
        return strings

    def read_relocations(self, f, header):
        if self.sh_type != SHT_RELA:
            raise ValueError('not a .rela section')
        f.seek(self.sh_offset)
        relocations = []
        if header.is_64bit:
            for i in range(self.sh_size // 24):
                relocations.append(Relocation(struct.unpack('QQq', f.read(24)), header))
        else:
            for i in range(self.sh_size // 12):
                relocations.append(Relocation(struct.unpack('IIi', f.read(12)), header))
        return relocations

    def read(self, f):
        f.seek(self.sh_offset)
        return f.read(self.sh_size)

class Relocation:
    def __init__(self, relocation, header):
        self.data = relocation
        self.r_offset = relocation[0]
        self.r_info   = relocation[1]
        if header.is_64bit:
            self.r_sym    = self.r_info >> 32
            self.r_type   = self.r_info & 0xffffffff
        else:
            self.r_sym    = self.r_info >> 8
            self.r_type   = self.r_info & 0xff
        if len(relocation) >= 3:
            self.r_addend = relocation[2]
        else:
            self.r_addend = None

def read_sections(f, header):
    f.seek(header.e_shoff)
    sections = []
    for i in range(header.e_shnum):
        if header.is_64bit:
            section = Section(struct.unpack('IIQQQQIIQQ', f.read(header.e_shentsize)))
        else:
            section = Section(struct.unpack('IIIIIIIIII', f.read(header.e_shentsize)))
        sections.append(section)

    shstrndx = sections[header.e_shstrndx].read_strtab(f)
    return {shstrndx[s.sh_name]: s for s in sections[1:]}

def create_mpy(elfpath, mpypath):
    f = open(elfpath, 'rb')

    ident = Ident(struct.unpack('4sbbbb8x', f.read(16)))
    if ident.i_magic != b'\x7fELF':
        raise ValueError('not an ELF file')
    if ident.i_class not in [1, 2]:
        raise ValueError('not 32-bit or 64-bit')
    if ident.i_data != 1:
        raise ValueError('not little endian')
    if ident.i_version != 1:
        raise ValueError('not version 1 ELF')

    if ident.is_64bit:
        header = Header(struct.unpack('HHIQQQIHHHHHH', f.read(48)), ident)
    else:
        header = Header(struct.unpack('HHIIIIIHHHHHH', f.read(36)), ident)
    if header.e_version != 1:
        raise ValueError('not version 1 ELF')

    sections = read_sections(f, header)

    section_all = bytearray(sections['.all'].read(f))
    header_len = 16

    relocations = None

    if header.e_machine == ISA_XTENSA:
        relocations = []
        # Useful resources:
        # http://elixir.free-electrons.com/linux/latest/source/arch/xtensa/kernel/module.c#L72
        # http://wiki.linux-xtensa.org/index.php/ELF_Relocation_Notes
        # http://0x04.net/~mwk/doc/xtensa.pdf
        for r in sections['.rela.all'].read_relocations(f, header):
            if r.r_type == R_XTENSA_32:
                if r.r_offset % 4 != 0:
                    raise ValueError('relocation offset isn\'t aligned')
                # addend appears to be off, so we have to use what's already
                # stored
                value = struct.unpack('I', section_all[r.r_offset:r.r_offset+4])[0]
                value -= header_len # value is now relative to start of data, not the .all section
                # insert relative relocation
                section_all[r.r_offset:r.r_offset+4] = struct.pack('I', value)
                if r.r_offset >= header_len:
                    relocations.append((r.r_offset - header_len) // 4)
                else:
                    pass # init() pointer
            elif r.r_type == R_XTENSA_SLOT0_OP:
                # appears to be unnecessary (L32R, jumps, etc.)
                continue
            else:
                print('WARNING: unknown relocation type %d' % r.r_type)

    mpyheader, num_qstrs, init_address = struct.unpack('6sHQ', section_all[:header_len])
    data = section_all[header_len:]

    init_offset = None
    if header.e_machine == ISA_XTENSA:
        init_offset = init_address

    with open(mpypath, 'wb') as f:
        f.write(mpyheader)
        f.write(to_uint(num_qstrs))
        f.write(to_uint(len(data)))
        f.write(data)
        if relocations is not None:
            f.write(to_uint(len(relocations)))
            for r in relocations:
                f.write(to_uint(r))
        if init_offset is not None:
            f.write(to_uint(init_offset))

if __name__ == '__main__':
    elfpath = sys.argv[1]
    mpypath = sys.argv[2]
    create_mpy(elfpath, mpypath)
