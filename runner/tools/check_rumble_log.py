#!/usr/bin/env python3
"""Validate the standalone rumble test against DuckStation's Debug-level log."""
import re
import sys
from pathlib import Path


def check(text):
    start = text.index('RUMBLE TEST begin BIOS=1')
    end = text.index('RUMBLE TEST done', start)
    trace = text[start:end]
    requested = re.findall(r'RUMBLE TEST tick=\d+ requested=([01]) pad_status=00', trace)
    received = re.findall(r'Set small motor to ([01]), large motor to 0', trace)
    expected = ['1', '0'] * 4
    if requested != expected:
        raise ValueError(f'Expected four requested pulses, got {requested}')
    if received != expected:
        raise ValueError(f'Expected four emulator motor pulses, got {received}')


if __name__ == '__main__':
    try:
        check(Path(sys.argv[1]).read_text(errors='replace'))
    except (ValueError, IndexError) as exc:
        sys.exit(f'FAIL: {exc}')
    print('PASS: DuckStation received all four motor pulses and their stop commands.')
