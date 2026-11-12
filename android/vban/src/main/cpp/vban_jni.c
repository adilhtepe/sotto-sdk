/*
 * VBAN SDK - JNI glue for the Kotlin API in vban.sdk.*.
 *
 * Handles are pointers to small native structs allocated when an object is
 * created (never on the audio path). Per-call rules:
 *  - VbanReceiver.read*(): runs on the audio thread. It pins the Java array
 *    with Get/ReleasePrimitiveArrayCritical and lets the jitter buffer write
 *    straight into it: no copy, no allocation, no lock, no syscall.
 *  - VbanSender.write*(): copies the samples into a buffer allocated at
 *    creation (GetXxxArrayRegion) before sending, because sendto() must not
 *    run inside a critical region (it would hold off the GC).
 *  - VbanReceiver.poll(): the network thread (owned by the Kotlin class).
 * Builds for Android (bionic) and for a desktop JVM (used by the unit tests).
 */
#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include "vban.h"

#define TX_SCRATCH_SAMPLES 8192  /* per write chunk: 4096 stereo frames */

/* ------------------------------------------------------------------ util */

static void throw_vban(JNIEnv *env, int code)
{
    jclass cls = (*env)->FindClass(env, "vban/sdk/VbanException");
    jmethodID ctor;
    if (!cls) return;  /* NoClassDefFoundError already pending */
    ctor = (*env)->GetMethodID(env, cls, "<init>", "(ILjava/lang/String;)V");
    if (ctor) {
        jstring msg = (*env)->NewStringUTF(env, vban_strerror(code));
        jobject ex = (*env)->NewObject(env, cls, ctor, (jint)code, msg);
        if (ex) (*env)->Throw(env, (jthrowable)ex);
    }
}

/* Copy a Java string into a fixed buffer (modified UTF-8 == ASCII for names). */
static int get_str(JNIEnv *env, jstring s, char *out, size_t cap)
{
    const char *c;
    if (!s) { out[0] = 0; return 0; }
    c = (*env)->GetStringUTFChars(env, s, NULL);
    if (!c) return -1;
    strncpy(out, c, cap - 1);
    out[cap - 1] = 0;
    (*env)->ReleaseStringUTFChars(env, s, c);
    return 0;
}

static int resolve(JNIEnv *env, jstring host, jint port, vban_addr_t *a)
{
    char h[256];
    int r;
    if (get_str(env, host, h, sizeof h) < 0) return VBAN_E_ARG;
    r = vban_addr_parse(h, (uint16_t)port, a);
    return r;
}

/* Network-supplied text -> Java string. NewStringUTF() requires valid
 * modified UTF-8 and CheckJNI aborts the app on anything else, so a hostile
 * PING0 reply could crash it. The PING0 name fields are ASCII by spec: keep
 * printable ASCII, replace the rest with '?'. */
static jstring net_string(JNIEnv *env, const char *in, size_t cap)
{
    char tmp[129];
    size_t i;
    if (cap > sizeof tmp - 1) cap = sizeof tmp - 1;
    for (i = 0; i < cap && in[i]; ++i) {
        unsigned char ch = (unsigned char)in[i];
        tmp[i] = (ch >= 0x20 && ch < 0x7F) ? (char)ch : '?';
    }
    tmp[i] = 0;
    return (*env)->NewStringUTF(env, tmp);
}

static int g_net_ready;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
{
    (void)vm; (void)reserved;
    g_net_ready = vban_net_init() == VBAN_OK;
    return JNI_VERSION_1_6;
}

JNIEXPORT jstring JNICALL Java_vban_sdk_VbanNative_version(JNIEnv *env, jclass cls)
{
    (void)cls;
    return (*env)->NewStringUTF(env, VBAN_SDK_VERSION);
}

JNIEXPORT jstring JNICALL Java_vban_sdk_VbanNative_strerror(JNIEnv *env, jclass cls, jint code)
{
    (void)cls;
    return (*env)->NewStringUTF(env, vban_strerror(code));
}

