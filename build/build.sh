#!/bin/sh
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="${IMAGE:-arc-rpi-build:latest}"
docker image inspect "$IMAGE" >/dev/null 2>&1 \
  && docker run --rm "$IMAGE" sh -c 'command -v python3 >/dev/null && command -v perl >/dev/null' 2>/dev/null \
  || docker build -t "$IMAGE" "$ROOT/build"
exec docker run --rm -v "$ROOT":/work -w /work/build "$IMAGE" make "$@"
