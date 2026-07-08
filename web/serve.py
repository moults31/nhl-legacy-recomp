import http.server, os, sys, mimetypes
mimetypes.add_type("application/wasm", ".wasm")
PORT=8080
class H(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()
print(f"http://localhost:{PORT}/launcher.html", flush=True)
httpd = http.server.HTTPServer(("0.0.0.0", PORT), H)
httpd.serve_forever()