JNIEXPORT jboolean JNICALL Java_vban_sdk_VbanNative_threadRealtime(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    return vban_thread_realtime() == 0 ? JNI_TRUE : JNI_FALSE;
}

/* ================================================================ sender */

typedef struct jtx {
    vban_socket_t sock;
    vban_tx_t     tx;
    uint8_t      *red_buf;
    int16_t       s16[TX_SCRATCH_SAMPLES];
    float         f32[TX_SCRATCH_SAMPLES];
} jtx_t;

JNIEXPORT jlong JNICALL Java_vban_sdk_VbanNative_txCreate(JNIEnv *env, jclass cls, jstring name, jint rate,
                                                          jint channels, jint format, jint fpp, jboolean qos)
{
    jtx_t *t;
    vban_tx_config_t c;
    char nm[VBAN_STREAM_NAME_SIZE + 1];
    int r;
    (void)cls;
    if (!g_net_ready) { throw_vban(env, VBAN_E_NET); return 0; }
    if (channels < 1 || channels > 256 || get_str(env, name, nm, sizeof nm) < 0) { throw_vban(env, VBAN_E_ARG); return 0; }
    t = (jtx_t *)calloc(1, sizeof *t);
    if (!t) { throw_vban(env, VBAN_E_NOSPACE); return 0; }
    t->sock.fd = -1;
    memset(&c, 0, sizeof c);
    c.name = nm;
    c.sample_rate = (uint32_t)rate;
    c.channels = (uint16_t)channels;
    c.datatype = (uint8_t)format;
    c.frames_per_packet = (uint16_t)fpp;
    c.send = vban_udp_sink;
    c.send_ctx = &t->sock;
    if ((r = vban_tx_init(&t->tx, &c)) != VBAN_OK ||
        (r = vban_udp_open(&t->sock, NULL, 0, VBAN_SOCK_BROADCAST | (qos ? VBAN_SOCK_QOS : 0))) != VBAN_OK) {
        free(t);
        throw_vban(env, r);
        return 0;
    }
    return (jlong)(intptr_t)t;
}

JNIEXPORT void JNICALL Java_vban_sdk_VbanNative_txAddDest(JNIEnv *env, jclass cls, jlong h, jstring host, jint port)
{
    jtx_t *t = (jtx_t *)(intptr_t)h;
    vban_addr_t a;
    int r;
    (void)cls;
    if ((r = resolve(env, host, port, &a)) != VBAN_OK || (r = vban_tx_add_dest(&t->tx, &a)) != VBAN_OK) throw_vban(env, r);
}

JNIEXPORT void JNICALL Java_vban_sdk_VbanNative_txSetRedundancy(JNIEnv *env, jclass cls, jlong h, jint delay)
{
    jtx_t *t = (jtx_t *)(intptr_t)h;
    uint8_t *buf = NULL;
    int r;
    (void)cls;
    if (delay < 0 || delay > VBAN_TX_REDUNDANCY_MAX) { throw_vban(env, VBAN_E_ARG); return; }
    if (delay > 0 && !(buf = (uint8_t *)malloc((size_t)delay * VBAN_PACKET_MAX_SIZE))) { throw_vban(env, VBAN_E_NOSPACE); return; }
    if ((r = vban_tx_set_redundancy(&t->tx, (unsigned)delay, buf, (size_t)delay * VBAN_PACKET_MAX_SIZE)) != VBAN_OK) {
        free(buf);
        throw_vban(env, r);
        return;
    }
    free(t->red_buf);
    t->red_buf = buf;
}

