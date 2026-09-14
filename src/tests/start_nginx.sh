#!/bin/sh
set -e

# Create nginx config directory
mkdir -p /tmp/nginx

# Determine the mock response based on MOCK_TYPE
if [ "$MOCK_TYPE" = "fireworks" ]; then
    RESPONSE='{"choices": [{"message": {"content": "Mock Fireworks response"}}]}'
    ENDPOINT="/inference/v1/chat/completions"
elif [ "$MOCK_TYPE" = "groq" ]; then
    RESPONSE='{"choices": [{"message": {"content": "Mock Groq response"}}]}'
    ENDPOINT="/openai/v1/chat/completions"
elif [ "$MOCK_TYPE" = "together" ]; then
    RESPONSE='{"choices": [{"message": {"content": "Mock Together AI response"}}]}'
    ENDPOINT="/v1/chat/completions"
elif [ "$MOCK_TYPE" = "cerebras" ]; then
    RESPONSE='data: {"choices": [{"delta": {"content": "Mock Cerebras response"}}]}
data: [DONE]'
    ENDPOINT="/v1/chat/completions"
elif [ "$MOCK_TYPE" = "deepseek" ]; then
    RESPONSE='{"choices": [{"message": {"content": "Mock DeepSeek response"}}]}'
    ENDPOINT="/v1/chat/completions"
elif [ "$MOCK_TYPE" = "openai" ]; then
    RESPONSE='{"choices": [{"message": {"content": "Mock OpenAI response"}}]}'
    ENDPOINT="/v1/chat/completions"
elif [ "$MOCK_TYPE" = "ollama" ]; then
    RESPONSE='{"model": "llama3.2:3b", "created_at": "2023-09-23T00:00:00Z", "message": {"role": "assistant", "content": "This is a mock response from Ollama."}, "done": true}'
    ENDPOINT="/api/chat"
else
    RESPONSE='{"error": "Unknown mock"}'
    ENDPOINT="/v1/chat/completions"
fi

# Create nginx config
cat > /tmp/nginx/nginx.conf << EOF
error_log /dev/stderr warn;
pid /tmp/nginx/nginx.pid;

events {
    worker_connections 1024;
}

http {
    server {
        listen 80;
        access_log /dev/stdout;
        error_log /dev/stderr;

        location = /health {
            add_header Content-Type text/plain;
            return 200 "healthy\n";
        }

        location $ENDPOINT {
            add_header Content-Type application/json;
            return 200 '$RESPONSE';
        }
    }
}
EOF

# Start nginx
exec nginx -c /tmp/nginx/nginx.conf -g 'daemon off;'
