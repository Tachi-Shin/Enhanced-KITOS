#!/usr/bin/env python3
"""Minimal v17 DTB fixtures for memory reservation and allocation failure tests."""
import struct


def create_dtb(path, small_memory=False):
    strings = bytearray()
    structure = bytearray()

    def word(value):
        structure.extend(struct.pack('>I', value))

    def begin(name):
        word(1)
        structure.extend(name.encode() + b'\0')
        structure.extend(b'\0' * (-len(structure) % 4))

    def end():
        word(2)

    def prop(name, value):
        offset = len(strings)
        strings.extend(name.encode() + b'\0')
        word(3)
        word(len(value))
        word(offset)
        structure.extend(value)
        structure.extend(b'\0' * (-len(structure) % 4))

    def cells(*values):
        return struct.pack('>' + 'I' * len(values), *values)

    begin('')
    prop('#address-cells', cells(2))
    prop('#size-cells', cells(2))
    prop('compatible', b'raspberrypi,4-model-b\0brcm,bcm2711\0')
    begin('memory@0')
    prop('device_type', b'memory\0')
    prop('reg', struct.pack('>QQ', 0, 0x3c000000))
    end()
    begin('reserved-memory')
    prop('#address-cells', cells(2))
    prop('#size-cells', cells(2))
    prop('ranges', b'')
    base, size = (0x01000000, 0x3b000000) if small_memory else (0x02000000, 4096)
    begin(f'fixture@{base:x}')
    prop('reg', struct.pack('>QQ', base, size))
    prop('no-map', b'')
    end()
    end()
    begin('chosen')
    # These pages are deliberately reserved even without loading an initrd.
    prop('linux,initrd-start', cells(0x00400000))
    prop('linux,initrd-end', cells(0x00401000))
    end()
    end()
    word(9)
    reservations = struct.pack('>QQQQ', 0x00500000, 4096, 0, 0)
    structure_offset = 40 + len(reservations)
    strings_offset = structure_offset + len(structure)
    total = strings_offset + len(strings)
    header = struct.pack('>10I', 0xd00dfeed, total, structure_offset,
                         strings_offset, 40, 17, 16, 0, len(strings), len(structure))
    path.write_bytes(header + reservations + structure + strings)
