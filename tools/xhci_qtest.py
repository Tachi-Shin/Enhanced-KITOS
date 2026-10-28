#!/usr/bin/env python3
"""Run the production C xHCI backend against QEMU's real PCI xHCI model.
Qtest uses stdio, QMP uses FIFOs; no host USB, disks or listener sockets.
"""
import argparse
import ctypes as C
import json
import os
from pathlib import Path
import select
import subprocess
import tempfile
import time

U=C.c_uint32
Q=C.c_ulonglong
P=C.c_void_p
class API(C.Structure):
    _fields_=[('abi',U),('size',U),('owner',P),('platform',C.c_char_p)]+[(n,P) for n in (
        'now_ms','sleep_ms','log','dma_alloc','dma_free','dma_sync','read32','write32',
        'firmware_xhci_reset','usb_attach_host','usb_detach_host','usb_register_class',
        'usb_unregister_class','usb_control','usb_interrupt_in','input_emit','usb_bulk','usb_clear_halt','usb_begin','usb_end','block_register','block_unregister','device_publish','counter_read','counter_frequency','delay_us','read8','read16','write8','write16','gate_try','gate_release','uart_rx_push','uart_rx_overrun','gpio_function','gpio_pull','firmware_clock','irq_register','irq_unregister','firmware_request','framebuffer_map')]
