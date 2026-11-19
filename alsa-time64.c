// SPDX-License-Identifier: MIT
/*
 * alsa-time64-shim: run ALSA programs built with a 64-bit time_t on 32-bit
 * systems whose kernel is older than Linux 5.6.
 *
 * Built with _TIME_BITS=64, alsa-lib issues the time64 variants of three PCM
 * ioctls. They have the same command numbers as the old ones but different
 * argument sizes, so the request codes differ, and kernels before 5.6 reject
 * them with ENOTTY. alsa-lib cannot open a PCM without SYNC_PTR, so every
 * open fails:
 *
 *     Playback open error: -25,Inappropriate ioctl for device
 *
 * Preloaded, this library intercepts those three requests, tries them as
 * issued and, if the kernel answers ENOTTY, repeats them with the 32-bit time
 * layouts, converting the structures in both directions:
 *
 *     SNDRV_PCM_IOCTL_SYNC_PTR, SNDRV_PCM_IOCTL_STATUS, SNDRV_PCM_IOCTL_STATUS_EXT
 *
 * Every other request is passed through untouched. musl performs the same
 * conversion inside ioctl(); glibc does not.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sound/asound.h>

#if __SIZEOF_POINTER__ != 4
#error "alsa-time64-shim is only meaningful on 32-bit targets"
#endif
#if !defined(__USE_TIME_BITS64)
#error "build with -D_FILE_OFFSET_BITS=64 -D_TIME_BITS=64 (glibc 2.34 or later)"
#endif

typedef int (*ioctl_fn)(int, unsigned long, void *);

/* The pre-5.6 layouts, with a 32-bit tv_sec. */
struct ts32 {
	int32_t tv_sec;
	int32_t tv_nsec;
};

struct mmap_status32 {
	int state;
	int pad1;
	uint32_t hw_ptr;
	struct ts32 tstamp;
	int suspended_state;
	struct ts32 audio_tstamp;
};

struct mmap_control32 {
	uint32_t appl_ptr;
	uint32_t avail_min;
};

struct sync_ptr32 {
	unsigned int flags;
	union {
		struct mmap_status32 status;
		unsigned char reserved[64];
	} s;
	union {
		struct mmap_control32 control;
		unsigned char reserved[64];
	} c;
};

struct status32 {
	int state;
	struct ts32 trigger_tstamp;
	struct ts32 tstamp;
	uint32_t appl_ptr;
	uint32_t hw_ptr;
	int32_t delay;
	uint32_t avail;
	uint32_t avail_max;
	uint32_t overrange;
	int suspended_state;
	uint32_t audio_tstamp_data;
	struct ts32 audio_tstamp;
	struct ts32 driver_tstamp;
	uint32_t audio_tstamp_accuracy;
	unsigned char reserved[52 - 2 * sizeof(struct ts32)];
};

/* The sizes are part of the request codes, so they must match the kernel's. */
_Static_assert(sizeof(struct sync_ptr32) == 132, "struct sync_ptr32 layout");
_Static_assert(sizeof(struct status32) == 108, "struct status32 layout");

#define SYNC_PTR32   _IOWR('A', 0x23, struct sync_ptr32)
#define STATUS32     _IOR('A', 0x20, struct status32)
#define STATUS_EXT32 _IOWR('A', 0x24, struct status32)

/* With _TIME_BITS=64 these expand to the time64 request codes. */
#define SYNC_PTR64   SNDRV_PCM_IOCTL_SYNC_PTR
#define STATUS64     SNDRV_PCM_IOCTL_STATUS
#define STATUS_EXT64 SNDRV_PCM_IOCTL_STATUS_EXT

_Static_assert(SYNC_PTR64 != SYNC_PTR32 && STATUS64 != STATUS32,
	       "time64 and time32 request codes must differ");

static void ts_from32(struct timespec *dst, const struct ts32 *src)
{
	dst->tv_sec = src->tv_sec;
	dst->tv_nsec = src->tv_nsec;
}

