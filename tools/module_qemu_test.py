#!/usr/bin/env python3
"""Load/unload real AArch64 C modules in EKITOS, including failure rollback."""
import argparse
from pathlib import Path
import struct
import re
import subprocess
import tempfile
import time
from test_images import create
from usb_qemu_test import USBMachine
from process_test import free_pages
from stage_drivers import stage


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--qemu',default='qemu-system-aarch64');p.add_argument('--kernel',required=True);a=p.parse_args()
    directory=Path('build/tests');directory.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='modules-',dir=directory) as temp:
        temp=Path(temp);disk=temp/'sd.img';create(disk);stage(disk)
        original=Path('build/drivers/HELLO.EKM').read_bytes()
        fixtures={}
        b=bytearray(original);b[0]=0;fixtures['MAGIC']=b
        b=bytearray(original);struct.pack_into('<H',b,18,62);fixtures['ARCH']=b
        # e_entry is a virtual address; locate its corresponding file range.
        entry=struct.unpack_from('<Q',original,24)[0];phoff=struct.unpack_from('<Q',original,32)[0];phnum=struct.unpack_from('<H',original,56)[0]
        for i in range(phnum):
            typ,flags,off,va,_,filesz,memsz,_=struct.unpack_from('<IIQQQQQQ',original,phoff+i*56)
            if typ==1 and va<=entry<va+filesz:manifest=off+entry-va
        b=bytearray(original);struct.pack_into('<I',b,manifest+8,999);fixtures['ABI']=b
        b=bytearray(original);reloc=original.index(struct.pack('<QQ',entry+48,1027));struct.pack_into('<Q',b,reloc+16,entry);fixtures['INIT']=b
        fixtures['TRUNC']=original[:20]
        for name,data in fixtures.items():
            path=temp/(name+'.EKM');path.write_bytes(data);subprocess.run(['mcopy','-i',str(disk),str(path),'::lib/modules/'],check=True)
        for n in range(32):
            b=bytearray(original);b[manifest+16:manifest+48]=(f'pool{n}'.encode()+b'\0').ljust(32,b'\0')
            path=temp/f'POOL{n}.EKM';path.write_bytes(b)
            subprocess.run(['mcopy','-i',str(disk),str(path),'::lib/modules/'],check=True)
        m=USBMachine(a.qemu,a.kernel,temp,['usb-kbd,id=kbd,port=1'],disk)
        try:
            m.prompt();m.usb_wait(1,keyboard=1);time.sleep(.05)
            assert 'boot-hid state=2' in m.command('lsmod')
            baseline=free_pages(m)
            for name in fixtures:
                out=m.command('insmod /lib/modules/'+name+'.EKM',allow_error=True)
                assert '-8' in out,(name,out)
                assert free_pages(m)==baseline,name+' leaked pages'
            # Wrong platform fails before accessing Pi hardware and releases image pages.
            assert '-19' in m.command('insmod /lib/modules/PI4USB.EKM',allow_error=True)
            assert free_pages(m)==baseline
            for _ in range(5):
                assert 'loaded hello' in m.command('insmod /lib/modules/HELLO.EKM')
                live=free_pages(m);assert live<baseline
                assert '-17' in m.command('insmod /lib/modules/HELLO.EKM',allow_error=True)
                assert free_pages(m)==live
                m.command('rmmod hello','unloaded hello')
                assert free_pages(m)==baseline
            active=int(re.search(r"count=(\d+)",m.command("lsmod")).group(1))
            available=32-active
            for n in range(available):
                m.command(f'insmod /lib/modules/POOL{n}.EKM',f'loaded pool{n}')
            assert 'count=32' in m.command('lsmod')
            live=free_pages(m)
            assert '-28' in m.command(f'insmod /lib/modules/POOL{available}.EKM',allow_error=True)
            assert free_pages(m)==live
            for n in list(range(available))[::-2]+list(range(available))[-2::-2]:m.command(f'rmmod pool{n}',f'unloaded pool{n}')
            assert free_pages(m)==baseline
            for _ in range(2):
                m.key('shift',True)
                # Calls may encounter the active USB gate; explicitly retry EBUSY.
                for attempt in range(100):
                    out=m.command('rmmod boot-hid',allow_error=True)
                    if 'unloaded boot-hid' in out:break
                    assert '-16' in out,out;time.sleep(.02)
                else:raise AssertionError('HID unload stayed busy')
                assert 'key=225 value=0' in m.command('input')
                m.key('shift',False)
                assert 'driver=unbound' in m.command('usb')
                for attempt in range(100):
                    out=m.command('insmod /lib/modules/HID.EKM',allow_error=True)
                    if 'loaded boot-hid' in out:break
                    assert '-16' in out,out;time.sleep(.02)
                else:raise AssertionError('HID reload stayed busy')
                m.usb_wait(1,keyboard=1)
                assert 'reload\r\n' in m.type_command('echo reload')
                m.command('input');assert free_pages(m)==baseline
            m.command('memtest','MMU/page/VM/uaccess/reclaim PASS')
            print('AArch64 C module ABI/rejection/duplicate/pool limit/rollback/HID reload: PASS')
        finally:
            m.close();(directory/'modules-qemu-transcript.txt').write_bytes(m.log)
if __name__=='__main__':main()
