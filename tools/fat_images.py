#!/usr/bin/env python3
"""Disposable FAT-family fixtures. mtools formats FAT12/16/32 independently;
FatFs f_mkfs creates exFAT where no regular-file exFAT formatter is installed.
"""
import argparse
from pathlib import Path
import struct
import subprocess
import zlib


def create(directory):
    directory=Path(directory);directory.mkdir(parents=True,exist_ok=True)
    specs=[('fat12',1440*1024,['-f','1440']),('fat16',32*1024*1024,['-t','128','-h','16','-s','32']),
           ('fat32',128*1024*1024,['-F'])]
    paths=[]
    for name,size,args in specs:
        path=directory/(name+'.img')
        with path.open('wb') as f:f.truncate(size)
        subprocess.run(['mformat','-i',str(path),*args,'::'],check=True);paths.append(path)
    for name,sector in [('exfat',512),('exfat4k',4096)]:
        path=directory/(name+'.img')
        with path.open('wb') as f:f.truncate(64*1024*1024)
        formatter=Path('build/deps/exfat-utils-1.4.0/mkfs/mkexfatfs')
        if sector==512 and formatter.exists():subprocess.run([str(formatter),str(path)],check=True)
        else:subprocess.run(['build/host/fat_family_test','format',str(path),'4',str(sector)],check=True)
        # The host VFS harness uses physical 512-byte sectors, so a logical 4K
        # volume is tested through a block adapter in USB integration instead.
        paths.append(path)
    raw=(directory/'exfat.img').read_bytes();start=2048;sectors=len(raw)//512;total=start+sectors+2048
    for layout in ('mbr','gpt'):
        path=directory/('exfat-'+layout+'.img')
        with path.open('wb') as f:
            f.truncate(total*512);mbr=bytearray(512);mbr[510:]=b'\x55\xaa'
            struct.pack_into('<B3sB3sII',mbr,446,0,b'\0'*3,7 if layout=='mbr' else 0xee,b'\0'*3,start if layout=='mbr' else 1,sectors if layout=='mbr' else total-1)
            f.write(mbr)
            if layout=='gpt':
                entries=bytearray(16384);entries[:16]=bytes.fromhex('a2a0d0ebe5b9334487c068b6b72699c7');entries[16:32]=b'K'*16
                struct.pack_into('<QQQ',entries,32,start,start+sectors-1,0);entries[56:68]='EKITOS'.encode('utf-16le')
                def header(lba,backup,table):
                    h=bytearray(512);struct.pack_into('<8sIIIIQQQQ16sQIII',h,0,b'EFI PART',0x10000,92,0,0,lba,backup,34,total-34,b'E'*16,table,128,128,zlib.crc32(entries))
                    struct.pack_into('<I',h,16,zlib.crc32(h[:92]));return h
                f.seek(512);f.write(header(1,total-1,2));f.write(entries)
                f.seek((total-33)*512);f.write(entries);f.write(header(total-1,1,total-33))
            f.seek(start*512);f.write(raw)
        paths.append(path)
    return paths
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory');a=p.parse_args()
    for path in create(a.directory):print(path)
