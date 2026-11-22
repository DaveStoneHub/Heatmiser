#!/usr/bin/env python3
"""
Serve a firmware build to the bridge over the LAN, for testing an update
before it is released.

    idf.py build
    python tools/ota_serve.py                 # serves build/heatmiser_bridge.bin on port 8070

It prints the console command to type on the bridge, e.g.

    ota install http://192.168.1.20:8070/heatmiser_bridge.bin

The PC's firewall must let the bridge reach the port. Ctrl+C stops it.
"""
import argparse
import functools
import http.server
import os
import socket
import sys


def lan_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(('192.0.2.1', 9))  # no packet is sent; this picks the LAN interface
        return s.getsockname()[0]
    except OSError:
        return '127.0.0.1'
    finally:
        s.close()


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--bin', default=os.path.join('build', 'heatmiser_bridge.bin'))
    p.add_argument('--port', type=int, default=8070)
    args = p.parse_args()
    if not os.path.isfile(args.bin):
        sys.exit(f'{args.bin} not found: run idf.py build first')
    folder, name = os.path.split(os.path.abspath(args.bin))
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=folder)
    with http.server.ThreadingHTTPServer(('0.0.0.0', args.port), handler) as srv:
        size = os.path.getsize(args.bin)
        print(f'Serving {name} ({size // 1024} KB). On the bridge console type:\n')
        print(f'    ota install http://{lan_ip()}:{args.port}/{name}\n')
        try:
            srv.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == '__main__':
    main()