class Machine:
    def __init__(self,qemu,directory,controller_options=""):
        pipe=directory/'qmp'
        for suffix in ('.in','.out'):os.mkfifo(str(pipe)+suffix)
        self.qin=os.open(str(pipe)+'.in',os.O_RDWR|os.O_NONBLOCK)
        self.qout=os.open(str(pipe)+'.out',os.O_RDWR|os.O_NONBLOCK)
        self.stderr=(directory/'stderr.txt').open('w+')
        self.p=subprocess.Popen([qemu,'-M','virt,highmem=off','-accel','tcg','-m','128M',
            '-nodefaults','-display','none','-qtest','stdio','-qtest-log',str(directory/'qtest.log'),
            '-qmp','pipe:'+str(pipe),'-device','qemu-xhci,id=xhci'+controller_options,
            '-device','usb-kbd,id=kbd,bus=xhci.0,port=1',
            '-device','usb-mouse,id=mouse,bus=xhci.0,port=2',
            '-device','loader,addr=0x40000000,data=0x14000000,data-len=4',
            '-device','loader,addr=0x40000000,cpu-num=0'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.stderr)
        self.qbytes=b'';self.qid=0;self.dma={};self.next_dma=0x41000000;self.failure=None;self.fail_alloc=None;self.drop_command=False;self.command_db=0;self.fail_halt=False;self.op=0
        self.qread();self.qmp('qmp_capabilities')
    def qread(self):
        end=time.monotonic()+5
        while time.monotonic()<end:
            if b'\n' in self.qbytes:
                line,self.qbytes=self.qbytes.split(b'\n',1);return json.loads(line)
            if select.select([self.qout],[],[],.1)[0]:self.qbytes+=os.read(self.qout,8192)
        raise RuntimeError('QMP timeout')
    def qmp(self,command,args=None):
        self.qid+=1;r={'execute':command,'id':self.qid}
        if args is not None:r['arguments']=args
        os.write(self.qin,json.dumps(r).encode()+b'\n')
        while True:
            r=self.qread()
            if 'event' in r:continue
            assert r.get('id')==self.qid and 'error' not in r,r
            return r.get('return')
    def qt(self,command):
        self.p.stdin.write(command.encode()+b'\n');self.p.stdin.flush()
        while True:
            if not select.select([self.p.stdout],[],[],5)[0]:raise RuntimeError('qtest timeout: '+command[:100])
            line=self.p.stdout.readline().decode().strip()
            if line.startswith('IRQ'):continue
            if not line.startswith('OK'):
                self.stderr.flush();self.stderr.seek(0)
                raise RuntimeError(line+' '+self.stderr.read())
            return line[2:].strip()
    def read32(self,address):
        value=int(self.qt(f'readl {address:#x}'),0)
        if getattr(self,'bad_link_port',None)==address:value=(value&~(3|(15<<5)))|(6<<5)
        return value
    def write32(self,address,value):
        hook=getattr(self,'before_write',None)
        if hook:hook(address,value)
        if self.drop_command and address==self.command_db:return
        if self.fail_halt and address==self.op and value==0:return
        if getattr(self,'bad_link_port',None)==address and value&(1<<31):
            self.bad_link_port=None;self.warm_resets=getattr(self,'warm_resets',0)+1
        self.qt(f'writel {address:#x} {value:#x}')
    def callback(self,typ,fn):
        def guarded(*args):
            try:return fn(*args)
            except BaseException as exc:
                self.failure=exc;print('callback failed:',exc,flush=True);return 0
        return typ(guarded)
    def api(self):
        def alloc(owner,physical):
            if self.fail_alloc is not None:
                if self.fail_alloc==0:return None
                self.fail_alloc-=1
            raw=C.create_string_buffer(8192);v=(C.addressof(raw)+4095)&~4095
            p=self.next_dma;self.next_dma+=4096;physical[0]=p;self.dma[v]=(raw,p)
            return v
        def free(owner,v):
            assert v in self.dma;del self.dma[v];return 0
        def sync(v,n,direction):
            assert n<=4096 and v in self.dma
            p=self.dma[v][1]
            if direction==1:self.qt(f'write {p:#x} {n:#x} 0x'+C.string_at(v,n).hex())
            else:
                C.memmove(v,bytes.fromhex(self.qt(f'read {p:#x} {n:#x}')[2:]),n)
                if getattr(self,'corrupt_csw',False) and C.string_at(v,4)==b'USBS':
                    self.corrupt_csw=False;self.corrupted_csw=getattr(self,'corrupted_csw',0)+1
                    tag=int.from_bytes(C.string_at(v+4,4),'little')^0x100
                    C.memmove(v+4,tag.to_bytes(4,'little'),4)
        self.callbacks=[
            self.callback(C.CFUNCTYPE(Q),lambda:int(time.monotonic()*1000)),
            self.callback(C.CFUNCTYPE(None,U),lambda ms:time.sleep(ms/1000)),
            self.callback(C.CFUNCTYPE(None,C.c_char_p,Q,Q),lambda msg,a,b:print(msg.decode(),hex(a),hex(b),flush=True)),
            self.callback(C.CFUNCTYPE(P,P,C.POINTER(Q)),alloc),
            self.callback(C.CFUNCTYPE(C.c_int,P,P),free),
            self.callback(C.CFUNCTYPE(None,P,C.c_ulong,U),sync),
            self.callback(C.CFUNCTYPE(U,Q),self.read32),
            self.callback(C.CFUNCTYPE(None,Q,U),self.write32)]
        a=API();a.abi=6;a.size=C.sizeof(API);a.platform=b'qtest/virt'
        for (name,_),f in zip(API._fields_[4:],self.callbacks):setattr(a,name,C.cast(f,P).value)
        return a
    def close(self):
        self.p.terminate()
        try:self.p.wait(timeout=3)
        except subprocess.TimeoutExpired:self.p.kill();self.p.wait()
        self.p.stdin.close();self.p.stdout.close();os.close(self.qin);os.close(self.qout);self.stderr.close()

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--qemu',default='qemu-system-aarch64');args=ap.parse_args()
    lib=C.CDLL(str(Path('build/host/xhci.so').resolve()))
    lib.test_start.argtypes=[C.POINTER(API),Q];lib.test_read.argtypes=[U,P];lib.test_control.argtypes=[U,U,U,U,U,P,U]
    directory=Path('build/tests/xhci');directory.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(dir=directory) as temp:
        m=Machine(args.qemu,Path(temp))
        try:
            ecam=0x3f000000;found=None
            for dev in range(32):
                c=ecam+(dev<<15)
                if m.read32(c+8)>>8==0xc0330:found=c;break
            assert found is not None,'xHCI PCI device absent'
            m.write32(found+4,0);m.write32(found+0x10,0x10000004);m.write32(found+0x14,0);m.write32(found+4,6)
            m.command_db=0x10000000+(m.read32(0x10000014)&~3)
            m.op=0x10000000+(m.read32(0x10000000)&255)
            api=m.api();assert lib.test_api_size()==C.sizeof(API)
            rc=lib.test_start(C.byref(api),0x10000000);assert rc==0,('start',rc)
            ports=[p for p in range(1,9) if lib.test_status(p)==1];print('connected root ports',ports,flush=True)
            assert len(ports)==2,ports
            for n,port in enumerate(ports):
                rc=lib.test_enumerate(n,port,0);assert rc==0,('enumerate',n,rc)
            print('keyboard and mouse enumeration PASS',flush=True)
            report=C.create_string_buffer(1024)
            for n in range(2):
                for _ in range(10):
                    rc=lib.test_read(n,report)
                    if rc>0:break
                    assert rc==-11,rc;time.sleep(.01)
            def key(down):m.qmp('input-send-event',{'events':[{'type':'key','data':{'down':down,'key':{'type':'qcode','data':'a'}}}]})
            # Leave a NAK request pending and then deliver input.
            rc=lib.test_read(0,report);assert rc==-11,rc
            key(True)
            end=time.monotonic()+2
            while time.monotonic()<end:
                rc=lib.test_read(0,report)
                if rc>0:break
                assert rc==-11,rc;time.sleep(.005)
            assert rc==8 and report.raw[2]==4,(rc,report.raw[:8]);key(False)
            print('persistent interrupt IN / keyboard report PASS',flush=True)
            rc=lib.test_read(1,report);assert rc==-11,rc
            m.qmp('input-send-event',{'events':[{'type':'rel','data':{'axis':'x','value':7}},
                {'type':'rel','data':{'axis':'y','value':-5}}, {'type':'btn','data':{'button':'left','down':True}}]})
            end=time.monotonic()+2
            while time.monotonic()<end:
                rc=lib.test_read(1,report)
                if rc>0:break
                assert rc==-11,rc;time.sleep(.005)
            assert rc>=3 and report.raw[:3]==bytes([1,7,251]),(rc,report.raw[:8])
            print('mouse report PASS',flush=True)
            # Short descriptors must report the data-stage residual, not status length.
            rc=lib.test_control(0,128,6,0x100,0,report,64);assert rc==18,('short',rc)
            # Stall then reset endpoint and move dequeue: next request must succeed.
            assert lib.test_control(0,128,255,0,0,report,8)==-5
            assert lib.test_control(0,128,6,0x100,0,report,18)==18
            print('short control / endpoint stall recovery PASS',flush=True)
            for i in range(180):
                assert lib.test_control(0,128,6,0x100,0,report,18)==18,('EP0 ring wrap',i)
            print('control transfer ring cycle wrap PASS',flush=True)
            assert lib.test_release(1)==0
            m.qmp('device_del',{'id':'mouse'});time.sleep(.05)
            m.qmp('device_add',{'driver':'usb-hub','id':'hub','bus':'xhci.0','port':'2'})
            m.qmp('device_add',{'driver':'usb-kbd','id':'child','bus':'xhci.0','port':'2.1'})
            time.sleep(.05)
            assert lib.test_enumerate(1,ports[1],0)==0
            rc=lib.test_enumerate(2,1,2);assert rc==0,('hub child',rc)
            assert lib.test_control(2,128,6,0x100,0,report,18)==18
            assert lib.test_release(2)==0;assert lib.test_release(1)==0
            m.qmp('device_del',{'id':'hub'});time.sleep(.05)
            print('external USB 2 hub / route string PASS',flush=True)
            for i in range(130):
                assert lib.test_release(0)==0
                rc=lib.test_enumerate(0,ports[0],0);assert rc==0,(i,rc)
            print('command, transfer and event ring wrap / slot reuse PASS',flush=True)
            m.qmp('device_del',{'id':'kbd'});time.sleep(.05)
            assert lib.test_status(ports[0])==0
            assert lib.test_release(0)==0
            assert lib.test_stop()==0
            assert not m.dma,('DMA leak',len(m.dma))
            assert m.failure is None,m.failure
            print('hot unplug / halted DMA reclamation PASS',flush=True)
            # Allocation failure part way through start must roll back all pages.
            m.fail_alloc=2
            assert lib.test_start(C.byref(api),0x10000000)==-12
            assert not m.dma,('start failure leaked DMA',len(m.dma))
            m.fail_alloc=None
            assert lib.test_start(C.byref(api),0x10000000)==0
            m.qmp('device_add',{'driver':'usb-kbd','id':'timeout-kbd','bus':'xhci.0','port':'2'});time.sleep(.05)
            m.drop_command=True
            # Enable Slot can no longer complete: driver must halt DMA before free.
            assert lib.test_enumerate(0,ports[1],0)==-110
            assert lib.test_stop()==0 and not m.dma
            m.drop_command=False
            print('allocation rollback / command timeout / halt before free PASS',flush=True)
            assert lib.test_start(C.byref(api),0x10000000)==0
            m.drop_command=True;m.fail_halt=True
            assert lib.test_enumerate(0,ports[1],0)==-110
            assert lib.test_stop()==-110
            assert m.dma,'failed halt must retain controller-owned DMA pages'
            m.fail_halt=False
            assert lib.test_stop()==0 and not m.dma
            m.drop_command=False
            print('failed halt retains DMA until stop is confirmed PASS',flush=True)
        finally:m.close()
if __name__=='__main__':main()
