// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * snd-vban: ALSA virtual sound card that speaks VB-Audio VBAN.
 *
 *   playback  -> sent as a VBAN audio stream to `dest`
 *   capture   <- one VBAN audio stream received on `listen_port`
 *
 * Any Linux application (ALSA, PipeWire, JACK) can then send or receive VBAN
 * with no userspace daemon. Timing comes from a SCHED_FIFO kernel thread that
 * wakes every packet period (64 frames @ 48 kHz = 1.33 ms) on an hrtimer.
 *
 * Wire handling reuses the SDK's protocol module (src/vban_proto.c: header
 * builder, packet validation, frame-counter sequencing hardened against
 * spoofed counters) compiled into the module unchanged. Everything here is
 * integer-only: no FPU use in kernel context.
 *
 * Receive path: packets are placed in a ring by frame counter (a late or
 * reordered packet still fills its slot), missing audio is concealed at
 * playout time (decay + fade-in), starvation holds playout, and sender clock
 * drift is followed by a Q16 linear-interpolation resampler (vban_kring.h).
 *
 * Locking:
 *   pcm_lock (irqsave)  running flags / pointers, shared with trigger()
 *   rx_lock             receive ring, shared by the rx and clock threads
 *   work_mutex          held by the clock thread while it touches a
 *                       substream; sync_stop() takes it so hw_free/close
 *                       never race an in-flight copy or period_elapsed()
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/kthread.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/socket.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/math64.h>
#include <linux/unaligned.h>
#include <net/sock.h>
#include <net/ip.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/info.h>
#include <sound/initval.h>

#include "vban/vban_proto.h"
#include "vban_kring.h"

/* ------------------------------------------------------------- parameters */

static char *dest = "127.0.0.1:6980";
module_param(dest, charp, 0444);
MODULE_PARM_DESC(dest, "Playback destination ip:port (\"\" disables sending)");
static char *tx_name = "Linux";
module_param(tx_name, charp, 0444);
MODULE_PARM_DESC(tx_name, "Stream name sent by playback");
static int listen_port = 6980;
module_param(listen_port, int, 0444);
MODULE_PARM_DESC(listen_port, "UDP port for the capture stream (0 disables receiving)");
static char *rx_name = "Stream1";
module_param(rx_name, charp, 0444);
MODULE_PARM_DESC(rx_name, "Stream name accepted by capture");
static char *rx_source = "";
module_param(rx_source, charp, 0444);
MODULE_PARM_DESC(rx_source, "Only accept the capture stream from this IPv4 (\"\" = any)");
static int rate = 48000;
module_param(rate, int, 0444);
MODULE_PARM_DESC(rate, "Sample rate (a VBAN rate)");
static int channels = 2;
module_param(channels, int, 0444);
MODULE_PARM_DESC(channels, "Channels 1..8");
static int fpp = 64;
module_param(fpp, int, 0444);
MODULE_PARM_DESC(fpp, "Frames per packet 16..256 (smaller = lower latency)");
static int rx_target = 256;
module_param(rx_target, int, 0444);
MODULE_PARM_DESC(rx_target, "Capture buffering target in frames");

/* ------------------------------------------------------------- state */

#define MAX_CH       KRING_MAX_CH

struct vban_stream {
	struct snd_pcm_substream *ss;
	bool running;
	u64 start_ns;
	u64 frames;                      /* frames moved since start */
	snd_pcm_uframes_t ptr;           /* hw pointer reported to ALSA */
};

struct vban_dev {
	struct platform_device *pdev;
	struct snd_card *card;
	struct snd_pcm *pcm;
	spinlock_t pcm_lock;
	struct mutex work_mutex;
	struct vban_stream pb, cp;

	/* transmit */
	struct socket *tx_sock;
	struct sockaddr_in tx_addr;
	vban_name_t tx_vname;
	u32 tx_frame;
	u8 tx_pkt[8][VBAN_PACKET_MAX_SIZE];
	u64 tx_packets, tx_errors;

	/* receive */
	struct socket *rx_sock;
	vban_name_t rx_vname;
	u32 rx_src_ip;                   /* network order, 0 = any */
	u8 rx_buf[2048];
	spinlock_t rx_lock;
	struct vban_kring kr;            /* receive ring (vban_kring.h), under rx_lock */
	u64 rx_badfmt;

	struct task_struct *clock_task, *rx_task;
	u64 tick_ns;
};

static struct vban_dev *g_dev;

/* ------------------------------------------------------------- receive ring */

