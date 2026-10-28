#!/usr/bin/env python3
"""Prepare a new QEMU image, or explicitly update runtime files with a backup."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
from test_images import create
from rootfs import stage


def current_shell(disk):
    with tempfile.TemporaryDirectory(prefix='check-shell-',dir=disk.parent) as directory:
        extracted=Path(directory)/'sh'
        r=subprocess.run(['mcopy','-i',str(disk),'::bin/sh',str(extracted)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        return not r.returncode and extracted.read_bytes()==Path('build/usr/bin/sh').read_bytes()


def prepare(disk,update=False):
    disk=Path(disk)
    if disk.exists():
        if not disk.is_file():raise RuntimeError('Regular-file image required')
        if update:
            # Preserve a complete copy before touching managed runtime files.
            # stage leaves unrelated files alone; preserve_config keeps /etc.
            with tempfile.NamedTemporaryFile(prefix=disk.name+'.before-update-',suffix='.bak',dir=disk.parent,delete=False) as f:
                backup=Path(f.name)
            shutil.copy2(disk,backup)
            print('Backup:',backup,flush=True)
            stage(disk,preserve_config=True)
            if not current_shell(disk):raise RuntimeError('Updated /bin/sh did not match the build; backup retained: '+str(backup))
            print('Updated /bin, /sbin and /lib/modules; settings and user files retained:',disk)
            return backup
        print('Using existing system image:',disk)
        if not current_shell(disk):
            print('NOTICE: /bin/sh differs from the current build. History/Tab features may be from an older version.')
            print('Update with a backup: make update-system-image SD_IMAGE='+str(disk))
    else:
        create(disk,fixture_files=False);stage(disk)
        print('Created Unix system image:',disk)
    return None


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('disk');p.add_argument('--update',action='store_true')
    a=p.parse_args();prepare(a.disk,a.update)
