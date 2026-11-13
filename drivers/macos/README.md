# SottoAudio: virtual sound devices for macOS

A CoreAudio plug-in (AudioServerPlugIn, runs in user space, no kernel extension) that adds two
audio devices:
- **Sotto Out.** Apps play into it, and `sotto send` captures it and sends it as VBAN.
- **Sotto In.** `sotto play` plays a received VBAN stream into it, and apps record from it, as from
  a microphone.

Each device loops its output back to its input. Both are stereo, 32-bit float, at 44.1, 48, 88.2
or 96 kHz. The plug-in does no networking itself (`coreaudiod` is sandboxed); the `sotto` app in
`apps/` does, with the same jitter buffer, secure VBAN and hub support as on the other platforms.

## Build and install
You need the Xcode command line tools (`xcode-select --install`).
```bash
make
sudo make install            # copies to /Library/Audio/Plug-Ins/HAL, restarts coreaudiod
```
The devices then show up in System Settings › Sound and in every app.

## Use it
Send what this Mac plays, e.g. to the hub. Pick **Sotto Out** as the output device first.
```bash
sotto send -d "Sotto Out" -i 192.168.2.1 -n Mac
```
Receive a VBAN stream as a microphone. Apps record from **Sotto In**.
```bash
sotto play -d "Sotto In" -n Mix
```

## Uninstall
```bash
sudo make uninstall
```

## Status
It has not been built or run yet: there is no macOS on the machine this was written on. The code
passed a C syntax and type check against stand-in headers only. The first build and test need a
Mac; report any `make` errors or missing devices.