JNIEXPORT jint JNICALL Java_vban_sdk_VbanNative_txWriteShorts(JNIEnv *env, jclass cls, jlong h, jshortArray a,
                                                              jint offset, jint frames)
{
    jtx_t *t = (jtx_t *)(intptr_t)h;
    jint ch = t->tx.channels, done = 0, sent = 0;
    jint chunk = TX_SCRATCH_SAMPLES / ch;
    (void)cls;
    if (offset < 0 || frames < 0 || (*env)->GetArrayLength(env, a) < offset + frames * ch) { throw_vban(env, VBAN_E_ARG); return 0; }
    while (done < frames) {
        jint n = frames - done > chunk ? chunk : frames - done, r;
        (*env)->GetShortArrayRegion(env, a, offset + done * ch, n * ch, (jshort *)t->s16);
        r = vban_tx_write_s16(&t->tx, t->s16, (size_t)n);
        if (r < 0) { throw_vban(env, r); return sent; }
        sent += r;
        done += n;
    }
    return sent;
}

JNIEXPORT jint JNICALL Java_vban_sdk_VbanNative_txWriteFloats(JNIEnv *env, jclass cls, jlong h, jfloatArray a,
                                                              jint offset, jint frames)
{
    jtx_t *t = (jtx_t *)(intptr_t)h;
    jint ch = t->tx.channels, done = 0, sent = 0;
    jint chunk = TX_SCRATCH_SAMPLES / ch;
    (void)cls;
    if (offset < 0 || frames < 0 || (*env)->GetArrayLength(env, a) < offset + frames * ch) { throw_vban(env, VBAN_E_ARG); return 0; }
    while (done < frames) {
        jint n = frames - done > chunk ? chunk : frames - done, r;
        (*env)->GetFloatArrayRegion(env, a, offset + done * ch, n * ch, t->f32);
        r = vban_tx_write_f32(&t->tx, t->f32, (size_t)n);
        if (r < 0) { throw_vban(env, r); return sent; }
        sent += r;
        done += n;
    }
    return sent;
}

JNIEXPORT jint JNICALL Java_vban_sdk_VbanNative_txFlush(JNIEnv *env, jclass cls, jlong h)
{
    (void)env; (void)cls;
    return vban_tx_flush(&((jtx_t *)(intptr_t)h)->tx);
}

/* stats: [packets, redundant, errors, bytes] */
JNIEXPORT void JNICALL Java_vban_sdk_VbanNative_txStats(JNIEnv *env, jclass cls, jlong h, jlongArray out)
{
    jtx_t *t = (jtx_t *)(intptr_t)h;
    jlong v[4];
    (void)cls;
    v[0] = t->tx.stats.packets; v[1] = t->tx.stats.redundant; v[2] = t->tx.stats.errors; v[3] = (jlong)t->tx.stats.bytes;
    (*env)->SetLongArrayRegion(env, out, 0, 4, v);
}

JNIEXPORT void JNICALL Java_vban_sdk_VbanNative_txDestroy(JNIEnv *env, jclass cls, jlong h)
{
    jtx_t *t = (jtx_t *)(intptr_t)h;
    (void)env; (void)cls;
    if (!t) return;
    vban_udp_close(&t->sock);
    free(t->red_buf);
    free(t);
}

/* ================================================================ receiver */

typedef struct jrx {
    vban_socket_t sock;
    vban_rx_t     rx;
    vban_jb_t     jb;
    vban_ping0_t  identity;
    void         *ring;
    uint8_t       bufs[VBAN_NET_BATCH][VBAN_PACKET_MAX_SIZE];
    vban_msg_t    msgs[VBAN_NET_BATCH];
} jrx_t;

