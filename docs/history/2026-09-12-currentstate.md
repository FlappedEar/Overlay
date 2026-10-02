# Historical checkpoint — 12 September 2026

Archived on 2 October 2026 (KAN-159). This was the root `currentstate.md`. The text below is retained as historical evidence; it does not describe the current implementation, CI or milestone status. Current status is in the [delivery ledger](../product-delivery.md); end-user limitations are in the [user guide](../user-guide/pages/limitations.html).

# Flapped Ear Telemetry — current state

Updated 12 September 2026. Version remains 0.2.0. **Development; full track-day product incomplete.**

The authoritative scope is [product vision](../product-vision.md); the current
code audit, blockers, milestones, estimates and acceptance record are in
[product delivery](../product-delivery.md). These documents supersede the
historical single-session beta scope as the definition of the requested product.

## Integrated baseline

Reconciled for [KAN-12](https://kozucharkadiusz.atlassian.net/browse/KAN-12) against
`main` at [`7138fbd511e3d06ed9b237130d385e1f62bde027`](https://github.com/arekkozuch/VBOOverlay/commit/7138fbd511e3d06ed9b237130d385e1f62bde027).
This dated snapshot includes the native overlay editor/export, VBO/RCZ parsers,
source-defined lap timing, v3 Event → Run → Lap/source ownership, transactional
multi-file review, active-run selection, no-video lap listing and corrected
RaceChrono calculated-G selection/presentation.

[PR #11](https://github.com/arekkozuch/VBOOverlay/pull/11), merged at `a0122ab`,
provides whole-outing import, dated GPS-evidence pairing, chronological OUT/LAP/IN
sections and independent selected-section map/charts/cursor. Its native-QPA fix
retained rendering/input assertions and passed the PR and main CI gates.

[PR #12](https://github.com/arekkozuch/VBOOverlay/pull/12), merged at `7138fbd`,
excludes incomplete GPS laps from reference ranking while retaining measured
timings, exposes quality/best-of-run information, and corrects export-log
retention for production UUIDs. [KAN-5](https://kozucharkadiusz.atlassian.net/browse/KAN-5)
is complete: both [PR CI](https://github.com/arekkozuch/VBOOverlay/actions/runs/34715062783)
and [integrated-main CI](https://github.com/arekkozuch/VBOOverlay/actions/runs/34715471586)
passed macOS arm64 and Windows x64, Debug/Release, Qt 6.8.3. Each job passed all
seven CTest registrations; Release packaging and Windows NSIS checks also passed.

The [delivery ledger](../product-delivery.md#actual-capability-audit) records
all 21 F00–F20 states and Jira references. The queue has 100 individual Tasks
plus seven milestone Epics. Live status and subsequent task PR/merge/CI evidence
belong in Jira; this snapshot does not predeclare later tasks complete.

## Still needed for the core product

Compatible event ranking and progression, metadata/exclusions, independent A/B
track-progress comparison, dual traces/delta, reviewed corners/sectors, Corner
Analyzer, sector theoretical, consistency, G-G, ranked losses and the automatic
evidence-linked report. Raw OBD/HR charts do not yet constitute vehicle/driver
analysis. The full original vision remains itemized as F00–F20.

The editor/export foundation should be preserved. Remaining hardening includes
slow/full destinations.

## Subsequent task changes

[KAN-18](https://kozucharkadiusz.atlassian.net/browse/KAN-18) standardizes the
Flapped Ear Telemetry display, About, bundle and candidate names while retaining
existing settings, recovery and installation identities. See [application identity
and upgrades](../application-identity.md). macOS startup and preservation
regressions cover the change; exact PR/main CI evidence belongs in Jira. Windows
name edits are static only while Windows execution remains paused.

[KAN-12](https://kozucharkadiusz.atlassian.net/browse/KAN-12) reconciled this
baseline and the 100-task backlog in PR #13, merged at `a2fde85`.
[KAN-13](https://kozucharkadiusz.atlassian.net/browse/KAN-13) / PR #14 adds bounded
shutdown of export descendants after their leader exits. The controller waits
for all writers before cleanup, rejects reported success with surviving writers,
and keeps the export active while shutdown is unconfirmed. Startup recovery also
retains manifests for surviving Unix groups. Exact PR/main CI and integration
results are recorded in KAN-13; physical/private-media acceptance stays separate.

[KAN-14](https://kozucharkadiusz.atlassian.net/browse/KAN-14) / PR #15 replaces
bulk line and field splitting with bounded scanning and cancellation checks.
It avoids a joined header allocation, retains only declared data fields, and
validates ignored extra fields while preserving their warning counts. Existing
size/count limits and timestamp calculations are unchanged. Synthetic boundary,
format and cancellation regressions accompany the change; exact CI and merge
evidence belongs in KAN-14.

[KAN-15](https://kozucharkadiusz.atlassian.net/browse/KAN-15) validates VBO absolute
and derived times against finite, signed 64-bit microsecond conversion bounds
before publication. Rollover, elapsed time, duration and UTC chronology are checked;
unsafe numeric ranges reject the parse. Chart sampling checks its derived range
and integer bucket conversion. Boundary and mixed-format regressions accompany
the changes; final PR/main CI evidence is recorded in Jira.

[KAN-16](https://kozucharkadiusz.atlassian.net/browse/KAN-16) replaces coordinate
magnitude guessing with a single explicit unit decision for GPS samples and gates.
The verified RaceChrono Pro 10.2.4 marker means signed total arc-minutes; custom
exports can declare degrees or arc-minutes through the documented FlappedEar
header extension. Missing, unsupported or conflicting evidence withholds GPS and
gates with a warning while retaining other valid telemetry. Track geometry and
its current marker consume validated degrees. Synthetic regressions cover all
quadrants, zero crossings, mixed axis magnitudes, conflicting evidence and bounds.
Existing lap/pairing fixtures retain their assertions with explicit source units.
Exact PR/main macOS Debug and Release CI evidence belongs in Jira; no new Windows,
private-recording or physical-hardware verification is claimed.

[KAN-17](https://kozucharkadiusz.atlassian.net/browse/KAN-17) adds checked optional
forward/inverse synchronization times at preview, analysis and export boundaries.
Overflow becomes explicit no data; valid finite project settings remain preserved.
Auto-sync validates channels before access and uses bounded integer grids with
source/grid/work budgets, avoiding non-advancing floating loops. Invalid confidence
or transforms cannot auto-apply. Thirty-three new cases include an ambiguous engine
result preserving confirmed controller settings. Exact macOS PR/main CI evidence
is recorded in Jira; Windows and private/hardware acceptance remain separate.

## Evidence boundaries

Historical development results are preserved in
[the pre-audit checkpoint](2026-09-12-before-product-audit.md) and
[the September 1 checkpoint](2026-09-01-currentstate.md).
PR #11 records local Qt 6.11.1 tests and private telemetry-only UI checks. Those
earlier results do not certify a newly built candidate. This coordinator workspace
lacks native CMake/Qt and cannot claim local native execution; the baseline build
and synthetic test evidence above comes from CI.

Current CI covers macOS arm64 Debug/Release synthetic tests and internal candidates.
Windows builds, tests and installer validation are paused by owner direction on
13 September 2026 until explicitly resumed; prior Windows results are historical.
It does not certify the exact installed Mac candidate on the owner's full-day
telemetry and matching GoPro media. [Candidate acceptance](../beta-acceptance.md)
remains required, tracked through [M5](https://kozucharkadiusz.atlassian.net/browse/KAN-10)
and [M6](https://kozucharkadiusz.atlassian.net/browse/KAN-11). No release is approved
by these implementation changes. The owner's local Codex can use the
[update/build/test handoff](../development-workflow.md#local-codex-update-compile-and-test-current-main).
