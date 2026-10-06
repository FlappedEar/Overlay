# KAN-79 — Full-day analysis acceptance on the Jastrząb recordings

Run on 27 September 2026 on a Mac mini (Mac16,10, Apple M4, macOS 27.0). The
data is the owner's Jastrząb day of 29 August 2026: six RaceChrono Pro
v10.2.4 VBO recordings, 33.6 MiB in total. The recordings are private and
stay out of the repository (`jastrzab/` is git-ignored), and so do the
screenshots.

To reproduce, at commit `7eae6cd` (the last main with the Analysis window, which KAN-166 removed):

```bash
FLAPPEDEAR_REAL_DAY=$PWD/jastrzab \
FLAPPEDEAR_CORNER_REVIEW_DIR=/tmp/review \
FLAPPEDEAR_REVIEW_SIZE=1180x720 \
./build-native/native/tests/flappedear_native_tests analyzesPrivateTrackDayCorners
```

The test drives the production controller and the real Analysis window
(QML). It saves screenshots and fails if any check below fails. The
reachability check also passes at 760×480 (KAN-78).

## Cases

| Case | Result |
|---|---|
| Import of the six recordings as one event | Pass. Six sessions, named in recording order |
| OUT / LAP / IN per session | Pass. 6 OUT, 25 LAP, 6 IN. Each session starts with its out lap and ends with its in lap (table below) |
| Alternative export grouping (RCZ + VBO of one session) | **Blocked.** The set has no RCZ or other alternative recordings. It is covered only by synthetic fixtures (`importsAnalysisRunsAutomatically`) |
| Exclusions | Pass. Excluding the best lap (Session 5 LAP 2) moves the best of the day to Session 6 LAP 3 (1:51.238); restoring it brings the original back exactly |
| Best lap | Session 5 LAP 2, 1:49.898 |
| Reviewed sectors | 14 proposals on the best lap (axis 2,043 m), all approved as a driver would |
| Theoretical best | 1:47.905. That is 1.993 s under the best lap, from donors in Sessions 4, 5 and 6 |
| Losses | 62 observed across the other five sessions' best laps, each against the best lap. The largest is Corners 9–16 in Session 2 LAP 1 (+11.290 s) |
| A/B and Corner Analyzer | Pass. Opening the top loss loads the pair and shows per-segment sector times, speeds, braking and throttle pickup |
| Day report | Pass. All 9 results are available, with evidence. The saved-and-reopened report matches on 15 key values |
| Where to look next | Two areas: Corners 9–16 (0.619 s against Session 6 LAP 3), and the braking spread at Corners 5–6 (20.6 m across 21 laps) |
| Car and driver | Temperatures and heart rate per session. Heart rate runs from a mean of 118.6 bpm (Session 2) to 135.5 bpm (Session 5) |

## Sections as derived

For the owner to compare with RaceChrono's own lap list. "Not eligible"
laps are timed but left out of rankings and comparisons, with the reason
shown.

| Session | Section | Time | Note |
|---|---|---|---|
| 1 | OUT | 14:10.456 | |
| 1 | LAP 1 | 2:31.192 | |
| 1 | LAP 2 | 2:24.484 | |
| 1 | LAP 3 | 2:22.511 | |
| 1 | LAP 4 | 2:18.655 | |
| 1 | IN | 2:10.402 | |
| 2 | OUT | 14:18.070 | |
| 2 | LAP 1 | 2:21.790 | |
| 2 | LAP 2 | 2:25.232 | |
| 2 | LAP 3 | 2:23.241 | |
| 2 | LAP 4 | 2:28.310 | Not eligible: does not follow the supported route |
| 2 | IN | 2:16.157 | |
| 3 | OUT | 11:19.472 | |
| 3 | LAP 1 | 2:18.528 | |
| 3 | LAP 2 | 2:17.697 | |
| 3 | LAP 3 | 2:10.506 | |
| 3 | LAP 4 | 2:04.053 | |
| 3 | IN | 1:22.844 | |
| 4 | OUT | 7:11.478 | |
| 4 | LAP 1 | 2:04.258 | |
| 4 | LAP 2 | 2:03.137 | |
| 4 | LAP 3 | 2:04.206 | |
| 4 | LAP 4 | 1:55.923 | |
| 4 | LAP 5 | 1:53.277 | |
| 4 | IN | 2:49.519 | |
| 5 | OUT | 10:48.156 | |
| 5 | LAP 1 | 1:55.450 | |
| 5 | LAP 2 | 1:49.898 | Best of the day |
| 5 | LAP 3 | 1:52.585 | |
| 5 | LAP 4 | 2:12.555 | |
| 5 | LAP 5 | 2:08.979 | Not eligible: does not follow the supported route |
| 5 | IN | 2:14.177 | |
| 6 | OUT | 7:23.431 | |
| 6 | LAP 1 | 1:55.402 | |
| 6 | LAP 2 | 1:56.279 | |
| 6 | LAP 3 | 1:51.238 | |
| 6 | IN | 2:02.850 | |

Out laps are long because each recording starts before the car first
crosses the timing gate.

## For the owner

- Do the lap times match RaceChrono's lap list for the day?
- Did Session 2 LAP 4 and Session 5 LAP 5 include a pit entry, a detour or
  an off? That is what "does not follow the supported route" means.
- Do "Where to look next" (Corners 9–16; braking at Corners 5–6) match
  your own view of the day?
- If a session was also recorded as RCZ, import both to close the blocked
  alternative-grouping case.