JNIEXPORT jlong JNICALL Java_vban_sdk_VbanNative_rxCreate(JNIEnv *env, jclass cls, jint port, jstring name,
                                                          jstring source, jint channels, jint out_rate,
                                                          jint target_frames, jint capacity, jboolean s16_ring)
{
    jrx_t *t;
    vban_jb_config_t jc;
    vban_rx_config_t rc;
    vban_addr_t src;
    char nm[VBAN_STREAM_NAME_SIZE + 1];
    unsigned i;
    int r;
    (void)cls;
    if (!g_net_ready) { throw_vban(env, VBAN_E_NET); return 0; }
    if (channels < 1 || channels > 256 || capacity < 256 || (capacity & (capacity - 1)) ||
        get_str(env, name, nm, sizeof nm) < 0) {
        throw_vban(env, VBAN_E_ARG);
        return 0;
    }
    src.ip = 0;
    if (source && (r = resolve(env, source, 0, &src)) != VBAN_OK) { throw_vban(env, r); return 0; }
    t = (jrx_t *)calloc(1, sizeof *t);
    if (!t) { throw_vban(env, VBAN_E_NOSPACE); return 0; }
    t->sock.fd = -1;
    t->ring = calloc(1, vban_jb_storage_bytes((uint32_t)capacity, (unsigned)channels, s16_ring ? VBAN_JB_S16 : VBAN_JB_F32));
    if (!t->ring) { free(t); throw_vban(env, VBAN_E_NOSPACE); return 0; }

    memset(&jc, 0, sizeof jc);
    jc.channels = (uint16_t)channels;
    jc.out_rate = (uint32_t)out_rate;
    jc.target_frames = (uint32_t)target_frames;
    jc.adaptive = 1;
    jc.drift = VBAN_JB_DRIFT_RESAMPLE;
    jc.storage = s16_ring ? VBAN_JB_S16 : VBAN_JB_F32;
    if ((r = vban_jb_init(&t->jb, &jc, t->ring, (uint32_t)capacity)) != VBAN_OK ||
        (r = vban_udp_open(&t->sock, NULL, (uint16_t)port, VBAN_SOCK_REUSE | VBAN_SOCK_BROADCAST | VBAN_SOCK_QOS)) != VBAN_OK) {
        free(t->ring);
        free(t);
        throw_vban(env, r);
        return 0;
    }
    vban_udp_set_buffers(&t->sock, 1 << 20, 0);
    memset(&rc, 0, sizeof rc);
    rc.send = vban_udp_sink;
    rc.send_ctx = &t->sock;
    vban_rx_init(&t->rx, &rc);
    vban_rx_subscribe_audio(&t->rx, nm, src.ip, &t->jb, NULL, NULL);
    for (i = 0; i < VBAN_NET_BATCH; ++i) {
        t->msgs[i].buf = t->bufs[i];
        t->msgs[i].cap = VBAN_PACKET_MAX_SIZE;
    }
    return (jlong)(intptr_t)t;
}

/* Answer PING0 discovery as this device (call before the network thread starts). */
JNIEXPORT void JNICALL Java_vban_sdk_VbanNative_rxSetIdentity(JNIEnv *env, jclass cls, jlong h, jstring device,
                                                              jstring app, jstring host, jint pref_rate)
{
    jrx_t *t = (jrx_t *)(intptr_t)h;
    (void)cls;
    memset(&t->identity, 0, sizeof t->identity);
    t->identity.bit_type = VBAN_PING_TYPE_RECEPTOR;
    t->identity.bit_feature = VBAN_PING_FEATURE_AUDIO;
    t->identity.prefered_rate = (uint32_t)pref_rate;
    t->identity.min_rate = 6000;
    t->identity.max_rate = 705600;
    get_str(env, device, t->identity.device_name, sizeof t->identity.device_name);
    get_str(env, app, t->identity.application_name, sizeof t->identity.application_name);
    get_str(env, host, t->identity.host_name, sizeof t->identity.host_name);
    strcpy(t->identity.manufacturer_name, "VBAN SDK");
    t->rx.cfg.identity = &t->identity;
}

/* Network thread: wait up to timeout_ms, process every datagram that arrived.
 * Returns datagrams processed (0 on timeout). */
