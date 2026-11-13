#!/bin/sh
# Runs inside ci/pipewire (docker): start PipeWire + WirePlumber, build, play into "Sotto out",
# record from "Sotto in", check both tones.
set -e
export XDG_RUNTIME_DIR=/tmp/xdg; mkdir -p $XDG_RUNTIME_DIR; chmod 700 $XDG_RUNTIME_DIR
B=/tmp/pwbuild; cmake -S /sdk -B $B -DCMAKE_BUILD_TYPE=Release -DVBAN_BUILD_TESTS=OFF -DVBAN_BUILD_BENCH=OFF >/dev/null
cmake --build $B -j --target sotto-pw vban_send vban_recv 2>&1 | grep -E "error|warning: " || true
cd /tmp
dbus-daemon --session --address=unix:path=$XDG_RUNTIME_DIR/bus --fork >/dev/null
export DBUS_SESSION_BUS_ADDRESS=unix:path=$XDG_RUNTIME_DIR/bus
pipewire >/tmp/pw.log 2>&1 &
sleep 1
wireplumber >/tmp/wp.log 2>&1 &
sleep 2
python3 -I -c "
import math,struct
f=open('tone.wav','wb'); n=48000*5
f.write(b'RIFF'+struct.pack('<I',36+n*4)+b'WAVEfmt '+struct.pack('<IHHIIHH',16,1,2,48000,192000,4,16)+b'data'+struct.pack('<I',n*4))
for i in range(n):
  v=int(8000*math.sin(2*math.pi*440*i/48000)); f.write(struct.pack('<hh',v,v))"
$B/sotto-pw -i 127.0.0.1:17402 -o Desk -n Mic -p 17401 2> spw.err &
SPW=$!
sleep 1.5
pw-cli ls Node 2>/dev/null | grep -E 'node.(name|description)' | grep -i sotto || true
check() { python3 -I - "$1" "$2" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read()
if sys.argv[2] == 'wav': d = d[44:]
s = struct.unpack('<%dh' % (len(d) // 2), d[:len(d) // 2 * 2])
s = s[len(s) // 3: -len(s) // 6 or None]
L = s[0::2]
step = max(abs(L[i + 1] - L[i]) for i in range(len(L) - 1)) if len(L) > 1 else 0
peak = max(abs(x) for x in s) if s else 0
print("samples %d peak %d max-step %d" % (len(s), peak, step))
sys.exit(0 if 7000 < peak < 9000 and step < 800 else 1)
PY
}
echo "== app -> \"Sotto out\" -> VBAN -> vban_recv"
timeout -s INT 6 $B/vban_recv -p 17402 -n Desk -q > out.raw &
RX=$!
sleep 0.3
timeout 5 pw-play --target sotto_out tone.wav || true
wait $RX || true
check out.raw raw && echo "PASS Sotto out" || echo "FAIL Sotto out"
echo "== VBAN -> \"Sotto in\" -> app (pw-record)"
timeout -s INT 4 pw-record --target sotto_in --format s16 --rate 48000 --channels 2 rec.wav &
REC=$!
sleep 0.3
timeout -s INT 3 $B/vban_send -i 127.0.0.1 -p 17401 -n Mic >/dev/null 2>&1 || true
wait $REC || true
check rec.wav wav && echo "PASS Sotto in" || echo "FAIL Sotto in"
kill -INT $SPW; sleep 1; kill -9 $SPW 2>/dev/null || true
cat spw.err | head -3
echo PW_DONE
