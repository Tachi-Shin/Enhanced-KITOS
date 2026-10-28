#!/usr/bin/env python3
"""Boot-module, Unix root, real ELF shell/utilities and syscall integration."""
import argparse
from pathlib import Path
import re
import struct
import subprocess
import tempfile
from qemu_test import Machine
from test_images import create
from rootfs import stage, DIRECTORIES
from process_test import free_pages


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--qemu',default='qemu-system-aarch64');p.add_argument('--kernel',required=True);a=p.parse_args()
    root=Path('build/tests');root.mkdir(exist_ok=True)
    transcript=bytearray()
    # These hardware implementations must be absent from the linked kernel.
    symbols=subprocess.check_output(['aarch64-elf-nm',a.kernel.replace('.img','.elf')]).decode()
    for name in ('sd_card_init','sdhci_init','bcm2711_gpio_ops','pl011_get_device','mini_uart_get_device',
                 'SetupGIC','dwc2_qemu_host','usb_hid_decode'):
        assert not re.search(r'\b'+name+r'$',symbols,re.M),name+' remained linked into kernel'
    with tempfile.TemporaryDirectory(prefix='unix-',dir=root) as tmp:
        tmp=Path(tmp);disk=tmp/'sd.img';create(disk,fixture_files=False);stage(disk)
        subprocess.run(['mcopy','-i',str(disk),'build/drivers/IRQTEST.EKM','::lib/modules/'],check=True)
        for name in ('bin/sh','bin/ls','bin/echo','sbin/lsmod','sbin/insmod'):
            target=tmp/'check.elf'
            subprocess.run(['mcopy','-o','-i',str(disk),'::'+name,str(target)],check=True)
            data=target.read_bytes();assert data[:7]==b'\x7fELF\x02\x01\x01'
            assert struct.unpack_from('<HH',data,16)==(2,183)
        m=Machine(a.qemu,a.kernel,disk)
        try:
            boot=m.prompt();assert '[shell] /bin/sh ready' in boot and 'recovery mode' not in boot
            assert b'\x1b[1;32mroot@ekitos' in m.log
            listing=m.command('ls /')
            for name in ('bin/','sbin/','etc/','lib/','boot/','dev/','proc/','tmp/','var/','home/','root/','mnt/','media/','usr/'):assert name in listing,name
            for name in DIRECTORIES:m.command('ls /'+name)
            assert b'\x1b[1;34mbin/' in m.log
            modules=m.command('lsmod');assert 'count=10 ABI=6' in modules
            for name in ('arm-timer','gicv2','bcm2711-gpio','bcm2711-mailbox','bcm2711-uart'):
                assert 'error -16' in m.command('rmmod '+name,allow_error=True)
            assert 'error -16' in m.command('rmmod bcm2711-sd',allow_error=True)
            for _ in range(3):
                out=m.command('insmod /lib/modules/IRQTEST.EKM','IRQ ownership/bounds/duplicate')
                assert 'Firmware module DMA/clock property' in out
                assert 'Firmware tag list/bounds/duplicate/reuse' in out
                m.command('rmmod irq-test','unloaded irq-test')
            m.command('mkdir -p /tmp/one/two')
            m.command('cd /tmp/one/two');m.command('pwd','/tmp/one/two')
            m.command('write relative.txt local-data');m.command('cat relative.txt','local-data')
            m.command('cd .././two/..');m.command('pwd','/tmp/one')
            m.command('cd /');m.command('cat /tmp/one/two/relative.txt','local-data')
            m.command("export GREETING='hello world'")
            m.command('echo "$GREETING"','hello world')
            m.command("echo '$GREETING'",'$GREETING')
            m.command('false');assert '\r\n1\r\n' in m.command('echo $?')
            m.command('true');assert '\r\n0\r\n' in m.command('echo $?')
            m.command("sh -c 'echo $GREETING'",'hello world')
            m.command('export PATH=/sbin')
            assert 'error -2' in m.command('echo not-found',allow_error=True)
            m.command('/bin/echo explicit-path','explicit-path')
            m.command('export PATH=/bin:/sbin:/usr/bin:/usr/sbin')
            m.command('mv /bin/echo /bin/echo.off')
            assert 'error -2' in m.command('echo no-builtin-fallback',allow_error=True)
            m.command('/bin/mv /bin/echo.off /bin/echo')
            m.command('echo restored','restored')
            m.command('cp /tmp/one/two/relative.txt /tmp/copied')
            assert 'error -22' in m.command('cp /tmp/copied /tmp/./COPIED',allow_error=True)
            m.command('cat /tmp/copied','local-data')
            m.command('write /tmp/notelf abc')
            assert 'error -8' in m.command('/tmp/notelf',allow_error=True)
            m.command('echo unsupported && cat /etc/hostname','unsupported operator')
            m.command('export NO_COLOR=1');mark=len(m.log);m.command('ls /')
            assert b'\x1b[' not in m.log[mark:]
            m.command('unset NO_COLOR')
            m.command('unix-probe','cwd/inheritance/isolation/readdir/mutation/uaccess PASS')
            baseline=free_pages(m)
            for _ in range(8):m.command('echo process-reuse','process-reuse')
            assert free_pages(m)==baseline,'ELF utility exit leaked physical pages'
            m.command('selftest','preempt/sleep PASS');m.command('memtest','MMU/page/VM/uaccess/reclaim PASS')
            print('Boot .EKM/ownership/IRQ pin + Unix root + colored EL0 sh/PATH/cwd/environment/ELF utilities PASS',flush=True)
        finally:m.close();transcript.extend(m.log)
        # Missing sh uses the recovery shell; it must not silently claim that
        # the normal utility files are running.
        subprocess.run(['mdel','-i',str(disk),'::bin/sh'],check=True)
        m=Machine(a.qemu,a.kernel,disk)
        try:
            boot=m.prompt();assert '[shell] recovery mode' in boot
            assert '/bin/sh: file not found (error -2)' in boot
            m.command('lsmod','count=10 ABI=6');m.command('echo recovery','recovery')
        finally:m.close();transcript.extend(m.log)
        invalid=tmp/'not-elf';invalid.write_bytes(b'not an AArch64 ELF executable')
        subprocess.run(['mcopy','-i',str(disk),str(invalid),'::bin/sh'],check=True)
        m=Machine(a.qemu,a.kernel,disk)
        try:
            boot=m.prompt();assert '/bin/sh: invalid AArch64 ELF (error -8)' in boot
            m.command('echo invalid-elf-recovery','invalid-elf-recovery')
        finally:m.close();transcript.extend(m.log)
    m=Machine(a.qemu,a.kernel,None)
    try:
        assert 'unavailable; shell remains available' in m.prompt()
        m.command('lsmod','count=10 ABI=6');m.command('selftest','preempt/sleep PASS')
    finally:m.close();transcript.extend(m.log)
    (root/'unix-shell-transcript.txt').write_bytes(transcript)
    print('No-media / missing-shell recovery with module-provided UART/timer PASS',flush=True)


if __name__=='__main__':main()