JNIEXPORT jint JNICALL Java_vban_sdk_VbanNative_rxPoll(JNIEnv *env, jclass cls, jlong h, jint timeout_ms)
{
    jrx_t *t = (jrx_t *)(intptr_t)h;
    int n, i;
    uint64_t now;
    (void)cls;
    n = vban_udp_recv_batch(&t->sock, t->msgs, VBAN_NET_BATCH, timeout_ms);
    if (n == VBAN_E_TIMEOUT) return 0;
    if (n < 0) { throw_vban(env, n); return 0; }
    now = vban_time_ns();
    for (i = 0; i < n; ++i) vban_rx_process(&t->rx, t->msgs[i].buf, t->msgs[i].len, &t->msgs[i].addr, now);
    return n;
}

/* Audio thread: always fills `frames` frames; returns frames played from the stream (0 while priming). */
JNIEXPORT jint JNICALL Java_vban_sdk_VbanNative_rxReadShorts(JNIEnv *env, jclass cls, jlong h, jshortArray a,
                                                             jint offset, jint frames)
{
    jrx_t *t = (jrx_t *)(intptr_t)h;
    jshort *p;
    size_t got;
    (void)cls;
    if (offset < 0 || frames < 0 || (*env)->GetArrayLength(env, a) < offset + frames * t->jb.channels) {
        throw_vban(env, VBAN_E_ARG);
        return 0;
    }
    p = (jshort *)(*env)->GetPrimitiveArrayCritical(env, a, NULL);
    if (!p) return 0;
    got = vban_jb_pull_s16(&t->jb, (int16_t *)p + offset, (size_t)frames);
    (*env)->ReleasePrimitiveArrayCritical(env, a, p, 0);
    return (jint)got;
}

JNIEXPORT jint JNICALL Java_vban_sdk_VbanNative_rxReadFloats(JNIEnv *env, jclass cls, jlong h, jfloatArray a,
                                                             jint offset, jint frames)
{
    jrx_t *t = (jrx_t *)(intptr_t)h;
    jfloat *p;
    size_t got;
    (void)cls;
    if (offset < 0 || frames < 0 || (*env)->GetArrayLength(env, a) < offset + frames * t->jb.channels) {
        throw_vban(env, VBAN_E_ARG);
        return 0;
    }
    p = (jfloat *)(*env)->GetPrimitiveArrayCritical(env, a, NULL);
    if (!p) return 0;
    got = vban_jb_pull_f32(&t->jb, p + offset, (size_t)frames);
    (*env)->ReleasePrimitiveArrayCritical(env, a, p, 0);
    return (jint)got;
}

/* stats: [packets, lost, repaired, late, dup, underruns, concealed_frames, fill_frames,
 *         target_frames, stream_rate, latency_us, rejected] */
JNIEXPORT void JNICALL Java_vban_sdk_VbanNative_rxStats(JNIEnv *env, jclass cls, jlong h, jlongArray out)
{
    jrx_t *t = (jrx_t *)(intptr_t)h;
    vban_jb_stats_t st;
    jlong v[12];
    (void)cls;
    vban_jb_get_stats(&t->jb, &st);
    v[0] = st.packets; v[1] = st.lost; v[2] = st.repaired; v[3] = st.late; v[4] = st.dup; v[5] = st.underruns;
    v[6] = st.concealed_frames; v[7] = st.fill_frames; v[8] = st.target_frames; v[9] = st.stream_rate;
    v[10] = vban_jb_latency_us(&t->jb); v[11] = st.rejected;
    (*env)->SetLongArrayRegion(env, out, 0, 12, v);
}

JNIEXPORT jint JNICALL Java_vban_sdk_VbanNative_rxLocalPort(JNIEnv *env, jclass cls, jlong h)
{
    (void)env; (void)cls;
    return vban_udp_local_port(&((jrx_t *)(intptr_t)h)->sock);
}

JNIEXPORT void JNICALL Java_vban_sdk_VbanNative_rxDestroy(JNIEnv *env, jclass cls, jlong h)
{
    jrx_t *t = (jrx_t *)(intptr_t)h;
    (void)env; (void)cls;
    if (!t) return;
    vban_udp_close(&t->sock);
    free(t->ring);
    free(t);
}

