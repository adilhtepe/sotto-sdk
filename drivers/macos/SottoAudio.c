/*
 * SottoAudio - two loopback audio devices for macOS (CoreAudio AudioServerPlugIn, user space).
 *
 *   "Sotto Out"  apps play into it; `sotto send -d "Sotto Out" -i <host>` captures it as VBAN
 *   "Sotto In"   `sotto play -d "Sotto In" -n <stream>` plays VBAN into it; apps record from it
 *
 * Each device has one output and one input stream that share a ring indexed by the device's
 * sample time: what is written to the output at sample time T is read back by the input at T.
 * The plug-in does no networking (coreaudiod is sandboxed); the sotto app does.
 *
 * Written against the AudioServerPlugIn API (CoreAudio/AudioServerPlugIn.h, macOS 10.13+), in the
 * style of Apple's NullAudio example. Build: see Makefile.
 */
#include <CoreAudio/AudioServerPlugIn.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <string.h>

#define kPlugIn          kAudioObjectPlugInObject   /* 1 */
#define kDevOut          2
#define kDevOutIn        3   /* input stream of "Sotto Out" */
#define kDevOutOut       4   /* output stream of "Sotto Out" */
#define kDevIn           5
#define kDevInIn         6
#define kDevInOut        7
#define kChannels        2
#define kRingFrames      16384u                     /* power of two; also the zero time stamp period */
#define kLatencyFrames   0
#define kSafetyFrames    0

typedef struct {
    AudioObjectID   id, in_stream, out_stream;
    const char     *name, *uid;
    Float64         rate;
    UInt64          io_running;                     /* number of clients doing IO */
    UInt64          anchor_host;                    /* host time of sample 0 of the current period */
    UInt64          num_periods;
    Float64         ticks_per_frame;
    Boolean         in_active, out_active;
    float           ring[kRingFrames * kChannels];
} device_t;

static device_t g_dev[2] = {
    { kDevOut, kDevOutIn, kDevOutOut, "Sotto Out", "SottoAudio_Out", 48000.0, 0, 0, 0, 0, false, false, { 0 } },
    { kDevIn,  kDevInIn,  kDevInOut,  "Sotto In",  "SottoAudio_In",  48000.0, 0, 0, 0, 0, false, false, { 0 } },
};
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static AudioServerPlugInHostRef g_host;
static Float64 g_host_ticks_per_second;
static UInt32 g_ref_count;

static device_t *dev_of(AudioObjectID id)
{
    int i;
    for (i = 0; i < 2; ++i)
        if (g_dev[i].id == id || g_dev[i].in_stream == id || g_dev[i].out_stream == id)
            return &g_dev[i];
    return NULL;
}

static int is_device(AudioObjectID id) { return id == kDevOut || id == kDevIn; }
static int is_stream(AudioObjectID id) { return id == kDevOutIn || id == kDevOutOut || id == kDevInIn || id == kDevInOut; }

static AudioStreamBasicDescription format_at(Float64 rate)
{
    AudioStreamBasicDescription f;
    memset(&f, 0, sizeof f);
    f.mSampleRate = rate;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
    f.mBytesPerPacket = 4 * kChannels;
    f.mFramesPerPacket = 1;
    f.mBytesPerFrame = 4 * kChannels;
    f.mChannelsPerFrame = kChannels;
    f.mBitsPerChannel = 32;
    return f;
}

static const Float64 kRates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
#define kNumRates (sizeof kRates / sizeof kRates[0])

/* ------------------------------------------------------------------ COM boilerplate */

