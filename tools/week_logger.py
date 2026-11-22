#!/usr/bin/env python3
"""
Week logger for the milestone 6 gate: a full week running without
intervention.

Listens to the bridge's console, timestamps every line into a log file and
follows the hourly HMBRIDGE-HEALTH line. At the end (or on Ctrl+C) it prints
a summary and a verdict. The gate fails on:

  - a reset of the board (HMBRIDGE-BOOT), or a firmware rollback,
  - a stall: no health line for 2.5 hours (board hung or console lost),
  - a stat clock left unset: once the bridge has run a day, its last
    confirmed clock write must never be more than 26 hours old,
  - the bridge losing the time (time=0 after it had it).

Stats offline, failed writes, Wi-Fi drops and Matter controllers are
reported for you to judge.

    pip install pyserial
    python tools/week_logger.py --port COM5              # 7 days
    python tools/week_logger.py --port COM5 --hours 24   # a shorter run
    python tools/week_logger.py --parse week-20261123-0900.log
    python tools/week_logger.py --self-test
"""
import argparse
import datetime as dt
import re
import sys
import time

KV = re.compile(r'(\w+)=("[^"]*"|\S+)')
STALL_S = 2.5 * 3600
CLOCK_MAX_AGE_S = 26 * 3600


def kv(line):
    return {k: v.strip('"') for k, v in KV.findall(line)}


def num(d, k, default=0):
    try:
        return int(d.get(k, default))
    except ValueError:
        return default


class Tracker:
    def __init__(self):
        self.t0 = None
        self.t_last = None
        self.last_health_at = None
        self.first = None
        self.last = None
        self.health_lines = 0
        self.resets = []
        self.rollbacks = []
        self.stalls = 0
        self.clock_late = []
        self.time_lost = 0
        self.had_time = False
        self.offline_hours = {}
        self.max_drops = 0
        self.notices = 0

    def feed(self, line, now):
        if self.t0 is None:
            self.t0 = now
            self.last_health_at = now
        self.t_last = now
        if line.startswith('HMBRIDGE-BOOT'):
            self.resets.append((now, kv(line).get('reset_reason', '?')))
            return 'RESET'
        if line.startswith('HMBRIDGE-OTA') and ' rollback ' in f' {line} ':
            self.rollbacks.append(now)
            return 'ROLLBACK'
        if 'notice:' in line:
            self.notices += 1
            return 'NOTICE'
        if not line.startswith('HMBRIDGE-HEALTH'):
            return None
        h = kv(line)
        self.health_lines += 1
        self.last_health_at = now
        if self.first is None:
            self.first = h
        self.last = h
        up = num(h, 'up_s')
        if num(h, 'time') == 1:
            self.had_time = True
        elif self.had_time:
            self.time_lost += 1
        age = num(h, 'clk_age_s', -1)
        if up > 26 * 3600 and (age < 0 or age > CLOCK_MAX_AGE_S):
            self.clock_late.append((now, age))
        for a in h.get('offline', '-').split(','):
            if a and a != '-':
                self.offline_hours[a] = self.offline_hours.get(a, 0) + 1
        self.max_drops = max(self.max_drops, num(h, 'wifi_drops'))
        return 'HEALTH'

    def check_stall(self, now):
        if self.last_health_at and now - self.last_health_at > STALL_S:
            self.stalls += 1
            self.last_health_at = now
            return True
        return False

    def ok(self):
        return not self.resets and not self.rollbacks and not self.stalls and not self.clock_late \
            and not self.time_lost and self.health_lines > 0

    def report(self, want_hours=None):
        print('\n==== Week summary ====')
        span = (self.t_last - self.t0) / 3600 if self.t0 is not None and self.t_last is not None else 0
        print(f'Logged {span:.1f} h, {self.health_lines} health lines')
        if self.last:
            l, f = self.last, self.first
            print(f"Firmware {l.get('fw')}, up {num(l, 'up_s') / 86400:.2f} days, "
                  f"{l.get('online')}/{l.get('zones')} stats online at the end")
            for k in ('polls', 'timeouts', 'crc', 'bad', 'wr_ok', 'wr_fail', 'clk_ok', 'clk_fail', 'clk_auto'):
                d = num(l, k) - num(f, k)
                print(f'  {k:9s} {num(l, k):9d}  (+{d} over the log)')
            print(f"  Wi-Fi drops {num(l, 'wifi_drops')}, Matter {'running' if num(l, 'matter') else 'not running'}"
                  f", {l.get('fabrics')} controller(s)")
        if self.offline_hours:
            print('Stats offline (health lines):', ', '.join(f'stat {a}: {n}' for a, n in
                                                          sorted(self.offline_hours.items(), key=lambda x: int(x[0]))))
        print(f'Notices: {self.notices}')
        print(f'Resets: {len(self.resets)}', ''.join(f'\n  reset_reason={r}' for _, r in self.resets))
        print(f'Rollbacks: {len(self.rollbacks)}, stalls: {self.stalls}, '
              f'clock writes overdue: {len(self.clock_late)}, time lost: {self.time_lost}')
        ok = self.ok()
        if want_hours and span < want_hours - 0.5:
            print(f'Ran {span:.1f} h of the {want_hours} h asked for.')
            ok = False
        print('GATE:', 'PASS' if ok else 'FAIL')
        return 0 if ok else 1


