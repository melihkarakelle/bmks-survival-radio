#!/bin/sh
# Build the survival edition for the UV-K1 / UV-K5 V3 (PY32F071) in the Docker image of
# the F4HWN V3 tree, output to builds/bmks.survival.v3.bin.
# Usage: [SURVIVAL_VERSION=v6.9] tools/build-f4hwn-v3.sh
# The whole repository is mounted: firmware-v3 compiles the survival sources from firmware/.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/builds"
SURVIVAL_VERSION="${SURVIVAL_VERSION:-v6.9}"
mkdir -p "$OUT"

if command -v colima >/dev/null 2>&1; then
  colima status >/dev/null 2>&1 || colima start
fi
docker build -q -t uvk1-uvk5v3 "$ROOT/firmware-v3"

docker run --rm -u "$(id -u):$(id -g)" -v "$ROOT:/src" -w /src/firmware-v3 uvk1-uvk5v3 /bin/bash -c "\
  rm -rf build/Survival \
  && cmake --preset Survival -DVERSION_STRING_2=$SURVIVAL_VERSION -DSURVIVAL_VERSION=$SURVIVAL_VERSION >/dev/null \
  && cmake --build --preset Survival -j \
  && cp build/Survival/firmware.bin /src/builds/bmks.survival.v3.bin"

SIZE=$(wc -c < "$OUT/bmks.survival.v3.bin")
echo "bin: $SIZE bytes, free: $((120832 - SIZE)) of 120832"
