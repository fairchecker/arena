#!/usr/bin/env python3
"""Local browser interface for the C++ arena simulation."""
import argparse
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
import subprocess
import sys
import webbrowser

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8000)
    parser.add_argument('--binary', type=Path,
                        default=ROOT / 'build-release' / 'bin' / 'Release' /
                        ('arena_gui_backend.exe' if sys.platform == 'win32' else 'arena_gui_backend'))
    parser.add_argument('--no-browser', action='store_true')
    parser.add_argument('--seed', type=int, help='Fixed food seed for reproducible runs (0..4294967295)')
    args = parser.parse_args()
    if args.seed is not None and not 0 <= args.seed <= 0xffffffff:
        parser.error('--seed must be between 0 and 4294967295')
    backend_command = [str(args.binary.resolve())]
    if args.seed is not None:
        backend_command += ['--seed', str(args.seed)]
    try:
        backend = subprocess.Popen(backend_command, stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, text=True, bufsize=1)
    except OSError as exc:
        parser.exit(1, f'Cannot start simulation backend: {exc}\nBuild the project first or pass --binary.\n')

    class Handler(BaseHTTPRequestHandler):
        def valid_host(self):
            return self.headers.get('Host') == f'127.0.0.1:{self.server.server_port}'

        def respond(self, status, data, content_type):
            self.send_response(status)
            self.send_header('Content-Type', content_type)
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Cache-Control', 'no-store')
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            if not self.valid_host():
                self.send_error(403)
                return
            if self.path == '/':
                self.respond(200, (ROOT / 'gui' / 'index.html').read_bytes(), 'text/html; charset=utf-8')
            else:
                self.send_error(404)

        def do_POST(self):
            origin = f'http://127.0.0.1:{self.server.server_port}'
            if not self.valid_host() or self.headers.get('Origin') != origin or self.headers.get('X-Arena-Request') != '1':
                self.send_error(403)
                return
            command = self.path[1:] if self.path.startswith('/') else ''
            if command not in ('state', 'step', 'reset'):
                self.send_error(404)
                return
            try:
                backend.stdin.write(command + '\n')
                backend.stdin.flush()
                result = backend.stdout.readline()
                if not result:
                    raise BrokenPipeError('simulation backend stopped')
                self.respond(200, result.encode('utf-8'), 'application/json')
            except (BrokenPipeError, OSError):
                self.send_error(502, 'Simulation backend stopped')

    try:
        with HTTPServer(('127.0.0.1', args.port), Handler) as server:
            url = f'http://127.0.0.1:{server.server_port}/'
            print(f'Arena GUI: {url} (Ctrl+C to stop)', flush=True)
            if not args.no_browser:
                webbrowser.open(url)
            try:
                server.serve_forever()
            except KeyboardInterrupt:
                pass
    finally:
        backend.stdin.close()
        backend.wait()


if __name__ == '__main__':
    main()
