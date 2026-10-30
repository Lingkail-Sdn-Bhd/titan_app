#!/usr/bin/env python3
"""Small LAN viewer for frames saved by the existing Titan receiver."""
import argparse
import datetime
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        route = urlsplit(self.path).path
        if route == '/':
            return self.reply(200, 'text/html; charset=utf-8', Path(__file__).with_name('index.html').read_bytes())
        if route == '/api/result':
            try:
                data = (Path(__file__).parent / 'detections' / 'result.json').read_bytes()
            except FileNotFoundError:
                data = b'{"status":"starting"}'
            return self.reply(200, 'application/json', data)
        if route.startswith('/detections/'):
            name = route.removeprefix('/detections/')
            if Path(name).name != name or not name.endswith('.jpg'):
                return self.send_error(404)
            try:
                data = (Path(__file__).parent / 'detections' / name).read_bytes()
            except FileNotFoundError:
                return self.send_error(404)
            return self.reply(200, 'image/jpeg', data)
        if route == '/api/latest':
            files = list(self.server.captures.glob('*.png'))
            if not files:
                return self.reply(200, 'application/json', json.dumps({'image': None}).encode())
            latest = max(files, key=lambda p: p.stat().st_mtime_ns)
            stat = latest.stat()
            return self.reply(200, 'application/json', json.dumps({
                'image': '/captures/' + latest.name,
                'name': latest.name,
                'captured_at': datetime.datetime.fromtimestamp(stat.st_mtime).astimezone().isoformat(),
                'timestamp': stat.st_mtime,
                'count': len(files),
            }).encode())
        if route.startswith('/captures/'):
            name = route.removeprefix('/captures/')
            if Path(name).name != name or not name.endswith('.png'):
                return self.send_error(404)
            try:
                data = (self.server.captures / name).read_bytes()
            except (FileNotFoundError, IsADirectoryError):
                return self.send_error(404)
            return self.reply(200, 'image/png', data)
        self.send_error(404)

    def reply(self, status, content_type, data):
        self.send_response(status)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.end_headers()
        self.wfile.write(data)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--captures', type=Path, required=True)
    parser.add_argument('--host', default='0.0.0.0')
    parser.add_argument('--port', type=int, default=8080)
    args = parser.parse_args()
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    server.captures = args.captures.resolve()
    print(f'Viewer listening on {args.host}:{args.port}', flush=True)
    server.serve_forever()
