#!/usr/bin/env python3
"""End-to-end UART, timer, scheduler and SD/FAT32 regression on disposable media."""
import argparse
from pathlib import Path
import selectors
import shutil
import subprocess
import tempfile
import time
import re
from test_images import create
from boot_fixture import create_dtb
import process_test

class Machine:
    def __init__(self, qemu, kernel, disk, dtb=None):
        command = [qemu, '-M', 'raspi4b', '-m', '2g', '-display', 'none',
            '-monitor', 'none', '-kernel', kernel, '-serial', 'null', '-serial', 'stdio']
        if disk is not None: command += ['-drive', f'if=sd,file={disk},format=raw']
        if dtb is not None: command += ['-dtb', str(dtb)]
        self.p = subprocess.Popen(command, stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.p.stdout, selectors.EVENT_READ)
        self.log = bytearray()
    def prompt(self, timeout=15):
        data = bytearray(); end = time.monotonic() + timeout
        while time.monotonic() < end:
            for key, _ in self.selector.select(.2):
                chunk = key.fileobj.read1(8192)
                if not chunk: raise RuntimeError('QEMU exited: ' + data.decode(errors='replace'))
                data.extend(chunk); self.log.extend(chunk)
                if b'[shell] /bin/sh ready' in data or b'\x1b]133;' in data:self.normal_shell=True
                ready=b'\x1b]133;B\x07' in data
                clean=re.sub(rb'\x1b\][^\x07]*(?:\x07)',b'',data)
                clean=re.sub(rb'\x1b\[[0-9;?]*[A-Za-z]',b'',clean)
                if ready or (not getattr(self,'normal_shell',False) and re.search(rb'root@ekitos:[^\r\n]*# ',clean)):
                    return clean.decode(errors='replace')
        raise RuntimeError('QEMU timeout: ' + data.decode(errors='replace'))
    def command(self, text, expect=None, allow_error=False, timeout=15):
        self.p.stdin.write(text.encode() + b'\r'); self.p.stdin.flush()
        result = self.prompt(timeout)
        if ('error' in result and not allow_error) or 'FAIL' in result or (expect and expect not in result):
            raise AssertionError(f'{text}: {result}')
        return result
    def close(self):
        self.p.terminate()
        try: self.p.wait(timeout=3)
        except subprocess.TimeoutExpired: self.p.kill(); self.p.wait()
        self.selector.close()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu', default='qemu-system-aarch64')
    parser.add_argument('--kernel', required=True)
    parser.add_argument('--user-elf', default='build/usr/probe.elf')
    args = parser.parse_args()
    directory = Path('build/tests'); directory.mkdir(parents=True, exist_ok=True)
    transcript = bytearray()
    with tempfile.TemporaryDirectory(prefix='qemu-', dir=directory) as temp:
        disk = Path(temp) / 'sd.img'; create(disk)
        process_test.stage(disk, args.user_elf)
        machine = Machine(args.qemu, args.kernel, disk)
        try:
            boot = machine.prompt()
            assert 'FAT32 ready via VFS' in boot and 'TTBR1 kernel; TTBR0 empty' in boot
            process_test.run(machine)
            machine.command('help', 'write PATH TEXT')
            machine.command('echo serial-ok', 'serial-ok')
            machine.command('selftest', 'preempt/sleep PASS')
            machine.command('selftest', 'preempt/sleep PASS') # task slot reuse
            machine.command('memtest', 'MMU/page/VM/uaccess/reclaim PASS')
            machine.command('usertest', 'EL0 syscall/fault-isolation/reclaim PASS')
            machine.command('usertest', 'EL0 syscall/fault-isolation/reclaim PASS')
            machine.command('memtest', 'MMU/page/VM/uaccess/reclaim PASS')
            machine.command('time', 'irq=')
            machine.command('uart', 'drop=0 overrun=0')
            machine.command('cat HELLO.TXT', 'initial-data')
            machine.command('mkdir /regress')
            machine.command('write "/regress/Long Name.txt" "hello SD"')
            machine.command('append "/regress/Long Name.txt" " + VFS"')
            machine.command('cat "/regress/Long Name.txt"', 'hello SD + VFS')
            machine.command('truncate "/regress/Long Name.txt" 5')
            machine.command('cat "/regress/Long Name.txt"', 'hello')
            machine.command('mv "/regress/Long Name.txt" /PERSIST.TXT')
            machine.command('rmdir /regress')
            machine.command('ls /', 'PERSIST.TXT')
            machine.command('gpio 17 out')
            machine.command('gpio 17 1')
            machine.command('gpio 17 in')
            machine.command('gpio 17 up')
            machine.command('ps', '/bin/sh')
        finally:
            machine.close(); transcript.extend(machine.log)
            (directory / 'qemu-transcript.txt').write_bytes(transcript)
        machine = Machine(args.qemu, args.kernel, disk)
        try:
            assert 'FAT32 ready via VFS' in machine.prompt()
            machine.command('cat /PERSIST.TXT', 'hello')
            machine.command('cat /USER.TXT', 'Newlib file IO 42')
            machine.command('selftest', 'preempt/sleep PASS')
            machine.command('usertest', 'EL0 syscall/fault-isolation/reclaim PASS')
        finally:
            machine.close(); transcript.extend(machine.log)
            (directory / 'qemu-transcript.txt').write_bytes(transcript)
        host_file = Path(temp) / 'persist.txt'
        subprocess.run(['mcopy', '-i', str(disk), '::PERSIST.TXT', str(host_file)], check=True)
        assert host_file.read_bytes() == b'hello'
        fsck = shutil.which('fsck.fat') or ('/sbin/fsck_msdos' if Path('/sbin/fsck_msdos').exists() else None)
        if fsck: subprocess.run([fsck, '-n', str(disk)], check=True)
    machine = Machine(args.qemu, args.kernel, None)
    try:
        assert 'unavailable; shell remains available' in machine.prompt()
        machine.command('selftest', 'preempt/sleep PASS')
        machine.command('memtest', 'MMU/page/VM/uaccess/reclaim PASS')
        machine.command('usertest', 'EL0 syscall/fault-isolation/reclaim PASS')
    finally:
        machine.close(); transcript.extend(machine.log)
        (directory / 'qemu-transcript.txt').write_bytes(transcript)
    for small in (False, True):
        dtb = directory / ('small-memory.dtb' if small else 'reserved-memory.dtb')
        create_dtb(dtb, small_memory=small)
        media = tempfile.TemporaryDirectory(prefix='dtb-elf-', dir=directory)
        disk = Path(media.name) / 'sd.img'; create(disk)
        process_test.stage(disk, args.user_elf)
        machine = Machine(args.qemu, args.kernel, disk, dtb)
        try:
            boot = machine.prompt()
            assert 'TTBR1 kernel; TTBR0 empty' in boot and 'reservations=6' in boot
            machine.command('memtest', 'MMU/page/VM/uaccess/reclaim PASS')
            if small: machine.command('oomtest', 'page/VM OOM rollback PASS')
            process_test.run(machine, small=small)
            machine.command('usertest', 'EL0 syscall/fault-isolation/reclaim PASS')
            machine.command('selftest', 'preempt/sleep PASS')
        finally:
            machine.close(); transcript.extend(machine.log)
            (directory / 'qemu-transcript.txt').write_bytes(transcript)
            media.cleanup()
    print('QEMU boot/UART/IRQ/tasks/SD/VFS/persistence/no-media/MMU/EL0/DTB/OOM/ELF/Newlib/process/FP: PASS')

if __name__ == '__main__': main()
