import random
import subprocess
import sys
import tempfile

FULL = 65535


def trunc_div(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def lerp(a, b, pos, span):
    return a + trunc_div((b - a) * pos, span)


def intensity(level):
    return min(abs(level) * 2, FULL)


def parse(text):
    cfg = {'mix': 0, 'scale': FULL, 'interval': 0, 'end': None}
    events = []
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        parts = line.split()
        if parts[0].isdigit():
            events.append((int(parts[0]), parts[1], [int(x) for x in parts[2:]]))
        else:
            cfg[parts[0]] = int(parts[1])
    last = max([e[0] for e in events], default=0) + 2000
    cfg['end'] = max(cfg['end'] or 0, last)
    return cfg, events


class Effect:
    def __init__(self, a):
        (self.type, self.strong, self.weak, self.level, self.level_end, self.delay, self.length,
         self.attack_ms, self.attack_level, self.fade_ms, self.fade_level) = a[:11]

    def base(self, u):
        if self.type == 1:
            return self.strong, self.weak
        level = self.level
        if self.type == 4 and self.length > 0:
            level = lerp(self.level, self.level_end, u, self.length)
        v = intensity(level)
        return v, v

    def at(self, u):
        values = self.base(u)
        if self.attack_ms and u < self.attack_ms:
            return tuple(lerp(self.attack_level, v, u, self.attack_ms) for v in values)
        left = self.length - u
        if self.fade_ms and self.length and left <= self.fade_ms:
            return tuple(lerp(self.fade_level, v, left, self.fade_ms) for v in values)
        return values


class Slot:
    def __init__(self):
        self.effect = None
        self.playing = False
        self.start = 0
        self.count = 0

    def active(self, t):
        e = self.effect
        if not self.playing or e is None:
            return False
        return e.length == 0 or t - self.start < (e.delay + e.length) * self.count

    def done(self, t):
        e = self.effect
        if not self.playing or e is None or e.length == 0:
            return 0
        return (t - self.start) // (e.delay + e.length)

    def at(self, t):
        e = self.effect
        if not self.playing or e is None:
            return 0, 0
        elapsed = t - self.start
        if e.length:
            if elapsed >= (e.delay + e.length) * self.count:
                return 0, 0
            elapsed %= e.delay + e.length
        if elapsed < e.delay:
            return 0, 0
        return e.at(elapsed - e.delay)


def simulate(text):
    cfg, events = parse(text)
    slots = [Slot() for _ in range(16)]
    gain = FULL
    sent = (0, 0)
    last_send = None
    out = []
    i = 0
    for t in range(cfg['end'] + 1):
        while i < len(events) and events[i][0] <= t:
            _, op, a = events[i]
            i += 1
            if op == 'upload' and 0 <= a[0] < 16 and 1 <= a[1] <= 4:
                slots[a[0]].playing = slots[a[0]].active(t)
                slots[a[0]].count -= slots[a[0]].done(t)
                slots[a[0]].effect = Effect(a[1:])
                slots[a[0]].start = t
            elif op == 'play' and 0 <= a[0] < 16 and slots[a[0]].effect is not None:
                slots[a[0]].playing = a[1] > 0
                slots[a[0]].start = t
                slots[a[0]].count = a[1]
            elif op == 'erase' and 0 <= a[0] < 16:
                slots[a[0]].effect = None
                slots[a[0]].playing = False
            elif op == 'halt' and 0 <= a[0] < 16:
                slots[a[0]].playing = False
            elif op == 'gain':
                gain = a[0]
            elif op == 'stop':
                for s in slots:
                    s.playing = False
        mixed = [0, 0]
        for s in slots:
            for k, v in enumerate(s.at(t)):
                v = v * gain // FULL
                mixed[k] = min(mixed[k] + v, FULL) if cfg['mix'] == 1 else max(mixed[k], v)
        now = tuple((v * cfg['scale'] + FULL // 2) // FULL for v in mixed)
        ready = last_send is None or t - last_send >= cfg['interval']
        if now != sent and ready:
            out.append('%d %d %d' % (t, now[0], now[1]))
            sent = now
            last_send = t
    return out


def random_trace(rng):
    lines = ['mix %d' % rng.randint(0, 1), 'scale %d' % rng.choice([1, 3, 255, 1023, FULL]),
             'interval %d' % rng.choice([0, 0, 1, 8, 16, 33])]
    t = 0
    for _ in range(rng.randint(1, 40)):
        t += rng.choice([0, 0, 1, 5, 16, 50, 200, 700])
        op = rng.random()
        slot = rng.randint(0, 5)
        if op < 0.4:
            fields = [rng.randint(1, 4), rng.randint(0, FULL), rng.randint(0, FULL),
                      rng.randint(-32768, 32767), rng.randint(-32768, 32767),
                      rng.choice([0, 0, 10, 40, 300]), rng.choice([0, 1, 30, 100, 250, 600]),
                      rng.choice([0, 0, 5, 50, 200]), rng.randint(0, FULL),
                      rng.choice([0, 0, 5, 50, 200]), rng.randint(0, FULL)]
            lines.append('%d upload %d %s' % (t, slot, ' '.join(map(str, fields))))
        elif op < 0.75:
            lines.append('%d play %d %d' % (t, slot, rng.choice([0, 1, 1, 2, 3, 5])))
        elif op < 0.8:
            lines.append('%d erase %d' % (t, slot))
        elif op < 0.85:
            lines.append('%d halt %d' % (t, slot))
        elif op < 0.95:
            lines.append('%d gain %d' % (t, rng.randint(0, FULL)))
        else:
            lines.append('%d stop' % t)
    return '\n'.join(lines) + '\n'


def differential(runner, count, seed):
    rng = random.Random(seed)
    with tempfile.TemporaryDirectory() as tmp:
        for n in range(count):
            text = random_trace(rng)
            trace, expected = tmp + '/t.trace', tmp + '/t.expected'
            with open(trace, 'w') as f:
                f.write(text)
            with open(expected, 'w') as f:
                f.write('\n'.join(simulate(text)) + '\n')
            result = subprocess.run([runner, trace, expected], capture_output=True, text=True)
            if result.returncode != 0:
                sys.stderr.write('case %d differs:\n%s%s' % (n, text, result.stderr))
                return 1
    print('%d random traces match' % count)
    return 0


def main():
    if len(sys.argv) == 3 and sys.argv[1] == 'expect':
        with open(sys.argv[2]) as f:
            print('\n'.join(simulate(f.read())))
        return 0
    if len(sys.argv) >= 4 and sys.argv[1] == 'random':
        seed = int(sys.argv[4]) if len(sys.argv) > 4 else 1
        return differential(sys.argv[2], int(sys.argv[3]), seed)
    sys.stderr.write('usage: oracle.py expect <trace> | random <trace_run> <count> [seed]\n')
    return 2


if __name__ == '__main__':
    sys.exit(main())
