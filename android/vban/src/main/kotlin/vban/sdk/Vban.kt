package vban.sdk

import java.util.concurrent.atomic.AtomicInteger

/** Error raised by the native SDK; [code] is a VBAN_E_* value. */
class VbanException(val code: Int, message: String) : RuntimeException(message)

/** Wire sample formats (VBAN_DATATYPE_*). */
object VbanFormat {
    const val U8 = 0
    const val S16 = 1
    const val S24 = 2
    const val S32 = 3
    const val F32 = 4
    const val F64 = 5
}

object Vban {
    const val DEFAULT_PORT = 6980
    /** Native SDK version. */
    val version: String get() = VbanNative.version()
    /** Best-effort real-time priority for the calling thread (SCHED_FIFO, else nice -19 on Android). */
    fun boostCurrentThread(): Boolean = VbanNative.threadRealtime()
}

/** A device that answered a PING0 discovery. Strings are printable ASCII only. */
data class VbanDevice(
    val ip: String,
    val port: Int,
    val deviceName: String,
    val applicationName: String,
    val hostName: String,
    val manufacturer: String,
    val type: Int,
    val features: Int,
    val preferredRate: Int,
)

/**
 * Lifetime guard for a native handle used from a real-time thread.
 * enter()/exit() are two atomic ops (no lock, no allocation); close() marks
 * the handle closed, waits until no call is inside, then frees it once.
 */
internal class NativeHandle(private var h: Long, private val destroy: (Long) -> Unit) {
    private val users = AtomicInteger(0)
    @Volatile private var closed = false

    /** Returns the handle or 0 if closed; pair every non-zero result with exit(). */
    fun enter(): Long {
        users.incrementAndGet()
        if (closed) { users.decrementAndGet(); return 0L }
        return h
    }

    fun exit() { users.decrementAndGet() }

    inline fun <T> use(closedValue: T, block: (Long) -> T): T {
        val handle = enter()
        if (handle == 0L) return closedValue
        try { return block(handle) } finally { exit() }
    }

    fun close() {
        synchronized(this) {
            if (closed) return
            closed = true
        }
        while (users.get() != 0) Thread.yield()  // onSpinWait() needs API 26
        val handle = h
        h = 0L
        destroy(handle)
    }

    val isClosed: Boolean get() = closed
}
