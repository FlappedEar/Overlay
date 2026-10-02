# FlappedEar Telemetry — event analysis delivery

Updated 2 October 2026. The complete scope is now maintained in
[product-vision.md](product-vision.md); actual status, blockers, milestones,
forecasts and acceptance live in [product-delivery.md](product-delivery.md).
This page is an entry point, not a competing roadmap.

## Foundation already implemented

- Bounded VBO/RCZ preparation, provenance, duplicates and cancellation.
- Version 3 Event/Run/source persistence, recovery, rebasing and explicit relinking.
- Multi-file transactional review, create/append and optional per-run video/sync.
- Whole-outing import and chronological section list with independent map/channel
  inspection: PR #11 merged at `a0122ab`, with passing PR/main CI.
- GPS-reference eligibility and quality/no-delta presentation: PR #12 merged at
  `7138fbd`, with passing PR/main CI; [KAN-5](https://kozucharkadiusz.atlassian.net/browse/KAN-5)
  is complete. Exact source and run links are in the delivery ledger.

The manual review workflow leaves source grouping explicit. Whole-outing import
can group a unique dated VBO/RCZ GPS match and use VBO for calculated G. These are
different entry points, documented in [batch-import.md](batch-import.md).
Neither combines raw channels across files. HR stays within the imported session.

Day-analysis compatibility is separate from duplicate/alternative source grouping.
By owner correction on 13 September 2026, repeated complete GPS routes and ordered
travel automatically establish supported layouts/directions. Import activates
available group results without per-run configuration. Manual correction remains
an override for ambiguous or incorrect matches. See
[the inference, persistence and invalidation contract](event-project-format.md#automatic-compatibility-groups-and-corrections-steps-012016).

## Video-free day results (KAN-27)

The Day results panel exposes `loading`, `empty`, `missing-source`, `error` and
`ready` states with individual run identities and messages. A partial day retains
available sections, rankings and progression while naming unavailable recordings.
Ready means sections are available; eligibility still depends on route, direction,
timing gates, source integrity and exclusions. Missing or failed sources are never
presented as an ordinary empty day.

After restoring a missing file, **Retry recordings** reruns the existing bounded,
cancellable worker and full-content identity checks. A different file at the same
path requires the existing explicit source verification/relink workflow. Retrying,
choosing a result and inspecting a lap do not select an editor run or change its
synchronization. Unaffected open details survive. Pending and failed run messages
share a bounded scroll area, including at the 760×480 analysis-window minimum.

## Independent A/B selections (KAN-29)

**Day results → Compare laps…** opens two separately owned selections. Choose
eligible complete laps from compatible runs, swap A/B, set the best lap of A's
run or its whole compatibility group as B, clear a slot or inspect either lap.
The selectors show the full run/lap label in a wide, wrapping dropdown. OUT/IN,
excluded laps, unresolved routes and route/GPS outliers are not candidates.

Each slot retains its own verified session, map and source-bound reference.
One cancellable worker serializes pair loading; rapid changes coalesce and stale
completions cannot overwrite a replacement or swapped slot. A missing or changed
recording fails only the affected slot. Metadata updates retain valid selections;
source/configuration changes and exclusions invalidate affected slots. Starting
another document clears the pair. Pair selection and inspection do not activate
an editor run, alter synchronization or dirty the saved project.

Shared track progress, distance delta, paired traces and charts, and saving the
A/B selection were added later in M2 (KAN-31 to KAN-41); see [testing](testing.md).
The single-lap inspector remains the evidence view for each selected lap.

## Shared comparison source budget (KAN-30)

A/B and the single-lap inspector share a **256 MiB decoded-source budget** and
one cancellable decode lock. Selecting another lap from a loaded source reuses
its immutable session; each lap still builds its own gap-preserving map range.
Up to two verified sources remain in the reuse cache. Sessions held by a slot,
inspector or worker remain charged after cache eviction until their last owner
releases them. Idle entries are evicted before another source is decoded.

Every request resolves the source again and checks its full content hash and
fingerprint, including cache hits. Keys also include the lap derivation revision,
algorithm and file format. A missing, changed, cancelled or superseded read cannot
replace the current selection. Budget rejection leaves the other slot intact;
clear an unused selection or close its inspector to make room, then select again.

This is a conservative limit for retained decoded sessions and the allowance
reserved by their decoder, not a process-RSS ceiling. VBO sample vectors and RCZ
expanded members are checked against the remaining allowance before large sample
allocation. Shared buffers are counted per channel, so admission can be stricter
than their physical size. Parser input/section/JSON/decompression scratch keeps
its existing separate hard limits; editor telemetry, day-summary derivation and
bounded per-lap map geometry are outside this source-cache budget.

## Product outcomes

Outcomes 1–4 are delivered; their milestone epics (M1–M4) were closed on
2 October 2026 after every task was done. Outcome 5 is next, after the
stabilisation epic KAN-144.

1. Day results (M1): [KAN-28](kan28-m1-acceptance.md), with local private-VBO evidence separate.
2. Independent cross-run A/B with shared track progress, delta and paired map/channels (M2): [KAN-42](kan42-m2-acceptance.md).
3. Reviewed corners/sectors, metrics and sector theoretical (M3): [KAN-58](kan58-m3-acceptance.md).
4. Ranked losses, consistency, G-G, available thermal/HR data and the day report (M4): [KAN-74](kan74-m4-acceptance.md).
5. Full Mac journey/overlay-export acceptance (M5), then remaining advanced F00–F20 work (M6).

A timed interval, usable spatial reference and comparable event lap are distinct.
GPS continuity alone cannot establish compatibility; repeated route geometry,
ordered traversal and compatible timing definitions are required. Conditions are
never inferred from the route or lap time.
Do not extend the old live nearest-segment overlay delta into cross-run analysis
without the alignment and eligibility gates in the product contract.

The [previous plan](history/2026-09-12-event-analysis-plan.md) is a historical
implementation record. Its old baseline, not-wired statements and beta scope are
not current instructions.