/* Produce n capture frames into o (interleaved S16_LE) from the ring. */
static void rx_pull(struct vban_dev *d, __le16 *o, u32 n)
{
	spin_lock(&d->rx_lock);
	kring_pull(&d->kr, o, n);
	spin_unlock(&d->rx_lock);
}

static int rx_thread(void *arg)
{
	struct vban_dev *d = arg;

	sched_set_fifo(current);
	while (!kthread_should_stop()) {
		struct sockaddr_in from;
		struct kvec iov = { .iov_base = d->rx_buf, .iov_len = sizeof(d->rx_buf) };
		struct msghdr msg = { .msg_name = &from, .msg_namelen = sizeof(from) };
		vban_packet_t p;
		int n = kernel_recvmsg(d->rx_sock, &msg, &iov, 1, sizeof(d->rx_buf), 0);

		if (n <= 0 || (msg.msg_flags & MSG_TRUNC))
			continue;                /* timeout (100 ms) / error / oversized */
		if (vban_parse(d->rx_buf, (size_t)n, &p, 0) != VBAN_OK || p.protocol != VBAN_PROTOCOL_AUDIO)
			continue;
		if (!vban_name_eq(&d->rx_vname, p.name))
			continue;
		if (d->rx_src_ip && from.sin_addr.s_addr != d->rx_src_ip)
			continue;
		if (p.datatype != VBAN_DATATYPE_S16 || p.channels != channels || p.sample_rate != (u32)rate) {
			d->rx_badfmt++;
			continue;
		}
		spin_lock(&d->rx_lock);
		kring_place(&d->kr, &p);
		spin_unlock(&d->rx_lock);
	}
	return 0;
}

/* ------------------------------------------------------------- clock thread */

static void do_playback(struct vban_dev *d, u64 now)
{
	struct snd_pcm_substream *ss;
	struct snd_pcm_runtime *rt;
	unsigned long flags;
	int npk = 0, k;
	bool elapsed = false;
	size_t plen = VBAN_HEADER_SIZE + (size_t)fpp * channels * 2;

	spin_lock_irqsave(&d->pcm_lock, flags);
	ss = d->pb.ss;
	if (!d->pb.running || !ss) {
		spin_unlock_irqrestore(&d->pcm_lock, flags);
		return;
	}
	rt = ss->runtime;
	while (npk < 8) {
		u64 due = div_u64((now - d->pb.start_ns) * (u64)rate, NSEC_PER_SEC);
		u32 i, pos;
		u8 *pkt = d->tx_pkt[npk];
		const u8 *dma = rt->dma_area;
		size_t fb = (size_t)channels * 2;

		if (due < d->pb.frames + fpp)
			break;
		vban_build_audio_header(pkt, &d->tx_vname, (u32)rate, (unsigned)fpp, (unsigned)channels,
					VBAN_DATATYPE_S16, d->tx_frame++);
		pos = (u32)(d->pb.frames % rt->buffer_size);
		for (i = 0; i < (u32)fpp; ++i, pos = (pos + 1 == rt->buffer_size) ? 0 : pos + 1)
			memcpy(pkt + VBAN_HEADER_SIZE + i * fb, dma + (size_t)pos * fb, fb); /* S16_LE == wire */
		if (div_u64(d->pb.frames, rt->period_size) != div_u64(d->pb.frames + fpp, rt->period_size))
			elapsed = true;
		d->pb.frames += fpp;
		d->pb.ptr = (snd_pcm_uframes_t)(d->pb.frames % rt->buffer_size);
		++npk;
	}
	spin_unlock_irqrestore(&d->pcm_lock, flags);

	for (k = 0; k < npk; ++k) {
		struct kvec iov = { .iov_base = d->tx_pkt[k], .iov_len = plen };
		struct msghdr msg = { .msg_name = &d->tx_addr, .msg_namelen = sizeof(d->tx_addr),
				      .msg_flags = MSG_DONTWAIT };

		if (d->tx_sock && kernel_sendmsg(d->tx_sock, &msg, &iov, 1, plen) == (int)plen)
			d->tx_packets++;
		else
			d->tx_errors++;
	}
	if (elapsed)
		snd_pcm_period_elapsed(ss);
}

