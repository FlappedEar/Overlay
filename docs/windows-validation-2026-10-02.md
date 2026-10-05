# Windows validation by the owner (2 October 2026)

The owner built and tested FlappedEar Overlays on Windows locally, twice: once on
`c8f0c9f`, then again after the fixes, on `fa79447`. Nothing from either run was
committed; this page records both. As `AGENTS.md` requires, results from real
recordings are reported separately from synthetic tests.

## Retest after the fixes (`fa79447`)

`fa79447` was the head of [PR #146](https://github.com/arekkozuch/VBOOverlay/pull/146),
which was then merged as `1a96ae3`. The environment is the same as below, with the
NVIDIA driver at 617.14. Everything passes, with no failures and no local workarounds.

**Synthetic tests**
- **Build:** a clean Release build passes, with no errors and no compiler warnings
  in project code ([KAN-176]).
- **CTest:** 39 of 39 pass. The `._*` folder-import test that failed before now
  passes ([KAN-173]).

**Real recordings (reported separately)**
- **Data:** same results as the first run. The VBO parses with 9,093 samples and
  3 laps; auto-sync gives an offset of 7.817 s at correlation 0.998.
- **End to end in the app:** import, sync, seek, and an export that passed
  validation, with no test changes.
- **Paused seek ([KAN-172]):** fixed. The preview lands 38 s into lap 3 and shows
  live values (53 km/h, 4,020 rpm, 91% throttle, heart rate 147) instead of
  jumping back to the start.
- **NVIDIA encoder ([KAN-174]):** fixed. The app selected NVENC on its own. The
  encoder ran at 46.5 fps against 43.0 fps on Quick Sync, but the whole 8 s 1080p
  export still took about 22 s, so the encoder is not the bottleneck for short
  1080p exports ([KAN-177]).

**Still open**
- About 135 harmless "access denied" registry warnings during test cleanup
  ([KAN-179]).

## First run (`c8f0c9f`)

### Environment

- **System and build:** Windows 11 Pro; MSVC 2022 (Enterprise), Ninja, Release build.
- **Qt and FFmpeg:** Qt 6.11.0 msvc2022_64 with the Qt Multimedia FFmpeg 7.1.3
  backend; system FFmpeg 9.0.1 (gyan.dev full build).
- **GPUs:** Intel UHD Graphics (driver 31.0.101.1999) and NVIDIA GeForce RTX
  3050 Laptop GPU (driver 546.30, updated to 617.14 during the run).
- **Revision:** `main` at `c8f0c9f` (#135). This is before the FlappedEar
  Overlays rename, and 21 commits before `978dc2a`.

### Synthetic tests

- **Build:** passed after four local changes, now [KAN-176].
- **CTest:** 37 of 38 entries passed; 20 cases are skipped by design. The one
  failure was the folder-scan test ([KAN-173]).
- **Startup smoke:** passed, once `QT_FORCE_STDERR_LOGGING` was set ([KAN-176]).

### Real recordings (reported separately)

| Check | Result |
| --- | --- |
| VBO parse (one Jastrząb session) | 9,093 samples, 49 channels, 909.2 s, no warnings, no non-finite values |
| VBO laps | 3 complete laps: 115.402 s, 116.279 s and 111.238 s (one start gate) |
| GoPro GPS (`GX010091.MP4`: HEVC Main 10, 3840×2160, 59.94 fps, 25:37, 11.5 GB) | 1,536 packets, 14,530 GPS9 samples |
| Auto-sync | Offset 7.817 s, correlation 0.998, confidence 0.86, applied automatically |
| Laps on the video | Lap 1 at 435.614 s, lap 2 at 551.016 s, lap 3 at 667.295 s |
| Value lookups on the real VBO | 10,000 lookups in 6 ms |
| End to end in the app | Import, video, auto-sync, 26 widgets, then an 8 s 1080p export of lap 3 through the export worker (Quick Sync HEVC, 1920×1080, 59.94 fps, AAC, 481 frames). The export completed and passed validation |

**Not tested:** importing a day of several recordings; the installer and
packaging; frame-by-frame inspection of the exported file.

### Findings

| Finding | Ticket | Status |
| --- | --- | --- |
| Seeking while paused jumps back to the start (the Windows backend reports `LoadedMedia` again) | [KAN-172] | Fixed; verified on Windows (retest) |
| Folder import picks up macOS `._*` metadata files | [KAN-173] | Fixed; verified on Windows (retest) |
| NVENC is never selected (the encoder probe used a 64×64 frame) | [KAN-174] | Fixed (256×256 probe); verified on Windows (retest) |
| An MP4 with an edit list fails to export: frames are counted from packets, and validation refuses the output | [KAN-175] | Fixed on 5 October 2026 (PR #167); not yet re-checked on Windows |
| Build and test portability (Mach headers, `/bigobj`, smoke logging, VideoToolbox skip) and three MSVC warnings | [KAN-176] | Fixed; verified on Windows (retest) |
| No GPU choice for rendering; CPU-side 10-bit composition limits export speed | [KAN-177] | Recorded idea; not scheduled |
| `--export-test` rejected every export | — | Obsolete: KAN-156 removed that mode. An unknown flag opened the editor until 5 October 2026; it now reports a usage error ([KAN-178]) |
| About 135 harmless "access denied" registry warnings during test cleanup | [KAN-179] | Open |

### Intel and NVIDIA

The source was 600 frames of the real 4K 10-bit clip.

**Overlay rendering** (`--benchmark-render`, 300 frames):

| GPU | 4K | 1080p |
| --- | --- | --- |
| Intel UHD (the app's default, D3D11 adapter 0) | 64.6 fps (15.5 ms/frame) | 222 fps (4.5 ms/frame) |
| RTX 3050 | 99.5 fps (10.1 ms/frame) | 335 fps (3.0 ms/frame) |

**HEVC 10-bit encoding only:**

| Encoder | 4K | 1080p |
| --- | --- | --- |
| Quick Sync (`hevc_qsv`) | 26 fps | 83 fps |
| NVENC (`hevc_nvenc`) | 75 fps | 225 fps |

**The app's full final-encode stage** (decode, overlay composition and encode):

| Encoder | 4K | 1080p |
| --- | --- | --- |
| Quick Sync | 22.6 fps (67.5 MB, PSNR 45.17 dB) | 42.6 fps (17.1 MB, 41.87 dB) |
| NVENC | 25.8 fps (68.7 MB, 45.06 dB) | 48.7 fps (16.3 MB, 41.92 dB) |

At the same bitrate, size and quality are equivalent.

- **Rendering** is well above export speed, so it is not the bottleneck.
- **NVENC in the full stage** is only 10–15% faster, because the CPU-side
  10-bit composition (unpremultiply and blend) becomes the limit.
- **Hardware decoding** of the source (`-hwaccel d3d11va`) did not help.
- **Driver:** FFmpeg 9.0.1 needs NVIDIA driver 610 or newer for NVENC.

## Status of Windows work

Since 2 October 2026 the owner has resumed Windows code changes for defects
this validation finds. Windows CI, packaging and installer validation stay
paused until the owner resumes them ([KAN-162], [KAN-84]).

[KAN-84]: https://kozucharkadiusz.atlassian.net/browse/KAN-84
[KAN-162]: https://kozucharkadiusz.atlassian.net/browse/KAN-162
[KAN-172]: https://kozucharkadiusz.atlassian.net/browse/KAN-172
[KAN-173]: https://kozucharkadiusz.atlassian.net/browse/KAN-173
[KAN-174]: https://kozucharkadiusz.atlassian.net/browse/KAN-174
[KAN-175]: https://kozucharkadiusz.atlassian.net/browse/KAN-175
[KAN-176]: https://kozucharkadiusz.atlassian.net/browse/KAN-176
[KAN-177]: https://kozucharkadiusz.atlassian.net/browse/KAN-177
[KAN-178]: https://kozucharkadiusz.atlassian.net/browse/KAN-178
[KAN-179]: https://kozucharkadiusz.atlassian.net/browse/KAN-179
