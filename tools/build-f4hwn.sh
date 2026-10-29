#!/bin/sh
# Build F4HWN-based firmware (V1 / DP32G030) in the Alpine Docker image, output to builds/.
# Usage: [EDITION=bandscope|survival] [TARGET=name] tools/build-f4hwn.sh [src dir]
#   default src: refs/armel_uv-k5-firmware-custom, default EDITION: bandscope
# Does NOT run the upstream compile-with-docker.sh: that one prunes all Docker volumes.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${1:-$ROOT/refs/armel_uv-k5-firmware-custom}"
OUT="$ROOT/builds"
EDITION="${EDITION:-bandscope}"
SURVIVAL_VERSION="${SURVIVAL_VERSION:-v1.1}"
mkdir -p "$OUT"

case "$EDITION" in
  bandscope)
    TARGET="${TARGET:-f4hwn.bandscope}"
    FLAGS="ENABLE_SPECTRUM=1 ENABLE_FMRADIO=0 ENABLE_VOX=0 ENABLE_AIRCOPY=1 \
      ENABLE_FEAT_F4HWN_SCREENSHOT=1 ENABLE_FEAT_F4HWN_GAME=0 ENABLE_FEAT_F4HWN_PMR=1 \
      ENABLE_FEAT_F4HWN_GMRS_FRS_MURS=1 ENABLE_NOAA=0 ENABLE_FEAT_F4HWN_RESCUE_OPS=0 \
      EDITION_STRING=Bandscope" ;;
  survival)
    TARGET="${TARGET:-bmks.survival}"
    FLAGS="ENABLE_SURVIVAL=1 ENABLE_SPECTRUM=0 ENABLE_FMRADIO=1 ENABLE_VOX=0 ENABLE_AIRCOPY=0 \
      ENABLE_FEAT_F4HWN_SCREENSHOT=1 ENABLE_FEAT_F4HWN_GAME=0 ENABLE_FEAT_F4HWN_PMR=1 \
      ENABLE_FEAT_F4HWN_GMRS_FRS_MURS=1 ENABLE_NOAA=0 ENABLE_FEAT_F4HWN_RESCUE_OPS=0 \
      BMK_SIGNATURE= EDITION_STRING=Survival \
      ENABLE_AUDIO_BAR=0 ENABLE_FEAT_F4HWN_RESUME_STATE=0 ENABLE_FEAT_F4HWN_SLEEP=0 \
      ENABLE_FEAT_F4HWN_RX_TX_TIMER=0 ENABLE_FEAT_F4HWN_CA=0 ENABLE_NO_CODE_SCAN_TIMEOUT=0 \
      AUTHOR_STRING_1=BMKs AUTHOR_STRING_2=BMKs AUTHOR_STRING=BMKs \
      VERSION_STRING_2=$SURVIVAL_VERSION SURVIVAL_VERSION=$SURVIVAL_VERSION" ;;
  *) echo "unknown EDITION $EDITION"; exit 1 ;;
esac

colima status >/dev/null 2>&1 || colima start
docker build -q --build-arg ALPINE_TAG=3.22 -t uvk5-f4hwn "$SRC"

docker run --rm -v "$OUT:/out" uvk5-f4hwn /bin/bash -c "cd /app \
  && find . -name '*.d' -delete && find . -name '*.o' -delete \
  && make -s -j8 $FLAGS TARGET=$TARGET \
  && arm-none-eabi-size $TARGET \
  && cp $TARGET $TARGET.bin $TARGET.packed.bin /out/"

SIZE=$(stat -f %z "$OUT/$TARGET.bin")
echo "bin: $SIZE bytes, free: $((61440 - SIZE)) of 61440"