static void do_capture(struct vban_dev *d, u64 now)
{
	struct snd_pcm_substream *ss;
	struct snd_pcm_runtime *rt;
	unsigned long flags;
	bool elapsed = false;
	int guard = 0;

	spin_lock_irqsave(&d->pcm_lock, flags);
	ss = d->cp.ss;
	if (!d->cp.running || !ss) {
		spin_unlock_irqrestore(&d->pcm_lock, flags);
		return;
	}
	rt = ss->runtime;
	while (guard++ < 8) {
		u64 due = div_u64((now - d->cp.start_ns) * (u64)rate, NSEC_PER_SEC);
		u32 n = (u32)fpp, pos = (u32)(d->cp.frames % rt->buffer_size), first;

		if (due < d->cp.frames + fpp)
			break;
		first = min_t(u32, n, rt->buffer_size - pos);
		rx_pull(d, (__le16 *)(rt->dma_area + (size_t)pos * channels * 2), first);
		if (n > first)
			rx_pull(d, (__le16 *)rt->dma_area, n - first);
		if (div_u64(d->cp.frames, rt->period_size) != div_u64(d->cp.frames + n, rt->period_size))
			elapsed = true;
		d->cp.frames += n;
		d->cp.ptr = (snd_pcm_uframes_t)(d->cp.frames % rt->buffer_size);
	}
	spin_unlock_irqrestore(&d->pcm_lock, flags);
	if (elapsed)
		snd_pcm_period_elapsed(ss);
}

static int clock_thread(void *arg)
{
	struct vban_dev *d = arg;
	ktime_t next = ktime_add_ns(ktime_get(), d->tick_ns);

	sched_set_fifo(current);
	while (!kthread_should_stop()) {
		u64 now;

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_hrtimeout_range(&next, 20 * NSEC_PER_USEC, HRTIMER_MODE_ABS);
		now = ktime_get_ns();
		next = ktime_add_ns(next, d->tick_ns);
		if (ktime_before(next, ns_to_ktime(now - 10 * d->tick_ns)))
			next = ns_to_ktime(now + d->tick_ns);   /* resync after a long stall */

		mutex_lock(&d->work_mutex);
		do_playback(d, now);
		do_capture(d, now);
		mutex_unlock(&d->work_mutex);
	}
	return 0;
}

/* ------------------------------------------------------------- ALSA */

static const struct snd_pcm_hardware vban_hw = {
	.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP_VALID,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.buffer_bytes_max = 256 * 1024,
	.period_bytes_min = 64,
	.period_bytes_max = 128 * 1024,
	.periods_min = 2,
	.periods_max = 1024,
};

static struct vban_stream *stream_of(struct vban_dev *d, struct snd_pcm_substream *ss)
{
	return ss->stream == SNDRV_PCM_STREAM_PLAYBACK ? &d->pb : &d->cp;
}

static int vban_open(struct snd_pcm_substream *ss)
{
	struct vban_dev *d = snd_pcm_substream_chip(ss);
	struct snd_pcm_runtime *rt = ss->runtime;
	unsigned long flags;

	rt->hw = vban_hw;
	rt->hw.rates = snd_pcm_rate_to_rate_bit(rate);
	if (rt->hw.rates == SNDRV_PCM_RATE_KNOT)
		rt->hw.rates = SNDRV_PCM_RATE_KNOT;
	rt->hw.rate_min = rt->hw.rate_max = rate;
	rt->hw.channels_min = rt->hw.channels_max = channels;
	spin_lock_irqsave(&d->pcm_lock, flags);
	stream_of(d, ss)->ss = ss;
	spin_unlock_irqrestore(&d->pcm_lock, flags);
	return 0;
}

static int vban_close(struct snd_pcm_substream *ss)
{
	struct vban_dev *d = snd_pcm_substream_chip(ss);
	unsigned long flags;

	mutex_lock(&d->work_mutex);              /* no clock-thread work in flight */
	spin_lock_irqsave(&d->pcm_lock, flags);
	stream_of(d, ss)->running = false;
	stream_of(d, ss)->ss = NULL;
	spin_unlock_irqrestore(&d->pcm_lock, flags);
	mutex_unlock(&d->work_mutex);
	return 0;
}

static int vban_prepare(struct snd_pcm_substream *ss)
{
	struct vban_dev *d = snd_pcm_substream_chip(ss);
	unsigned long flags;

	spin_lock_irqsave(&d->pcm_lock, flags);
	stream_of(d, ss)->frames = 0;
	stream_of(d, ss)->ptr = 0;
	spin_unlock_irqrestore(&d->pcm_lock, flags);
	return 0;
}

