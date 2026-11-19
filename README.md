# alsa-time64-shim

`libalsa-time64.so` lets ALSA programs built with a 64-bit `time_t` play and
record on 32-bit systems whose kernel is older than Linux 5.6.

Debian 13 (trixie) and Ubuntu 24.04 build their 32-bit ARM ports with
`_TIME_BITS=64`. Put such a userland on top of an older kernel, as in a chroot
on an Android device or on a board stuck with its vendor kernel, and every PCM
open fails:

    $ speaker-test -t sine
    Playback open error: -25,Inappropriate ioctl for device

## How it works

With a 64-bit `time_t`, alsa-lib issues the time64 variants of three PCM
ioctls. They keep their command numbers, but their arguments are larger, so the
request codes change. Kernels before 5.6 do not recognise the new codes and
return `ENOTTY`. alsa-lib cannot open a PCM without `SNDRV_PCM_IOCTL_SYNC_PTR`,
so nothing plays.

The library is loaded with `LD_PRELOAD` and wraps `ioctl()` (and
`__ioctl_time64()`, which glibc redirects `ioctl()` to in time64 builds). For
the three requests below it first passes the call on unchanged. If the kernel
answers `ENOTTY`, it repeats the call with the 32-bit time layout, converts the
result back, and does so for every later call. On kernels that support the
time64 codes the requests go through untouched, so the library can stay
installed after a kernel upgrade.

| Request | Converted |
|---|---|
| `SNDRV_PCM_IOCTL_SYNC_PTR` | hardware and application pointers, state, timestamps |
| `SNDRV_PCM_IOCTL_STATUS` | the whole status structure |
| `SNDRV_PCM_IOCTL_STATUS_EXT` | the same, plus the requested timestamp type |

Every other ioctl, ALSA or not, is passed through as it is.

alsa-lib tries to map the status and control pages first. Kernels before 5.6
refuse the time64 page offsets, and alsa-lib falls back to `SYNC_PTR` on its
own.

musl converts these requests inside its own `ioctl()`, so musl-based systems
are not affected. glibc does no conversion.

## Status

| | |
|---|---|
| Hardware | Samsung Galaxy Tab A 7.0 (SM-T285M), Linux 3.10.65, Debian 13 armhf chroot, alsa-lib 1.2.14 |
| Verified | playback with `speaker-test` on the built-in codec (`hw:3,0`); capture not tested |
| Tests | `make check` against a fake pre-5.6 kernel, on armhf (QEMU) and i386 in CI |
| C library | glibc 2.34 or later |

## Limitations

- Only the three PCM requests above are converted. The ALSA timer interface
  (`SNDRV_TIMER_IOCTL_TREAD64`, `SNDRV_TIMER_IOCTL_STATUS`) and the raw MIDI
  status request still fail with `ENOTTY`, so programs that use timer
  timestamps, PortAudio among them, may not work.
- Programs that are linked statically, or that make the `ioctl` system call
  without going through libc, are not affected by `LD_PRELOAD`.
- It is a 32-bit library. On 64-bit systems `time_t` has always been 64 bits
  and nothing needs converting; the build refuses to compile there.

## Building

    make
    make check

To cross-compile for armhf and run the tests under QEMU:

    make CC=arm-linux-gnueabihf-gcc
    make check CC=arm-linux-gnueabihf-gcc RUNNER="qemu-arm -L /usr/arm-linux-gnueabihf"

On Debian and Ubuntu the cross compiler is in `gcc-arm-linux-gnueabihf`, QEMU in
`qemu-user`. The build needs the kernel's `sound/asound.h`, from
`linux-libc-dev`.

## Installation

    sudo make install

installs `/usr/local/lib/libalsa-time64.so` (`PREFIX`, `LIBDIR` and `DESTDIR`
are honoured).

For one program:

    LD_PRELOAD=/usr/local/lib/libalsa-time64.so aplay music.wav

For everything, add the path to `/etc/ld.so.preload`:

    echo /usr/local/lib/libalsa-time64.so | sudo tee -a /etc/ld.so.preload

For a service, set `LD_PRELOAD` in its environment. Under systemd that is
`Environment=LD_PRELOAD=/usr/local/lib/libalsa-time64.so` in the unit.

## License

MIT, see [LICENSE](LICENSE).
