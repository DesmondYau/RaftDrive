#!/bin/bash
set -e

BINARY=/workspace/raftdrive/build/kvcluster/kvnode
LOCK=/workspace/raftdrive/build/.build.lock

mkdir -p /workspace/raftdrive/build

# Serialize builds across all containers sharing the build_cache volume.
# Only one container compiles at a time; others wait for the lock.
(
  flock 200

  if [ ! -f "$BINARY" ]; then
    echo "[build] Binary not found — running cmake + make..."
    cd /workspace/raftdrive
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
    echo "[build] cmake done."
  fi

  echo "[build] Building..."
  cmake --build /workspace/raftdrive/build --parallel 1
  echo "[build] Done."

) 200>"$LOCK"              # Open the file specified by $LOCK and assign it to File Descriptor (Channel) 200

exec "$BINARY" "$@"