static int vban_trigger(struct snd_pcm_substream *ss, int cmd)
{
	struct vban_dev *d = snd_pcm_substream_chip(ss);
	struct vban_stream *st = stream_of(d, ss);
	unsigned long flags;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (ss->stream == SNDRV_PCM_STREAM_CAPTURE) {
			spin_lock(&d->rx_lock);  /* re-anchor on the next packet: fresh timeline */
			kring_start(&d->kr);
			spin_unlock(&d->rx_lock);
		}
		spin_lock_irqsave(&d->pcm_lock, flags);
		st->start_ns = ktime_get_ns();
		st->frames = 0;
		st->ptr = 0;
		st->running = true;
		spin_unlock_irqrestore(&d->pcm_lock, flags);
		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (ss->stream == SNDRV_PCM_STREAM_CAPTURE) {
			spin_lock(&d->rx_lock);
			kring_stop(&d->kr);
			spin_unlock(&d->rx_lock);
		}
		spin_lock_irqsave(&d->pcm_lock, flags);
		st->running = false;
		spin_unlock_irqrestore(&d->pcm_lock, flags);
		return 0;
	}
	return -EINVAL;
}

static int vban_sync_stop(struct snd_pcm_substream *ss)
{
	struct vban_dev *d = snd_pcm_substream_chip(ss);

	mutex_lock(&d->work_mutex);              /* wait for an in-flight copy */
	mutex_unlock(&d->work_mutex);
	return 0;
}

static snd_pcm_uframes_t vban_pointer(struct snd_pcm_substream *ss)
{
	struct vban_dev *d = snd_pcm_substream_chip(ss);

	return READ_ONCE(stream_of(d, ss)->ptr);
}

static const struct snd_pcm_ops vban_ops = {
	.open = vban_open,
	.close = vban_close,
	.prepare = vban_prepare,
	.trigger = vban_trigger,
	.sync_stop = vban_sync_stop,
	.pointer = vban_pointer,
};

static void vban_proc_read(struct snd_info_entry *e, struct snd_info_buffer *b)
{
	struct vban_dev *d = e->private_data;
	const struct vban_kring *k = &d->kr;

	spin_lock(&d->rx_lock);
	snd_iprintf(b, "tx: dest %pI4:%u name %s packets %llu errors %llu\n", &d->tx_addr.sin_addr,
		    ntohs(d->tx_addr.sin_port), tx_name, d->tx_packets, d->tx_errors);
	snd_iprintf(b, "rx: port %d name %s packets %llu lost %llu repaired %llu late %llu dup %llu "
		    "rejected %llu badfmt %llu idle %llu concealed_frames %llu (loss %llu stall %llu) underruns %llu "
		    "reprimes %llu skips %llu buffered %d target %d ratio_ppm %d playing %d\n",
		    listen_port, rx_name, k->packets, k->lost, k->repaired, k->late, k->dup,
		    k->rejected, d->rx_badfmt, k->idle, k->concealed, k->concealed_loss, k->concealed_stall,
		    k->underruns, k->reprimes, k->skips, kring_fill(k), k->target,
		    (int)(((s64)k->ratio_q16 - 65536) * 1000000 / 65536), k->playing);
	spin_unlock(&d->rx_lock);
}

/* ------------------------------------------------------------- setup */

static int parse_dest(const char *s, struct sockaddr_in *a)
{
	const char *end;
	u16 port = VBAN_DEFAULT_PORT;

	memset(a, 0, sizeof(*a));
	a->sin_family = AF_INET;
	if (!in4_pton(s, -1, (u8 *)&a->sin_addr.s_addr, ':', &end))
		return -EINVAL;
	if (*end == ':' && kstrtou16(end + 1, 10, &port))
		return -EINVAL;
	a->sin_port = htons(port);
	return 0;
}

static int open_sockets(struct vban_dev *d)
{
	int r;

	if (dest && *dest) {
		r = parse_dest(dest, &d->tx_addr);
		if (r)
			return r;
		r = sock_create_kern(&init_net, PF_INET, SOCK_DGRAM, IPPROTO_UDP, &d->tx_sock);
		if (r)
			return r;
		sock_set_flag(d->tx_sock->sk, SOCK_BROADCAST);
		ip_sock_set_tos(d->tx_sock->sk, 0xB8);   /* DSCP EF: voice queue */
	}
	if (listen_port > 0) {
		struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons((u16)listen_port),
					 .sin_addr.s_addr = htonl(INADDR_ANY) };

		r = sock_create_kern(&init_net, PF_INET, SOCK_DGRAM, IPPROTO_UDP, &d->rx_sock);
		if (r)
			return r;
		sock_set_reuseaddr(d->rx_sock->sk);
		d->rx_sock->sk->sk_rcvtimeo = msecs_to_jiffies(100);
		r = kernel_bind(d->rx_sock, (void *)&a, sizeof(a));
		if (r)
			return r;
	}
	return 0;
}

