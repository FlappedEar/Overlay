# CLAUDE.md

Read [AGENTS.md](AGENTS.md) first: it holds the engineering rules, safety invariants and the
required build/test gate. Then read [handover.md](handover.md) for current state and traps,
and check `origin/main` and Jira project KAN before starting; other sessions may have moved on.

- Status records: [docs/product-delivery.md](docs/product-delivery.md) and Jira KAN.
- Private sample recordings: `FlappedEar/refdata`. Point `FLAPPEDEAR_REAL_DAY` at a checkout
  to run the opt-in real-day tests ([docs/testing.md](docs/testing.md)).

## Building in a cloud container

Cloud containers have CMake, Ninja, GCC and FFmpeg but no Qt, and `download.qt.io` is not
reachable. Qt 6.8.3 (the CI version) installs from conda-forge:

```bash
curl -sSL https://conda.anaconda.org/conda-forge/linux-64/micromamba-2.9.0-0.tar.bz2 | tar xj -C "$HOME" bin/micromamba
MAMBA_ROOT_PREFIX=$HOME/mamba "$HOME/bin/micromamba" create -y -p "$HOME/qtenv" -c conda-forge \
  "qt6-main=6.8.3" "qt6-multimedia=6.8.3"
cmake -S . -B build-native -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$HOME/qtenv"
cmake --build build-native --parallel
```

`flappedear_native_tests` needs an OpenGL context: install `xvfb` and `libgl1-mesa-dri`, then run
CTest under `xvfb-run -a` with `QT_QPA_PLATFORM=xcb QSG_RHI_BACKEND=opengl`. Linux is not a
supported platform: report its results separately from macOS CI. Export tests need an FFmpeg
whose `setparams` filter has `alpha_mode` (Ubuntu 24.04's FFmpeg 6.1 does not), and tests that
make folders unwritable fail when run as root.
