# AGENTS.md

## Engineering rules

- Follow the [task delivery workflow](docs/development-workflow.md): one active Jira
  task, a focused commit and recorded applicable build/test evidence before marking
  implementation complete. Cloud CI is enabled for macOS
  Debug and Release; verify the exact PR head and resulting main revision.
  All Jira content must be in English.
- Treat Qt 6, C++, and QML as the only production application architecture.
- Preserve the native `.fetproject` schema and behavioral test fixtures.
- Use C++20, Qt Test, CMake targets, compiler warnings, and explicit ownership in native code.
- Model multiple GoPro chunks as one ordered, time-based media timeline; never reset telemetry time
  at a clip boundary.
- Do not rewrite working architecture without a concrete reason.
- Keep telemetry parsing independent from QML and widget rendering.
- Keep video and media code independent from widget UI.
- Keep synchronization in one central telemetry module.
- Heart Rate comes from the imported VBO/RCZ session; do not add a separate HR source.
- Never invent brake telemetry or substitute another channel silently.
- Never couple telemetry to FPS. All synchronization is time based.
- Preview and export should share scene definitions and rendering logic.
- Always test parser changes, including malformed input.
- Always test synchronization changes with deterministic and ambiguous signals.
- Do not commit API keys, secrets, user paths, generated video, or build artifacts.
- Finish every completed implementation or fix with a focused local commit and a descriptive commit
  message. In ChatGPT Work/shared-repository sessions, push each completed commit when the user has
  authorized automatic publication for that session. A locally run Codex must not push or synchronize
  unless the user explicitly asks it to do so.
- Add no unrelated dependencies. Verify current versions, compatibility, maintenance, and licenses first.
- Do not implement roadmap features unless requested.
- Do not claim functionality works without running the relevant command or integration fixture.
- Preserve ordinary video import when GoPro metadata is absent or malformed.
- Treat VFR as a timing concern: warn about input cadence and preserve the validated CFR conversion policy.
- Documentation is part of every iteration.

## Safety and correctness invariants

- Never write FFmpeg directly to a user-selected export target. Only transaction-owned temporary paths
  may be deleted automatically, and an existing target requires explicit overwrite consent.
- Project saves remain atomic. New, open, and quit actions must respect dirty state, and project open
  document validation/commit must remain transactional even though external sources resolve afterward.
- A project document and its external assets are separate. Missing or moved media must not prevent a
  valid project document from opening. Relative source references are preferred where portable, and a
  source must not be silently accepted solely because a pathname matches.
- The saved `.fetproject` is the authoritative clean document state. Recovery data is separate,
  represents unsaved changes, and must never be silently marked clean. Discard removes unsaved
  recovery state rather than persisting it as the next clean session. A recovery file is offered only
  when its logical document state is newer than the authoritative saved state; failed deletion of a
  stale snapshot after Save is cleanup debt, not degraded data protection or a user warning.
- Source and project async results must be guarded by generation and source identity. Stale results
  must never mutate committed state.
- Long-running source parsing, media probing, and synchronization operations must be cooperatively
  cancellable. Source-generation checks prevent stale commits; cancellation prevents wasted work.
  Both are required.
- Untrusted/imported telemetry and media metadata must be resource-bounded before large allocation
  or recursion.
- External JSON documents and subprocess outputs must be resource-bounded before they can cause
  large application allocations. Failure to persist automatic recovery must be surfaced as degraded
  data protection and retried safely.
- Parser output timestamps must remain strictly monotonic. Public telemetry boundaries must not expose
  `NaN` or infinity; outside-range and missing telemetry are no data, and missing gaps are not bridged.
- Static telemetry geometry must not be rebuilt or repainted on playback-time updates. Dynamic
  playback markers must update independently from static geometry.
- At the supported 1180×720 editor minimum, every sidebar control must remain reachable through a
  single coherent vertical scroll surface; never strand controls below a fixed nested scroller.
- Very Verbose diagnostics must preserve the absolute historical viewport while detached from tail;
  only an explicit jump-to-latest may resume following new output.