/* ================================================================ text commands */

typedef struct jcmd {
    vban_socket_t sock;
    vban_out_t    out;
} jcmd_t;

JNIEXPORT jlong JNICALL Java_vban_sdk_VbanNative_cmdCreate(JNIEnv *env, jclass cls, jstring host, jint port, jstring name)
{
    jcmd_t *t;
    vban_addr_t a;
    char nm[VBAN_STREAM_NAME_SIZE + 1];
    int r;
    (void)cls;
    if (!g_net_ready) { throw_vban(env, VBAN_E_NET); return 0; }
    if ((r = resolve(env, host, port, &a)) != VBAN_OK || get_str(env, name, nm, sizeof nm) < 0) {
        throw_vban(env, r != VBAN_OK ? r : VBAN_E_ARG);
        return 0;
    }
    t = (jcmd_t *)calloc(1, sizeof *t);
    if (!t) { throw_vban(env, VBAN_E_NOSPACE); return 0; }
    t->sock.fd = -1;
    if ((r = vban_udp_open(&t->sock, NULL, 0, VBAN_SOCK_BROADCAST)) != VBAN_OK) { free(t); throw_vban(env, r); return 0; }
    vban_out_init(&t->out, nm, &a, vban_udp_sink, &t->sock);
    return (jlong)(intptr_t)t;
}

JNIEXPORT void JNICALL Java_vban_sdk_VbanNative_cmdSendText(JNIEnv *env, jclass cls, jlong h, jbyteArray utf8)
{
    jcmd_t *t = (jcmd_t *)(intptr_t)h;
    jbyte buf[VBAN_DATA_MAX_SIZE];
    jsize n = (*env)->GetArrayLength(env, utf8);
    int r;
    (void)cls;
    if (n > VBAN_DATA_MAX_SIZE) { throw_vban(env, VBAN_E_OVERSIZE); return; }
    (*env)->GetByteArrayRegion(env, utf8, 0, n, buf);
    r = vban_send_text_ex(&t->out, 18, 0, VBAN_TXT_UTF8, buf, (size_t)n);
    if (r < 0) throw_vban(env, r);
}

JNIEXPORT void JNICALL Java_vban_sdk_VbanNative_cmdDestroy(JNIEnv *env, jclass cls, jlong h)
{
    jcmd_t *t = (jcmd_t *)(intptr_t)h;
    (void)env; (void)cls;
    if (!t) return;
    vban_udp_close(&t->sock);
    free(t);
}

/* ================================================================ discovery */

typedef struct disc_ctx {
    JNIEnv   *env;
    jclass    cls;
    jmethodID ctor;
    jobjectArray arr;
    jint      count, cap;
    uint32_t  id;
} disc_ctx_t;

static void on_ping_reply(void *user, const vban_packet_t *p, const vban_addr_t *from)
{
    disc_ctx_t *c = (disc_ctx_t *)user;
    JNIEnv *env = c->env;
    vban_ping0_t info;
    char ip[22], *colon;
    jobject dev;
    jstring js[5];
    int k;
    if (c->count >= c->cap || vban_service_type(p) != VBAN_SERVICE_IDENTIFICATION || !vban_service_is_reply(p) ||
        p->nu_frame != c->id)
        return;
    vban_ping0_decode(p->data, p->data_len, &info);
    vban_addr_format(from, ip);
    colon = strchr(ip, ':');
    if (colon) *colon = 0;
    js[0] = (*env)->NewStringUTF(env, ip);
    js[1] = net_string(env, info.device_name, sizeof info.device_name);
    js[2] = net_string(env, info.application_name, sizeof info.application_name);
    js[3] = net_string(env, info.host_name, sizeof info.host_name);
    js[4] = net_string(env, info.manufacturer_name, sizeof info.manufacturer_name);
    dev = (*env)->NewObject(env, c->cls, c->ctor, js[0], (jint)from->port, js[1], js[2], js[3], js[4],
                            (jint)info.bit_type, (jint)info.bit_feature, (jint)info.prefered_rate);
    if (dev) {
        (*env)->SetObjectArrayElement(env, c->arr, c->count++, dev);
        (*env)->DeleteLocalRef(env, dev);
    }
    for (k = 0; k < 5; ++k) if (js[k]) (*env)->DeleteLocalRef(env, js[k]);
}