static HRESULT QueryInterface(void *inDriver, REFIID inUUID, LPVOID *outInterface);
static ULONG AddRef(void *inDriver);
static ULONG Release(void *inDriver);
static OSStatus Initialize(AudioServerPlugInDriverRef d, AudioServerPlugInHostRef host);
static OSStatus CreateDevice(AudioServerPlugInDriverRef d, CFDictionaryRef desc, const AudioServerPlugInClientInfo *ci, AudioObjectID *out);
static OSStatus DestroyDevice(AudioServerPlugInDriverRef d, AudioObjectID id);
static OSStatus AddDeviceClient(AudioServerPlugInDriverRef d, AudioObjectID id, const AudioServerPlugInClientInfo *ci);
static OSStatus RemoveDeviceClient(AudioServerPlugInDriverRef d, AudioObjectID id, const AudioServerPlugInClientInfo *ci);
static OSStatus PerformDeviceConfigurationChange(AudioServerPlugInDriverRef d, AudioObjectID id, UInt64 action, void *info);
static OSStatus AbortDeviceConfigurationChange(AudioServerPlugInDriverRef d, AudioObjectID id, UInt64 action, void *info);
static Boolean HasProperty(AudioServerPlugInDriverRef d, AudioObjectID id, pid_t pid, const AudioObjectPropertyAddress *a);
static OSStatus IsPropertySettable(AudioServerPlugInDriverRef d, AudioObjectID id, pid_t pid, const AudioObjectPropertyAddress *a, Boolean *out);
static OSStatus GetPropertyDataSize(AudioServerPlugInDriverRef d, AudioObjectID id, pid_t pid, const AudioObjectPropertyAddress *a, UInt32 qs, const void *q, UInt32 *out);
static OSStatus GetPropertyData(AudioServerPlugInDriverRef d, AudioObjectID id, pid_t pid, const AudioObjectPropertyAddress *a, UInt32 qs, const void *q, UInt32 inSize, UInt32 *outSize, void *out);
static OSStatus SetPropertyData(AudioServerPlugInDriverRef d, AudioObjectID id, pid_t pid, const AudioObjectPropertyAddress *a, UInt32 qs, const void *q, UInt32 inSize, const void *in);
static OSStatus StartIO(AudioServerPlugInDriverRef d, AudioObjectID id, UInt32 client);
static OSStatus StopIO(AudioServerPlugInDriverRef d, AudioObjectID id, UInt32 client);
static OSStatus GetZeroTimeStamp(AudioServerPlugInDriverRef d, AudioObjectID id, UInt32 client, Float64 *st, UInt64 *ht, UInt64 *seed);
static OSStatus WillDoIOOperation(AudioServerPlugInDriverRef d, AudioObjectID id, UInt32 client, UInt32 op, Boolean *will, Boolean *inPlace);
static OSStatus BeginIOOperation(AudioServerPlugInDriverRef d, AudioObjectID id, UInt32 client, UInt32 op, UInt32 frames, const AudioServerPlugInIOCycleInfo *ci);
static OSStatus DoIOOperation(AudioServerPlugInDriverRef d, AudioObjectID id, AudioObjectID stream, UInt32 client, UInt32 op, UInt32 frames, const AudioServerPlugInIOCycleInfo *ci, void *main, void *secondary);
static OSStatus EndIOOperation(AudioServerPlugInDriverRef d, AudioObjectID id, UInt32 client, UInt32 op, UInt32 frames, const AudioServerPlugInIOCycleInfo *ci);

static AudioServerPlugInDriverInterface g_interface = {
    NULL, QueryInterface, AddRef, Release, Initialize, CreateDevice, DestroyDevice, AddDeviceClient, RemoveDeviceClient,
    PerformDeviceConfigurationChange, AbortDeviceConfigurationChange, HasProperty, IsPropertySettable,
    GetPropertyDataSize, GetPropertyData, SetPropertyData, StartIO, StopIO, GetZeroTimeStamp, WillDoIOOperation,
    BeginIOOperation, DoIOOperation, EndIOOperation
};
static AudioServerPlugInDriverInterface *g_interface_ptr = &g_interface;
static AudioServerPlugInDriverRef g_driver = &g_interface_ptr;

/* Factory named in Info.plist (CFPlugInFactories). */
void *SottoAudio_Create(CFAllocatorRef allocator, CFUUIDRef requestedType);
void *SottoAudio_Create(CFAllocatorRef allocator, CFUUIDRef requestedType)
{
    (void)allocator;
    return CFEqual(requestedType, kAudioServerPlugInTypeUUID) ? g_driver : NULL;
}

static HRESULT QueryInterface(void *inDriver, REFIID inUUID, LPVOID *outInterface)
{
    CFUUIDRef req = CFUUIDCreateFromUUIDBytes(NULL, inUUID);
    HRESULT r = E_NOINTERFACE;
    if (inDriver != g_driver || !outInterface) {
        if (req) CFRelease(req);
        return kAudioHardwareBadObjectError;
    }
    if (req && (CFEqual(req, IUnknownUUID) || CFEqual(req, kAudioServerPlugInDriverInterfaceUUID))) {
        pthread_mutex_lock(&g_lock);
        ++g_ref_count;
        pthread_mutex_unlock(&g_lock);
        *outInterface = g_driver;
        r = S_OK;
    }
    if (req) CFRelease(req);
    return r;
}

