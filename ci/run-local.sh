#!/usr/bin/env bash
# Local equivalent of .github/workflows/ci.yml for the jobs that can run on a
# Linux host. Each stage is skipped (not failed) when its tool is missing.
#
#   ci/run-local.sh [stage...]     stages: linux asan nonet aarch64 mingw espidf arduino
#                                  (default: all)
#
# Tools per stage:
#   linux/asan/nonet : cmake, gcc, clang
#   aarch64          : aarch64-linux-gnu-gcc (+ g++ for the C++ test), qemu-aarch64
#   mingw            : docker (image built from ubuntu:24.04 with mingw-w64 + wine)
#   espidf           : docker (espressif/idf:v5.4.1)
#   arduino          : arduino-cli on PATH with arduino:avr and esp32:esp32 cores
# Output goes to $VBAN_CI_OUT (default: build-ci/ in the SDK root).
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${VBAN_CI_OUT:-$ROOT/build-ci}
JOBS=$(nproc 2>/dev/null || echo 4)
mkdir -p "$OUT"
STAGES=${*:-linux asan nonet aarch64 mingw espidf arduino}
PASS=() FAIL=() SKIP=()

have() { command -v "$1" >/dev/null 2>&1; }
run_stage() { # name, function; a stage returns 77 when its tools are missing
    local rc
    echo "===================== $1"
    "$2"; rc=$?
    case $rc in 0) PASS+=("$1") ;; 77) SKIP+=("$1") ;; *) FAIL+=("$1") ;; esac
}
skip() { echo "skip: $*"; return 77; }

cmake_test() { # dir, extra cmake args...
    local d=$1; shift
    rm -rf "$d"
    cmake -S "$ROOT" -B "$d" -DCMAKE_BUILD_TYPE=Release -DVBAN_WERROR=ON "$@" >/dev/null &&
        cmake --build "$d" -j"$JOBS" && ctest --test-dir "$d" --output-on-failure
}

st_linux() { CC=gcc CXX=g++ cmake_test "$OUT/linux-gcc" && CC=clang CXX=clang++ cmake_test "$OUT/linux-clang"; }
st_asan()  { CC=clang CXX=clang++ cmake_test "$OUT/linux-asan" -DVBAN_SANITIZE=ON; }
st_nonet() {
    rm -rf "$OUT/nonet"
    cmake -S "$ROOT" -B "$OUT/nonet" -DVBAN_WERROR=ON -DVBAN_ENABLE_NET=OFF -DVBAN_BUILD_TESTS=OFF >/dev/null &&
        cmake --build "$OUT/nonet" -j"$JOBS"
}
st_aarch64() {
    have aarch64-linux-gnu-gcc && have qemu-aarch64 || { skip aarch64 "aarch64-linux-gnu-gcc / qemu-aarch64 missing"; }
    local d=$OUT/aarch64 cxx=(-DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++) targets=()
    if ! have aarch64-linux-gnu-g++; then # C-only: skip the C++ wrapper test
        cxx=(-DCMAKE_CXX_COMPILER=aarch64-linux-gnu-gcc -DCMAKE_CXX_COMPILER_WORKS=1)
        targets=(--target vban_tests)
    fi
    rm -rf "$d"
    cmake -S "$ROOT" -B "$d" -DCMAKE_BUILD_TYPE=Release -DVBAN_WERROR=ON -DCMAKE_SYSTEM_NAME=Linux \
        -DCMAKE_SYSTEM_PROCESSOR=aarch64 -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc "${cxx[@]}" >/dev/null &&
        cmake --build "$d" -j"$JOBS" "${targets[@]}" &&
        qemu-aarch64 -L /usr/aarch64-linux-gnu "$d/vban_tests" | tail -1 | grep -q ' 0 failures'
}
st_mingw() {
    have docker || { skip mingw "docker missing"; }
    docker image inspect vban-ci-mingw >/dev/null 2>&1 || docker build -t vban-ci-mingw - <<'EOF' || return 1
FROM ubuntu:24.04
RUN dpkg --add-architecture i386 && apt-get update && DEBIAN_FRONTEND=noninteractive \
    apt-get install -y --no-install-recommends mingw-w64 g++-mingw-w64 cmake make wine wine64 wine32:i386 \
    && rm -rf /var/lib/apt/lists/*
EOF
    docker run --rm --tmpfs /tmp:exec -u "$(id -u):$(id -g)" -v "$ROOT:/sdk:ro" -v "$OUT:/out" -e HOME=/out/winehome \
        -e WINEDEBUG=-all vban-ci-mingw bash -ec '
        mkdir -p /out/winehome
        for t in x86_64-w64-mingw32 i686-w64-mingw32; do
            d=/out/mingw-$t; rm -rf $d
            cmake -S /sdk -B $d -DCMAKE_TOOLCHAIN_FILE=/sdk/ci/mingw-toolchain.cmake -DMINGW_TRIPLE=$t \
                -DCMAKE_BUILD_TYPE=Release -DVBAN_WERROR=ON >/dev/null
            cmake --build $d -j'"$JOBS"'
            (cd $d && wine ./vban_tests.exe | tail -1 | grep -q " 0 failures" && wine ./vban_cpp_test.exe)
        done'
}
st_espidf() {
    have docker || { skip espidf "docker missing"; }
    docker run --rm --tmpfs /tmp:exec -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$ROOT:/src/vban_sdk:ro" -v "$OUT:/out" \
        espressif/idf:v5.4.1 bash -ec '
        . "$IDF_PATH/export.sh" >/dev/null 2>&1
        cd /src/vban_sdk/ci/esp-idf
        for t in esp32 esp32s3 esp32c3 esp32c6; do
            idf.py -B /out/idf-$t -DSDKCONFIG=/out/idf-$t.sdkconfig set-target $t build > /out/idf-$t.log 2>&1 \
                || { tail -30 /out/idf-$t.log; exit 1; }
            ! grep -E "(/src/|/ci/).*warning:" /out/idf-$t.log
            echo "esp-idf $t: ok"
        done'
}
st_arduino() {
    have arduino-cli || { skip arduino "arduino-cli missing"; }
    local fqbn rc=0
    for fqbn in arduino:avr:mega esp32:esp32:esp32 esp32:esp32:esp32c3; do
        echo "--- $fqbn"
        arduino-cli compile --fqbn "$fqbn" --warnings all --library "$ROOT" \
            --build-path "$OUT/arduino-${fqbn//:/_}" "$ROOT/ci/arduino/vban_smoke" || rc=1
    done
    return $rc
}

for s in $STAGES; do
    case $s in
        linux|asan|nonet|aarch64|mingw|espidf|arduino) run_stage "$s" "st_$s" ;;
        *) echo "unknown stage: $s"; exit 2 ;;
    esac
done
echo "===================== summary"
echo "pass: ${PASS[*]:-}"; echo "skip: ${SKIP[*]:-}"; echo "fail: ${FAIL[*]:-}"
[ ${#FAIL[@]} -eq 0 ]
