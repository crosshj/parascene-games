#!/usr/bin/env python3
"""Reject console executables that consume the stack/heap reserve in 2 MB RAM.

The SDK's default linker region is larger than retail PS1 RAM. Check ELF load
segments including BSS, not the smaller EXE/disc file or debug information.
"""
import struct
import sys
from pathlib import Path


def check(path):
    data = Path(path).read_bytes()
    if data[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('expected ELF32 little-endian executable')
    phoff = struct.unpack_from('<I', data, 28)[0]
    entsize, count = struct.unpack_from('<HH', data, 42)
    high = 0
    for i in range(count):
        kind, _, address, _, _, size, _, _ = struct.unpack_from('<8I', data, phoff + i * entsize)
        if kind == 1 and size:
            if address < 0x80010000:
                raise ValueError('load segment overlaps reserved low RAM')
            high = max(high, address + size)
    limit = 0x801C0000  # Keep the upper 256 KB for stack/heap/runtime headroom.
    if not high or high > limit:
        raise ValueError(f'load end {high:#x} exceeds retail RAM budget {limit:#x}')
    print(f'RAM {Path(path).name}: image+BSS {high - 0x80010000} bytes; '
          f'{0x80200000 - high} bytes above image (256 KB reserved)')


if __name__ == '__main__':
    try:
        check(sys.argv[1])
    except (ValueError, struct.error) as error:
        raise SystemExit(f'RAM budget: {error}')