static ULONG AddRef(void *inDriver)
{
    ULONG r;
    if (inDriver != g_driver) return 0;
    pthread_mutex_lock(&g_lock);
    r = ++g_ref_count;
    pthread_mutex_unlock(&g_lock);
    return r;
}

static ULONG Release(void *inDriver)
{
    ULONG r;
    if (inDriver != g_driver) return 0;
    pthread_mutex_lock(&g_lock);
    if (g_ref_count) --g_ref_count;
    r = g_ref_count;
    pthread_mutex_unlock(&g_lock);
    return r;
}

/* ------------------------------------------------------------------ life cycle */

static void set_clock(device_t *d)
{
    d->ticks_per_frame = g_host_ticks_per_second / d->rate;
}

static OSStatus Initialize(AudioServerPlugInDriverRef drv, AudioServerPlugInHostRef host)
{
    struct mach_timebase_info tb;
    int i;
    if (drv != g_driver) return kAudioHardwareBadObjectError;
    g_host = host;
    mach_timebase_info(&tb);
    g_host_ticks_per_second = 1.0e9 * (Float64)tb.denom / (Float64)tb.numer;
    for (i = 0; i < 2; ++i) {
        g_dev[i].in_active = g_dev[i].out_active = true;
        set_clock(&g_dev[i]);
    }
    return kAudioHardwareNoError;
}

static OSStatus CreateDevice(AudioServerPlugInDriverRef d, CFDictionaryRef desc, const AudioServerPlugInClientInfo *ci, AudioObjectID *out)
{
    (void)d; (void)desc; (void)ci; (void)out;
    return kAudioHardwareUnsupportedOperationError;
}

static OSStatus DestroyDevice(AudioServerPlugInDriverRef d, AudioObjectID id)
{
    (void)d; (void)id;
    return kAudioHardwareUnsupportedOperationError;
}

static OSStatus AddDeviceClient(AudioServerPlugInDriverRef d, AudioObjectID id, const AudioServerPlugInClientInfo *ci)
{
    (void)ci;
    if (d != g_driver) return kAudioHardwareBadObjectError;
    return is_device(id) ? kAudioHardwareNoError : kAudioHardwareBadObjectError;
}

static OSStatus RemoveDeviceClient(AudioServerPlugInDriverRef d, AudioObjectID id, const AudioServerPlugInClientInfo *ci)
{
    (void)ci;
    if (d != g_driver) return kAudioHardwareBadObjectError;
    return is_device(id) ? kAudioHardwareNoError : kAudioHardwareBadObjectError;
}

/* A sample-rate change requested through SetPropertyData lands here, outside any IO. */
static OSStatus PerformDeviceConfigurationChange(AudioServerPlugInDriverRef d, AudioObjectID id, UInt64 action, void *info)
{
    device_t *dev = dev_of(id);
    (void)info;
    if (d != g_driver || !dev || !is_device(id)) return kAudioHardwareBadObjectError;
    if (action >= kNumRates) return kAudioHardwareBadPropertySizeError;
    pthread_mutex_lock(&g_lock);
    dev->rate = kRates[action];
    set_clock(dev);
    memset(dev->ring, 0, sizeof dev->ring);
    pthread_mutex_unlock(&g_lock);
    return kAudioHardwareNoError;
}

static OSStatus AbortDeviceConfigurationChange(AudioServerPlugInDriverRef d, AudioObjectID id, UInt64 action, void *info)
{
    (void)d; (void)id; (void)action; (void)info;
    return kAudioHardwareNoError;
}

/* ------------------------------------------------------------------ properties */

static Boolean HasProperty(AudioServerPlugInDriverRef d, AudioObjectID id, pid_t pid, const AudioObjectPropertyAddress *a)
{
    UInt32 size;
    (void)pid;
    if (d != g_driver || !a) return false;
    return GetPropertyDataSize(d, id, pid, a, 0, NULL, &size) == kAudioHardwareNoError;
}

