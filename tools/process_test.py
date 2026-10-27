"""ELF/Newlib integration on disposable FAT media; called by qemu_test.py."""
from pathlib import Path
import re
import struct
import subprocess
import tempfile


def stage(disk, executable):
    data = Path(executable).read_bytes()
    subprocess.run(['mcopy', '-i', str(disk), str(executable), '::PROBE.ELF'], check=True)
    bad = {}
    b = bytearray(data); b[0] = 0; bad['BADMAG.ELF'] = b
    b = bytearray(data); struct.pack_into('<H', b, 18, 62); bad['BADARCH.ELF'] = b
    ph = struct.unpack_from('<Q', data, 32)[0]
    b = bytearray(data); struct.pack_into('<I', b, ph + 4, 7); bad['BADWX.ELF'] = b
    b = bytearray(data); struct.pack_into('<Q', b, ph + 16, 0xfffffffffffff000); bad['BADRNG.ELF'] = b
    b = bytearray(data); struct.pack_into('<H', b, 16, 3); bad['DYNAMIC.ELF'] = b
    bad['TRUNC.ELF'] = data[:20]
    # Valid small file whose RW BSS exhausts the 16 MiB memory fixture midway.
    b = bytearray(0x2010)
    b[:7] = b'\x7fELF\x02\x01\x01'
    struct.pack_into('<HHIQQQIHHHHHH', b, 16, 2, 183, 1, 0x400000, 64, 0, 0, 64, 56, 2, 0, 0, 0)
    struct.pack_into('<IIQQQQQQ', b, 64, 1, 5, 0x1000, 0x400000, 0, 4, 4, 4096)
    struct.pack_into('<IIQQQQQQ', b, 120, 1, 6, 0x2000, 0x500000, 0, 16, 15 * 1024 * 1024, 4096)
    struct.pack_into('<I', b, 0x1000, 0xd4000001)  # svc #0 -> exit(argc)
    bad['OOM.ELF'] = b
    with tempfile.TemporaryDirectory(prefix='elf-fixtures-', dir=Path(disk).parent) as temp:
        for name, content in bad.items():
            path = Path(temp) / name; path.write_bytes(content)
            subprocess.run(['mcopy', '-i', str(disk), str(path), '::' + name], check=True)


def free_pages(machine):
    return int(re.search(r'free=(\d+)', machine.command('mem')).group(1))


def run(machine, small=False):
    baseline = free_pages(machine)
    for name in ('BADMAG', 'BADARCH', 'BADWX', 'BADRNG', 'DYNAMIC', 'TRUNC'):
        machine.command(f'run /{name}.ELF', 'error -8', allow_error=True)
        assert free_pages(machine) == baseline, name + ': load failure leaked pages'
    if small:
        machine.command('run /OOM.ELF', 'error -12', allow_error=True)
        assert free_pages(machine) == baseline, 'ELF OOM rollback leaked pages'
    for _ in range(2):
        text = machine.command('run /PROBE.ELF hello', '[newlib] ELF/Newlib PASS')
        assert 'float=3.125' in text and '[newlib] atexit PASS' in text and 'status=0' in text
        assert 'orphan/FD-cleanup PASS' in text
        assert free_pages(machine) == baseline, 'process termination leaked pages'
    ids = []
    for seed in (1, 2):
        text = machine.command(f'spawn /PROBE.ELF fp {seed}', '[process] pid=')
        ids.append(int(re.search(r'\[process\] pid=(\d+)', text).group(1)))
    for pid in ids:
        machine.command(f'wait {pid}', 'status=0')
    assert free_pages(machine) == baseline, 'concurrent FP processes leaked pages'
    assert 'pid=' not in machine.command('procs')
    machine.command('cat /USER.TXT', 'Newlib file IO 42')
