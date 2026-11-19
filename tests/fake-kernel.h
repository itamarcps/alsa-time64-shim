/* SPDX-License-Identifier: MIT */
#ifndef FAKE_KERNEL_H
#define FAKE_KERNEL_H

#include <stdint.h>
#include <sys/ioctl.h>

/* The pre-5.6 kernel structures, written out independently of the shim. */
struct fake_ts32 {
	int32_t tv_sec;
	int32_t tv_nsec;
};

struct fake_sync_ptr32 {
	unsigned int flags;
	union {
		struct {
			int state;
			int pad1;
			uint32_t hw_ptr;
			struct fake_ts32 tstamp;
			int suspended_state;
			struct fake_ts32 audio_tstamp;
		} status;
		unsigned char reserved[64];
	} s;
	union {
		struct {
			uint32_t appl_ptr;
			uint32_t avail_min;
		} control;
		unsigned char reserved[64];
	} c;
};

struct fake_status32 {
	int state;
	struct fake_ts32 trigger_tstamp;
	struct fake_ts32 tstamp;
	uint32_t appl_ptr;
	uint32_t hw_ptr;
	int32_t delay;
	uint32_t avail;
	uint32_t avail_max;
	uint32_t overrange;
	int suspended_state;
	uint32_t audio_tstamp_data;
	struct fake_ts32 audio_tstamp;
	struct fake_ts32 driver_tstamp;
	uint32_t audio_tstamp_accuracy;
	unsigned char reserved[36];
};

#define FAKE_SYNC_PTR32   _IOWR('A', 0x23, struct fake_sync_ptr32)
#define FAKE_STATUS32     _IOR('A', 0x20, struct fake_status32)
#define FAKE_STATUS_EXT32 _IOWR('A', 0x24, struct fake_status32)

#define FAKE_FD             42
#define FAKE_IN_FLAGS       0x2u		/* SNDRV_PCM_SYNC_PTR_APPL */
#define FAKE_IN_APPL_PTR    100u
#define FAKE_IN_AVAIL_MIN   200u
#define FAKE_IN_TSTAMP_DATA 0x5u
#define FAKE_STATE          3		/* SNDRV_PCM_STATE_RUNNING */
#define FAKE_HW_PTR         4096u
#define FAKE_SUSPENDED      2
#define FAKE_SEC            1700000000
#define FAKE_NSEC           123456789

int fake_kernel_calls32(void);
int fake_kernel_calls64(void);

#endif
