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

If Windows drops the USB serial port (sleep, a USB power saving or a driver
reset), the logger notes it, waits for the port to come back and carries on.
If the port stays open but goes quiet for 65 minutes, it reopens it and asks
for a health line. A silence of 2.5 hours counts as a logger gap, not a
stall, only when the logger lost or reopened the port in it and the bridge's
up_s ran on with the clock; a restart still fails the gate.
Re-running with --log on an existing log picks the week up where it was.

    pip install pyserial
    python tools/week_logger.py --port COM5              # 7 days
    python tools/week_logger.py --port COM5 --hours 24   # a shorter run
    python tools/week_logger.py --port COM5 --log week-20261122-2213.log   # resume
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
POKE_S = 65 * 60          # silent this long: reopen the port and ask for health
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
        self.blind = False        # the logger has lost the port
        self.blind_since = None
        self.gaps = []            # (start, seconds) the logger could not see
        self.last_up = None
        self.marker = False       # a LOGGER line since the last health line
        self.warned = False

    def feed(self, line, now):
        if self.t0 is None:
            self.t0 = now
            self.last_health_at = now
        self.t_last = now
        if line.startswith('LOGGER'):
            self.marker = True
            if line.startswith('LOGGER port lost'):
                self.blind = True
            elif line.startswith('LOGGER port back'):
                self.blind = False
            return 'GAP'
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
        up = num(h, 'up_s')
        if self.health_lines and now - self.last_health_at > STALL_S:
            # A long silence. If the logger itself lost or reopened the port
            # in it, and the bridge's uptime ran on with the clock, the bridge
            # was fine and the logger was blind: a logger gap. Otherwise the
            # bridge stopped printing: a stall.
            gap = now - self.last_health_at
            ran_on = self.last_up is not None and abs((up - self.last_up) - gap) < 900
            if self.marker and ran_on:
                self.gaps.append((self.last_health_at, gap))
            else:
                self.stalls += 1
        self.marker = False
        self.warned = False
        self.health_lines += 1
        self.last_health_at = now
        if self.first is None:
            self.first = h
        self.last = h
        if self.last_up is not None and up < self.last_up:
            # Missed the boot line (the logger was not listening), but the
            # uptime went backwards: the board restarted.
            self.resets.append((now, 'seen from up_s'))
        self.last_up = up
        if num(h, 'time') == 1:
            self.had_time = True
        elif self.had_time:
            self.time_lost += 1
        age = num(h, 'clk_age_s', -1)
        if age < 0 and num(h, 'clk_ok') > 0:
            age = 0   # firmware v0.6.0 prints -1 in the second a clock write lands
        if up > 26 * 3600 and (age < 0 or age > CLOCK_MAX_AGE_S):
            self.clock_late.append((now, age))
        for a in h.get('offline', '-').split(','):
            if a and a != '-':
                self.offline_hours[a] = self.offline_hours.get(a, 0) + 1
        self.max_drops = max(self.max_drops, num(h, 'wifi_drops'))
        return 'HEALTH'

    def check_stall(self, now):
        """True once per silence, for a live warning. Whether a silence counts
        as a stall is decided when the next health line comes (or at the end)."""
        if self.blind or self.warned:
            return False
        if self.last_health_at and now - self.last_health_at > STALL_S:
            self.warned = True
            return True
        return False

    def finish(self, now):
        if self.health_lines and now - self.last_health_at > STALL_S:
            self.stalls += 1

    def ok(self):
        return not self.resets and not self.rollbacks and not self.stalls and not self.clock_late \
            and not self.time_lost and self.health_lines > 0

    def report(self, want_hours=None, now=None):
        self.finish(now if now is not None else (self.t_last or 0))
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
        if self.gaps:
            print(f'Logger gaps (port lost, not counted as stalls): {len(self.gaps)}, '
                  f'{sum(g for _, g in self.gaps) / 3600:.1f} h in all')
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
    line = re.sub(r'^(hm> )+', '', line.strip())
    # A bridge line can arrive behind the console prompt's escape codes or
    # another line's tail: start it at its own marker.
    i = line.find('HMBRIDGE-')
    return line[i:] if i > 0 else line


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


def open_port(serial, args):
    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = args.baud
    ser.timeout = 1
    ser.dtr = False   # opening the port must not reset the board
    ser.rts = False
    ser.open()
    return ser