static OSStatus IsPropertySettable(AudioServerPlugInDriverRef d, AudioObjectID id, pid_t pid, const AudioObjectPropertyAddress *a, Boolean *out)
{
    UInt32 size;
    if (d != g_driver || !a || !out) return kAudioHardwareBadObjectError;
    if (GetPropertyDataSize(d, id, pid, a, 0, NULL, &size) != kAudioHardwareNoError) return kAudioHardwareUnknownPropertyError;
    *out = (is_device(id) && a->mSelector == kAudioDevicePropertyNominalSampleRate) ||
           (is_stream(id) && (a->mSelector == kAudioStreamPropertyVirtualFormat || a->mSelector == kAudioStreamPropertyPhysicalFormat));
    return kAudioHardwareNoError;
}

/* One implementation answers both size and data: out == NULL means "size only". */
static OSStatus property(AudioObjectID id, const AudioObjectPropertyAddress *a, UInt32 qs, const void *q, UInt32 inSize,
                         UInt32 *outSize, void *out)
{
    device_t *dev = dev_of(id);
#define ANSWER(T, value) do { if (out) { if (inSize < sizeof(T)) return kAudioHardwareBadPropertySizeError; *(T *)out = (value); } \
                              *outSize = sizeof(T); return kAudioHardwareNoError; } while (0)
