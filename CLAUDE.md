# CLAUDE.md

Read [AGENTS.md](AGENTS.md) first: it holds the engineering rules, safety invariants and the
required build/test gate. Then read [handover.md](handover.md) for current state and traps,
and check `origin/main` and Jira project KAN before starting; other sessions may have moved on.

- Status records: [docs/product-delivery.md](docs/product-delivery.md) and Jira KAN.
- Editor look: FlappedEar Telemetry's design language. Editor QML takes colours, fonts and
  corners from `native/qml/Theme.js` and the `Fe*` controls ([docs/ui-theme.md](docs/ui-theme.md)).
- Private sample recordings: `FlappedEar/refdata` (read-only for agents: never push, branch or
  open PRs there). It holds the owner's Jastrząb day of 29 August 2026, six RaceChrono sessions
  each as `.vbo` and `.rcz`; expected results are in
  [docs/kan79-full-day-acceptance.md](docs/kan79-full-day-acceptance.md) (25 timed laps, best
  1:49.898). Point `FLAPPEDEAR_REAL_DAY` at a checkout to run the opt-in real-day tests, and
  pair one session for the RCZ check ([docs/testing.md](docs/testing.md) lists every variable):

  ```bash
  unzip -p session_X.rcz session.json > /tmp/session.json
  FLAPPEDEAR_REAL_RCZ=$PWD/session_X.rcz FLAPPEDEAR_RCZ_REFERENCE_VBO=$PWD/session_X.vbo \
  FLAPPEDEAR_RCZ_REFERENCE_SESSION_JSON=/tmp/session.json \
    ./build-native/native/tests/flappedear_rcz_tests optionalPrivatePair
  ```

## Building in a cloud container

Cloud containers have CMake, Ninja, GCC and FFmpeg but no Qt, and `download.qt.io` is not
reachable. In Claude Code cloud sessions the SessionStart hook
(`.claude/hooks/session-start.sh`) installs Qt 6.8.3 (the CI version) from conda-forge and sets
`CMAKE_PREFIX_PATH`; then configure with `cmake -S . -B build-native -G Ninja
-DCMAKE_BUILD_TYPE=Debug`. Elsewhere, the manual equivalent is:

```bash
curl -sSL https://conda.anaconda.org/conda-forge/linux-64/micromamba-2.9.0-0.tar.bz2 | tar xj -C "$HOME" bin/micromamba
MAMBA_ROOT_PREFIX=$HOME/mamba "$HOME/bin/micromamba" create -y -p "$HOME/qtenv" -c conda-forge \
  "qt6-main=6.8.3" "qt6-multimedia=6.8.3" libgl-devel libegl-devel libopengl-devel
cmake -S . -B build-native -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$HOME/qtenv"
cmake --build build-native --parallel
```

`flappedear_native_tests` needs an OpenGL context: install `xvfb` and `libgl1-mesa-dri`, then run
CTest under `xvfb-run -a` with `QT_QPA_PLATFORM=xcb QSG_RHI_BACKEND=opengl`. Linux is not a
supported platform: report its results separately from macOS CI. Export tests need FFmpeg 8.1 or
newer (`setparams` with `alpha_mode`; Ubuntu 24.04's FFmpeg 6.1 lacks it), and tests that
make folders unwritable fail when run as root.
