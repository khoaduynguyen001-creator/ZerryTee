#!/usr/bin/env bash
set -e
cd "$(dirname "$0")/.."
cd client
if [ "$#" -lt 3 ]; then
  echo "Usage: $0 <controller_host:8080> <local_udp_port> <node_id>"
  exit 1
fi
./client "$@"
