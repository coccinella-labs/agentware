#!/bin/bash
# Full check: clean → build → test → preflight-quick → preflight

set -e  # Exit on error

echo "==> Running full check sequence..."
for step in clean build test preflight-quick preflight; do
  echo "--> $step..."
  make "$step"
done
echo "✅ Full check completed successfully."
