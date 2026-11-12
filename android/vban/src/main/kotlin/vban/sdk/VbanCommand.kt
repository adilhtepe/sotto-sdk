package vban.sdk

import java.io.Closeable

/** Sends VBAN TEXT commands, e.g. Voicemeeter remote scripting ("Strip[0].Mute=1;"). */
class VbanCommand(host: String, port: Int = Vban.DEFAULT_PORT, streamName: String = "Command1") : Closeable {
    private val handle = NativeHandle(VbanNative.cmdCreate(host, port, streamName), VbanNative::cmdDestroy)

    /** UTF-8, at most 1436 bytes per command. */
    fun send(text: String) {
        val bytes = text.toByteArray(Charsets.UTF_8)
        handle.use(Unit) { VbanNative.cmdSendText(it, bytes) }
    }

    override fun close() = handle.close()
}
