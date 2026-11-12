package vban.sdk

import android.content.Context
import android.net.wifi.WifiManager
import android.os.Build
import java.io.Closeable

/**
 * Wi-Fi settings VBAN needs on Android, held while streaming:
 *  - MulticastLock: without it many devices drop broadcast/multicast packets
 *    (PING0 discovery, broadcast streams).
 *  - WifiLock in low-latency mode (API 29+, high-perf before): stops Wi-Fi power
 *    save from batching packets into 100-300 ms bursts.
 * Needs CHANGE_WIFI_MULTICAST_STATE and WAKE_LOCK (declared by this library).
 */
class VbanNetworkLocks(context: Context, tag: String = "vban") : Closeable {
    private val wifi = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
    private val multicast = wifi.createMulticastLock(tag).apply { setReferenceCounted(false) }
    @Suppress("DEPRECATION")
    private val lowLatency = wifi.createWifiLock(
        if (Build.VERSION.SDK_INT >= 29) WifiManager.WIFI_MODE_FULL_LOW_LATENCY else WifiManager.WIFI_MODE_FULL_HIGH_PERF,
        tag,
    ).apply { setReferenceCounted(false) }

    fun acquire(): VbanNetworkLocks {
        multicast.acquire()
        lowLatency.acquire()
        return this
    }

    override fun close() {
        if (lowLatency.isHeld) lowLatency.release()
        if (multicast.isHeld) multicast.release()
    }
}
