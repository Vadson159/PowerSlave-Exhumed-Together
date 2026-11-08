"""
Lightweight HTTP Server for PowerSlave Exhumed Together Roadmap Tracker.
Pure Python standard library (no pip dependencies required).

Features:
- Serves the spreadsheet UI on http://localhost:8080 (or next free port)
- /api/data endpoint dynamically parses the 4 markdown files on every request
- Full CORS support so file:/// opened pages can communicate with the server
- Automatically opens browser
- Graceful shutdown on Ctrl+C
"""

import os
import sys
import json
import socket
import webbrowser
from http.server import HTTPServer, BaseHTTPRequestHandler

# Ensure tracker directory is in sys.path
current_dir = os.path.dirname(os.path.abspath(__file__))
if current_dir not in sys.path:
    sys.path.insert(0, current_dir)

from tracker_parser import parse_all_4_files

PORT = 8080

class TrackerRequestHandler(BaseHTTPRequestHandler):
    def send_cors_headers(self):
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', '*')
        self.send_header('Access-Control-Max-Age', '86400')

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_cors_headers()
        self.end_headers()

    def do_GET(self):
        if self.path == '/api/data':
            try:
                data = parse_all_4_files()
                response_json = json.dumps(data, ensure_ascii=False)
                self.send_response(200)
                self.send_header('Content-Type', 'application/json; charset=utf-8')
                self.send_cors_headers()
                self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')
                self.end_headers()
                self.wfile.write(response_json.encode('utf-8'))
            except Exception as e:
                self.send_response(500)
                self.send_header('Content-Type', 'application/json; charset=utf-8')
                self.send_cors_headers()
                self.end_headers()
                err = json.dumps({'error': str(e)}, ensure_ascii=False)
                self.wfile.write(err.encode('utf-8'))
            return

        elif self.path == '/' or self.path == '/index.html':
            template_path = os.path.join(current_dir, 'template.html')
            if not os.path.exists(template_path):
                self.send_response(404)
                self.end_headers()
                return

            with open(template_path, 'r', encoding='utf-8') as f:
                content = f.read()

            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_cors_headers()
            self.send_header('Cache-Control', 'no-cache')
            self.end_headers()
            self.wfile.write(content.encode('utf-8'))
            return

        elif self.path == '/api/health':
            self.send_response(200)
            self.send_header('Content-Type', 'text/plain')
            self.send_cors_headers()
            self.end_headers()
            self.wfile.write(b'OK')
            return

        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, format, *args):
        if len(args) > 0 and '/api/data' in str(args[0]):
            return
        sys.stderr.write("%s - - [%s] %s\n" % (self.client_address[0], self.log_date_time_string(), format % args))


def find_free_port(start_port=8080, max_attempts=50):
    for p in range(start_port, start_port + max_attempts):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            try:
                s.bind(('127.0.0.1', p))
                return p
            except OSError:
                continue
    return start_port


def start_server(port=None, open_browser=True):
    if port is None:
        port = find_free_port(PORT)

    server_address = ('127.0.0.1', port)
    httpd = HTTPServer(server_address, TrackerRequestHandler)

    url = f"http://localhost:{port}"
    print(f"=================================================================")
    print(f"  PowerSlave Exhumed Together — Roadmap Tracker UI")
    print(f"  Динамическое чтение из 4 файлов в реальном времени:")
    print(f"   - docs/CURRENT_STATE.md")
    print(f"   - docs/ROADMAP.md")
    print(f"   - docs/PROGRESS.md")
    print(f"   - docs/EVIDENCE_INDEX.md")
    print(f"-----------------------------------------------------------------")
    print(f"  Сервер запущен: {url}")
    print(f"=================================================================")

    if open_browser:
        webbrowser.open(url)

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nОстановка сервера...")
        httpd.server_close()


if __name__ == '__main__':
    open_b = '--no-browser' not in sys.argv
    start_server(open_browser=open_b)
