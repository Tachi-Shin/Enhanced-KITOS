#!/usr/bin/env python3
"""Shell editing + USB navigation + actual raspi4 framebuffer pixels, on new media."""
import argparse
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import time
import zlib
from qemu_test import Machine
from usb_qemu_test import USBMachine
from test_images import create
from rootfs import stage


def png(path, width, height, pixels):
    def chunk(kind,data):
        return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    rows=b''.join(b'\0'+pixels[y*width*3:(y+1)*width*3] for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,2,0,0,0))+
                    chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b''))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--qemu',default='qemu-system-aarch64');p.add_argument('--kernel',required=True)
    p.add_argument('--image',help='Test a copy of this existing regular-file image; do not restage its programs')
    a=p.parse_args()
    root=Path('build/tests');root.mkdir(exist_ok=True);transcript=bytearray()
    with tempfile.TemporaryDirectory(prefix='terminal-',dir=root) as directory:
        directory=Path(directory);disk=directory/'sd.img'
        if a.image:
            source=Path(a.image)
            if not source.is_file():raise RuntimeError('Regular-file source image required')
            shutil.copyfile(source,disk);print('Testing a copy of existing image:',source,flush=True)
        else:create(disk,fixture_files=False);stage(disk)
        sample=directory/'screen.txt'
        sample.write_bytes(b'\x1b[2J\x1b[H\x1b[?25l\x1b[38;2;255;0;0mA\x1b[0mSCII 6x8 framebuffer\n'
            b'\x1b[31mRED \x1b[32mGREEN \x1b[34mBLUE \x1b[33mYELLOW \x1b[36mCYAN \x1b[35mMAGENTA\x1b[0m\n'
            b'ABCDEFGHIJKLMNOPQRSTUVWXYZ\nabcdefghijklmnopqrstuvwxyz\n0123456789 !@#$%^&*() []{} /\\\n'
            b'UART + HDMI | history: Up/Down | completion: Tab\n')
        subprocess.run(['mcopy','-i',str(disk),str(sample),'::tmp/screen.txt'],check=True)
        m=USBMachine(a.qemu,a.kernel,directory,['usb-kbd,id=kbd,bus=usb-bus.0'],disk)
        try:
            assert '[shell] /bin/sh ready' in m.prompt()
            m.command('/bin/sh --version','Tab=command,path,candidates')
            display=m.command('display');assert 'font=6x8 ASCII' in display and 'virtual=1' in display
            assert 'refresh=unknown' in display and 'EDID=0' in display
            m.usb_wait(2,keyboard=1)
            m.command('history -c');m.command('echo history-one','history-one');m.command('echo history-two','history-two')
            result=m.command('\x1b[A\x1b[A');assert '\r\nhistory-one\r\n' in result
            result=m.command('echo draft\x1b[A\x1b[B');assert '\r\ndraft\r\n' in result
            m.command('ec\thello-tab','hello-tab')
            m.command("write '/tmp/space name.txt' completed-path")
            assert 'completed-path' in m.command('cat /tmp/sp\t')
            m.command('cd /tm\t');assert '/tmp' in m.command('pwd');m.command('cd /')
            m.command('clear');m.p.stdin.write(b'e\t');m.p.stdin.flush()
            candidates=m.until(b'export');assert 'echo\r\n' in candidates and 'env\r\n' in candidates
            # Capture while Tab candidates are visible and the line remains
            # editable; no Enter/command execution is needed to show the list.
            frame=root/'tab-candidates-screen.ppm';end=time.monotonic()+5
            while True:
                m.qmp('screendump',{'filename':str(frame.resolve())})
                header,dimensions,maximum,pixels=frame.read_bytes().split(b'\n',3)
                width,height=map(int,dimensions.split());assert header==b'P6' and maximum==b'255'
                offset=(10*width+1)*3
                if pixels[offset:offset+3]==bytes((208,215,222)):break
                assert time.monotonic()<end,'Tab candidates did not render on framebuffer'
                time.sleep(.05)
            png(root/'tab-candidates-screen.png',width,height,pixels)
            m.command('\x15echo candidates','candidates')
            assert '\r\nabc\r\n' in m.command('echo ac\x1b[Db')
            m.command('echo usb-history','usb-history')
            m.key('up',True);m.key('up',False);m.key('ret',True);m.key('ret',False)
            assert '\r\nusb-history\r\n' in m.prompt()
            m.p.stdin.write(b'ec');m.p.stdin.flush();m.key('tab',True);m.key('tab',False)
            assert '\r\nusb-tab\r\n' in m.command('usb-tab')
            m.command('cat /tmp/screen.txt','ASCII 6x8 framebuffer')
            frame=root/'terminal-screen.ppm';end=time.monotonic()+8
            while True:
                m.qmp('screendump',{'filename':str(frame.resolve())})
                header,width_height,maximum,pixels=frame.read_bytes().split(b'\n',3)
                width,height=map(int,width_height.split());assert header==b'P6' and maximum==b'255'
                assert len(pixels)==width*height*3
                def rgb(x,y):return tuple(pixels[(y*width+x)*3:(y*width+x+1)*3])
                if rgb(1,0)==(255,0,0):break
                assert time.monotonic()<end,'framebuffer failed to render'
                time.sleep(.1)
            glyph=[14,17,17,31,17,17,17,0]
            for y,row in enumerate(glyph):
                for x in range(6):assert rgb(x,y)==((255,0,0) if x<5 and row&(16>>x) else (16,24,32)),(x,y,rgb(x,y))
            assert len(set(tuple(pixels[i:i+3]) for i in range(0,len(pixels),3)))>=7
            png(root/'terminal-screen.png',width,height,pixels)
            m.command('echo persistent-history-marker','persistent-history-marker')
        finally:m.close();transcript.extend(m.log)
        m=Machine(a.qemu,a.kernel,disk)
        try:
            m.prompt();result=m.command('\x1b[A');assert '\r\npersistent-history-marker\r\n' in result
            history=m.command('history');assert 'hello-tab' in history and 'persistent-history-marker' in history
            m.command('history -c');m.command('history');m.command('display','font=6x8 ASCII')
        finally:m.close();transcript.extend(m.log)
        history=directory/'history.txt';subprocess.run(['mcopy','-i',str(disk),'::root/.sh_history',str(history)],check=True)
        assert len(history.read_text().splitlines())<=64
    (root/'terminal-qemu-transcript.txt').write_bytes(transcript)
    print('Persistent history / editing / command+path Tab / USB Up / UART+HDMI / exact 6x8 RGB pixels PASS')
    print('QEMU EDID/timing unavailable: actual output geometry retained; refresh reported unknown')


if __name__=='__main__':main()
