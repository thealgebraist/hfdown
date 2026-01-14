import http.server
import socketserver
import os
import sys

PORT = 8891
DATA_DIR = sys.argv[1] if len(sys.argv) > 1 else "test_server_data"

class MockHFHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        # Handle Range header manually for better compatibility
        range_header = self.headers.get("Range")
        if range_header and "resolve" in self.path:
            path = self.translate_path(self.path)
            if os.path.exists(path) and os.path.isfile(path):
                size = os.path.getsize(path)
                try:
                    parts = range_header.replace("bytes=", "").split("-")
                    start = int(parts[0])
                    end = int(parts[1]) if parts[1] else size - 1
                    length = end - start + 1
                    
                    self.send_response(206)
                    self.send_header("Content-Type", "application/octet-stream")
                    self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
                    self.send_header("Content-Length", str(length))
                    self.send_header("Accept-Ranges", "bytes")
                    self.end_headers()
                    
                    with open(path, "rb") as f:
                        f.seek(start)
                        remaining = length
                        while remaining > 0:
                            chunk = f.read(min(remaining, 65536))
                            if not chunk: break
                            self.wfile.write(chunk)
                            remaining -= len(chunk)
                    return
                except Exception as e:
                    print(f"Error handling range: {e}")
        
        return super().do_GET()

    def translate_path(self, path):
        # HuggingFace API: /api/models/<model_id>/tree/main
        if path.startswith("/api/models/"):
            # Redirect to the pre-generated JSON file
            # Path: test_server_data/api/models/<model_id>/tree/main
            return os.path.join(os.getcwd(), DATA_DIR, path.lstrip("/"))
        
        # HuggingFace Resolve: /<model_id>/resolve/main/<filename>
        # We simplify this to look for <model_id>/<filename> in DATA_DIR
        parts = path.lstrip("/").split("/")
        if "resolve" in parts:
            idx = parts.index("resolve")
            model_id = "/".join(parts[:idx])
            filename = "/".join(parts[idx+2:])
            return os.path.join(os.getcwd(), DATA_DIR, model_id, filename)
            
        return super().translate_path(path)

    def end_headers(self):
        # Add some headers to mimic real HF server
        self.send_header("Accept-Ranges", "bytes")
        super().end_headers()

class ThreadedTCPServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    pass

def run_server():
    Handler = MockHFHandler
    with ThreadedTCPServer(("", 0), Handler) as httpd:
        port = httpd.server_address[1]
        with open("server.port", "w") as f:
            f.write(str(port))
        print(f"Serving Mock HF at port {port}")
        httpd.serve_forever()

if __name__ == "__main__":
    run_server()