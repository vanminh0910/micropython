#!/usr/bin/python3

from __future__ import print_function

import sys

def patch_isr_vector(inpath, outpath, vector_start, vector_end):
    # Resources:
    # https://en.wikipedia.org/wiki/Intel_HEX
    # http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.faqs/ka9903.html
    # read addresses from inpath
    vector = []
    for line in open(inpath, 'r'):
        line = bytearray.fromhex(line.strip()[1:])
        if not line:
            continue
        if line[3] != 0: # not a data record type
            continue
        if line[1] != 0x10: # check address
            # this data record isn't at address xxxx10xx where the ISR lies
            continue

        if line[0] != 16: # length of record: 16 bytes
            raise NotImplementedError('TODO: record lengths of different lengths')

        # read 4 words
        for i in range(4, 20, 4):
            vector.append(line[i:i+4])

        if len(vector) >= vector_end:
            break

    f = open(outpath, 'rb+')
    i_vect = 0
    # skip the first vector_start addresses: sp initial value and Reset_Handler
    while i_vect < vector_end:
        start = f.tell()
        line = f.readline()
        line = bytearray.fromhex(line.rstrip()[1:].decode('utf-8'))
        if not line:
            break
        if line[3] != 0: # data record type
            continue
        if line[0] != 16: # length of record: 16 bytes
            raise NotImplementedError('TODO: record lengths of different lengths')

        for i in range(4, 20, 4):
            existing = line[i:i+4]
            if i_vect >= vector_start and i_vect < vector_end:
                #print('write %2i %s %s' % (i_vect, existing.hex(), vector[i_vect].hex()))
                line[i:i+4] = vector[i_vect]
            else:
                pass
                #print('skip  %2i %s %s' % (i_vect, existing.hex(), vector[i_vect].hex()))
            i_vect += 1

        # calculate checksum
        checksum = 0
        for c in line[:-1]:
            checksum += c
        checksum = 256 - checksum % 256
        line[-1] = checksum

        f.seek(start+1, 0)
        f.write(line.hex().upper().encode('utf-8'))
        f.readline()

        # write back line
    f.close()

if __name__ == '__main__':
    patch_isr_vector(sys.argv[1], sys.argv[2], 2, int(sys.argv[3]))