#define ANSWER_CFSTR(s) do { if (out) { if (inSize < sizeof(CFStringRef)) return kAudioHardwareBadPropertySizeError; \
                                 *(CFStringRef *)out = CFStringCreateWithCString(NULL, (s), kCFStringEncodingUTF8); } \
                             *outSize = sizeof(CFStringRef); return kAudioHardwareNoError; } while (0)

    if (id == kPlugIn) {
        switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass: ANSWER(AudioClassID, kAudioObjectClassID);
        case kAudioObjectPropertyClass: ANSWER(AudioClassID, kAudioPlugInClassID);
        case kAudioObjectPropertyOwner: ANSWER(AudioObjectID, kAudioObjectUnknown);
        case kAudioObjectPropertyManufacturer: ANSWER_CFSTR("Sotto");
        case kAudioObjectPropertyOwnedObjects:
        case kAudioPlugInPropertyDeviceList: {
            UInt32 n = 2, i;
            if (out) {
                n = inSize / sizeof(AudioObjectID) < 2 ? inSize / sizeof(AudioObjectID) : 2;
                for (i = 0; i < n; ++i) ((AudioObjectID *)out)[i] = g_dev[i].id;
            }
            *outSize = n * sizeof(AudioObjectID);
            return kAudioHardwareNoError;
        }
        case kAudioPlugInPropertyTranslateUIDToDevice: {
            AudioObjectID found = kAudioObjectUnknown;
            int i;
            if (out && q && qs == sizeof(CFStringRef)) {
                CFStringRef uid = *(const CFStringRef *)q;
                for (i = 0; i < 2; ++i) {
                    CFStringRef mine = CFStringCreateWithCString(NULL, g_dev[i].uid, kCFStringEncodingUTF8);
                    if (mine && uid && CFStringCompare(uid, mine, 0) == kCFCompareEqualTo) found = g_dev[i].id;
                    if (mine) CFRelease(mine);
                }
            }
            ANSWER(AudioObjectID, found);
        }
        case kAudioPlugInPropertyResourceBundle: ANSWER_CFSTR("");
        default: return kAudioHardwareUnknownPropertyError;
        }
    }

    if (dev && is_device(id)) {
        switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass: ANSWER(AudioClassID, kAudioObjectClassID);
        case kAudioObjectPropertyClass: ANSWER(AudioClassID, kAudioDeviceClassID);
        case kAudioObjectPropertyOwner: ANSWER(AudioObjectID, kPlugIn);
        case kAudioObjectPropertyName: ANSWER_CFSTR(dev->name);
        case kAudioObjectPropertyManufacturer: ANSWER_CFSTR("Sotto");
        case kAudioDevicePropertyDeviceUID: ANSWER_CFSTR(dev->uid);
        case kAudioDevicePropertyModelUID: ANSWER_CFSTR("SottoAudio_Model");
        case kAudioDevicePropertyTransportType: ANSWER(UInt32, kAudioDeviceTransportTypeVirtual);
        case kAudioDevicePropertyRelatedDevices: ANSWER(AudioObjectID, dev->id);
        case kAudioDevicePropertyClockDomain: ANSWER(UInt32, 0);
        case kAudioDevicePropertyDeviceIsAlive: ANSWER(UInt32, 1);
        case kAudioDevicePropertyDeviceIsRunning: ANSWER(UInt32, dev->io_running ? 1 : 0);
        case kAudioDevicePropertyDeviceCanBeDefaultDevice: ANSWER(UInt32, 1);
        case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice: ANSWER(UInt32, 0);
        case kAudioDevicePropertyLatency: ANSWER(UInt32, kLatencyFrames);
        case kAudioDevicePropertySafetyOffset: ANSWER(UInt32, kSafetyFrames);
        case kAudioDevicePropertyNominalSampleRate: ANSWER(Float64, dev->rate);
        case kAudioDevicePropertyIsHidden: ANSWER(UInt32, 0);
        case kAudioDevicePropertyZeroTimeStampPeriod: ANSWER(UInt32, kRingFrames);
        case kAudioObjectPropertyControlList: *outSize = 0; return kAudioHardwareNoError;
        case kAudioObjectPropertyOwnedObjects:
        case kAudioDevicePropertyStreams: {
            AudioObjectID list[2];
            UInt32 n = 0, i;
            if (a->mScope != kAudioObjectPropertyScopeOutput) list[n++] = dev->in_stream;
            if (a->mScope != kAudioObjectPropertyScopeInput) list[n++] = dev->out_stream;
            if (out) {
                if (inSize / sizeof(AudioObjectID) < n) n = inSize / sizeof(AudioObjectID);
                for (i = 0; i < n; ++i) ((AudioObjectID *)out)[i] = list[i];
            }
            *outSize = n * sizeof(AudioObjectID);
            return kAudioHardwareNoError;
        }
        case kAudioDevicePropertyAvailableNominalSampleRates: {
            UInt32 n = kNumRates, i;
            if (out) {
                if (inSize / sizeof(AudioValueRange) < n) n = inSize / sizeof(AudioValueRange);
                for (i = 0; i < n; ++i) {
                    ((AudioValueRange *)out)[i].mMinimum = kRates[i];
                    ((AudioValueRange *)out)[i].mMaximum = kRates[i];
                }
            }
            *outSize = n * sizeof(AudioValueRange);
            return kAudioHardwareNoError;
        }
        case kAudioDevicePropertyPreferredChannelsForStereo: {
            if (out) {
                if (inSize < 2 * sizeof(UInt32)) return kAudioHardwareBadPropertySizeError;
                ((UInt32 *)out)[0] = 1;
                ((UInt32 *)out)[1] = 2;
            }
            *outSize = 2 * sizeof(UInt32);
            return kAudioHardwareNoError;
        }
        default: return kAudioHardwareUnknownPropertyError;
        }
    }

    if (dev && is_stream(id)) {
        Boolean input = id == dev->in_stream;
        switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass: ANSWER(AudioClassID, kAudioObjectClassID);
        case kAudioObjectPropertyClass: ANSWER(AudioClassID, kAudioStreamClassID);
        case kAudioObjectPropertyOwner: ANSWER(AudioObjectID, dev->id);
        case kAudioObjectPropertyOwnedObjects: *outSize = 0; return kAudioHardwareNoError;
        case kAudioStreamPropertyIsActive: ANSWER(UInt32, input ? dev->in_active : dev->out_active);
        case kAudioStreamPropertyDirection: ANSWER(UInt32, input ? 1 : 0);
        case kAudioStreamPropertyTerminalType: ANSWER(UInt32, input ? kAudioStreamTerminalTypeLine : kAudioStreamTerminalTypeLine);
        case kAudioStreamPropertyStartingChannel: ANSWER(UInt32, 1);
        case kAudioStreamPropertyLatency: ANSWER(UInt32, kLatencyFrames);
        case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat: ANSWER(AudioStreamBasicDescription, format_at(dev->rate));
        case kAudioStreamPropertyAvailableVirtualFormats:
        case kAudioStreamPropertyAvailablePhysicalFormats: {
            UInt32 n = kNumRates, i;
            if (out) {
                if (inSize / sizeof(AudioStreamRangedDescription) < n) n = inSize / sizeof(AudioStreamRangedDescription);
                for (i = 0; i < n; ++i) {
                    AudioStreamRangedDescription *r = &((AudioStreamRangedDescription *)out)[i];
                    r->mFormat = format_at(kRates[i]);
                    r->mSampleRateRange.mMinimum = kRates[i];
                    r->mSampleRateRange.mMaximum = kRates[i];
                }
            }
            *outSize = n * sizeof(AudioStreamRangedDescription);
            return kAudioHardwareNoError;
        }
        default: return kAudioHardwareUnknownPropertyError;
        }
    }
    return kAudioHardwareBadObjectError;
