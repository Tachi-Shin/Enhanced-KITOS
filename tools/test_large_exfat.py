#!/usr/bin/env python3
"""Sparse exFAT >4 GiB file interoperability and EL0 USB offset regression."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from exfat_large_image import inject
from test_images import create
from stage_drivers import stage
from storage_qemu_test import mounted
from usb_qemu_test import USBMachine


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--qemu',default='qemu-system-aarch64');p.add_argument('--kernel',required=True);a=p.parse_args()
    tools=Path('build/deps/exfat-utils-1.4.0');formatter=tools/'mkfs/mkexfatfs';checker=tools/'fsck/exfatfsck'
    if not formatter.exists():raise SystemExit('Build the optional exfat-utils 1.4.0 test tools first; see docs/usb-storage.md')
    directory=Path('build/tests')
    with tempfile.TemporaryDirectory(prefix='exfat-large-',dir=directory) as tmp:
        tmp=Path(tmp);image=tmp/'exfat-large.img'
        with image.open('wb') as f:f.truncate(8*1024**3)
        subprocess.run([str(formatter),'-s','256',str(image)],check=True);inject(image)
        subprocess.run(['build/host/fat_family_test',str(image)],check=True)
        subprocess.run([str(checker),'-n',str(image)],check=True)
        sd=tmp/'sd.img';create(sd);stage(sd)
        subprocess.run(['mcopy','-i',str(sd),'build/usr/probe.elf','::PROBE.ELF'],check=True)
        m=USBMachine(a.qemu,a.kernel,tmp,['usb-storage,id=storage,drive=mass,port=1'],sd,
            extra_args=['-drive',f'if=none,id=mass,file={image},format=raw'])
        try:
            m.prompt();mounted(m)
            m.command('run /PROBE.ELF usb-large /media/usb0/large.bin','[user-usb] exFAT seek/read/write above 4GiB PASS')
            m.command('umount /media/usb0')
        finally:
            m.close();(directory/'exfat-large-transcript.txt').write_bytes(m.log)
        subprocess.run([str(checker),'-n',str(image)],check=True)
    print('exFAT >4GiB host + EKITOS EL0/USB + independent fsck PASS')
if __name__=='__main__':main()
