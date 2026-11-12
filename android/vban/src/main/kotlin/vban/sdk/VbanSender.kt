package vban.sdk

import java.io.Closeable

/**
 * Sends one VBAN audio stream.
 *
 * write() may be called from an audio callback: samples are copied into a
 * native buffer allocated here, converted to the wire format and sent as soon
 * as a packet is full. Nothing is allocated per call.
 *
 * @param framesPerPacket 1..256; smaller = lower latency (64 frames @ 48 kHz = 1.3 ms). 0 = largest that fits.
 */
class VbanSender(
    streamName: String,
    val sampleRate: Int = 48000,
    val channels: Int = 2,
    format: Int = VbanFormat.S16,
    framesPerPacket: Int = 64,
    qos: Boolean = true,
) : Closeable {
    private val handle = NativeHandle(
        VbanNative.txCreate(streamName, sampleRate, channels, format, framesPerPacket, qos),
        VbanNative::txDestroy,
    )

    /** Add a destination (host name or IPv4, broadcast allowed). Up to 4. */
    fun addDestination(host: String, port: Int = Vban.DEFAULT_PORT): VbanSender {
        handle.use(Unit) { VbanNative.txAddDest(it, host, port) }
        return this
    }

    /**
     * Resend every packet [delayPackets] packets later (loss protection, 2x
     * bandwidth). About 5 ms is ideal: 4 at 64 frames / 48 kHz. 0 disables.
     * Intended for SDK receivers; Voicemeeter's handling of duplicates is unverified.
     */
    fun setRedundancy(delayPackets: Int): VbanSender {
        handle.use(Unit) { VbanNative.txSetRedundancy(it, delayPackets) }
        return this
    }

    /** Write interleaved 16-bit frames; returns packets sent. */
    fun write(samples: ShortArray, frames: Int = samples.size / channels, offset: Int = 0): Int =
        handle.use(0) { VbanNative.txWriteShorts(it, samples, offset, frames) }

    /** Write interleaved float frames (-1..1); returns packets sent. */
    fun write(samples: FloatArray, frames: Int = samples.size / channels, offset: Int = 0): Int =
        handle.use(0) { VbanNative.txWriteFloats(it, samples, offset, frames) }

    /** Send the partly filled packet now. */
    fun flush(): Int = handle.use(0) { VbanNative.txFlush(it) }

    data class Stats(val packets: Long, val redundant: Long, val errors: Long, val bytes: Long)

    fun stats(): Stats {
        val v = LongArray(4)
        handle.use(Unit) { VbanNative.txStats(it, v) }
        return Stats(v[0], v[1], v[2], v[3])
    }

    override fun close() = handle.close()
}
