#!/usr/bin/env python3
"""EKITOS + STORAGE.EKM + real QEMU USB disks, FAT family through exFAT."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
from test_images import create
from stage_drivers import stage
from usb_qemu_test import USBMachine
from process_test import free_pages


def mounted(m,count=1):
    end=time.monotonic()+10
    while time.monotonic()<end:
        out=m.command('disks')
        if out.count('mount=/media/usb')==count:return out
        time.sleep(.05)
    raise AssertionError('USB mount did not settle: '+out)


def check_image(path,kind):
    checker=Path('build/deps/exfat-utils-1.4.0/fsck/exfatfsck')
    if kind=='exfat' and checker.exists():subprocess.run([str(checker),'-n',str(path)],check=True)
    if kind.startswith('fat'):
        checker=shutil.which('fsck.fat') or shutil.which('fsck_msdos')
        if checker:subprocess.run([checker,'-n',str(path)],check=True)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--qemu',default='qemu-system-aarch64');p.add_argument('--kernel',required=True);a=p.parse_args()
    root=Path('build/tests');transcript=bytearray()
    for kind in ('fat12','fat16','fat32','exfat','exfat4k','exfat-mbr','exfat-gpt'):
        with tempfile.TemporaryDirectory(prefix='msc-'+kind+'-',dir=root) as tmp:
            tmp=Path(tmp);sd=tmp/'sd.img';create(sd);stage(sd)
            image=tmp/'usb.img';shutil.copyfile(root/'fat-family'/(kind+'.img'),image)
            sector=',logical_block_size=4096,physical_block_size=4096' if kind=='exfat4k' else ''
            m=USBMachine(a.qemu,a.kernel,tmp,['usb-storage,id=storage,drive=mass,port=1'+sector],sd,
                         extra_args=['-drive',f'if=none,id=mass,file={image},format=raw'])
            try:
                m.prompt();out=mounted(m)
                m.command('ls /media','usb0/')
                assert 'usb0/' not in m.command('ls /'), 'USB mount leaked into root listing'
                if sector:assert 'sector=4096' in out,out
                m.command('write /media/usb0/persist.txt usb-'+kind)
                m.command('cat /media/usb0/persist.txt','usb-'+kind)
                m.command('mkdir /media/usb0/work')
                m.command('write "/media/usb0/work/日本語 long name.txt" unicode-ok')
                m.command('append "/media/usb0/work/日本語 long name.txt" +append')
                m.command('cat "/media/usb0/work/日本語 long name.txt"','unicode-ok+append')
                m.command('mv "/media/usb0/work/日本語 long name.txt" /media/usb0/renamed.txt')
                m.command('rmdir /media/usb0/work')
                m.command('truncate /media/usb0/renamed.txt 7')
                m.command('cat /media/usb0/renamed.txt','unicode')
                m.command('umount /media/usb0');m.command('mount 1 /media/usb0')
                m.command('cat /media/usb0/persist.txt','usb-'+kind)
                assert '-16' in m.command('rmmod usb-storage',allow_error=True)
                m.command('umount /media/usb0')
                for _ in range(100):
                    out=m.command('rmmod usb-storage',allow_error=True)
                    if 'unloaded usb-storage' in out:break
                    assert '-16' in out,out;time.sleep(.01)
                else:raise AssertionError('storage unload stayed busy')
                print(kind+': enumerate/RW/Unicode/rename/truncate/remount/module pin PASS',flush=True)
            finally:
                m.close();transcript.extend(m.log);(root/'storage-qemu-transcript.txt').write_bytes(transcript)
            check_image(image,kind)
    # Hub, independent disks, RO media, and unplug while an EL0 descriptor is open.
    with tempfile.TemporaryDirectory(prefix='msc-hotplug-',dir=root) as tmp:
        tmp=Path(tmp);sd=tmp/'sd.img';create(sd);stage(sd)
        subprocess.run(['mcopy','-i',str(sd),'build/usr/probe.elf','::PROBE.ELF'],check=True)
        rw=tmp/'rw.img';ro=tmp/'ro.img';shutil.copyfile(root/'fat-family/exfat.img',rw);shutil.copyfile(root/'fat-family/fat16.img',ro)
        m=USBMachine(a.qemu,a.kernel,tmp,['usb-hub,id=hub','usb-storage,id=storage,drive=rw,port=1.1',
            'usb-storage,id=readonly,drive=ro,port=1.2'],sd,extra_args=['-drive',f'if=none,id=rw,file={rw},format=raw',
            '-drive',f'if=none,id=ro,file={ro},format=raw,readonly=on'])
        try:
            m.prompt();out=mounted(m,2);assert 'ro=1' in out,out
            m.command('write /media/usb0/persist.txt held-descriptor')
            assert '-30' in m.command('write /media/usb1/refused.txt nope',allow_error=True)
            baseline=free_pages(m)
            m.serial_in.write(b'run /PROBE.ELF usb-unplug /media/usb0/persist.txt\r')
            m.until(b'[user-usb] ready');m.qmp('device_del',{'id':'storage'})
            out=m.prompt();assert '[user-usb] unplug/ENODEV/close PASS' in out and 'status=0' in out,out
            mounted(m,1)
            assert free_pages(m)==baseline
            m.qmp('blockdev-add',{'driver':'raw','node-name':'rw2','file':{'driver':'file','filename':str(rw.resolve())}})
            m.qmp('device_add',{'driver':'usb-storage','id':'storage2','drive':'rw2','port':'1.1'})
            mounted(m,2);m.command('cat /media/usb0/persist.txt','held-descriptor')
            m.qmp('device_del',{'id':'hub'});mounted(m,0)
            m.command('memtest','MMU/page/VM/uaccess/reclaim PASS');m.command('selftest','preempt/sleep PASS')
            print('USB hub/multiple disks/write protection/EL0 open-FD unplug/replug/reclaim PASS',flush=True)
        finally:
            m.close();transcript.extend(m.log);(root/'storage-qemu-transcript.txt').write_bytes(transcript)
    # Two logical units behind one BOT interface must not alias one another.
    with tempfile.TemporaryDirectory(prefix='msc-luns-',dir=root) as tmp:
        tmp=Path(tmp);sd=tmp/'sd.img';create(sd);stage(sd)
        for n,kind in enumerate(('exfat','fat16')):shutil.copyfile(root/'fat-family'/(kind+'.img'),tmp/f'lun{n}.img')
        m=USBMachine(a.qemu,a.kernel,tmp,['usb-bot,id=multi,port=1',
            'scsi-hd,id=lun0,drive=disk0,bus=multi.0,scsi-id=0,lun=0',
            'scsi-hd,id=lun1,drive=disk1,bus=multi.0,scsi-id=0,lun=1'],sd,
            extra_args=['-drive',f'if=none,id=disk0,file={tmp}/lun0.img,format=raw',
                        '-drive',f'if=none,id=disk1,file={tmp}/lun1.img,format=raw'])
        try:
            m.prompt();mounted(m,2)
            m.command('write /media/usb0/lun.txt lun-zero');m.command('write /media/usb1/lun.txt lun-one')
            m.command('cat /media/usb0/lun.txt','lun-zero');m.command('cat /media/usb1/lun.txt','lun-one')
            m.command('umount /media/usb0');m.command('umount /media/usb1')
            print('one BOT interface / two independent SCSI LUNs PASS',flush=True)
        finally:
            m.close();transcript.extend(m.log);(root/'storage-qemu-transcript.txt').write_bytes(transcript)
    print('EKITOS USB storage FAT12/16/32/exFAT integration: PASS',flush=True)
if __name__=='__main__':main()
