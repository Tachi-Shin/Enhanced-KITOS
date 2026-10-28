#!/usr/bin/env python3
"""Reproduce an installed kernel with an empty /bin, including BOOT+DATA media."""
import argparse
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
from qemu_test import Machine
from rootfs import stage
from test_images import create, SIZE


def prompt_path(output):
    return re.findall(r'root@ekitos:([^\r\n]*?)# ',output)[-1]


def cwd(m, command, expected, error=None):
    out=m.command(command,allow_error=error is not None)
    if error is not None:assert 'error '+str(error) in out,out
    assert prompt_path(out)==expected,(command,out)
    out=m.command('pwd');assert '\r\n'+expected+'\r\n' in out,out


def recovery(m):
    boot=m.prompt();assert '[shell] recovery mode' in boot and '/bin/sh: file not found' in boot
    empty=m.command('ls /bin');assert re.fullmatch(r'ls /bin\r\nroot@ekitos:/# ',empty),repr(empty)
    cwd(m,'cd bin','/bin');cwd(m,'cd ..','/');cwd(m,'cd ..','/')
    cwd(m,'cd /../../..','/')
    m.command('mkdir /cwd');m.command('mkdir /cwd/leaf')
    m.command("mkdir '/cwd/space name'")
    cwd(m,'cd //cwd///leaf/./','/cwd/leaf')
    cwd(m,'cd .././leaf/../..','/')
    cwd(m,"cd 'cwd/space name/../leaf'",'/cwd/leaf')
    cwd(m,'cd missing/..','/cwd/leaf',-2)
    cwd(m,'cd /HELLO.TXT/..','/cwd/leaf',-20)
    cwd(m,'cd /HELLO.TXT','/cwd/leaf',-20)
    cwd(m,'cd ""','/cwd/leaf',-2)
    m.command('write ../marker relative-file-data')
    m.command('cat /cwd/marker','relative-file-data')
    m.command('mv ../marker ../moved');m.command('cat /cwd/moved','relative-file-data')
    for _ in range(4):cwd(m,'cd ..','/cwd');cwd(m,'cd leaf','/cwd/leaf')
    cwd(m,'cd /','/')
    m.command('ls /','HELLO.TXT')


def copy_region(source,target,offset,length,to_disk):
    # All files are new fixtures within this test's temporary directory.
    with Path(source).open('rb') as src,Path(target).open('r+b' if to_disk else 'xb') as dst:
        if to_disk:dst.seek(offset)
        else:src.seek(offset)
        while length:
            data=src.read(min(length,1024*1024));assert data
            dst.write(data);length-=len(data)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--qemu',default='qemu-system-aarch64');p.add_argument('--kernel',required=True);a=p.parse_args()
    root=Path('build/tests');root.mkdir(exist_ok=True);transcript=bytearray()
    with tempfile.TemporaryDirectory(prefix='recovery-cwd-',dir=root) as directory:
        directory=Path(directory)
        # Direct FAT, no user ELF or .EKM files: the kernel creates empty /bin.
        direct=directory/'direct.img';create(direct)
        m=Machine(a.qemu,a.kernel,direct)
        try:recovery(m)
        finally:m.close();transcript.extend(m.log)
        print('Empty-/bin recovery: cd .././root/repeated-slash/quoted names/errors/relative IO PASS',flush=True)
        # Put all programs on BOOT only, leaving DATA without /bin/sh. This
        # reproduces choosing a different root partition on a real SD card.
        boot=directory/'boot.img';data=directory/'data.img';create(boot,fixture_files=False);stage(boot);create(data)
        disk=directory/'sd.img';starts=(2048,2048+SIZE//512)
        with disk.open('xb') as f:
            f.truncate(512*1024*1024);mbr=bytearray(512)
            for i,start in enumerate(starts):
                mbr[446+16*i+4]=0x0c;struct.pack_into('<II',mbr,446+16*i+8,start,SIZE//512)
            mbr[510:]=b'\x55\xaa';f.write(mbr)
        copy_region(boot,disk,starts[0]*512,SIZE,True);copy_region(data,disk,starts[1]*512,SIZE,True)
        m=Machine(a.qemu,a.kernel,disk)
        try:recovery(m)
        finally:m.close();transcript.extend(m.log)
        print('BOOT has ELF files, DATA selected as / with empty /bin: same recovery semantics PASS',flush=True)
        # Repair only the selected root's files, preserving its prior contents.
        repaired=directory/'repaired-data.img';copy_region(disk,repaired,starts[1]*512,SIZE,False)
        stage(repaired);copy_region(repaired,disk,starts[1]*512,SIZE,True)
        m=Machine(a.qemu,a.kernel,disk)
        try:
            boot=m.prompt();assert '[shell] /bin/sh ready' in boot and 'recovery mode' not in boot
            cwd(m,'cd bin','/bin');cwd(m,'cd ..','/')
            m.command('cat /cwd/moved','relative-file-data');m.command('cat /HELLO.TXT','initial-data')
        finally:m.close();transcript.extend(m.log)
        print('Installing ELF files on DATA starts normal /bin/sh; cd and existing data preserved PASS',flush=True)
    (root/'recovery-cwd-qemu-transcript.txt').write_bytes(transcript)


if __name__=='__main__':main()
