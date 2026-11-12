#!/bin/sh
# Runs INSIDE the throwaway VM. Tests snd-vban end to end with the SDK tools.
set -u
SDK=/home/claude/vban/SDK
OUT=/tmp/vmtest; mkdir -p $OUT; cd $OUT
B=$SDK/build-rel
echo "== kernel $(uname -r), loading modules"
ip link set lo up
modprobe snd-pcm 2>&1; modprobe snd-timer 2>&1
insmod $SDK/kernel/snd-vban.ko dest=127.0.0.1:6990 tx_name=Linux listen_port=6991 rx_name=Tone || { echo INSMOD_FAIL; dmesg | tail -20; exit 1; }
dmesg | grep -i "snd-vban" | tail -2
cat /proc/asound/cards
CARD=$(awk '/VBAN/{print $1; exit}' /proc/asound/cards)
echo "card=$CARD"

echo "== playback: aplay -> snd-vban -> VBAN -> vban_recv"
python3 -c "
import struct,math,wave
w=wave.open('tone.wav','wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(48000)
w.writeframes(b''.join(struct.pack('<hh',int(8192*math.sin(2*math.pi*440*i/48000)),int(8192*math.sin(2*math.pi*440*i/48000))) for i in range(48000*2)))
w.close()"
timeout -s INT 4 $B/vban_recv -p 6990 -n Linux -q > pb.raw &
sleep 0.3
aplay -q -D hw:$CARD,0 tone.wav; echo "aplay exit=$?"
sleep 1; wait
python3 -c "
import struct
d=open('pb.raw','rb').read(); n=len(d)//2; s=struct.unpack('<%dh'%n,d[:n*2]); nz=[abs(x) for x in s if x]
steps=max(abs(s[i+2]-s[i]) for i in range(0,n-2,2)) if n>4 else 0
print('PLAYBACK received %.2fs, nonzero %d, peak %d, max step %d' % (len(d)/4/48000, len(nz), max(nz) if nz else 0, steps))"

echo "== capture: vban_send -> VBAN -> snd-vban -> arecord"
timeout -s INT 4 $B/vban_send -i 127.0.0.1 -p 6991 -n Tone -b 64 -t 440 2>/dev/null &
sleep 0.3
arecord -q -D hw:$CARD,0 -f S16_LE -r 48000 -c 2 -d 2 cap.wav; echo "arecord exit=$?"
wait
python3 -c "
import wave,struct
w=wave.open('cap.wav'); d=w.readframes(w.getnframes()); n=len(d)//2; s=struct.unpack('<%dh'%n,d[:n*2])
nz=[abs(x) for x in s if x]; tail=s[n//2:]
# continuity: a 440 Hz tone at 0.25 FS has max step ~ 8192*2*pi*440/48000 = 472
steps=max(abs(tail[i+2]-tail[i]) for i in range(0,len(tail)-2,2))
print('CAPTURE %.2fs, nonzero %d, peak %d, max step (2nd half) %d' % (n/2/48000, len(nz), max(nz) if nz else 0, steps))"

echo "== capture with loss: vban_send with redundancy + 0 loss, then stats"
cat /proc/asound/card$CARD/vban

echo "== capture from a sender with redundant copies (-R 4): duplicates dropped, audio continuous"
timeout -s INT 4 $B/vban_send -i 127.0.0.1 -p 6991 -n Tone -b 64 -R 4 2>/dev/null &
sleep 0.3
arecord -q -D hw:$CARD,0 -f S16_LE -r 48000 -c 2 -d 2 cap2.wav; wait
python3 -c "
import wave,struct
w=wave.open('cap2.wav'); d=w.readframes(w.getnframes()); n=len(d)//2; s=struct.unpack('<%dh'%n,d[:n*2]); tail=s[n//2:]
print('CAPTURE+COPIES peak %d, max step (2nd half) %d' % (max(abs(x) for x in s), max(abs(tail[i+2]-tail[i]) for i in range(0,len(tail)-2,2))))"
cat /proc/asound/card$CARD/vban

echo "== stress: 30 open/close cycles of both directions while streaming"
timeout -s INT 8 $B/vban_send -i 127.0.0.1 -p 6991 -n Tone -b 64 2>/dev/null &
for i in $(seq 1 30); do
  arecord -q -D hw:$CARD,0 -f S16_LE -r 48000 -c 2 -s 2000 /dev/null &
  aplay -q -D hw:$CARD,0 -s 2000 tone.wav 2>/dev/null
  wait $!
done
wait
cat /proc/asound/card$CARD/vban

echo "== unload"
rmmod snd_vban; echo "rmmod exit=$?"
echo "== kernel log (warnings/oops)"
dmesg | grep -iE "snd-vban|snd_vban|BUG|WARNING|Oops|lockdep|circular|sleeping function" | tail -20
echo VMTEST_DONE
