#!/usr/bin/env python3
"""
Build or check Heatmiser V3 frames on the PC (CRC-16/CCITT-FALSE, low byte first).

    python tools/hm_frame.py read 1            # full-DCB read request to stat 1 from 0x81
    python tools/hm_frame.py read 3 --master 0x82 --start 18 --count 1
    python tools/hm_frame.py check 81 4B 00 01 00 ...   # verify a captured frame's CRC

Handy for a USB-RS485 dongle or a logic analyser when checking the bus by hand.
"""
import argparse


def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def read_request(stat, master=0x81, start=0, count=0xFFFF):
    f = [stat, 0x0A, master, 0x00, start & 0xFF, start >> 8, count & 0xFF, count >> 8]
    c = crc16(f)
    return bytes(f + [c & 0xFF, c >> 8])


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('read')
    r.add_argument('stat', type=int)
    r.add_argument('--master', type=lambda s: int(s, 0), default=0x81)
    r.add_argument('--start', type=lambda s: int(s, 0), default=0)
    r.add_argument('--count', type=lambda s: int(s, 0), default=0xFFFF)
    c = sub.add_parser('check')
    c.add_argument('hex', nargs='+')
    a = p.parse_args()
    if a.cmd == 'read':
        print(read_request(a.stat, a.master, a.start, a.count).hex(' ').upper())
    else:
        b = bytes.fromhex(''.join(a.hex))
        want = b[-2] | (b[-1] << 8)
        got = crc16(b[:-2])
        print(f'{len(b)} bytes, CRC in frame 0x{want:04X}, computed 0x{got:04X}:',
              'OK' if want == got else 'MISMATCH')


if __name__ == '__main__':
    main()
