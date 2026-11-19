// SPDX-License-Identifier: MIT
/*
 * Issues the time64 PCM ioctls the way alsa-lib does and checks the results.
 *
 *   test-shim baseline   linked without the shim: the old kernel must refuse
 *   test-shim old        linked with the shim, kernel before 5.6
 *   test-shim new        linked with the shim, FAKE_KERNEL_TIME64=1 (5.6+)
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sound/asound.h>
#include "fake-kernel.h"

static int failures;

#define CHECK(cond)							\
	do {								\
		if (!(cond)) {						\
			fprintf(stderr, "%s:%d: check failed: %s\n",	\
				__FILE__, __LINE__, #cond);		\
			failures++;					\
		}							\
	} while (0)

static void check_ts(const struct timespec *ts, long long sec, long nsec)
{
	CHECK(ts->tv_sec == sec);
	CHECK(ts->tv_nsec == nsec);
}

static void baseline(void)
{
	struct snd_pcm_sync_ptr sp;

	memset(&sp, 0, sizeof(sp));
	CHECK(ioctl(FAKE_FD, SNDRV_PCM_IOCTL_SYNC_PTR, &sp) == -1);
	CHECK(errno == ENOTTY);
}

static void old_kernel(void)
{
	struct snd_pcm_sync_ptr sp;
	struct snd_pcm_status st;
	int version = 0;

	memset(&sp, 0, sizeof(sp));
	sp.flags = FAKE_IN_FLAGS;
	sp.c.control.appl_ptr = FAKE_IN_APPL_PTR;
	sp.c.control.avail_min = FAKE_IN_AVAIL_MIN;
	CHECK(ioctl(FAKE_FD, SNDRV_PCM_IOCTL_SYNC_PTR, &sp) == 0);
	CHECK(sp.s.status.state == FAKE_STATE);
	CHECK(sp.s.status.hw_ptr == FAKE_HW_PTR);
	check_ts(&sp.s.status.tstamp, FAKE_SEC, FAKE_NSEC);
	CHECK(sp.s.status.suspended_state == FAKE_SUSPENDED);
	check_ts(&sp.s.status.audio_tstamp, FAKE_SEC + 1, FAKE_NSEC + 1);
	CHECK(sp.c.control.appl_ptr == FAKE_IN_APPL_PTR + 1);
	CHECK(sp.c.control.avail_min == FAKE_IN_AVAIL_MIN + 1);

	for (int ext = 0; ext <= 1; ext++) {
		memset(&st, 0xff, sizeof(st));
		st.audio_tstamp_data = FAKE_IN_TSTAMP_DATA;
		CHECK(ioctl(FAKE_FD, ext ? SNDRV_PCM_IOCTL_STATUS_EXT : SNDRV_PCM_IOCTL_STATUS, &st) == 0);
		CHECK(st.state == FAKE_STATE);
		check_ts(&st.trigger_tstamp, FAKE_SEC + 2, FAKE_NSEC + 2);
		check_ts(&st.tstamp, FAKE_SEC + 3, FAKE_NSEC + 3);
		CHECK(st.appl_ptr == FAKE_IN_APPL_PTR);
		CHECK(st.hw_ptr == FAKE_HW_PTR);
		CHECK(st.delay == -7);
		CHECK(st.avail == 8);
		CHECK(st.avail_max == 9);
		CHECK(st.overrange == 10);
		CHECK(st.suspended_state == FAKE_SUSPENDED);
		CHECK(st.audio_tstamp_data == (ext ? FAKE_IN_TSTAMP_DATA : 0));
		check_ts(&st.audio_tstamp, FAKE_SEC + 4, FAKE_NSEC + 4);
		check_ts(&st.driver_tstamp, FAKE_SEC + 5, FAKE_NSEC + 5);
		CHECK(st.audio_tstamp_accuracy == 11);
	}

	/* Other requests pass through unchanged. */
	CHECK(ioctl(FAKE_FD, SNDRV_PCM_IOCTL_PVERSION, &version) == 0);
	CHECK(version == SNDRV_PCM_VERSION);

	/* Errors from the kernel are returned as they are. */
	CHECK(ioctl(FAKE_FD + 1, SNDRV_PCM_IOCTL_SYNC_PTR, &sp) == -1);
	CHECK(errno == EBADF);

	CHECK(fake_kernel_calls32() == 3);	/* SYNC_PTR, STATUS, STATUS_EXT */
	CHECK(fake_kernel_calls64() == 0);
}

static void new_kernel(void)
{
	struct snd_pcm_sync_ptr sp;
	struct snd_pcm_status st;

	memset(&sp, 0, sizeof(sp));
	memset(&st, 0, sizeof(st));
	CHECK(ioctl(FAKE_FD, SNDRV_PCM_IOCTL_SYNC_PTR, &sp) == 0);
	CHECK(sp.s.status.hw_ptr == FAKE_HW_PTR);
	CHECK(ioctl(FAKE_FD, SNDRV_PCM_IOCTL_STATUS, &st) == 0);
	CHECK(st.hw_ptr == FAKE_HW_PTR);

	/* A kernel that takes the time64 codes gets them untranslated. */
	CHECK(fake_kernel_calls64() == 2);
	CHECK(fake_kernel_calls32() == 0);
}

int main(int argc, char **argv)
{
	const char *mode = argc > 1 ? argv[1] : "";

	if (!strcmp(mode, "baseline"))
		baseline();
	else if (!strcmp(mode, "old"))
		old_kernel();
	else if (!strcmp(mode, "new"))
		new_kernel();
	else {
		fprintf(stderr, "usage: %s baseline|old|new\n", argv[0]);
		return 2;
	}

	printf("%s: %s\n", mode, failures ? "FAIL" : "ok");
	return failures ? 1 : 0;
}