- Playback transport shortcuts must be centrally disabled while a text or numeric editor is active.
- Analysis chart rows must distinguish an empty overlapping range from a rendering/data-shape failure;
  series transport must retain segment nesting so telemetry gaps remain disconnected.
- Preserve the staged, frame-correct telemetry-overlay export architecture unless evidence establishes a
  safer replacement. Do not restore the unsafe live-overlay FFmpeg approach.
- Qt Quick offscreen frames use an explicit premultiplied-alpha contract from QRhi readback through
  Stage A. For 10-bit YUV Stage B, explicitly unpremultiply the staged BGRA overlay and mark it
  straight before straight-alpha composition; premultiplied YUV blending corrupts transparent chroma
  offsets. Do not rely on implicit alpha interpretation.
- Offscreen export must dispatch pending component initialization, complete one scene-graph preparation
  frame, and dispatch its completion events before frame zero. This is the deterministic readiness
  boundary for asynchronous visual primitives such as QML Canvas; never replace it with sleeps.
- Raw-frame transport into encoder processes uses bounded byte-oriented backpressure. Never assume
  `QProcess` can buffer complete raw frames. Partial, rejected, or timed-out writes must never be
  treated as successful frame submission.
- Final exports are CFR at one authoritative rational export rate. Preserve that exact rate across
  overlay staging, source conversion, progress, and validation; do not reconstruct it from doubles.
- Authoritative export ranges are inclusive integer frame addresses. Decimal/container duration must
  never determine scheduled frame count; C++ owns SMPTE IN/OUT parsing and formatting.
- Preview widget geometry is derived from loaded media display geometry, never from transient
  `VideoOutput` decoded-frame state. User-accessible playback end is the last actual video frame,
  not the media-duration boundary after it.
- Current owner direction (2026-10-02): two applications share the telemetry core. This
  repository delivers the desktop overlay editor, Flapped Ear Overlays (working name), for macOS
  and Windows, and owns the shared core. Flapped Ear Telemetry (macOS, Windows, iOS and Android)
  will be developed in a separate repository once Overlays is standalone (Jira epic KAN-165); its
  technology and how it consumes the core are undecided (KAN-167). Do not add Telemetry or mobile
  app targets to this repository. The desktop app is Flapped Ear Overlays (`com.flappedear.overlays`,
  KAN-125). Keep it as it is: do not remove its Lap Analysis window, analysis workflows or other
  features until Flapped Ear Telemetry is mature enough to replace them (KAN-166, deferred). Both
  apps read and write one `.fetproject` format, which must stay compatible between them (KAN-170).
  Keep `flappedear_telemetry_core` and `flappedear_telemetry_app`
  free of Gui, overlay, video and platform-specific code so that the Telemetry app can reuse them.
  macOS remains the active development platform. The 2026-09-13 pause still applies: do not
  start Windows builds, tests, packaging or installer validation until the owner explicitly
  resumes them. This supersedes older dual-platform gates in Jira and
  historical documentation. The owner
  resumed macOS Cloud CI after the quota pause. Run the applicable local macOS
  build/test gate and hosted Debug/Release validation for published changes. Report
  real-media and hardware-encoder validation separately. An explicit
  cloud hardware-test skip must not disable QRhi rendering assertions or software FFmpeg integrations.
- QRhi/GuiPrivate use is version-sensitive. A Qt upgrade requires explicit local render/export smoke
  validation.
- Source raster, bit depth, and color characteristics are data, not presets. Never impose a product-level
  4K ceiling; runtime renderer, encoder, and resource capability determine native-export support.
- Ten-bit is not synonymous with HDR. Unsupported HDR/Log material must never be silently converted to
  8-bit SDR or declared preserved by metadata copying alone.

## Required validation

Before handing off a change, run as applicable:

```bash
cmake --build build-native --parallel
ctest --test-dir build-native --output-on-failure
```

Real VBO and GoPro integration results must be reported separately from synthetic tests.
