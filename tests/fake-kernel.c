// SPDX-License-Identifier: MIT
/*
 * A stand-in for the kernel side of the three PCM ioctls, for the tests.
 *
 * By default it behaves like Linux before 5.6: only the 32-bit time request
 * codes are understood, the time64 ones fail with ENOTTY. With
 * FAKE_KERNEL_TIME64=1 in the environment it behaves like 5.6 and later and
 * accepts the time64 codes as well.
 *
 * It checks what it receives and answers with fixed values, so the test can
 * verify every field the shim converts.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sound/asound.h>
#include "fake-kernel.h"

static int calls32, calls64;

int fake_kernel_calls32(void) { return calls32; }
int fake_kernel_calls64(void) { return calls64; }

static int fail(int err)
{
	errno = err;
	return -1;
}

static int handle(int fd, unsigned long req, void *arg)
{
	int time64 = getenv("FAKE_KERNEL_TIME64") && !strcmp(getenv("FAKE_KERNEL_TIME64"), "1");

	if (fd != FAKE_FD)
		return fail(EBADF);

	if (req == FAKE_SYNC_PTR32) {
		struct fake_sync_ptr32 *p = arg;

		calls32++;
		if (p->flags != FAKE_IN_FLAGS || p->c.control.appl_ptr != FAKE_IN_APPL_PTR ||
		    p->c.control.avail_min != FAKE_IN_AVAIL_MIN)
			return fail(EINVAL);
		p->s.status.state = FAKE_STATE;
		p->s.status.hw_ptr = FAKE_HW_PTR;
		p->s.status.tstamp.tv_sec = FAKE_SEC;
		p->s.status.tstamp.tv_nsec = FAKE_NSEC;
		p->s.status.suspended_state = FAKE_SUSPENDED;
		p->s.status.audio_tstamp.tv_sec = FAKE_SEC + 1;
		p->s.status.audio_tstamp.tv_nsec = FAKE_NSEC + 1;
		p->c.control.appl_ptr = FAKE_IN_APPL_PTR + 1;
		p->c.control.avail_min = FAKE_IN_AVAIL_MIN + 1;
		return 0;
	}

	if (req == FAKE_STATUS32 || req == FAKE_STATUS_EXT32) {
		struct fake_status32 *s = arg;

		calls32++;
		if (req == FAKE_STATUS_EXT32 && s->audio_tstamp_data != FAKE_IN_TSTAMP_DATA)
			return fail(EINVAL);
		s->state = FAKE_STATE;
		s->trigger_tstamp.tv_sec = FAKE_SEC + 2;
		s->trigger_tstamp.tv_nsec = FAKE_NSEC + 2;
		s->tstamp.tv_sec = FAKE_SEC + 3;
		s->tstamp.tv_nsec = FAKE_NSEC + 3;
		s->appl_ptr = FAKE_IN_APPL_PTR;
		s->hw_ptr = FAKE_HW_PTR;
		s->delay = -7;
		s->avail = 8;
		s->avail_max = 9;
		s->overrange = 10;
		s->suspended_state = FAKE_SUSPENDED;
		s->audio_tstamp.tv_sec = FAKE_SEC + 4;
		s->audio_tstamp.tv_nsec = FAKE_NSEC + 4;
		s->driver_tstamp.tv_sec = FAKE_SEC + 5;
		s->driver_tstamp.tv_nsec = FAKE_NSEC + 5;
		s->audio_tstamp_accuracy = 11;
		return 0;
	}

	if (req == SNDRV_PCM_IOCTL_SYNC_PTR || req == SNDRV_PCM_IOCTL_STATUS ||
	    req == SNDRV_PCM_IOCTL_STATUS_EXT) {
		if (!time64)
			return fail(ENOTTY);
		calls64++;
		if (req == SNDRV_PCM_IOCTL_SYNC_PTR) {
			struct snd_pcm_sync_ptr *p = arg;

			p->s.status.state = FAKE_STATE;
			p->s.status.hw_ptr = FAKE_HW_PTR;
		} else {
			struct snd_pcm_status *s = arg;

			s->state = FAKE_STATE;
			s->hw_ptr = FAKE_HW_PTR;
		}
		return 0;
	}

	if (req == SNDRV_PCM_IOCTL_PVERSION) {
		*(int *)arg = SNDRV_PCM_VERSION;
		return 0;
	}

	return fail(ENOTTY);
}

#define DEFINE(name)							\
	int fake_##name(int fd, unsigned long req, ...) __asm__(#name);	\
	int fake_##name(int fd, unsigned long req, ...)			\
	{								\
		va_list ap;						\
		void *arg;						\
									\
		va_start(ap, req);					\
		arg = va_arg(ap, void *);				\
		va_end(ap);						\
		return handle(fd, req, arg);				\
	}

DEFINE(ioctl)
DEFINE(__ioctl_time64)
