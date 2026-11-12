package vban.sdk

import java.io.Closeable

/**
 * Receives one VBAN audio stream into an adaptive, lock-free jitter buffer.
 *
 * start() runs the network thread (owned by this object). The audio side calls
 * read() from its callback (AAudio / Oboe / AudioTrack loop): it always fills
 * the requested frames - stream audio, concealment over losses, or silence
 * while priming - without locks, allocation or system calls.
 *
 * @param streamName VBAN stream name to accept (exact, case-sensitive)
 * @param source only accept this sender IP (null = any)
 * @param outputRate the device rate; 0 = play at the stream's rate
 * @param targetMs initial buffering; the buffer adapts from there
 * @param capacityFrames ring size, power of two (16384 = 340 ms @ 48 kHz)
 */
class VbanReceiver(
    streamName: String,
    val port: Int = Vban.DEFAULT_PORT,
    source: String? = null,
    val channels: Int = 2,
    outputRate: Int = 48000,
    targetMs: Int = 10,
    capacityFrames: Int = 16384,
    int16Ring: Boolean = true,
) : Closeable {
    private val handle = NativeHandle(
        VbanNative.rxCreate(port, streamName, source, channels, outputRate,
                            targetMs * (if (outputRate > 0) outputRate else 48000) / 1000, capacityFrames, int16Ring),
        VbanNative::rxDestroy,
    )
    @Volatile private var running = false
    private var thread: Thread? = null

    /** Answer PING0 discovery as this device. Call before start(). */
    fun setIdentity(deviceName: String, applicationName: String, hostName: String = "", preferredRate: Int = 48000): VbanReceiver {
        handle.use(Unit) { VbanNative.rxSetIdentity(it, deviceName, applicationName, hostName, preferredRate) }
        return this
    }

    /** UDP port actually bound (useful with port = 0). */
    val localPort: Int get() = handle.use(0) { VbanNative.rxLocalPort(it) }

    /** Start the network thread. [onError] is called (on that thread) if receiving fails; the thread then stops. */
    @Synchronized
    fun start(onError: ((Throwable) -> Unit)? = null): VbanReceiver {
        if (running || handle.isClosed) return this
        running = true
        thread = Thread({
            VbanNative.threadRealtime()
            try {
                while (running) {
                    if (handle.use(-1) { VbanNative.rxPoll(it, 100) } < 0) break
                }
            } catch (t: Throwable) {
                running = false
                onError?.invoke(t)
            }
        }, "vban-rx-$port").also { it.isDaemon = true; it.start() }
        return this
    }

    /** Fill [frames] interleaved frames. Returns frames played from the stream (0 while priming). Real-time safe. */
    fun read(out: ShortArray, frames: Int = out.size / channels, offset: Int = 0): Int =
        handle.use(0) { VbanNative.rxReadShorts(it, out, offset, frames) }

    /** Float variant (-1..1). Real-time safe. */
    fun read(out: FloatArray, frames: Int = out.size / channels, offset: Int = 0): Int =
        handle.use(0) { VbanNative.rxReadFloats(it, out, offset, frames) }

    data class Stats(
        val packets: Long, val lost: Long, val repaired: Long, val late: Long, val duplicates: Long,
        val underruns: Long, val concealedFrames: Long, val bufferedFrames: Long, val targetFrames: Long,
        val streamRate: Long, val latencyMicros: Long, val rejected: Long,
    )

    /** Approximate snapshot; safe from any thread. */
    fun stats(): Stats {
        val v = LongArray(12)
        handle.use(Unit) { VbanNative.rxStats(it, v) }
        return Stats(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11])
    }

    /** Stops the network thread and frees native memory. Concurrent read() calls return 0. */
    override fun close() {
        val t: Thread?
        synchronized(this) {
            running = false
            t = thread
            thread = null
        }
        t?.join(1000)
        handle.close()
    }
}
