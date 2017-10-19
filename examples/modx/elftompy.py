"""
This tool is ultimately intended to convert a .elf to a .mpy file.
But for now all it does is patch the text size within a .mpy file.

Usage (after building modx.mpy):

    $ python elftompy.py modx.mpy

This will make modx.mpy ready for importing.
"""

import sys
import struct

def to_uint(n):
    if n < 0:
        raise ValueError('unsigned integer cannot be < 0')
    b = bytearray()
    while n:
        b.insert(0, (n & 0x7f) | 0x80)
        n >>= 7
    if len(b) == 0:
        return b'\x00'
    b[-1] &= 0x7f # clear high bit: it's the last byte
    return bytes(b)

def align(n, a):
    return (n + a - 1) // a * a

def process_file(filename):
    with open(filename, 'rb') as f:
        header = f.read(6)
        num_qstrs = struct.unpack('H', f.read(2))[0]
        qstr_strings_len = struct.unpack('Q', f.read(8))[0]
        qstr_buffer_len = align(qstr_strings_len, 8)
        qstrbuf = f.read(qstr_buffer_len)[:qstr_strings_len]
        text = f.read()
    qstrs = qstrbuf.rstrip('\0').split(b'\0')
    if len(qstrs) != num_qstrs:
        raise ValueError('expected len(qstrs) == num_qstrs')
    with open(filename, 'wb') as f:
        f.write(header)
        f.write(to_uint(num_qstrs))
        for qstr in qstrs:
            f.write(to_uint(len(qstr)))
            f.write(qstr)
        f.write(to_uint(len(text)))
        f.write(text)

if __name__ == '__main__':
    process_file(sys.argv[1])