def run(args):
    try:
        import serial
    except ImportError:
        sys.exit('pyserial is missing: pip install pyserial')
    logname = args.log or dt.datetime.now().strftime('week-%Y%m%d-%H%M.log')
    t = Tracker()
    start = time.time()
    resumed = False
    try:
        with open(logname, encoding='utf-8', errors='replace') as f:
            for raw in f:
                stamp = raw.split(' | ', 1)[0] if ' | ' in raw[:40] else None
                try:
                    when = dt.datetime.fromisoformat(stamp).timestamp() if stamp else None
                except ValueError:
                    when = None
                if when is None:
                    continue
                if not resumed:
                    start, resumed = when, True
                t.check_stall(when)
                t.feed(strip(raw), when)
    except FileNotFoundError:
        pass
    log = open(logname, 'a', encoding='utf-8')

    def note(line, when=None):
        when = time.time() if when is None else when
        stamp = dt.datetime.fromtimestamp(when).isoformat(timespec='milliseconds')
        log.write(f'{stamp} | {line}\n')
        log.flush()
        return t.feed(line, when), stamp

    end = start + args.hours * 3600
    if resumed:
        print(f'Resuming {logname}: started {dt.datetime.fromtimestamp(start):%a %d %b %H:%M}, '
              f'{t.health_lines} health lines so far, ends {dt.datetime.fromtimestamp(end):%a %d %b %H:%M}.')
        # Stamped where the log stopped, so a re-parse sees the gap too.
        note('LOGGER port lost (logger stopped)', t.t_last)
        note('LOGGER port back (logger resumed)')
    else:
        print(f'Logging to {logname} for {args.hours} h. Ctrl+C stops and prints the summary.')
    ser = None
    last_open_err = None
    last_poke = time.time()
    buf = b''
    try:
        while time.time() < end:
            if ser is None:
                try:
                    ser = open_port(serial, args)
                except (serial.SerialException, OSError) as e:
                    if str(e) != last_open_err:
                        last_open_err = str(e)
                        print(f'{dt.datetime.now():%m-%d %H:%M:%S} cannot open {args.port} ({e}); '
                              'retrying every 5 s. Is idf.py monitor or another logger still open?')
                    time.sleep(5)
                    continue
                last_open_err = None
                print(f'{dt.datetime.now():%m-%d %H:%M:%S} listening on {args.port}; asked the bridge for a health line')
                if t.blind:
                    _, stamp = note('LOGGER port back')
                    print(f'{stamp[5:19]} port {args.port} is back')
                time.sleep(1.0)
                ser.write(b'\r\nhealth\r\n')
                buf = b''
            try:
                chunk = ser.read(256)
            except (serial.SerialException, OSError) as e:
                _, stamp = note(f'LOGGER port lost ({e})')
                print(f'{stamp[5:19]} lost port {args.port} ({e}); waiting for it to come back')
                try:
                    ser.close()
                except Exception:
                    pass
                ser = None
                continue
            now = time.time()
            if chunk:
                buf += chunk
                while b'\n' in buf:
                    raw, buf = buf.split(b'\n', 1)
                    line = strip(raw.decode('utf-8', errors='replace').strip('\r'))
                    if not line:
                        continue
                    kind, stamp = note(line)
                    if kind in ('RESET', 'ROLLBACK', 'HEALTH'):
                        print(f'{stamp[5:19]} {line}')
                    if kind == 'RESET':
                        print('!! The board reset: the gate fails.')
            if ser is not None and t.last_health_at and now - max(t.last_health_at, last_poke) > POKE_S:
                # Windows can leave a port open but silent. Reopen it and ask.
                last_poke = now
                note(f'LOGGER port reopen (no health line for {POKE_S // 60} min)')
                print(f'{dt.datetime.now():%m-%d %H:%M:%S} no health line for {POKE_S // 60} min; reopening {args.port}')
                try:
                    ser.close()
                except Exception:
                    pass
                ser = None
                continue
            if t.check_stall(now):
                print('!! No health line for 2.5 hours: board hung or console lost.')
    except KeyboardInterrupt:
        pass
    finally:
        log.close()
        if ser is not None:
            ser.close()
    return t.report(args.hours, time.time())


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
    t.finish(t0 + 3 * 3600)
    assert not t.ok(), 'no health line for 3 h is a stall'
    assert kv('HMBRIDGE-OTA rollback version=v0.6.1') == {'version': 'v0.6.1'}
    t = Tracker()
    t.feed(good.format(up=3600, p=0, c=0, age=60), t0)
    t.feed('HMBRIDGE-OTA rollback version=v0.6.1', t0 + 10)
    assert not t.ok(), 'a rollback fails'
    t = Tracker()
    t.feed(good.format(up=3600, p=0, c=0, age=60), t0)
    t.feed('LOGGER port lost (GetOverlappedResult failed)', t0 + 600)
    t.check_stall(t0 + 6 * 3600)
    t.feed('LOGGER port back', t0 + 6 * 3600)
    t.feed(good.format(up=3600 + 6 * 3600, p=0, c=0, age=60), t0 + 6 * 3600 + 5)
    assert t.ok() and len(t.gaps) == 1, 'a lost port is a logger gap, not a stall'
    t = Tracker()
    t.feed(good.format(up=3600, p=0, c=0, age=60), t0)
    t.feed(good.format(up=3600 + 4 * 3600, p=0, c=0, age=60), t0 + 4 * 3600)
    assert not t.ok(), 'silence with no logger event is a stall'
    t = Tracker()
    t.feed(good.format(up=3600, p=0, c=0, age=60), t0)
    t.feed('LOGGER port lost (access denied)', t0 + 3.6 * 3600)
    t.feed('LOGGER port back', t0 + 3.6 * 3600 + 5)
    t.feed(good.format(up=3600 + 4 * 3600, p=0, c=0, age=60), t0 + 4 * 3600)
    assert t.ok() and len(t.gaps) == 1, 'a port that went silent then errored is a logger gap'
    t = Tracker()
    t.feed(good.format(up=3600, p=0, c=0, age=60), t0)
    t.feed('LOGGER port lost (x)', t0 + 600)
    t.feed(good.format(up=100, p=0, c=0, age=60), t0 + 4 * 3600)
    assert not t.ok(), 'a gap that hides a restart still fails'
    t.feed(good.format(up=200, p=0, c=0, age=60), t0 + 7 * 3600)
    assert not t.ok(), 'uptime going backwards is a reset'
    t = Tracker()
    t.feed(good.format(up=30 * 3600, p=0, c=12, age=-1), t0)
    assert t.ok(), 'clk_age_s=-1 right after a write is not overdue'
    assert strip('\x1b[0Khm> HMBRIDGE-HEALTH up_s=1').startswith('HMBRIDGE-HEALTH')
    assert strip('2026-11-24T08:10:10.000 | I (1) x: y HMBRIDGE-HEALTH up_s=1') == 'HMBRIDGE-HEALTH up_s=1'
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
