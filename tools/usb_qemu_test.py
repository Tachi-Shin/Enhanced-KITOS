#!/usr/bin/env python3
"""Real QEMU USB enumeration, HID input, hubs and hotplug; no host USB passthrough."""
import argparse
import json
import os
from pathlib import Path
import select
import selectors
import subprocess
import tempfile
import time
from types import SimpleNamespace
from qemu_test import Machine
from test_images import create
import process_test
import stage_drivers


class USBMachine(Machine):
    def __init__(self, qemu, kernel, directory, devices, disk=None, extra_args=(), serial_index=1):
        # QMP uses process stdio; guest UART uses filesystem FIFOs. No listener
        # socket, physical USB device or existing disk image is needed.
        serial = directory / 'serial'
        for suffix in ('.in', '.out'):
            os.mkfifo(str(serial) + suffix)
        self.serial_in = os.fdopen(os.open(str(serial) + '.in', os.O_RDWR | os.O_NONBLOCK), 'wb', buffering=0)
        self.serial_out = os.fdopen(os.open(str(serial) + '.out', os.O_RDWR | os.O_NONBLOCK), 'rb')
        self.stderr = (directory / 'qemu-stderr.txt').open('w+')
        assert serial_index in (0,1)
        serials=['null','null'];serials[serial_index]='pipe:'+str(serial)
        command = [qemu, '-M', 'raspi4b', '-m', '2g', '-display', 'none', '-monitor', 'none',
                   '-kernel', kernel, '-serial', serials[0], '-serial', serials[1], '-qmp', 'stdio']
        for device in devices:
            command += ['-device', device]
        command += list(extra_args)
        if disk is not None:
            command += ['-drive', 'if=sd,file=' + str(disk) + ',format=raw']
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=self.stderr, bufsize=0)
        self.p = SimpleNamespace(stdin=self.serial_in, stdout=self.serial_out,
                                 terminate=self.process.terminate, wait=self.process.wait, kill=self.process.kill)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.serial_out, selectors.EVENT_READ)
        self.log = bytearray()
        self.qmp_bytes = bytearray()
        self.qmp_id = 0
        self.qmp_events = []
        try:
            greeting = self.qmp_read()
            if 'QMP' not in greeting:
                raise RuntimeError(str(greeting))
            self.qmp('qmp_capabilities')
        except Exception:
            self.close()
            raise

    def qmp_read(self, timeout=5):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if b'\n' in self.qmp_bytes:
                line, _, remainder = self.qmp_bytes.partition(b'\n')
                self.qmp_bytes = bytearray(remainder)
                return json.loads(line)
            if select.select([self.process.stdout], [], [], .1)[0]:
                chunk = os.read(self.process.stdout.fileno(), 8192)
                if not chunk:
                    self.stderr.flush(); self.stderr.seek(0)
                    raise RuntimeError('QEMU QMP exited: ' + self.stderr.read())
                self.qmp_bytes.extend(chunk)
        raise RuntimeError('QMP response timeout')

    def qmp(self, command, arguments=None):
        self.qmp_id += 1
        request = {'execute': command, 'id': self.qmp_id}
        if arguments is not None:
            request['arguments'] = arguments
        self.process.stdin.write(json.dumps(request).encode() + b'\n')
        self.process.stdin.flush()
        while True:
            response = self.qmp_read()
            if 'event' in response:
                self.qmp_events.append(response)
                continue
            assert response.get('id') == self.qmp_id, response
            if 'error' in response:
                raise RuntimeError(f'{command}: {response}')
            return response['return']

    def key(self, name, down):
        self.qmp('input-send-event', {'events': [
            {'type': 'key', 'data': {'down': down, 'key': {'type': 'qcode', 'data': name}}}]})
        time.sleep(.035)

    def until(self, expected, timeout=5):
        data = bytearray(); end = time.monotonic() + timeout
        while time.monotonic() < end:
            for key, _ in self.selector.select(.1):
                chunk = key.fileobj.read1(8192)
                if not chunk: raise RuntimeError('UART exited')
                data.extend(chunk); self.log.extend(chunk)
                if expected in data: return data.decode(errors='replace')
        raise RuntimeError('UART timeout: ' + data.decode(errors='replace'))

    def type_command(self, text):
        mapping = {' ': 'spc', '-': 'minus', '\r': 'ret'}
        for char in text + '\r':
            key = mapping.get(char, char)
            self.key(key, True); self.key(key, False)
        return self.prompt()

    def usb_wait(self, count, keyboard=0, mouse=0):
        end = time.monotonic() + 10
        while time.monotonic() < end:
            output = self.command('usb')
            if (f'devices={count} failures=0' in output and
                output.count('protocol=1 driver=boot-hid') == keyboard and
                output.count('protocol=2 driver=boot-hid') == mouse):
                return output
            time.sleep(.15)
        raise AssertionError('USB enumeration did not settle: ' + output)

    def close(self):
        super().close()
        self.process.stdin.close(); self.process.stdout.close()
        self.serial_in.close(); self.serial_out.close(); self.stderr.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu', default='qemu-system-aarch64')
    parser.add_argument('--kernel', required=True)
    parser.add_argument('--user-elf', default='build/usr/probe.elf')
    args = parser.parse_args()
    directory = Path('build/tests'); directory.mkdir(parents=True, exist_ok=True)
    transcript = bytearray()
    with tempfile.TemporaryDirectory(prefix='usb-', dir=directory) as temp:
        disk = Path(temp) / 'sd.img'; create(disk); process_test.stage(disk, args.user_elf); stage_drivers.stage(disk)
        machine = USBMachine(args.qemu, args.kernel, Path(temp), ['usb-hub,id=hub',
            'usb-kbd,id=kbd,port=1.1', 'usb-mouse,id=mouse,port=1.2'], disk)
        try:
            machine.prompt()
            machine.usb_wait(3, keyboard=1, mouse=1)
            assert 'usb-ok\r\n' in machine.type_command('echo usb-ok')
            events = machine.command('input')
            assert 'key=8 value=1' in events and 'key=8 value=0' in events
            machine.qmp('input-send-event', {'events': [
                {'type': 'rel', 'data': {'axis': 'x', 'value': 7}},
                {'type': 'rel', 'data': {'axis': 'y', 'value': -5}},
                {'type': 'btn', 'data': {'button': 'left', 'down': True}}]})
            time.sleep(.05)
            events = machine.command('input')
            assert 'mouse buttons=1 dx=7 dy=-5' in events, events
            machine.qmp('input-send-event', {'events': [{'type': 'btn', 'data': {'button': 'left', 'down': False}}]})
            time.sleep(.05)
            assert 'mouse buttons=0' in machine.command('input')
            machine.serial_in.write(b'run /PROBE.ELF input\r')
            machine.until(b'[user-input] ready')
            machine.qmp('input-send-event', {'events': [
                {'type': 'rel', 'data': {'axis': 'x', 'value': 13}},
                {'type': 'rel', 'data': {'axis': 'y', 'value': -9}},
                {'type': 'btn', 'data': {'button': 'left', 'down': True}}]})
            result = machine.prompt()
            assert '[user-input] mouse/uaccess/batch PASS' in result and 'status=0' in result, result
            machine.qmp('input-send-event', {'events': [{'type': 'btn', 'data': {'button': 'left', 'down': False}}]})
            time.sleep(.05); machine.command('input')
            # A held modifier must be released even when the device disappears.
            machine.key('shift', True)
            machine.qmp('device_del', {'id': 'kbd'})
            machine.usb_wait(2, mouse=1)
            events = machine.command('input')
            assert 'key=225 value=1' in events and 'key=225 value=0' in events, events
            for _ in range(2):
                machine.qmp('device_add', {'driver': 'usb-kbd', 'id': 'kbd', 'port': '1.1'})
                machine.usb_wait(3, keyboard=1, mouse=1)
                assert 'reuse\r\n' in machine.type_command('echo reuse')
                machine.command('input')
                machine.qmp('device_del', {'id': 'kbd'})
                machine.usb_wait(2, mouse=1)
            # An unsupported report-protocol interface stays visible/unbound.
            machine.qmp('device_add', {'driver': 'usb-tablet', 'id': 'tablet', 'port': '1.3'})
            assert 'protocol=0 driver=unbound' in machine.usb_wait(3, mouse=1)
            machine.qmp('device_del', {'id': 'tablet'}); machine.usb_wait(2, mouse=1)
            # Nested hub removal must release its entire descendant subtree.
            machine.qmp('device_add', {'driver': 'usb-hub', 'id': 'inner', 'port': '1.1'})
            machine.usb_wait(3, mouse=1)
            machine.qmp('device_add', {'driver': 'usb-kbd', 'id': 'nested', 'port': '1.1.1'})
            machine.usb_wait(4, keyboard=1, mouse=1)
            assert 'nested\r\n' in machine.type_command('echo nested')
            machine.command('input')
            machine.key('shift', True)
            machine.qmp('device_del', {'id': 'inner'}); machine.usb_wait(2, mouse=1)
            assert 'key=225 value=0' in machine.command('input')
            machine.qmp('device_del', {'id': 'mouse'})
            machine.usb_wait(1)
            machine.qmp('device_del', {'id': 'hub'})
            machine.usb_wait(0)
            machine.command('memtest', 'MMU/page/VM/uaccess/reclaim PASS')
            machine.command('selftest', 'preempt/sleep PASS')
        finally:
            machine.close(); transcript.extend(machine.log)
            (directory / 'usb-qemu-transcript.txt').write_bytes(transcript)
    # Explicit port=1 avoids QEMU's automatic hub and exercises root routing.
    with tempfile.TemporaryDirectory(prefix='usb-root-', dir=directory) as temp:
        machine = USBMachine(args.qemu, args.kernel, Path(temp), ['usb-kbd,id=kbd,port=1'])
        try:
            machine.prompt()
            assert 'speed=2' in machine.usb_wait(1, keyboard=1)
            assert 'direct\r\n' in machine.type_command('echo direct')
            machine.qmp('device_del', {'id': 'kbd'}); machine.usb_wait(0)
            machine.qmp('device_add', {'driver': 'usb-mouse', 'id': 'mouse', 'port': '1', 'usb_version': 1})
            assert 'speed=1' in machine.usb_wait(1, mouse=1)
            machine.qmp('device_del', {'id': 'mouse'}); machine.usb_wait(0)
        finally:
            machine.close(); transcript.extend(machine.log)
            (directory / 'usb-qemu-transcript.txt').write_bytes(transcript)
    print('QEMU USB hub/keyboard/mouse/console/EL0-input/hotplug/release/reuse/root/regression: PASS')


if __name__ == '__main__':
    main()
