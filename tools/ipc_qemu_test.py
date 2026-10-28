#!/usr/bin/env python3
"""Real EL0 IPC + concurrent shell pipes and repeated ownership cleanup."""
import argparse
from pathlib import Path
import tempfile
import shutil
from qemu_test import Machine
from test_images import create
from rootfs import stage
from process_test import free_pages

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--qemu',default='qemu-system-aarch64');p.add_argument('--kernel',required=True);p.add_argument('--image');a=p.parse_args()
 root=Path('build/tests');root.mkdir(exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='ipc-',dir=root) as tmp:
  disk=Path(tmp)/'sd.img'
  if a.image:shutil.copyfile(a.image,disk)
  else:create(disk,fixture_files=False);stage(disk)
  m=Machine(a.qemu,a.kernel,disk)
  try:
   m.prompt();baseline=free_pages(m)
   for _ in range(3):
    m.command('ipc-probe','kill/reclaim PASS',timeout=40)
    assert free_pages(m)==baseline,'IPC process/object/VM leak'
   for line,expected in [('echo hello | cat','hello'),('echo hello|cat|cat|cat','hello'),("echo 'a|b' | cat",'a|b'),('pwd | cat','/'),('export | cat','export PATH='),("echo a\\|b | cat",'a|b'),("echo '$HOME' | cat",'$HOME')]:m.command(line,expected)
   m.command('echo eight | cat | cat | cat | cat | cat | cat | cat','eight')
   m.command('cd /tmp | cat');assert '\r\n/\r\n' in m.command('pwd')
   m.command('false | true');assert '\r\n0\r\n' in m.command('echo $?')
   m.command('true | false');assert '\r\n1\r\n' in m.command('echo $?')
   for line in ('echo bad |','| cat','echo bad || cat','echo bad | cat && echo sideeffect'):
    m.command(line,'invalid pipeline')
   m.command('echo bad | absent-command','error -2',allow_error=True)
   for _ in range(8):m.command('echo cleanup | cat | cat','cleanup')
   assert free_pages(m)==baseline,'pipeline leaked pages'
   print('IPC EL0 + quote-aware concurrent shell pipes + EOF + status + rollback + repeated reclaim PASS',flush=True)
  finally:
   m.close();(root/'ipc-qemu-transcript.txt').write_bytes(m.log)
if __name__=='__main__':main()
