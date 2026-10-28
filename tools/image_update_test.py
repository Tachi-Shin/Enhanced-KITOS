#!/usr/bin/env python3
"""Update an old shell without losing user data/config/history/custom commands."""
from pathlib import Path
import subprocess
import tempfile
from rootfs import stage
from run_image import prepare,current_shell
from test_images import create


def main():
    root=Path('build/tests');root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='image-update-',dir=root) as directory:
        directory=Path(directory);disk=directory/'system.img';create(disk);stage(disk)
        payloads={'etc/profile':b'export PATH=/custom:/bin:/sbin\n','root/.sh_history':b'echo keep-history\n',
                  'HELLO.TXT':b'user data must survive\n','bin/custom':b'custom program\n','bin/sh':b'old shell'}
        for name,data in payloads.items():
            source=directory/'source';source.write_bytes(data)
            subprocess.run(['mcopy','-o','-i',str(disk),str(source),'::'+name],check=True)
        assert not current_shell(disk)
        before=disk.read_bytes();backup=prepare(disk,True)
        assert backup.read_bytes()==before and current_shell(disk)
        for name,data in payloads.items():
            target=directory/'readback'
            subprocess.run(['mcopy','-o','-i',str(disk),'::'+name,str(target)],check=True)
            assert target.read_bytes()==(Path('build/usr/bin/sh').read_bytes() if name=='bin/sh' else data),name
        # Starting an existing image is read-only, including an intentionally
        # different shell: report the discrepancy instead of replacing it.
        source=directory/'source';source.write_bytes(b'custom shell')
        subprocess.run(['mcopy','-o','-i',str(disk),str(source),'::bin/sh'],check=True)
        before=disk.read_bytes();assert prepare(disk) is None;assert disk.read_bytes()==before
    print('Image update backup / current sh / config+history+user data+custom command preservation PASS')


if __name__=='__main__':main()