#undef ANSWER
#undef ANSWER_CFSTR
}

static OSStatus GetPropertyDataSize(AudioServerPlugInDriverRef d, AudioObjectID id, pid_t pid, const AudioObjectPropertyAddress *a, UInt32 qs, const void *q, UInt32 *out)
{
    (void)pid;
    if (d != g_driver || !a || !out) return kAudioHardwareBadObjectError;
    return property(id, a, qs, q, 0, out, NULL);
}

static OSStatus GetPropertyData(AudioServerPlugInDriverRef d, AudioObjectID id, pid_t pid, const AudioObjectPropertyAddress *a, UInt32 qs, const void *q, UInt32 inSize, UInt32 *outSize, void *out)
{
    (void)pid;
    if (d != g_driver || !a || !outSize || !out) return kAudioHardwareBadObjectError;
    return property(id, a, qs, q, inSize, outSize, out);
}

static UInt64 rate_index(Float64 rate)
{
    UInt64 i;
    for (i = 0; i < kNumRates; ++i)
        if (kRates[i] == rate) return i;
    return kNumRates;
}

static OSStatus SetPropertyData(AudioServerPlugInDriverRef d, AudioObjectID id, pid_t pid, const AudioObjectPropertyAddress *a, UInt32 qs, const void *q, UInt32 inSize, const void *in)
{
    device_t *dev = dev_of(id);
    Float64 rate;
    UInt64 idx;
    (void)pid; (void)qs; (void)q;
    if (d != g_driver || !a || !in || !dev) return kAudioHardwareBadObjectError;
    if (is_device(id) && a->mSelector == kAudioDevicePropertyNominalSampleRate) {
        if (inSize < sizeof(Float64)) return kAudioHardwareBadPropertySizeError;
        rate = *(const Float64 *)in;
    } else if (is_stream(id) && (a->mSelector == kAudioStreamPropertyVirtualFormat || a->mSelector == kAudioStreamPropertyPhysicalFormat)) {
        const AudioStreamBasicDescription *f = (const AudioStreamBasicDescription *)in;
        AudioStreamBasicDescription want;
        if (inSize < sizeof(AudioStreamBasicDescription)) return kAudioHardwareBadPropertySizeError;
        want = format_at(f->mSampleRate);
        if (f->mFormatID != want.mFormatID || f->mChannelsPerFrame != kChannels || f->mBitsPerChannel != 32 ||
            f->mFormatFlags != want.mFormatFlags)
            return kAudioDeviceUnsupportedFormatError;
        rate = f->mSampleRate;
    } else {
        return kAudioHardwareUnknownPropertyError;
    }
    idx = rate_index(rate);
    if (idx == kNumRates) return kAudioDeviceUnsupportedFormatError;
    if (rate != dev->rate && g_host)
        g_host->RequestDeviceConfigurationChange(g_host, dev->id, idx, NULL);   /* applied in PerformDeviceConfigurationChange */
    return kAudioHardwareNoError;
}

/* ------------------------------------------------------------------ IO */

static OSStatus StartIO(AudioServerPlugInDriverRef d, AudioObjectID id, UInt32 client)
{
    device_t *dev = dev_of(id);
    (void)client;
    if (d != g_driver || !dev || !is_device(id)) return kAudioHardwareBadObjectError;
    pthread_mutex_lock(&g_lock);
    if (dev->io_running++ == 0) {
        dev->num_periods = 0;
        dev->anchor_host = mach_absolute_time();
        memset(dev->ring, 0, sizeof dev->ring);
    }
    pthread_mutex_unlock(&g_lock);
    return kAudioHardwareNoError;
}

