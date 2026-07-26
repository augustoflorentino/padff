import fcntl
import os
import select
import struct
import subprocess
import sys
import time

EV_SYN, EV_KEY, EV_ABS, EV_FF, EV_UINPUT = 0x00, 0x01, 0x03, 0x15, 0x0101
UI_FF_UPLOAD, UI_FF_ERASE = 1, 2
FF_RUMBLE, FF_PERIODIC, FF_CONSTANT, FF_RAMP, FF_GAIN = 0x50, 0x51, 0x52, 0x57, 0x60
FF_SINE, FF_SQUARE, FF_TRIANGLE = 0x5a, 0x58, 0x59
BUTTONS = list(range(0x130, 0x13f))
AXES = {0: (0, -32768, 32767, 16, 128, 0), 1: (0, -32768, 32767, 16, 128, 0),
        3: (0, -32768, 32767, 16, 128, 0), 4: (0, -32768, 32767, 16, 128, 0),
        2: (0, 0, 255, 0, 0, 0), 5: (0, 0, 255, 0, 0, 0),
        0x10: (0, -1, 1, 0, 0, 0), 0x11: (0, -1, 1, 0, 0, 0)}

EVENT = struct.Struct('llHHi')
UPLOAD = struct.Struct('Ii48s48s')
ERASE = struct.Struct('IiI')


def ioc(direction, number, size=0):
    return (direction << 30) | (size << 16) | (ord('U') << 8) | number


UI_DEV_CREATE, UI_DEV_DESTROY = ioc(0, 1), ioc(0, 2)
UI_DEV_SETUP, UI_ABS_SETUP = ioc(1, 3, 92), ioc(1, 4, 28)
UI_SET_EVBIT, UI_SET_KEYBIT, UI_SET_ABSBIT, UI_SET_FFBIT = (ioc(1, n, 4) for n in (100, 101, 103, 107))
UI_BEGIN_FF_UPLOAD, UI_END_FF_UPLOAD = ioc(3, 200, UPLOAD.size), ioc(1, 201, UPLOAD.size)
UI_BEGIN_FF_ERASE, UI_END_FF_ERASE = ioc(3, 202, ERASE.size), ioc(1, 203, ERASE.size)


def create():
    fd = os.open('/dev/uinput', os.O_RDWR | os.O_NONBLOCK)
    for ev in (EV_KEY, EV_ABS, EV_FF):
        fcntl.ioctl(fd, UI_SET_EVBIT, ev)
    for key in BUTTONS:
        fcntl.ioctl(fd, UI_SET_KEYBIT, key)
    for axis, info in AXES.items():
        fcntl.ioctl(fd, UI_SET_ABSBIT, axis)
        fcntl.ioctl(fd, UI_ABS_SETUP, struct.pack('Hxx6i', axis, *info))
    for ff in (FF_RUMBLE, FF_PERIODIC, FF_CONSTANT, FF_RAMP, FF_GAIN, FF_SINE, FF_SQUARE, FF_TRIANGLE):
        fcntl.ioctl(fd, UI_SET_FFBIT, ff)
    setup = struct.pack('4H80sI', 3, 0x045e, 0x028e, 0x110, b'padff recorder pad', 16)
    fcntl.ioctl(fd, UI_DEV_SETUP, setup)
    fcntl.ioctl(fd, UI_DEV_CREATE)
    return fd


def envelope(raw, offset):
    attack_ms, attack_level, fade_ms, fade_level = struct.unpack_from('4H', raw, offset)
    return attack_ms, min(attack_level, 0x7fff) * 2, fade_ms, min(fade_level, 0x7fff) * 2


def convert(raw):
    kind, ident, _, _, _, length, delay = struct.unpack_from('HhHHHHH', raw)
    strong = weak = level = level_end = 0
    env = (0, 0, 0, 0)
    if kind == FF_RUMBLE:
        ptype = 1
        strong, weak = struct.unpack_from('HH', raw, 16)
    elif kind == FF_PERIODIC:
        ptype = 2
        level = struct.unpack_from('h', raw, 20)[0]
        env = envelope(raw, 26)
    elif kind == FF_CONSTANT:
        ptype = 3
        level = struct.unpack_from('h', raw, 16)[0]
        env = envelope(raw, 18)
    elif kind == FF_RAMP:
        ptype = 4
        level, level_end = struct.unpack_from('hh', raw, 16)
        env = envelope(raw, 20)
    else:
        return ident, None
    return ident, (ptype, strong, weak, level, level_end, delay, length) + env


class Recorder:
    def __init__(self, fd):
        self.fd = fd
        self.lines = []
        self.origin = None

    def stamp(self, sec, usec):
        us = sec * 1000000 + usec
        if self.origin is None:
            self.origin = us
        return (us - self.origin) // 1000

    def upload(self, ms, request):
        buf = bytearray(UPLOAD.pack(request, 0, bytes(48), bytes(48)))
        fcntl.ioctl(self.fd, UI_BEGIN_FF_UPLOAD, buf)
        _, _, effect, old = UPLOAD.unpack(buf)
        ident, fields = convert(effect)
        retval = 0 if fields else -22
        fcntl.ioctl(self.fd, UI_END_FF_UPLOAD, UPLOAD.pack(request, retval, effect, old))
        if fields:
            self.lines.append('%d upload %d %s' % (ms, ident, ' '.join(map(str, fields))))

    def erase(self, ms, request):
        buf = bytearray(ERASE.pack(request, 0, 0))
        fcntl.ioctl(self.fd, UI_BEGIN_FF_ERASE, buf)
        _, _, ident = ERASE.unpack(buf)
        fcntl.ioctl(self.fd, UI_END_FF_ERASE, ERASE.pack(request, 0, ident))
        self.lines.append('%d erase %d' % (ms, ident))

    def handle(self, sec, usec, kind, code, value):
        if kind not in (EV_UINPUT, EV_FF):
            return
        ms = self.stamp(sec, usec)
        if kind == EV_UINPUT and code == UI_FF_UPLOAD:
            self.upload(ms, value)
        elif kind == EV_UINPUT and code == UI_FF_ERASE:
            self.erase(ms, value)
        elif code == FF_GAIN:
            self.lines.append('%d gain %d' % (ms, value))
        else:
            self.lines.append('%d play %d %d' % (ms, code, value))

    def drain(self):
        try:
            data = os.read(self.fd, EVENT.size * 64)
        except BlockingIOError:
            return
        for i in range(0, len(data), EVENT.size):
            self.handle(*EVENT.unpack_from(data, i))


def main():
    driver, scenario, out = sys.argv[1:4]
    fd = create()
    time.sleep(0.5)
    rec = Recorder(fd)
    child = subprocess.Popen([driver, scenario])
    while child.poll() is None:
        select.select([fd], [], [], 0.05)
        rec.drain()
    rec.drain()
    fcntl.ioctl(fd, UI_DEV_DESTROY)
    with open(out, 'w') as f:
        f.write('# recorded from SDL3 %s on Linux uinput\n' % scenario)
        f.write('\n'.join(rec.lines) + '\n')
    return child.returncode


if __name__ == '__main__':
    sys.exit(main())
