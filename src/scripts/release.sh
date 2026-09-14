#!/bin/bash
# Agentware Agent - Release Script

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_ROOT"

VERSION=$(cat VERSION | tr -d '\n')
RELEASE_DIR="release/v${VERSION}"

echo "Building release v${VERSION}..."

make clean && make build

mkdir -p "$RELEASE_DIR"
make package
cp -r dist/* "$RELEASE_DIR/"
git archive --format=tar.gz --prefix="agentware-agent-${VERSION}/" HEAD > "$RELEASE_DIR/agentware-agent-${VERSION}-source.tar.gz"

cd "$RELEASE_DIR"
shasum -a 256 * > checksums.sha256

echo "Release ready: $RELEASE_DIR"
ls -la