/* Broadcast (or unicast) a PING0 and collect replies for timeout_ms. Device
 * strings are reduced to printable ASCII (see net_string). */
JNIEXPORT jobjectArray JNICALL Java_vban_sdk_VbanNative_discover(JNIEnv *env, jclass cls, jstring target, jint port,
                                                                 jint timeout_ms, jint max_devices)
{
    vban_socket_t s = VBAN_SOCKET_INIT;
    vban_rx_t rx;
    vban_rx_config_t rc;
    vban_out_t out;
    vban_addr_t to;
    vban_ping0_t me;
    disc_ctx_t c;
    uint8_t buf[VBAN_PACKET_MAX_SIZE];
    uint64_t end;
    jobjectArray result;
    int r;
    jint i;
    (void)cls;
    if (!g_net_ready) { throw_vban(env, VBAN_E_NET); return NULL; }
    if ((r = resolve(env, target, port, &to)) != VBAN_OK) { throw_vban(env, r); return NULL; }
    memset(&c, 0, sizeof c);
    c.env = env;
    c.cls = (*env)->FindClass(env, "vban/sdk/VbanDevice");
    if (!c.cls) return NULL;
    c.ctor = (*env)->GetMethodID(env, c.cls, "<init>",
                                 "(Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;III)V");
    if (!c.ctor) return NULL;
    c.cap = max_devices > 0 ? max_devices : 32;
    c.arr = (*env)->NewObjectArray(env, c.cap, c.cls, NULL);
    if (!c.arr) return NULL;
    if ((r = vban_udp_open(&s, NULL, 0, VBAN_SOCK_BROADCAST)) != VBAN_OK) { throw_vban(env, r); return NULL; }

    memset(&me, 0, sizeof me);
    me.bit_type = VBAN_PING_TYPE_RECEPTOR | VBAN_PING_TYPE_TRANSMITTER;
    me.bit_feature = VBAN_PING_FEATURE_AUDIO;
    strcpy(me.application_name, "VBAN SDK (Android)");
    memset(&rc, 0, sizeof rc);
    rc.on_service = on_ping_reply;
    rc.user = &c;
    vban_rx_init(&rx, &rc);
    vban_out_init(&out, "VBAN SPOT PING", &to, vban_udp_sink, &s);
    out.frame = (uint32_t)(vban_time_ns() & 0x7FFFFFFF);
    c.id = out.frame;
    if ((r = vban_send_ping(&out, &me)) < 0) { vban_udp_close(&s); throw_vban(env, r); return NULL; }

    end = vban_time_ns() + (uint64_t)(timeout_ms > 0 ? timeout_ms : 1000) * 1000000ull;
    for (;;) {
        vban_addr_t from;
        uint64_t now = vban_time_ns();
        int n;
        if (now >= end || c.count >= c.cap || (*env)->ExceptionCheck(env)) break;
        n = vban_udp_recv(&s, buf, sizeof buf, &from, (int)((end - now) / 1000000) + 1);
        if (n > 0) vban_rx_process(&rx, buf, (size_t)n, &from, now);
    }
    vban_udp_close(&s);
    if ((*env)->ExceptionCheck(env)) return NULL;

    result = (*env)->NewObjectArray(env, c.count, c.cls, NULL);
    if (!result) return NULL;
    for (i = 0; i < c.count; ++i) {
        jobject o = (*env)->GetObjectArrayElement(env, c.arr, i);
        (*env)->SetObjectArrayElement(env, result, i, o);
        (*env)->DeleteLocalRef(env, o);
    }
    return result;
}
