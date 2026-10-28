#!/usr/bin/env python3
"""Unmodified xHCI + MSC + FAT/exFAT C stack against QEMU USB storage."""
import argparse
import ctypes as C
from pathlib import Path
import shutil
import struct
import tempfile
import time
from xhci_qtest import API,Machine,U,Q,P


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--qemu',default='qemu-system-aarch64');ap.add_argument('--super',dest='super_speed',action='store_true');a=ap.parse_args()
    lib=C.CDLL(str(Path('build/host/xhci_storage.so').resolve()))
    lib.test_start.argtypes=[C.POINTER(API),Q]
    lib.test_file_write.argtypes=[C.c_char_p,P,U];lib.test_file_read.argtypes=[C.c_char_p,P,U]
    directory=Path('build/tests');directory.mkdir(parents=True,exist_ok=True)
    text=bytes((i*37+11)&255 for i in range(17011));buffer=C.create_string_buffer(text);out=C.create_string_buffer(len(text)+100)
    name='/日本語 xHCI.bin'.encode()
    with tempfile.TemporaryDirectory(prefix='storage-xhci-',dir=directory) as tmp:
        tmp=Path(tmp);image=tmp/'usb.img';shutil.copyfile('build/tests/fat-family/exfat.img',image)
        for readonly in (False,True):
            run=tmp/('ro' if readonly else 'rw');run.mkdir()
            # p3=0 selects USB 2; otherwise the storage negotiates SuperSpeed.
            m=Machine(a.qemu,run,controller_options='' if a.super_speed else ',p3=0')
            try:
                found=None
                for dev in range(32):
                    c=0x3f000000+(dev<<15)
                    if m.read32(c+8)>>8==0xc0330:found=c;break
                assert found is not None
                m.write32(found+4,0);m.write32(found+0x10,0x10000004);m.write32(found+0x14,0);m.write32(found+4,6)
                api=m.api();assert lib.test_start(C.byref(api),0x10000000)==0
                assert lib.test_storage_start()==0
                m.qmp('blockdev-add',{'driver':'raw','node-name':'mass','read-only':readonly,'file':{'driver':'file','filename':str(image.resolve())}})
                m.qmp('device_add',{'driver':'usb-storage','id':'storage','drive':'mass','bus':'xhci.0','port':'3'})
                time.sleep(.05);assert lib.test_status(3)==1
                if a.super_speed:
                    op=0x10000000+(m.read32(0x10000000)&255)
                    m.bad_link_port=op+0x400+2*16
                    assert lib.test_status(3)==1  # recoverable Inactive link without CCS
                rc=lib.test_enumerate(2,3,0);assert rc==0,('enum',rc)
                assert lib.test_speed(2)==(3 if a.super_speed else 2)
                assert lib.test_ep0(2)==(512 if a.super_speed else 64)
                if a.super_speed:assert m.warm_resets==1
                if a.super_speed:
                    for e in range(2):assert lib.test_endpoint(2,e)==(1024|(15<<16)|(1<<24))
                    # Inspect the actual controller-owned contexts, not only parser state.
                    op=0x10000000+(m.read32(0x10000000)&255)
                    dcbaa=m.read32(op+0x30)|(m.read32(op+0x34)<<32)
                    slot=lib.test_host_id(2)
                    outctx=int.from_bytes(bytes.fromhex(m.qt(f'read {dcbaa+slot*8:#x} 8')[2:]),'little')
                    ctxsize=64 if m.read32(0x10000010)&4 else 32
                    def context_word(ep,word):return m.read32(outctx+ep*ctxsize+word*4)
                    assert (context_word(0,0)>>20)&15==4
                    assert context_word(1,1)>>16==512
                    for ep in (3,4):
                        word=context_word(ep,1);assert word>>16==1024 and (word>>8)&255==15
                    # USB 2 keyboard/mouse remain usable alongside the SS disk.
                    assert lib.test_enumerate(0,5,0)==0 and lib.test_speed(0)<3
                    assert lib.test_enumerate(1,6,0)==0 and lib.test_speed(1)<3
                    report=C.create_string_buffer(64);lib.test_read.argtypes=[U,P]
                    for _ in range(5):
                        rc=lib.test_read(0,report)
                        if rc==-11:break
                    m.qmp('input-send-event',{'events':[{'type':'key','data':{'down':True,'key':{'type':'qcode','data':'a'}}}]})
                    deadline=time.monotonic()+2
                    while time.monotonic()<deadline:
                        rc=lib.test_read(0,report)
                        if rc==8 and report.raw[2]==4:break
                        assert rc==-11 or rc==8,rc;time.sleep(.005)
                    assert rc==8 and report.raw[2]==4
                    m.qmp('input-send-event',{'events':[{'type':'key','data':{'down':False,'key':{'type':'qcode','data':'a'}}}]})
                    print('SuperSpeed link / EP0=512 / bulk=1024 / burst=16 / mixed USB 2 HID PASS',flush=True)
                rc=lib.test_storage_mount(0);assert rc==0,('mount',rc)
                if not readonly:
                    rc=lib.test_file_write(name,buffer,len(text));assert rc==0,('write',rc)
                rc=lib.test_file_read(name,out,len(out));assert rc==len(text) and out.raw[:rc]==text,('read',rc)
                if readonly:
                    assert lib.test_file_write(b'/readonly',buffer,1)==-30
                    print('remount persistence / device write protection PASS',flush=True)
                else:print(('SuperSpeed' if a.super_speed else 'USB 2')+' xHCI -> BOT/SCSI -> exFAT Unicode/multi-block read/write/flush PASS',flush=True)
                if a.super_speed and not readonly:
                    m.corrupt_csw=True
                    rc=lib.test_file_read(name,out,len(out));assert rc==-5 and m.corrupted_csw==1,('bad CSW',rc)
                    rc=lib.test_file_read(name,out,len(out));assert rc==len(text) and out.raw[:rc]==text
                    print('SuperSpeed bad-CSW BOT reset / endpoint sequence reset / next read PASS',flush=True)
                    previous='storage'
                    for cycle in range(3):
                        assert lib.test_storage_unmount()==0
                        m.qmp('device_del',{'id':previous});time.sleep(.03)
                        assert lib.test_status(3)==0 and lib.test_release(2)==0
                        assert lib.test_storage_finish()==0
                        clone=tmp/f'replug-{cycle}.img';shutil.copyfile(image,clone)
                        node=f'mass{cycle}';previous=f'storage{cycle}'
                        m.qmp('blockdev-add',{'driver':'raw','node-name':node,'file':{'driver':'file','filename':str(clone.resolve())}})
                        m.qmp('device_add',{'driver':'usb-storage','id':previous,'drive':node,'bus':'xhci.0','port':'3'})
                        time.sleep(.03);assert lib.test_status(3)==1
                        assert lib.test_storage_start()==0
                        assert lib.test_enumerate(2,3,0)==0 and lib.test_speed(2)==3
                        assert lib.test_storage_mount(0)==0
                        assert lib.test_file_read(name,out,len(out))==len(text) and out.raw[:len(text)]==text
                    print('SuperSpeed hot unplug/replug / slot reuse / persisted media PASS',flush=True)
                else:previous='storage'
                removed=[]
                if a.super_speed and not readonly:
                    # Unplug the real QEMU device when the first bulk IN of an
                    # actual filesystem read is about to ring its doorbell.
                    db=0x10000000+(m.read32(0x10000014)&~3)
                    slot=lib.test_host_id(2)
                    def remove_during_io(address,value):
                        if address==db+slot*4 and value==3:
                            m.before_write=None;m.qmp('device_del',{'id':previous});removed.append(True)
                    m.before_write=remove_during_io
                    rc=lib.test_file_read(name,out,len(out));assert removed and rc<0,('unplug during read',rc)
                    assert rc==-19 and not (m.read32(op+4)&1),('unplug must preserve other ports',rc)
                    # The controller remains running for the USB 2 input device.
                    m.qmp('input-send-event',{'events':[{'type':'key','data':{'down':True,'key':{'type':'qcode','data':'b'}}}]})
                    deadline=time.monotonic()+2
                    while time.monotonic()<deadline:
                        rc=lib.test_read(0,report)
                        if rc==8 and report.raw[2]==5:break
                        assert rc in (-11,8),rc;time.sleep(.005)
                    assert rc==8 and report.raw[2]==5
                    print('SuperSpeed removal during active bulk I/O -> ENODEV / USB 2 HID stays live PASS',flush=True)
                assert lib.test_storage_finish()==-16,'mounted block must pin its C driver'
                assert lib.test_storage_unmount()==0
                if not removed:m.qmp('device_del',{'id':previous})
                time.sleep(.05)
                assert lib.test_release(2)==0
                assert lib.test_storage_finish()==0
                assert lib.test_stop()==0 and not m.dma
                assert m.failure is None,m.failure
            finally:m.close()
        print('storage module pin / unplug / DMA reclamation PASS',flush=True)
if __name__=='__main__':main()
