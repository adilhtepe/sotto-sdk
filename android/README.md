# VBAN SDK for Android (Kotlin)

A Kotlin API over the C SDK, as an Android library (`vban.sdk`, minSdk 21, arm64-v8a /
armeabi-v7a / x86_64 / x86). The release AAR is about 128 KB: each ABI's `.so` is 35–53 KB and
exports only the 23 JNI entry points. Native code is 16 KB page aligned, as required for
Android 15+ devices.

```kotlin
// Send the microphone (or any PCM) to a hub / Voicemeeter
val tx = VbanSender("Mic", sampleRate = 48000, channels = 2, framesPerPacket = 64)
    .addDestination("192.168.2.1")      // up to 4 destinations
    .setRedundancy(4)                   // optional loss protection, 2x bandwidth (SDK receivers)
tx.write(shorts)                        // from the capture callback; no allocation per call

// Play a stream
val rx = VbanReceiver("Mix", outputRate = 48000, targetMs = 10)
    .setIdentity("Pixel 8", "MyApp")    // answer PING0 discovery (optional)
    .start()                            // network thread, owned by rx
rx.read(buffer)                         // from the AAudio / Oboe / AudioTrack callback
println(rx.stats())                     // packets, lost, repaired, latency, ...

// Discover devices, send Voicemeeter commands
val devices = VbanDiscovery.discover()  // blocking: call off the main thread
VbanCommand("192.168.2.10").use { it.send("Strip[0].Mute=1;") }

// While streaming over Wi-Fi
val locks = VbanNetworkLocks(context).acquire()   // MulticastLock + low-latency WifiLock
```

## Behaviour
- **`read()` is real-time safe.** It pins the Java array with `GetPrimitiveArrayCritical` and the
  lock-free jitter buffer fills it directly: no copy, lock, allocation or system call. It always
  fills the whole buffer with stream audio, concealment over losses, or silence while priming.
- **`write()`** copies into a native buffer allocated once, then converts and sends.
  `sendto()` is never called inside a critical region.
- **`close()`** can be called from any thread, including while another thread is inside
  `read()`/`write()`. A two-atomic usage guard waits for the in-flight call, and any later call
  returns 0. There is no use-after-free.
- **Errors** throw `VbanException(code, message)`, with `code` = `VBAN_E_*`.
- **PING0 replies:** device names in them are cut down to printable ASCII before they become Java
  strings. Invalid modified UTF-8 would make CheckJNI abort the app.

## Use it from an app
Composite build: Gradle builds the library from source with your app.
```kotlin
// settings.gradle.kts
includeBuild("../SDK/android")
// app/build.gradle.kts
dependencies { implementation("vban.sdk:vban:1.0.0") }
```
This was verified by adding exactly these two lines to a copy of `android-transmitter`
(AGP 8.5.2, Kotlin 1.9.24). `:app:assembleDebug` builds and packages `libvban_jni.so` for all four
ABIs.

To use a prebuilt AAR instead:
```bash
./gradlew :vban:assembleRelease
```
Then add `vban/build/outputs/aar/vban-release.aar` to the app.

**Toolchain.** The build needs an NDK and CMake. If the Android SDK has none, AGP downloads NDK
26.1 and CMake 3.22.1 into it. To use an NDK that is somewhere else:
```bash
./gradlew ... -Pvban.ndkPath=/path/to/ndk
```
The version is read from that NDK. `cmake.dir=...` in `local.properties` does the same for CMake.

## Tests
```bash
./gradlew :vban:testDebugUnitTest
```
This builds `libvban_jni` for the host and runs the Kotlin API on the JVM against it, with real UDP
on 127.0.0.1. It covers:
- sender to receiver, checking amplitude and that nothing is lost
- redundancy copies being de-duplicated
- stream-name filtering
- PING0 discovery of a receiver's identity
- text command limits
- error mapping
- use after close
- 20 rounds of `close()` racing a reader thread

Result: 9/9 pass. The same C code paths run on Android. The C SDK's own 426-check suite also passes
on Android bionic (arm64, x86_64).

Not tested here: on a device or emulator, with AAudio/Oboe, or how a device treats the `nice -19`
priority request.
