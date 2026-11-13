#!/bin/sh
# sotto on real-time ALSA devices (snd-aloop), inside a throwaway VM (virtme-ng):
#   sudo vng -r /boot/vmlinuz-$(uname -r) --memory 1G --cpus 2 --exec "sh $PWD/apps/sotto/vm-test.sh"
B=$(cd "$(dirname "$0")/../.." && pwd)/build-sec
T=/tmp/sottotest; mkdir -p $T; cd $T
modprobe snd-aloop pcm_substreams=2 || { echo "FAIL no snd-aloop"; exit 1; }
CARD=$(grep -l Loopback /proc/asound/card*/id | sed 's#.*/card\([0-9]*\)/id#\1#')
python3 -I -c "
import math,struct
f=open('tone.wav','wb'); n=48000*6
f.write(b'RIFF'+struct.pack('<I',36+n*4)+b'WAVEfmt '+struct.pack('<IHHIIHH',16,1,2,48000,192000,4,16)+b'data'+struct.pack('<I',n*4))
for i in range(n):
  v=int(8000*math.sin(2*math.pi*440*i/48000)); f.write(struct.pack('<hh',v,v))"
check() { python3 -I - "$1" "$2" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read()
if sys.argv[2] == 'f32':
    s = struct.unpack('<%df' % (len(d) // 4), d[:len(d) // 4 * 4]); s = [int(x * 32767) for x in s]
else:
    s = struct.unpack('<%dh' % (len(d) // 2), d[:len(d) // 2 * 2])
s = s[len(s) // 2:]                     # second half: steady state
L = s[0::2]
step = max(abs(L[i + 1] - L[i]) for i in range(len(L) - 1)) if len(L) > 1 else 0
print("samples %d peak %d max-step %d" % (len(s), max(abs(x) for x in s) if s else 0, step))
sys.exit(0 if s and 7000 < max(abs(x) for x in s) < 9000 and step < 700 else 1)
PY
}

echo "== sotto play -> Loopback,0,0 -> arecord Loopback,1,0"
echo "pcm.!default { type hw; card $CARD; device 0; subdevice 0 }" > $T/.asoundrc
HOME=$T timeout -s INT 5 $B/sotto play -p 17201 -n Tone -q 2> play.err &
sleep 0.7
timeout -s INT 3 arecord -D hw:$CARD,1,0 -f FLOAT_LE -r 48000 -c 2 -t raw play.raw 2> arec.err &
timeout -s INT 3 $B/vban_send -i 127.0.0.1 -p 17201 -n Tone 2>/dev/null
wait
cat play.err | tr '\r' '\n' | grep -v '^$' | head -2
cat arec.err | head -3; check play.raw f32 && echo "PASS play" || echo "FAIL play"

echo "== aplay -> Loopback,0,1 -> sotto send (capture Loopback,1,1) -> vban_recv"
echo "pcm.!default { type hw; card $CARD; device 1; subdevice 1 }" > $T/.asoundrc
timeout -s INT 5 $B/vban_recv -p 17202 -n Cap -q > send.raw &
aplay -q -D hw:$CARD,0,1 tone.wav &
sleep 0.3
HOME=$T timeout -s INT 3 $B/sotto send -i 127.0.0.1 -p 17202 -n Cap -q 2> send.err
wait
cat send.err | tr '\r' '\n' | grep -v '^$' | head -2
check send.raw s16 && echo "PASS send" || echo "FAIL send"
echo SOTTO_DONE
