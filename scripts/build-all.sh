#!/usr/bin/env bash
set -e
cd "$(dirname "$0")/.."
echo "Building client..."
cd client
make
echo "Done."
