#!/usr/bin/env python3
"""Inject one sparse >4 GiB contiguous file into an independently formatted exFAT
fixture. Only ignored test images are accepted by the caller. No 4 GiB write.
"""
from pathlib import Path
import struct

OFFSET=0x100000011
MARKER=b'beyond-four-gib'


def inject(path):
    with Path(path).open('r+b') as f:
        boot=f.read(512);assert boot[3:11]==b'EXFAT   '
        sector=1<<boot[108];cluster=sector*(1<<boot[109]);heap=struct.unpack_from('<I',boot,88)[0]*sector
        count=struct.unpack_from('<I',boot,92)[0];root=struct.unpack_from('<I',boot,96)[0]
        def addr(c):return heap+(c-2)*cluster
        f.seek(addr(root));directory=bytearray(f.read(cluster));bitmap=None;empty=None
        for pos in range(0,len(directory),32):
            typ=directory[pos]
            if typ==0x81:bitmap=(struct.unpack_from('<I',directory,pos+20)[0],struct.unpack_from('<Q',directory,pos+24)[0])
            if typ==0:empty=pos;break
        assert bitmap and empty is not None and empty+96<=len(directory)
        first=1000;length=OFFSET+len(MARKER);needed=(length+cluster-1)//cluster
        assert first+needed<=count+2
        f.seek(addr(bitmap[0]));bits=bytearray(f.read(bitmap[1]))
        for c in range(first,first+needed):
            n=c-2;assert not bits[n//8]&(1<<(n%8));bits[n//8]|=1<<(n%8)
        f.seek(addr(bitmap[0]));f.write(bits)
        entries=bytearray(96);entries[0]=0x85;entries[1]=2;struct.pack_into('<H',entries,4,0x20)
        timestamp=((2025-1980)<<25)|(1<<21)|(1<<16)
        for pos in (8,12,16):struct.pack_into('<I',entries,pos,timestamp)
        name='large.bin';encoded=name.encode('utf-16le');entries[32]=0xc0;entries[33]=3;entries[35]=len(name)
        namehash=0
        for b in name.upper().encode('utf-16le'):namehash=(((namehash>>1)|(namehash<<15))+b)&0xffff
        struct.pack_into('<H',entries,36,namehash);struct.pack_into('<Q',entries,40,length)
        struct.pack_into('<I',entries,52,first);struct.pack_into('<Q',entries,56,length)
        entries[64]=0xc1;entries[66:66+len(encoded)]=encoded
        checksum=0
        for i,b in enumerate(entries):
            if i not in (2,3):checksum=(((checksum>>1)|(checksum<<15))+b)&0xffff
        struct.pack_into('<H',entries,2,checksum)
        f.seek(addr(root)+empty);f.write(entries)
        f.seek(addr(first)+OFFSET);f.write(MARKER)