static void vban_teardown(struct vban_dev *d)
{
	if (d->clock_task)
		kthread_stop(d->clock_task);
	if (d->rx_task)
		kthread_stop(d->rx_task);
	if (d->card)
		snd_card_free(d->card);
	if (d->tx_sock)
		sock_release(d->tx_sock);
	if (d->rx_sock)
		sock_release(d->rx_sock);
	if (d->pdev)
		platform_device_unregister(d->pdev);
	vfree(d->kr.ring);
	kfree(d);
}

static int __init snd_vban_init(void)
{
	struct vban_dev *d;
	int r;

	if (vban_sr_to_index((u32)rate) < 0 || channels < 1 || channels > MAX_CH || fpp < 16 ||
	    fpp > VBAN_SAMPLES_MAX || (u32)fpp * channels * 2 > VBAN_DATA_MAX_SIZE ||
	    rx_target < fpp || rx_target > (int)(KRING_FRAMES / 2)) {
		pr_err("snd-vban: invalid rate/channels/fpp/rx_target\n");
		return -EINVAL;
	}
	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;
	spin_lock_init(&d->pcm_lock);
	spin_lock_init(&d->rx_lock);
	mutex_init(&d->work_mutex);
	{
		s16 *ring = vzalloc((size_t)KRING_FRAMES * channels * sizeof(s16));

		if (!ring) {
			r = -ENOMEM;
			goto fail;
		}
		kring_init(&d->kr, ring, channels, fpp, rx_target);
	}
	vban_name_set(&d->tx_vname, tx_name);
	vban_name_set(&d->rx_vname, rx_name);
	d->tick_ns = div_u64((u64)fpp * NSEC_PER_SEC, (u32)rate);
	if (rx_source && *rx_source && !in4_pton(rx_source, -1, (u8 *)&d->rx_src_ip, -1, NULL)) {
		r = -EINVAL;
		goto fail;
	}

	d->pdev = platform_device_register_simple("snd_vban", -1, NULL, 0);
	if (IS_ERR(d->pdev)) {
		r = PTR_ERR(d->pdev);
		d->pdev = NULL;
		goto fail;
	}
	r = snd_card_new(&d->pdev->dev, -1, "VBAN", THIS_MODULE, 0, &d->card);
	if (r)
		goto fail;
	strscpy(d->card->driver, "snd-vban", sizeof(d->card->driver));
	strscpy(d->card->shortname, "VBAN", sizeof(d->card->shortname));
	snprintf(d->card->longname, sizeof(d->card->longname), "VBAN network audio (tx %s -> %s, rx %s on :%d)",
		 tx_name, dest, rx_name, listen_port);
	r = snd_pcm_new(d->card, "VBAN", 0, 1, 1, &d->pcm);
	if (r)
		goto fail;
	d->pcm->private_data = d;
	strscpy(d->pcm->name, "VBAN", sizeof(d->pcm->name));
	snd_pcm_set_ops(d->pcm, SNDRV_PCM_STREAM_PLAYBACK, &vban_ops);
	snd_pcm_set_ops(d->pcm, SNDRV_PCM_STREAM_CAPTURE, &vban_ops);
	snd_pcm_set_managed_buffer_all(d->pcm, SNDRV_DMA_TYPE_VMALLOC, NULL, 0, 0);
	snd_card_ro_proc_new(d->card, "vban", d, vban_proc_read);

	r = open_sockets(d);
	if (r)
		goto fail;
	r = snd_card_register(d->card);
	if (r)
		goto fail;

	d->clock_task = kthread_run(clock_thread, d, "vban-clock");
	if (IS_ERR(d->clock_task)) {
		r = PTR_ERR(d->clock_task);
		d->clock_task = NULL;
		goto fail;
	}
	if (d->rx_sock) {
		d->rx_task = kthread_run(rx_thread, d, "vban-rx");
		if (IS_ERR(d->rx_task)) {
			r = PTR_ERR(d->rx_task);
			d->rx_task = NULL;
			goto fail;
		}
	}
	g_dev = d;
	pr_info("snd-vban: card %d, %d Hz %d ch, %d frames/packet, tx %s -> %s, rx %s on :%d\n",
		d->card->number, rate, channels, fpp, tx_name, dest, rx_name, listen_port);
	return 0;
fail:
	vban_teardown(d);
	return r;
}

static void __exit snd_vban_exit(void)
{
	if (g_dev)
		vban_teardown(g_dev);
}

module_init(snd_vban_init);
module_exit(snd_vban_exit);
MODULE_DESCRIPTION("ALSA virtual sound card over VB-Audio VBAN");
MODULE_LICENSE("Dual MIT/GPL");
