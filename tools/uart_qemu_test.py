#!/usr/bin/env python3
"""Run the hardware PL011/GIC .EKM path and prove input survives masked RX IRQs.

QEMU raspi4b has no PCIe root complex. A temporary kernel fixture replaces only
the embedded PI4USB.EKM resource with HELLO.EKM, preventing unsupported PCIe
MMIO. The production hardware UART, GIC, MMU, scheduler and shell are unchanged.
"""
import argparse
import os
from pathlib import Path
import re
import select
import subprocess
import tempfile
import time
from usb_qemu_test import USBMachine


def kernel_fixture(source, target):
    symbols={}
    for line in subprocess.check_output(['aarch64-elf-nm',str(source.with_suffix('.elf'))]).decode().splitlines():
        parts=line.split()
        if len(parts)==3:symbols[parts[2]]=int(parts[0],16)
    base=0xffff000000000000+symbols['_start']
    start=symbols['boot_PI4USB_start']-base;end=symbols['boot_PI4USB_end']-base
    data=bytearray(source.read_bytes());stub=Path('build/drivers/HELLO.EKM').read_bytes()
    assert 0<=start<end<=len(data) and len(stub)<=end-start
    data[start:end]=stub+b'\0'*(end-start-len(stub));target.write_bytes(data)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--qemu',default='qemu-system-aarch64')
    p.add_argument('--kernel',default='build/rpi4/hardware/kernel8.img');a=p.parse_args()
    root=Path('build/tests');root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='uart-pl011-',dir=root) as directory:
        directory=Path(directory);kernel=directory/'kernel.img';kernel_fixture(Path(a.kernel),kernel)
        qt=directory/'qtest'
        for suffix in ('.in','.out'):os.mkfifo(str(qt)+suffix)
        qin=os.open(str(qt)+'.in',os.O_RDWR|os.O_NONBLOCK);qout=os.open(str(qt)+'.out',os.O_RDWR|os.O_NONBLOCK)
        m=None
        try:
            m=USBMachine(a.qemu,str(kernel),directory,[],extra_args=(
                '-qtest','pipe:'+str(qt),'-qtest-log',str(directory/'qtest.log')),serial_index=0)
            qbytes=bytearray()
            def qtest(command):
                os.write(qin,command.encode()+b'\n');end=time.monotonic()+5
                while time.monotonic()<end:
                    if b'\n' in qbytes:
                        line,_,rest=qbytes.partition(b'\n');qbytes[:]=rest
                        if line.startswith(b'IRQ'):continue
                        assert line.startswith(b'OK'),line
                        return line[2:].strip()
                    if select.select([qout],[],[],.1)[0]:qbytes.extend(os.read(qout,8192))
                raise RuntimeError('qtest timeout: '+command)
            boot=m.prompt(timeout=30);assert '[rpi4/hardware]' in boot and 'recovery mode' in boot
            assert '[console] PL011 RX irq=153' in boot and '[module] loaded hello' in boot
            m.command('echo pl011-irq','pl011-irq')
            before=m.command('uart');irq=int(re.search(r'irq=(\d+)',before).group(1));assert irq>0,before
            # Block PL011 RX interrupt sources while retaining the normal GIC/timer path.
            assert int(qtest('readl 0xfe201038'),0)==0x450
            qtest('writel 0xfe201038 0')
            assert int(qtest('readl 0xfe201038'),0)==0
            # A single character must echo without FIFO-threshold/timeout IRQs.
            m.p.stdin.write(b'e');m.p.stdin.flush();m.until(b'e')
            m.command('cho polled-input','polled-input')
            after=m.command('uart');assert int(re.search(r'irq=(\d+)',after).group(1))==irq,(before,after)
            assert int(re.search(r'rx=(\d+)',after).group(1))>int(re.search(r'rx=(\d+)',before).group(1))
            assert 'drop=0 overrun=0' in after
            qtest('writel 0xfe201038 0x450')
            m.command('echo irq-restored','irq-restored');m.command('selftest','preempt/sleep PASS')
            m.command('display','font=6x8 ASCII')
        finally:
            if m:m.close();(root/'uart-pl011-qemu-transcript.txt').write_bytes(m.log)
            os.close(qin);os.close(qout)
    print('Hardware PL011.EKM/GIC path: IRQ input, masked-IRQ FIFO input, single-byte echo, recovery shell PASS')
    print('QEMU fixture stubs only unsupported PCIe .EKM; no physical Pi test is claimed')


if __name__=='__main__':main()
