#!/bin/sh
# Perf matrix v2: hidden, vsync off (--benchmark), world only, stress workloads, 800x600 by default.
# Rows interleaved over 3 reps; each run writes telemetry JSONL + stress summary here.
# Usage: perf_run.sh REALM_CPP_DIR OUT_DIR BIN_DIR (BIN_DIR holds realm_client and realm_client_fif3, a
# build with frames_in_flight = 3). Add --width 1920 --height 1024 below for the real matrix.
SRC=${1:?realm-cpp dir}; OUT=${2:?output dir}; BIN=${3:?binary dir}
mkdir -p "$OUT"
cd "$SRC" || exit 1
run() {  # name binary renderer fixture extra-env...
  name=$1; bin=$2; api=$3; fixture=$4; shift 4
  env REALM_TEST_WORLD_ONLY=1 "$@" timeout 300 "$bin" --hidden --benchmark \
    --renderer "$api" --stress "$SRC/tests/fixtures/$fixture.json" --frames 300 \
    --stress-summary "$OUT/$name.summary.json" --telemetry "$OUT/$name.jsonl" \
    > "$OUT/$name.log" 2>&1
  echo "$name exit=$?"
}
for rep in 1 2 3; do
  for w in s3 nexus; do
    if [ $w = s3 ]; then fx=S3-combat-cluster; else fx=nexus-pan; fi
    run "gl-$w-r$rep"      "$BIN/realm_client"      gl     $fx
    run "vk2-$w-r$rep"     "$BIN/realm_client"      vulkan $fx
    run "vk2host-$w-r$rep" "$BIN/realm_client"      vulkan $fx REALM_VK_RING_MEMORY=host
    run "vk2pc-$w-r$rep"   "$BIN/realm_client"      vulkan $fx REALM_VK_PRESENTED_COPY=1
    run "vk3-$w-r$rep"     "$BIN/realm_client_fif3" vulkan $fx
  done
done
echo "perf-done"
