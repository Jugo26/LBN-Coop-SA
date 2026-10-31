#!/usr/bin/env python3
"""Local HTTPS stand-in for api.github.com + release downloads, used by tests/launcher_wine_test.sh.
Usage: launcher_test_server.py <port> <certfile> <keyfile> <dir with LbnCoopSA-v0.2.0.zip>
Routes:
  /api/ok          latest release v0.2.0 with good zip + sha256
  /api/badhash     same release, but the .sha256 asset does not match
  /api/ratelimit   HTTP 403 + X-RateLimit-Remaining: 0 (GitHub rate limit)
  /api/garbage     HTML instead of JSON
  /api/same        latest release v0.1.0 (= installed)
  /dl/...          files from <dir>; /dl/bad.sha256 returns a wrong hash"""
import http.server, json, os, ssl, sys, time

PORT, CERT, KEY, DIR = int(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4]
BASE = f"https://127.0.0.1:{PORT}"

def release(tag, sha_name):
    z = f"LbnCoopSA-{tag}.zip"
    return {"tag_name": tag, "html_url": f"{BASE}/rel/{tag}", "name": f"LBN Coop SA {tag}",
            "assets": [{"name": z, "size": os.path.getsize(os.path.join(DIR, "LbnCoopSA-v0.2.0.zip")),
                        "browser_download_url": f"{BASE}/redirect/{z}"},
                       {"name": z + ".sha256", "size": 90, "browser_download_url": f"{BASE}/dl/{sha_name}"}]}

class H(http.server.BaseHTTPRequestHandler):
    def send(self, code, body, ctype="application/json", extra=None):
        b = body if isinstance(body, bytes) else body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype); self.send_header("Content-Length", str(len(b)))
        for k, v in (extra or {}).items(): self.send_header(k, v)
        self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        p = self.path
        sys.stderr.write(f"[srv] GET {p} UA={self.headers.get('User-Agent')}\n")
        if p == "/api/ok": return self.send(200, json.dumps(release("v0.2.0", "LbnCoopSA-v0.2.0.zip.sha256")))
        if p == "/api/badhash": return self.send(200, json.dumps(release("v0.2.0", "bad.sha256")))
        if p == "/api/same": return self.send(200, json.dumps(release("v0.1.0", "LbnCoopSA-v0.2.0.zip.sha256")))
        if p == "/api/ratelimit":
            return self.send(403, json.dumps({"message": "API rate limit exceeded for 1.2.3.4."}),
                             extra={"X-RateLimit-Remaining": "0", "X-RateLimit-Reset": str(int(time.time()) + 1800)})
        if p == "/api/garbage": return self.send(200, "<html>oops</html>", "text/html")
        if p.startswith("/redirect/"):  # like GitHub: browser_download_url redirects to the CDN
            self.send_response(302); self.send_header("Location", f"{BASE}/dl/v0.2.0.zip"); self.send_header("Content-Length", "0"); self.end_headers(); return
        if p == "/dl/bad.sha256": return self.send(200, "0" * 64 + "  LbnCoopSA-v0.2.0.zip\n", "text/plain")
        if p == "/dl/v0.2.0.zip": p = "/dl/LbnCoopSA-v0.2.0.zip"
        if p.startswith("/dl/"):
            f = os.path.join(DIR, os.path.basename(p))
            if os.path.isfile(f): return self.send(200, open(f, "rb").read(), "application/octet-stream")
        self.send(404, '{"message":"Not Found"}')
    def log_message(self, *a): pass

srv = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), H)
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.load_cert_chain(CERT, KEY)
srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
print("pronto", flush=True)
srv.serve_forever()
