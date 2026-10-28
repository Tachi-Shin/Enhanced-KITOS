#!/usr/bin/env python3
"""Build/stage the Unix directory tree. Never access a physical block device."""
import argparse
from pathlib import Path
import shutil
import subprocess

DIRECTORIES = ('bin','sbin','etc','lib','lib/modules','boot','dev','proc','tmp',
    'var','var/log','var/tmp','home','root','mnt','media','usr','usr/bin','usr/sbin','usr/lib','usr/share')
MODULES = ('VIDEO','TIMER','GIC','GPIO','MAILBOX','UART','SD','DWC2','PI4USB','HID','STORAGE','HELLO')


def files(include_modules=True):
    for p in sorted(Path('rootfs').rglob('*')):
        if p.is_file(): yield p, p.relative_to('rootfs')
    for p in sorted(Path('build/usr/bin').glob('*')):
        if p.is_file(): yield p, Path('bin')/p.name
    for p in sorted(Path('build/usr/sbin').glob('*')):
        if p.is_file(): yield p, Path('sbin')/p.name
    for name in MODULES:
        if not include_modules: break
        p=Path('build/drivers')/(name+'.EKM')
        if not p.is_file(): raise RuntimeError(f'Build drivers first: {p}')
        yield p, Path('lib/modules')/p.name


def build(directory, kernel=None):
    directory=Path(directory)
    for name in DIRECTORIES: (directory/name).mkdir(parents=True,exist_ok=True)
    for source,dest in files():
        target=directory/dest
        if dest.parts[0]=='etc' and target.exists(): continue
        shutil.copyfile(source,target)
    if kernel: shutil.copyfile(kernel,directory/'boot/kernel8.img')
    if not (directory/'bin/sh').is_file(): raise RuntimeError('Build users first')


def stage(disk, drivers=True, preserve_config=False):
    disk=Path(disk)
    if not disk.is_file(): raise RuntimeError('Explicit regular-file FAT image required')
    for name in DIRECTORIES:
        r=subprocess.run(['mmd','-i',str(disk),'::'+name],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        if r.returncode: subprocess.run(['mdir','-i',str(disk),'::'+name],check=True,stdout=subprocess.DEVNULL)
    for source,dest in files(drivers):
        if preserve_config and dest.parts[0]=='etc':continue
        subprocess.run(['mcopy','-o','-i',str(disk),str(source),'::'+dest.as_posix()],check=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',default='build/rootfs');p.add_argument('--kernel')
    a=p.parse_args();build(a.out,a.kernel)
    print('Root filesystem tree:',a.out)
