#!/usr/bin/env python3
"""Create disposable FAT32 fixtures; never open a block device or an existing image."""
import argparse
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

SIZE = 128 * 1024 * 1024

def create(path, offset=0, sector_size=512):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as f:
        f.truncate(SIZE if sector_size == 512 else 512 * 1024 * 1024)
    target = str(path) + (f'@@{offset * 512}' if offset else '')
    sectors = path.stat().st_size // sector_size - offset * 512 // sector_size
    subprocess.run(['mformat', '-i', target, '-F', '-M', str(sector_size), '-T', str(sectors), '-v', 'KITOSTEST', '::'], check=True)
    if offset:
        with path.open('r+b') as f:
            mbr = bytearray(512)
            mbr[450] = 0x0c
            struct.pack_into('<II', mbr, 454, offset, path.stat().st_size // 512 - offset)
            mbr[510:] = b'\x55\xaa'
            f.write(mbr)
    for source, name in [('hello.txt', 'HELLO.TXT'), ('long-name.txt', 'Long File Name.txt'), ('mbr.txt', 'MBRFILE.TXT')]:
        subprocess.run(['mcopy', '-i', target, str(Path(__file__).resolve().parent.parent / 'tests/fixtures' / source), '::' + name], check=True)

def suite(directory, sector_size=512):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    # Replace only generated test artifacts, not caller-specified SD media.
    with tempfile.TemporaryDirectory(prefix='fixtures-', dir=directory) as tmp:
        for name, offset in [('direct.img', 0), ('mbr.img', 2048)]:
            create(Path(tmp) / name, offset, sector_size)
            shutil.move(str(Path(tmp) / name), directory / name)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--suite', action='store_true')
    parser.add_argument('--sector-size', type=int, choices=[512, 1024, 2048, 4096], default=512)
    parser.add_argument('path')
    args = parser.parse_args()
    if args.suite: suite(args.path, args.sector_size)
    else: create(args.path, sector_size=args.sector_size)
