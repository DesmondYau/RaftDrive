#!/bin/bash
set -e

BINARY=/workspace/raftdrive/build/raftdrive/raftdrive
LOCK=/workspace/raftdrive/build/.build.lock

mkdir -p /workspace/raftdrive/build

# Serialize builds across all containers sharing the build_cache volume.
(
  flock 200

  if [ ! -f "$BINARY" ]; then
    echo "[build] Binary not found — running cmake + make..."
    cd /workspace/raftdrive
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
    echo "[build] cmake done."
  fi

  echo "[build] Building..."
  cmake --build /workspace/raftdrive/build --parallel 2
  echo "[build] Done."

) 200>"$LOCK"

exec "$BINARY" "$@"
