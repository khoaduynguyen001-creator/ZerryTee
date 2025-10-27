#!/usr/bin/env bash
# quick local test: controller + two clients (requires tmux or separate terminals)
set -e
ROOT=$(cd "$(dirname "$0")/.."; pwd)
echo "Start controller in one terminal: (cd $ROOT/controller && go run main.go)"
echo "Then run two clients in other terminals:"
echo "Terminal1: cd $ROOT/client && ./client 127.0.0.1:8080 40000 node-A"
echo "Terminal2: cd $ROOT/client && ./client 127.0.0.1:8080 40001 node-B"
