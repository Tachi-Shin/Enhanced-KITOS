#!/usr/bin/env python3
"""Embed exact .EKM files as read-only boot resources, not linked driver code."""
from pathlib import Path
import sys
out = Path(sys.argv[1])
text = ['.section .rodata.boot_modules,"a",@progbits', '.balign 8']
for arg in sys.argv[2:]:
    p = Path(arg).resolve()
    if not p.stem.isalnum(): raise SystemExit('invalid module name')
    name = 'boot_' + p.stem
    text += [f'.global {name}_start, {name}_end', f'{name}_start:',
             f'.incbin "{p}"', f'{name}_end:', '.balign 8']
out.write_text('\n'.join(text) + '\n')
