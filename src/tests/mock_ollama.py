import http.server
import socketserver
import json
import time

class Handler(http.server.BaseHTTPRequestHandler):
    def _set_headers(self, content_type='application/json'):
        self.send_response(200)
        self.send_header('Content-type', content_type)
        self.end_headers()

    def do_HEAD(self):
        """Handle HEAD requests (used for health checks)"""
        self._set_headers('text/plain')

    def do_GET(self):
        if self.path == '/' or self.path == '/health':
            self._set_headers('text/plain')
            self.wfile.write(b'healthy\n')
        elif self.path == '/api/tags':
            self._set_headers()
            response = {
                'models': [
                    {
                        'name': 'llama3',
                        'modified_at': '2023-09-23T00:00:00Z',
                        'size': 4096,
                        'digest': 'mock-digest-123',
                        'details': {
                            'format': 'gguf',
                            'family': 'llama',
                            'parameter_size': '8B',
                            'quantization_level': 'Q4_0'
                        }
                    }
                ]
            }
            self.wfile.write(json.dumps(response).encode('utf-8'))
        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
        content_length = int(self.headers['Content-Length'])
        post_data = self.rfile.read(content_length)

        try:
            data = json.loads(post_data.decode('utf-8'))

            if self.path == '/api/generate':
                self._set_headers()
                response = {
                    'model': data.get('model', 'llama3'),
                    'created_at': '2023-09-23T00:00:00Z',
                    'response': 'This is a mock response from Ollama.',
                    'done': True,
                    'context': [0] * 64,  # Mock context
                    'total_duration': 1000000000,  # 1 second in nanoseconds
                    'load_duration': 100000000,
                    'prompt_eval_count': len(data.get('prompt', '').split()),
                    'eval_count': 10,
                    'eval_duration': 900000000
                }
                self.wfile.write(json.dumps(response).encode('utf-8'))
            elif self.path == '/api/chat':
                self._set_headers()
                response = {
                    'model': data.get('model', 'llama3.2:3b'),
                    'created_at': '2023-09-23T00:00:00Z',
                    'message': {
                        'role': 'assistant',
                        'content': 'This is a mock response from Ollama.'
                    },
                    'done': True,
                    'total_duration': 1000000000,
                    'load_duration': 100000000,
                    'prompt_eval_count': 10,
                    'eval_count': 10,
                    'eval_duration': 900000000
                }
                self.wfile.write(json.dumps(response).encode('utf-8'))
            else:
                self.send_response(404)
                self.end_headers()

        except json.JSONDecodeError:
            self.send_response(400)
            self.end_headers()
            self.wfile.write(b'Invalid JSON')

def run(server_class=http.server.HTTPServer, handler_class=Handler, port=11434):
    server_address = ('0.0.0.0', port)
    httpd = server_class(server_address, handler_class)
    print(f'Starting mock Ollama server on port {port}...')
    httpd.serve_forever()

if __name__ == '__main__':
    run()