static int sync_ptr(ioctl_fn real, int fd, struct snd_pcm_sync_ptr *p)
{
	struct sync_ptr32 o;
	int ret;

	memset(&o, 0, sizeof(o));
	o.flags = p->flags;
	o.c.control.appl_ptr = p->c.control.appl_ptr;
	o.c.control.avail_min = p->c.control.avail_min;

	ret = real(fd, SYNC_PTR32, &o);
	if (ret < 0)
		return ret;

	p->s.status.state = o.s.status.state;
	p->s.status.hw_ptr = o.s.status.hw_ptr;
	ts_from32(&p->s.status.tstamp, &o.s.status.tstamp);
	p->s.status.suspended_state = o.s.status.suspended_state;
	ts_from32(&p->s.status.audio_tstamp, &o.s.status.audio_tstamp);
	p->c.control.appl_ptr = o.c.control.appl_ptr;
	p->c.control.avail_min = o.c.control.avail_min;
	return ret;
}

static int status(ioctl_fn real, int fd, int ext, struct snd_pcm_status *p)
{
	struct status32 o;
	int ret;

	memset(&o, 0, sizeof(o));
	if (ext)
		o.audio_tstamp_data = p->audio_tstamp_data;	/* input */

	ret = real(fd, ext ? STATUS_EXT32 : STATUS32, &o);
	if (ret < 0)
		return ret;

	memset(p, 0, sizeof(*p));
	p->state = o.state;
	ts_from32(&p->trigger_tstamp, &o.trigger_tstamp);
	ts_from32(&p->tstamp, &o.tstamp);
	p->appl_ptr = o.appl_ptr;
	p->hw_ptr = o.hw_ptr;
	p->delay = o.delay;
	p->avail = o.avail;
	p->avail_max = o.avail_max;
	p->overrange = o.overrange;
	p->suspended_state = o.suspended_state;
	p->audio_tstamp_data = o.audio_tstamp_data;
	ts_from32(&p->audio_tstamp, &o.audio_tstamp);
	ts_from32(&p->driver_tstamp, &o.driver_tstamp);
	p->audio_tstamp_accuracy = o.audio_tstamp_accuracy;
	return ret;
}

/*
 * Whether the kernel accepts the time64 request codes: unknown until the first
 * call, then remembered. Kernels from 5.6 on take them directly.
 */
static int kernel_has_time64 = -1;

static int dispatch(ioctl_fn real, int fd, unsigned long req, void *arg)
{
	int ret;

	if (req != SYNC_PTR64 && req != STATUS64 && req != STATUS_EXT64)
		return real(fd, req, arg);

	if (kernel_has_time64 != 0) {
		ret = real(fd, req, arg);
		if (ret >= 0 || errno != ENOTTY) {
			if (ret >= 0)
				kernel_has_time64 = 1;
			return ret;
		}
		if (kernel_has_time64 == 1)
			return ret;
		kernel_has_time64 = 0;
	}

	if (req == SYNC_PTR64)
		return sync_ptr(real, fd, arg);
	return status(real, fd, req == STATUS_EXT64, arg);
}

static int raw_ioctl(int fd, unsigned long req, void *arg)
{
	return syscall(SYS_ioctl, fd, req, arg);
}

/*
 * With _TIME_BITS=64 glibc's headers redirect ioctl() to __ioctl_time64(), so
 * both entry points are defined under explicit assembler names. glibc before
 * 2.34 has no __ioctl_time64; fall back to the system call then.
 */
#define WRAP(name)								\
	int shim_##name(int fd, unsigned long req, ...) __asm__(#name);		\
	int shim_##name(int fd, unsigned long req, ...)				\
	{									\
		static ioctl_fn real;						\
		va_list ap;							\
		void *arg;							\
										\
		if (!real) {							\
			real = (ioctl_fn)dlsym(RTLD_NEXT, #name);		\
			if (!real)						\
				real = raw_ioctl;				\
		}								\
		va_start(ap, req);						\
		arg = va_arg(ap, void *);					\
		va_end(ap);							\
		return dispatch(real, fd, req, arg);				\
	}

WRAP(ioctl)
WRAP(__ioctl_time64)
