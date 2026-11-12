package vban.sdk

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.sin

/**
 * Runs on the host JVM against the real libvban_jni (built by :vban:buildHostJni)
 * and real UDP sockets on 127.0.0.1. Covers the same code paths as on Android.
 */
class VbanJniTest {

    private fun tone(frames: Int, channels: Int, start: Int): ShortArray {
        val a = ShortArray(frames * channels)
        for (i in 0 until frames) {
            val v = (sin(2 * PI * 440 * (start + i) / 48000.0) * 8000).toInt().toShort()
            for (c in 0 until channels) a[i * channels + c] = v
        }
        return a
    }

    /** Send [seconds] of tone in real time-ish blocks while reading on this thread. */
    private fun stream(tx: VbanSender, rx: VbanReceiver, seconds: Double): Pair<Int, Int> {
        val block = 128
        val out = ShortArray(block * 2)
        var played = 0
        var peak = 0
        val blocks = (seconds * 48000 / block).toInt()
        for (b in 0 until blocks) {
            tx.write(tone(block, 2, b * block))
            Thread.sleep(0, 500_000)
            val got = rx.read(out)
            played += got
            for (s in out) peak = maxOf(peak, abs(s.toInt()))
        }
        return played to peak
    }

    @Test fun versionLoads() {
        assertTrue(Vban.version.isNotEmpty())
    }

    @Test fun badArgumentsThrowVbanException() {
        try {
            VbanSender("x", channels = 0)
            fail("expected VbanException")
        } catch (e: VbanException) {
            assertEquals(-1, e.code)  // VBAN_E_ARG
        }
        try {
            VbanSender("x", sampleRate = 47999)  // not a VBAN rate
            fail("expected VbanException")
        } catch (e: VbanException) {
            assertTrue(e.message!!.isNotEmpty())
        }
    }

    @Test fun senderToReceiverOverUdp() {
        VbanReceiver("Tone", port = 0, targetMs = 10).start().use { rx ->
            VbanSender("Tone", framesPerPacket = 64).addDestination("127.0.0.1", rx.localPort).use { tx ->
                val (played, peak) = stream(tx, rx, 1.0)
                val st = rx.stats()
                println("loopback: played=$played peak=$peak stats=$st tx=${tx.stats()}")
                assertTrue("packets arrived", st.packets > 500)
                assertTrue("audio played", played > 24000)
                assertTrue("tone amplitude came through", peak in 7000..8200)
                assertEquals(0L, st.lost)
            }
        }
    }

    @Test fun redundancyCopiesAreDeduplicated() {
        VbanReceiver("Red", port = 0).start().use { rx ->
            VbanSender("Red").addDestination("127.0.0.1", rx.localPort).setRedundancy(4).use { tx ->
                stream(tx, rx, 0.5)
                Thread.sleep(50)
                val st = rx.stats()
                val ts = tx.stats()
                println("redundancy: rx=$st tx=$ts")
                assertTrue(ts.redundant > 0)
                assertTrue("copies recognised as duplicates", st.duplicates > 0)
                assertEquals(0L, st.lost)
            }
        }
    }

    @Test fun floatPathAndWrongStreamName() {
        VbanReceiver("Wanted", port = 0).start().use { rx ->
            VbanSender("Other", format = VbanFormat.F32).addDestination("127.0.0.1", rx.localPort).use { tx ->
                val f = FloatArray(256 * 2) { 0.25f }
                repeat(20) { tx.write(f) }
                Thread.sleep(100)
                assertEquals("other stream names are ignored", 0L, rx.stats().packets)
            }
        }
    }

    @Test fun discoveryFindsReceiverIdentity() {
        VbanReceiver("Any", port = 0).setIdentity("JNI test device", "vban-jni-test").start().use { rx ->
            val found = VbanDiscovery.discover("127.0.0.1", rx.localPort, timeoutMs = 500)
            println("discovery: $found")
            assertEquals(1, found.size)
            assertEquals("JNI test device", found[0].deviceName)
            assertEquals("vban-jni-test", found[0].applicationName)
            assertEquals(rx.localPort, found[0].port)
        }
    }

    @Test fun textCommandLimits() {
        VbanCommand("127.0.0.1", 6981).use { cmd ->
            cmd.send("Strip[0].Mute=1;")
            try {
                cmd.send("x".repeat(1437))
                fail("expected VbanException")
            } catch (e: VbanException) {
                assertEquals(-10, e.code)  // VBAN_E_OVERSIZE
            }
        }
    }

    @Test fun useAfterCloseIsHarmless() {
        val rx = VbanReceiver("C", port = 0).start()
        val tx = VbanSender("C")
        rx.close()
        tx.close()
        rx.close()  // idempotent
        assertEquals(0, rx.read(ShortArray(256)))
        assertEquals(0, tx.write(ShortArray(256)))
    }

    @Test fun closeWhileAudioThreadReads() {
        // a reader hammering read() while another thread closes: must not crash
        repeat(20) {
            val rx = VbanReceiver("R", port = 0).start()
            val reader = Thread {
                val out = ShortArray(256)
                for (i in 0 until 2000) rx.read(out)
            }
            reader.start()
            Thread.sleep(1)
            rx.close()
            reader.join()
        }
    }
}
