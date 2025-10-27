#!/usr/bin/env bash
set -e
cd "$(dirname "$0")/.."
cd controller
echo "Running controller on :8080"
go run main.go