def strip(raw):
    line = raw.split(' | ', 1)[1] if ' | ' in raw[:40] else raw
    return re.sub(r'^(hm> )+', '', line.strip())


def parse_file(path):
    t = Tracker()
    with open(path, encoding='utf-8', errors='replace') as f:
        for raw in f:
            stamp = raw.split(' | ', 1)[0] if ' | ' in raw[:40] else None
            try:
                now = dt.datetime.fromisoformat(stamp).timestamp() if stamp else time.time()
            except ValueError:
                now = time.time()
            t.check_stall(now)
            t.feed(strip(raw), now)
    return t.report()


def run(args):
    try:
        import serial
    except ImportError:
        sys.exit('pyserial is missing: pip install pyserial')
    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = args.baud
    ser.timeout = 1
    ser.dtr = False   # opening the port must not reset the board
    ser.rts = False
    ser.open()
    logname = args.log or dt.datetime.now().strftime('week-%Y%m%d-%H%M.log')
    log = open(logname, 'a', encoding='utf-8')
    print(f'Logging to {logname} for {args.hours} h. Ctrl+C stops and prints the summary.')
    t = Tracker()
    time.sleep(1.0)
    ser.write(b'\r\nhealth\r\n')
    end = time.time() + args.hours * 3600
    buf = b''
    try:
        while time.time() < end:
            chunk = ser.read(256)
            now = time.time()
            if chunk:
                buf += chunk
                while b'\n' in buf:
                    raw, buf = buf.split(b'\n', 1)
                    line = strip(raw.decode('utf-8', errors='replace').strip('\r'))
                    if not line:
                        continue
                    stamp = dt.datetime.now().isoformat(timespec='milliseconds')
                    log.write(f'{stamp} | {line}\n')
                    log.flush()
                    kind = t.feed(line, now)
                    if kind in ('RESET', 'ROLLBACK', 'HEALTH'):
                        print(f'{stamp[5:19]} {line}')
                    if kind == 'RESET':
                        print('!! The board reset: the gate fails.')
            if t.check_stall(now):
                print('!! No health line for 2.5 hours: board hung or console lost.')
    except KeyboardInterrupt:
        pass
    finally:
        log.close()
        ser.close()
    return t.report(args.hours)


def self_test():
    t = Tracker()
    t0 = 1_800_000_000
    good = ('HMBRIDGE-HEALTH up_s={up} fw=v0.6.0 zones=12 online=11 offline=9 polls={p} timeouts=3 crc=0 bad=0 '
            'wr_ok=4 wr_fail=0 clk_ok={c} clk_fail=0 clk_auto={c} clk_age_s={age} time=1 wifi=1 wifi_drops=0 '
            'matter=1 fabrics=2 ota="idle" trial=0')
    for h in range(0, 169):
        t.check_stall(t0 + h * 3600)
        t.feed(good.format(up=h * 3600, p=h * 4000, c=h // 24, age=(h % 24) * 3600 + 60), t0 + h * 3600)
    assert t.ok(), 'a clean week passes'
    assert t.offline_hours == {'9': 169}
    t.feed('HMBRIDGE-BOOT reset_reason=4', t0 + 169 * 3600)
    assert not t.ok(), 'a reset fails'
    t = Tracker()
    t.feed(good.format(up=30 * 3600, p=0, c=1, age=27 * 3600), t0)
    assert not t.ok(), 'a clock write older than 26 h fails'
    t = Tracker()
    t.feed(good.format(up=3600, p=0, c=0, age=-1), t0)
    t.check_stall(t0 + 3 * 3600)
    assert not t.ok(), 'no health line for 3 h is a stall'
    assert kv('HMBRIDGE-OTA rollback version=v0.6.1') == {'version': 'v0.6.1'}
    t = Tracker()
    t.feed(good.format(up=3600, p=0, c=0, age=60), t0)
    t.feed('HMBRIDGE-OTA rollback version=v0.6.1', t0 + 10)
    assert not t.ok(), 'a rollback fails'
    print('self-test passed')
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--port', help='serial port, e.g. COM5 or /dev/ttyACM0')
    p.add_argument('--baud', type=int, default=115200)
    p.add_argument('--hours', type=float, default=168)
    p.add_argument('--log', help='log file name (default week-YYYYMMDD-HHMM.log)')
    p.add_argument('--parse', metavar='LOG', help='analyse a saved log instead of running')
    p.add_argument('--self-test', action='store_true')
    args = p.parse_args()
    if args.self_test:
        sys.exit(self_test())
    if args.parse:
        sys.exit(parse_file(args.parse))
    if not args.port:
        p.error('--port is required unless --parse or --self-test is given')
    sys.exit(run(args))


if __name__ == '__main__':
    main()
