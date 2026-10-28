#!/usr/bin/env python3
"""A DTB inside NOLOAD BSS must be read before clearing that memory."""
import argparse
from pathlib import Path
import struct
import tempfile
from boot_fixture import create_dtb
from usb_qemu_test import USBMachine

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--qemu',default='qemu-system-aarch64');p.add_argument('--kernel',required=True);a=p.parse_args()
 root=Path('build/tests');root.mkdir(exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='boot-snapshot-',dir=root) as tmp:
  tmp=Path(tmp);blob=tmp/'overlap.dtb';create_dtb(blob,small_memory=True)
  code=bytearray(Path(a.kernel).read_bytes());assert struct.unpack_from('<I',code,0)[0]==0xaa0003f4
  # Test fixture only: mov x20,#0x200000, the DTB we load into BSS.
  struct.pack_into('<I',code,0,0xd2a00414);kernel=tmp/'kernel8.img';kernel.write_bytes(code)
  m=USBMachine(a.qemu,str(kernel),tmp,[],extra_args=['-dtb',str(blob),'-device',f'loader,file={blob},addr=0x200000,force-raw=on'])
  try:
   out=m.prompt();assert 'DTB=0x200000' in out and 'reservations=6' in out
   m.command('memtest','MMU/page/VM/uaccess/reclaim PASS');m.command('oomtest','page/VM OOM rollback PASS')
   print('DTB overlapping kernel NOLOAD BSS: early snapshot + VM + 16MiB OOM PASS',flush=True)
  finally:m.close();(root/'boot-snapshot-transcript.txt').write_bytes(m.log)
if __name__=='__main__':main()