static OSStatus StopIO(AudioServerPlugInDriverRef d, AudioObjectID id, UInt32 client)
{
    device_t *dev = dev_of(id);
    (void)client;
    if (d != g_driver || !dev || !is_device(id)) return kAudioHardwareBadObjectError;
    pthread_mutex_lock(&g_lock);
    if (dev->io_running) dev->io_running--;
    pthread_mutex_unlock(&g_lock);
    return kAudioHardwareNoError;
}

/* The device clock: one zero time stamp per kRingFrames frames, from the host clock. */
static OSStatus GetZeroTimeStamp(AudioServerPlugInDriverRef d, AudioObjectID id, UInt32 client, Float64 *st, UInt64 *ht, UInt64 *seed)
{
    device_t *dev = dev_of(id);
    UInt64 now, next;
    Float64 ticks_per_period;
    (void)client;
    if (d != g_driver || !dev) return kAudioHardwareBadObjectError;
    pthread_mutex_lock(&g_lock);
    ticks_per_period = dev->ticks_per_frame * (Float64)kRingFrames;
    now = mach_absolute_time();
    next = dev->anchor_host + (UInt64)((Float64)(dev->num_periods + 1) * ticks_per_period);
    if (next <= now) dev->num_periods++;
    *st = (Float64)(dev->num_periods * kRingFrames);
    *ht = dev->anchor_host + (UInt64)((Float64)dev->num_periods * ticks_per_period);
    *seed = 1;
    pthread_mutex_unlock(&g_lock);
    return kAudioHardwareNoError;
}

static OSStatus WillDoIOOperation(AudioServerPlugInDriverRef d, AudioObjectID id, UInt32 client, UInt32 op, Boolean *will, Boolean *inPlace)
{
    (void)client;
    if (d != g_driver || !is_device(id)) return kAudioHardwareBadObjectError;
    *will = op == kAudioServerPlugInIOOperationReadInput || op == kAudioServerPlugInIOOperationWriteMix;
    *inPlace = true;
    return kAudioHardwareNoError;
}

static OSStatus BeginIOOperation(AudioServerPlugInDriverRef d, AudioObjectID id, UInt32 client, UInt32 op, UInt32 frames, const AudioServerPlugInIOCycleInfo *ci)
{
    (void)client; (void)op; (void)frames; (void)ci;
    return d == g_driver && is_device(id) ? kAudioHardwareNoError : kAudioHardwareBadObjectError;
}

/* Loopback: the mixed output at sample time T lands in the ring at T; the input reads T and
 * clears it, so a device with no player delivers silence instead of an old buffer. */
static OSStatus DoIOOperation(AudioServerPlugInDriverRef d, AudioObjectID id, AudioObjectID stream, UInt32 client, UInt32 op, UInt32 frames, const AudioServerPlugInIOCycleInfo *ci, void *main, void *secondary)
{
    device_t *dev = dev_of(id);
    float *buf = (float *)main;
    UInt64 t, i;
    (void)stream; (void)client; (void)secondary;
    if (d != g_driver || !dev || !buf) return kAudioHardwareBadObjectError;
    if (op == kAudioServerPlugInIOOperationWriteMix) {
        t = (UInt64)ci->mOutputTime.mSampleTime;
        for (i = 0; i < frames; ++i) {
            float *slot = &dev->ring[((t + i) & (kRingFrames - 1)) * kChannels];
            slot[0] = buf[i * kChannels];
            slot[1] = buf[i * kChannels + 1];
        }
    } else if (op == kAudioServerPlugInIOOperationReadInput) {
        t = (UInt64)ci->mInputTime.mSampleTime;
        for (i = 0; i < frames; ++i) {
            float *slot = &dev->ring[((t + i) & (kRingFrames - 1)) * kChannels];
            buf[i * kChannels] = slot[0];
            buf[i * kChannels + 1] = slot[1];
            slot[0] = slot[1] = 0.0f;
        }
    }
    return kAudioHardwareNoError;
}

static OSStatus EndIOOperation(AudioServerPlugInDriverRef d, AudioObjectID id, UInt32 client, UInt32 op, UInt32 frames, const AudioServerPlugInIOCycleInfo *ci)
{
    (void)client; (void)op; (void)frames; (void)ci;
    return d == g_driver && is_device(id) ? kAudioHardwareNoError : kAudioHardwareBadObjectError;
}
