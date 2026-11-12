package vban.sdk

/** JNI entry points (libvban_jni). Internal: use the public classes. */
internal object VbanNative {
    init { System.loadLibrary("vban_jni") }

    @JvmStatic external fun version(): String
    @JvmStatic external fun strerror(code: Int): String
    @JvmStatic external fun threadRealtime(): Boolean

    @JvmStatic external fun txCreate(name: String, rate: Int, channels: Int, format: Int, framesPerPacket: Int, qos: Boolean): Long
    @JvmStatic external fun txAddDest(h: Long, host: String, port: Int)
    @JvmStatic external fun txSetRedundancy(h: Long, delayPackets: Int)
    @JvmStatic external fun txWriteShorts(h: Long, a: ShortArray, offset: Int, frames: Int): Int
    @JvmStatic external fun txWriteFloats(h: Long, a: FloatArray, offset: Int, frames: Int): Int
    @JvmStatic external fun txFlush(h: Long): Int
    @JvmStatic external fun txStats(h: Long, out: LongArray)
    @JvmStatic external fun txDestroy(h: Long)

    @JvmStatic external fun rxCreate(port: Int, name: String, source: String?, channels: Int, outRate: Int,
                                     targetFrames: Int, capacityFrames: Int, s16Ring: Boolean): Long
    @JvmStatic external fun rxSetIdentity(h: Long, device: String, app: String, host: String, preferredRate: Int)
    @JvmStatic external fun rxPoll(h: Long, timeoutMs: Int): Int
    @JvmStatic external fun rxReadShorts(h: Long, a: ShortArray, offset: Int, frames: Int): Int
    @JvmStatic external fun rxReadFloats(h: Long, a: FloatArray, offset: Int, frames: Int): Int
    @JvmStatic external fun rxStats(h: Long, out: LongArray)
    @JvmStatic external fun rxLocalPort(h: Long): Int
    @JvmStatic external fun rxDestroy(h: Long)

    @JvmStatic external fun cmdCreate(host: String, port: Int, name: String): Long
    @JvmStatic external fun cmdSendText(h: Long, utf8: ByteArray)
    @JvmStatic external fun cmdDestroy(h: Long)

    @JvmStatic external fun discover(target: String, port: Int, timeoutMs: Int, maxDevices: Int): Array<VbanDevice>
}
