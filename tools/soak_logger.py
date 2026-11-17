#!/usr/bin/env python3
"""
Soak logger for the Heatmiser RS485 bring-up gate.

Starts a soak on the board, timestamps every console line into a log file,
counts errors independently of the firmware, and watches for resets or a
stalled board. Exits 0 only if the firmware reports PASS and the logger saw
nothing wrong.

    pip install pyserial
    python tools/soak_logger.py --port COM5                 # 1 hour on the default stat
    python tools/soak_logger.py --port COM5 --addr 3 --seconds 3600 --period 1000
    python tools/soak_logger.py --parse soak-20261117-1700.log   # re-check a saved log

Ctrl+C stops the soak on the board and prints the result so far.
"""
import argparse
import datetime as dt
import re
import sys
import time

KV = re.compile(r'(\w+)=("[^"]*"|\S+)')


def kv(line):
    return {k: v.strip('"') for k, v in KV.findall(line)}


class Tracker:
    def __init__(self):
        self.started = False
        self.resets = 0
        self.err_lines = 0
        self.err_kinds = {}
        self.last_progress = None
        self.last_progress_at = None
        self.result = None
        self.result_line = None

    def feed(self, line, now):
        if line.startswith('HMBRIDGE-BOOT') and self.started:
            self.resets += 1
            return 'RESET'
        if line.startswith('SOAK-START'):
            self.started = True
            self.last_progress_at = now
        elif line.startswith('SOAK-ERR'):
            self.err_lines += 1
            k = kv(line).get('result', '?') + ' / ' + kv(line).get('proto', '?')
            self.err_kinds[k] = self.err_kinds.get(k, 0) + 1
            return 'ERR'
        elif line.startswith('SOAK-RESULT'):
            self.result = 'PASS' if ' PASS' in line else 'FAIL'
            self.result_line = line
            return 'RESULT'
        elif line.startswith('SOAK '):
            self.last_progress = kv(line)
            self.last_progress_at = now
            return 'PROGRESS'
        return None

    def verdict(self):
        ok = self.result == 'PASS' and self.resets == 0 and self.err_lines == 0
        return ok

    def report(self):
        print('\n==== Soak summary ====')
        if self.result_line:
            print(self.result_line)
        elif self.last_progress:
            print('No SOAK-RESULT line. Last progress:', ' '.join(f'{k}={v}' for k, v in self.last_progress.items()))
        else:
            print('No soak output seen.')
        print(f'Logger: {self.err_lines} SOAK-ERR lines, {self.resets} unexpected resets')
        for k, n in sorted(self.err_kinds.items(), key=lambda x: -x[1]):
            print(f'  {n:5d} x {k}')
        ok = self.verdict()
        print('GATE:', 'PASS' if ok else 'FAIL')
        return 0 if ok else 1


def parse_file(path):
    t = Tracker()
    with open(path, encoding='utf-8', errors='replace') as f:
        for raw in f:
            # Accept our own "ISO-time | line" format or a bare console capture.
            line = raw.split(' | ', 1)[1] if ' | ' in raw[:40] else raw
            t.feed(line.strip(), time.time())
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
    # Keep DTR/RTS deasserted so opening the port does not reset the board.
    ser.dtr = False
    ser.rts = False
    ser.open()

    logname = args.log or dt.datetime.now().strftime('soak-%Y%m%d-%H%M.log')
    log = open(logname, 'a', encoding='utf-8')
    print(f'Logging to {logname}')

    def send(cmd):
        ser.write((cmd + '\r\n').encode())
        ser.flush()

    tracker = Tracker()
    time.sleep(0.5)
    ser.reset_input_buffer()
    if not args.no_start:
        send('')
        time.sleep(0.3)
        send(f'soak start {args.addr} {args.seconds} {args.period}')

    stall_s = 180  # progress lines come once a minute
    deadline = time.time() + args.seconds + 300
    buf = b''
    try:
        while True:
            chunk = ser.read(256)
            now = time.time()
            if chunk:
                buf += chunk
                while b'\n' in buf:
                    raw, buf = buf.split(b'\n', 1)
                    line = raw.decode('utf-8', errors='replace').strip('\r').strip()
                    if not line:
                        continue
                    # Drop the console prompt echo that can prefix a line.
                    line = re.sub(r'^(hm> )+', '', line)
                    stamp = dt.datetime.now().isoformat(timespec='milliseconds')
                    log.write(f'{stamp} | {line}\n')
                    log.flush()
                    kind = tracker.feed(line, now)
                    if kind in ('ERR', 'RESET', 'RESULT', 'PROGRESS') or line.startswith('SOAK-START'):
                        print(f'{stamp[11:19]} {line}')
                    if kind == 'RESET':
                        print('!! Board reset during the soak: the gate fails.')
                    if kind == 'RESULT':
                        time.sleep(0.5)
                        return tracker.report()
            if tracker.started and tracker.last_progress_at and now - tracker.last_progress_at > stall_s:
                print(f'!! No progress line for {stall_s} s: board stalled or console lost.')
                tracker.err_lines += 1
                tracker.err_kinds['stall'] = tracker.err_kinds.get('stall', 0) + 1
                tracker.last_progress_at = now
            if now > deadline:
                print('!! Soak overran its duration by 5 minutes without a result.')
                return tracker.report()
    except KeyboardInterrupt:
        print('\nStopping soak on the board...')
        send('soak stop')
        end = time.time() + 5
        while time.time() < end:
            raw = ser.readline().decode('utf-8', errors='replace').strip()
            if raw:
                raw = re.sub(r'^(hm> )+', '', raw)
                log.write(f'{dt.datetime.now().isoformat(timespec="milliseconds")} | {raw}\n')
                if tracker.feed(raw, time.time()) == 'RESULT':
                    break
        return tracker.report()
    finally:
        log.close()
        ser.close()


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--port', help='serial port, e.g. COM5 or /dev/ttyACM0')
    p.add_argument('--baud', type=int, default=115200)
    p.add_argument('--addr', type=int, default=1, help='thermostat address (1-32)')
    p.add_argument('--seconds', type=int, default=3600)
    p.add_argument('--period', type=int, default=1000, help='ms between reads; 0 = back to back')
    p.add_argument('--log', help='log file name (default soak-YYYYMMDD-HHMM.log)')
    p.add_argument('--no-start', action='store_true', help='only listen; start the soak by hand')
    p.add_argument('--parse', metavar='LOG', help='analyse a saved log instead of running')
    args = p.parse_args()
    if args.parse:
        sys.exit(parse_file(args.parse))
    if not args.port:
        p.error('--port is required unless --parse is given')
    sys.exit(run(args))


if __name__ == '__main__':
    main()
