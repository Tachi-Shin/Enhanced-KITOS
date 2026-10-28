#!/usr/bin/env python3
"""Verify real scanout pixels stay unchanged until present, and crash recovery."""
import argparse
from pathlib import Path
import tempfile
import shutil
import time
from usb_qemu_test import USBMachine
from test_images import create
from rootfs import stage
from process_test import free_pages
from terminal_qemu_test import png

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--qemu',default='qemu-system-aarch64');p.add_argument('--kernel',required=True);p.add_argument('--image');a=p.parse_args()
 root=Path('build/tests');root.mkdir(exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='display-',dir=root) as tmp:
  tmp=Path(tmp);disk=tmp/'sd.img'
  if a.image:shutil.copyfile(a.image,disk)
  else:create(disk,fixture_files=False);stage(disk)
  m=USBMachine(a.qemu,a.kernel,tmp,[],disk)
  def scan(name,color):
   f=root/(name+'.ppm');end=time.monotonic()+8
   while True:
    m.qmp('screendump',{'filename':str(f.resolve())})
    h,d,maxv,pixels=f.read_bytes().split(b'\n',3);w,ht=map(int,d.split());assert h==b'P6' and maxv==b'255'
    if pixels==bytes(color)*(w*ht):break
    assert time.monotonic()<end,(name,'scanout is not expected solid color')
    time.sleep(.05)
   png(root/(name+'.png'),w,ht,pixels)
  def go():m.p.stdin.write(b'\r');m.p.stdin.flush()
  try:
   m.prompt();m.command('display','double-buffer=1');baseline=free_pages(m)
   for cycle in range(2):
    m.p.stdin.write(b'display-probe\r');m.p.stdin.flush();m.until(b'RED_READY',10);scan('display-red',(255,0,0));go()
    m.until(b'BLUE_STAGED');scan('display-staged',(255,0,0));go()
    m.until(b'BLUE_READY');scan('display-blue',(0,0,255));go();m.prompt()
    assert free_pages(m)==baseline,'display SHM/VM/child leak'
   m.p.stdin.write(b'display-probe --crash\r');m.p.stdin.flush();m.until(b'RED_READY',10);scan('display-before-crash',(255,0,0));go();m.prompt()
   assert free_pages(m)==baseline,'display owner crash leaked pages'
   m.command('echo display-recovered','display-recovered');m.command('display','double-buffer=1')
   f=root/'display-recovered.ppm';end=time.monotonic()+5
   while True:
    m.qmp('screendump',{'filename':str(f.resolve())});h,d,maxv,pixels=f.read_bytes().split(b'\n',3);w,ht=map(int,d.split())
    if len(set(tuple(pixels[n:n+3]) for n in range(0,len(pixels),3)))>1:break
    assert time.monotonic()<end,'text did not resume after owner crash'
    time.sleep(.05)
   png(root/'display-recovered.png',w,ht,pixels)
   print('GPU page flip + exact red/staged-red/blue scanout + exclusive ownership + release/crash/reclaim PASS',flush=True)
  finally:m.close();(root/'display-qemu-transcript.txt').write_bytes(m.log)
if __name__=='__main__':main()
