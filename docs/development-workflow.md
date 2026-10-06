# Task delivery and acceptance

## Current owner direction — 13 September 2026

The owner has resumed Cloud CI after the quota pause. Native CI runs on pull
requests, pushes to `main`, and manual dispatch, with macOS Debug and Release
builds/tests, Release deployment/startup validation, Linux ASan+UBSan and TSan jobs, a Linux clang-tidy job and a Linux parser fuzz job. Verify successful runs
for the exact published PR head and resulting main revision; earlier task CI
results are not evidence for new code.
Since 2 October 2026, Windows code changes are resumed for defects found by the
owner's local Windows validation ([record](windows-validation-2026-10-02.md)).
Windows CI, packaging and installer validation remain paused. Development
targets macOS.

## One implementation task at a time

The owner has authorized coordinator implementation again. For shared-repository
work, implement the requested task, open a focused PR and validate it through
macOS CI. If the local native toolchain is unavailable, say so; hosted CI supplies
the synthetic build/test gate, not private-media or owner-operated acceptance.

When the owner requests local Codex execution, use the following handoff.

The coordinator prepares one self-contained English implementation prompt for the
next requested task in the ordered 100-task Jira backlog. The local Codex session
implements and verifies it on the owner's Mac. The prompt must contain:

- Jira key, goal, acceptance criteria and dependencies checked against current code.
- Relevant source paths, bounded scope and the applicable AGENTS.md invariants.
- A safe starting point: inspect branch/status/history and preserve unrelated work.
- Implementation, regression coverage and documentation requirements.
- Exact applicable local configure/build/test commands and an interactive scenario.
- A focused local commit and a report of SHA, commands, results, skips and limitations.

Do not replace implementation instructions with an instruction to start Cloud CI.
Do not claim completion from a prompt or unexecuted commands. If the local native
toolchain is unavailable, report what is missing and leave validation outstanding.

Keep one implementation task active. Review the local Codex result and actual
changes before updating Jira. A code task is complete only when its acceptance
criteria and applicable build/tests pass. Run Debug application/tests as
the default local gate; also validate Release and deployed startup for packaging,
release-sensitive changes or when the task requires them. Keep private-media,
physical hardware/encoder and interactive checks separate from synthetic tests.

Local Codex must not push, open a PR, merge or publish unless the owner explicitly
requests it. When publication is authorized, use a focused PR and record applicable
verification for its exact head and wait for its macOS CI jobs to pass before
merging. After integration, verify CI on the resulting main revision. Local
real-media and interactive acceptance remain separate; never substitute old CI results.
Since 4 October 2026 `main` is protected by a branch ruleset: changes land only
through a pull request, and the `macOS arm64 / Debug / Qt 6.8.3` and
`macOS arm64 / Release / Qt 6.8.3` checks must pass first. No approval is required
(single maintainer); force pushes and branch deletion are blocked.
Since 5 October 2026 Native CI also runs `Linux x64 / ASan+UBSan / Qt 6.8.3`
(KAN-154), the GUI-free tests under AddressSanitizer and
UndefinedBehaviorSanitizer. The owner made it a required check the same day.
On pull requests Native CI also runs `Linux x64 / clang-tidy / Qt 6.8.3` (KAN-154)
on the changed C++ files, with the checks in `.clang-tidy`; bugprone and
analyzer findings fail it. It is not a required check.
Native CI also runs `Linux x64 / Fuzz parsers / Qt 6.8.3` (KAN-155): 60 seconds of libFuzzer
on each of the VBO, RCZ and GPMF decoders from the seed corpora, with inputs up
to 1 MiB. It is not a required check.
Since 6 October 2026 Native CI also runs `Linux x64 / TSan / Qt 6.8.3` (KAN-219):
the GUI-free tests except the telemetry-app suite under ThreadSanitizer, with
the Qt suppressions in `.github/tsan-suppressions.txt`. It is not a required
check. The clang-tidy job also checks changed headers through the `.cpp` files
that include them.
The macOS jobs fail when Homebrew's FFmpeg is outside the tested range, 8.1 to
9.x (KAN-154); widening it is a reviewed change to `.github/workflows/build.yml`
after the export tests pass on the new version.
All Jira content remains in English. Record the implementation SHA, local results,
limitations and any authorized PR/merge links. No public releases or tags are
implied. Historical CI wording elsewhere does not override this workflow.

## Local Codex: update, compile and test current main

When the owner asks the local Codex session to update, build and test the current
version:

1. Read `AGENTS.md` and inspect `git status --short --branch`. Preserve dirty
   work, local commits and the current branch. Do not reset, clean or stash work
   automatically. Use a separate checkout if the current worktree cannot be
   safely updated.
2. Fetch `origin` and identify `origin/main`. In a clean checkout already on
   `main`, use a fast-forward-only update. If local history has diverged, report
   it instead of rewriting it. Do not push from the local session unless asked.
3. Record `git rev-parse HEAD`, Qt and compiler versions. Configure using the
   existing [build instructions](../README.md#build); preserve a valid local
   toolchain rather than assuming a hosted runner's installation path.
4. Run the application build and test suite:

   ```bash
   cmake --build build-native --parallel
   ctest --test-dir build-native --output-on-failure
   ```

5. Launch the built application for the owner. Report the tested SHA, commands,
   results and skipped cases. Use the task's acceptance scenario for interactive
   checks; only use real recordings the owner has made available locally.

For the documented Apple Silicon/Homebrew setup, configuration and launch are:

```bash
cmake -S . -B build-native \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
open "build-native/native/FlappedEar Overlays.app"
```

Run the build and tests between configuration and launch. If a different Qt
installation or generator is already configured, follow the repository's build
instructions for that environment and report the actual commands used.
