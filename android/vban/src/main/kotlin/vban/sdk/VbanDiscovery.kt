package vban.sdk

/** PING0 discovery. Blocking: call off the main thread. */
object VbanDiscovery {
    /**
     * Ping [target] (broadcast by default) and collect replies for [timeoutMs].
     * On Android Wi-Fi, hold a MulticastLock (see VbanNetworkLocks) or replies to
     * broadcasts may be filtered.
     */
    fun discover(
        target: String = "255.255.255.255",
        port: Int = Vban.DEFAULT_PORT,
        timeoutMs: Int = 1000,
        maxDevices: Int = 32,
    ): List<VbanDevice> = VbanNative.discover(target, port, timeoutMs, maxDevices).toList()
}
