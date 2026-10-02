# Flapped Ear Telemetry — product contract

Established 12 September 2026 from the owner's original analysis proposal and
subsequent decisions. Updated 2 October 2026 with the owner's decision to build two
applications (KAN-122). This document preserves the destination across agents and
implementation sessions. Implementation and execution status belong in
[product delivery](product-delivery.md), not in this vision.

## Product promise

Two applications share the telemetry core: a desktop video overlay
editor/generator for macOS and Windows (working name **Flapped Ear Overlays**),
developed in this repository, and **Flapped Ear Telemetry**, a track-day
performance analyzer for macOS, Windows, iOS and Android, to be developed in its
own repository (KAN-165). The analysis answers:

**Where did I lose time? What differed? What might explain it? What should I
focus on next?**

The central workflow is **import a day → see the important results → select a
loss → inspect its corner → check map, channels and optional video**. A lap list
and more charts are foundations, not completion of this promise.

## Durable decisions

- Product names: **Flapped Ear Telemetry** is the analysis app
  (`com.flappedear.telemetry`); the desktop editor is **Flapped Ear Overlays**
  (`com.flappedear.overlays`). The editor's first start under the new name moves
  existing preferences and data across without deleting anything (KAN-125). Existing internal
  identifiers may remain for compatibility; user-facing naming must be consistent.
- Keep this repository and its native Qt/C++/QML architecture for Flapped Ear
  Overlays. Flapped Ear Telemetry is a new Flutter app, started from a blank page in
  its own repository and Jira project; it shares no code with this repository and
  cross-references its behaviour, tests and documents (KAN-167;
  [architect handover](telemetry-handover.md)). The overlay editor stays as it is,
  with its day-analysis window, until Flapped Ear Telemetry is mature enough to
  replace it (KAN-166, deferred). Both apps read and write one compatible
  `.fetproject` format (KAN-170).
- Event → Run → Lap. An event groups the day; a run owns recording sources,
  optional video, sync and setup/conditions notes. Lap identity includes its run.
- Analyze across runs without concatenating paddock breaks. Recording time,
  video time and shared track progress are different coordinates.
- Video is optional. Heart rate is an imported telemetry channel, not a separate
  source workflow. Alternative files are not channel fusion.
- Prefer VBO when the user needs RaceChrono's exported calculated G channels.
  RCZ preserves native sensor clocks. Keep provenance and explain this choice.
- Platforms: the desktop editor targets macOS and Windows; Flapped Ear Telemetry
  targets macOS, Windows, iOS and Android. macOS remains the active development
  platform, and Windows builds and CI stay paused until the owner resumes them. No unsolicited framework
  replacement or silent scope reduction.

## Complete feature ledger

