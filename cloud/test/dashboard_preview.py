"""พรีวิว Cloud Dashboard (docs/dashboard/index.html) ด้วยข้อมูลจำลอง ไม่ต่อ Supabase จริง
แทนแท็ก <script> ของ supabase-js ด้วย dashboard_stub.js ตอนเสิร์ฟ (ไฟล์ใน docs/ ไม่ถูกแก้)

    python cloud/test/dashboard_preview.py        # → http://127.0.0.1:8766/  (?out=1 ?nodev=1 ?empty=1 ?delok=1)
"""
import http.server
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE = os.path.join(HERE, '..', '..', 'docs', 'dashboard', 'index.html')
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8766
TAG = '<script src="https://cdn.jsdelivr.net/npm/@supabase/supabase-js@'


def page():
    s = open(PAGE, encoding='utf-8').read()
    a = s.index(TAG)
    b = s.index('</script>', a) + len('</script>')
    return (s[:a] + '<script src="/dashboard_stub.js"></script>' + s[b:]).encode()


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def reply(self, body, ctype):
        self.send_response(200)
        self.send_header('Content-Type', ctype)
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split('?')[0]
        if path in ('/', '/index.html'):
            self.reply(page(), 'text/html; charset=utf-8')
        elif path == '/dashboard_stub.js':
            self.reply(open(os.path.join(HERE, 'dashboard_stub.js'), 'rb').read(), 'text/javascript; charset=utf-8')
        else:
            self.send_error(404)


print(f'http://127.0.0.1:{PORT}/')
http.server.ThreadingHTTPServer(('127.0.0.1', PORT), Handler).serve_forever()