All original capabilities stay visible here even when implementation is staged.
Finishing an intermediate milestone does not close the full vision.
The [current capability audit](product-delivery.md#actual-capability-audit) maps
each ID below to its implementation state and concrete Jira work. The
[M0–M6 queue](product-delivery.md#dependency-ordered-delivery) contains 100 separate
Tasks plus seven milestone Epics. These links carry delivery status without
changing the product outcomes defined here.

| ID | Capability | Observable outcome |
| --- | --- | --- |
| F00 | Whole-day organization | Multi-file, folder and drop import; review duplicates/source groups; save/reopen event; run notes and conditions; reusable vehicle/track references |
| F01 | Lap comparison | Independent A/B from compatible runs, best run/day references, shared track-progress axis and delta time; incomplete laps remain inspectable |
| F02 | Track segmentation | Automatically proposed straights/corners and phases, then reviewed/editable braking zones, entry, apex and exit, with stable segment identity |
| F03 | Time Loss Analyzer | Rank non-overlapping observed losses; each result opens its evidence and affected corner/following straight |
| F04 | Optimal lap | Sector theoretical with source-lap provenance; separately validated realistic potential respects adjoining fragments' compatibility |
| F05 | Analytical map | Two traces, shared cursor, selected segment, speed/delta/G/pedal/temperature layers; no precision beyond GPS evidence |
| F06 | Corner Analyzer | Entry/minimum/apex/exit speeds, braking point/distance/deceleration, throttle pickup and section time versus reference |
| F07 | Coasting | Duration/distance and map locations; distinguish measured pedal inactivity from inferred driving state |
| F08 | Driving states | Acceleration, braking, cornering, coasting and overlaps, with documented prerequisites and inference labels |
| F09 | Trail braking | Measured or explicitly inferred braking/cornering overlap; compare duration/distance, without claiming more is always better |
| F10 | Consistency | Lap/sector/braking/exit/line variability, eligible sample population and spread; no unexplained percentage score |
| F11 | Progression | Within-day run progression and later comparable visits; conditions, traffic and setup changes stay visible |
| F12 | Vehicle health | Recorded engine/transmission/other temperatures, run extrema, thermal trends and recovery where actually recorded |
| F13 | Temperature/performance | Sample-backed correlations with lap time/acceleration; distinguish association from cause |
| F14 | G-G analysis | Lap sample scatter/comparison and lateral/braking/combined peaks; instantaneous G widget alone does not satisfy this |
| F15 | Video evidence | Bidirectional telemetry/video navigation, optional side-by-side lap video, continuous GoPro chapter timeline |
| F16 | Multiple data sources | Explicit clock alignment, per-channel provenance and conflict policy for actual fusion; future sensors do not block existing recorded channels |
| F17 | Driver physiology | Recorded HR summaries and lap/segment/run comparisons; HR is not labelled stress or confidence |
| F18 | Automatic report | Best lap, theoretical potential, consistency, principal losses, progress and available health/HR observations, all with click-through evidence |
| F19 | Explain this lap | Explain computed differences and their limitations; language generation never calculates or invents the underlying metrics |
| F20 | Overlay editor/generator | Edit/share/save scene; preview and export from chosen run using the same renderer; dependable cancellation, recovery and supported output |

## Numerical and product rules

- Measured, calculated, inferred and unavailable values are distinguishable.
  No missing brake sensor is replaced with fabricated brake pressure.
- A start-to-start timed interval can be displayed while excluded from reference
  comparison because GPS is incomplete. Eligibility is not a guarantee of matching
  layout, dry conditions, clear traffic or representative driver performance.
- Compatible layout/direction/gate and user exclusions govern event rankings and
  potential. Nearest GPS point or equal percentage of lap length alone is not a
  valid alignment at crossings or materially different racing lines.
- Delta convention: A elapsed time minus B elapsed time at corresponding track
  progress; positive means A is slower. Never sum overlapping loss windows.
- Potential figures are estimates tied to an explicit algorithm and observed
  fragments. A sum of best sectors is not automatically realistically achievable.
- No connecting missing GPS segments, filling unrecorded thermal breaks, claiming
  available-grip percentage from G alone, or causal coaching from correlation.

## Definition of done

The first complete core workflow includes the day model, quality-aware ranking,
A/B distance comparison, reviewed sectors, Corner Analyzer, sector theoretical,
ranked losses, consistency, G-G and available thermal/HR report, plus the existing
editor/export. It must pass an end-to-end macOS walkthrough with the owner's
full-day files and matching video, then save/reopen without losing decisions.

Advanced realistic potential, expanded driving-state/trail/coasting analysis,
multi-event history, actual channel fusion, multi-chapter/side-by-side video and
richer explanations remain explicit subsequent acceptance items. They are part
of the full vision, not discarded ideas. The coordinator must report both core
workflow completion and full-vision completion separately.

## Coordination contract

The owner delegates implementation coordination to the primary agent and bounded
subagents. Read current code and open PRs before assigning work; do not recreate
pending implementation. Assign non-overlapping ownership and integrate centrally.
Keep focused commits, evidence and this ledger current. Publish development PRs
and merge after required CI passes under standing authorization. Release/tagging
and public distribution remain separate from development merge authorization.
