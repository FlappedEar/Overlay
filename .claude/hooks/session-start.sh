#!/bin/bash
# Claude Code cloud sessions only: install Qt 6.8.3 (the CI version) from
# conda-forge so the native build and tests run. download.qt.io is not
# reachable from cloud containers; conda-forge is. Idempotent.
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

ROOT="${FLAPPEDEAR_TOOLS_DIR:-$HOME/.cache/flappedear}"
QT_ENV="$ROOT/qtenv"
MAMBA="$ROOT/bin/micromamba"
export MAMBA_ROOT_PREFIX="$ROOT/mamba"

mkdir -p "$ROOT"
if [ ! -x "$MAMBA" ]; then
  curl -sSL https://conda.anaconda.org/conda-forge/linux-64/micromamba-2.9.0-0.tar.bz2 \
    | tar xj -C "$ROOT" bin/micromamba
fi

# Qt Gui needs the OpenGL/EGL development files, which the container lacks.
if [ ! -f "$QT_ENV/lib/cmake/Qt6/Qt6Config.cmake" ]; then
  "$MAMBA" create -y -q -p "$QT_ENV" -c conda-forge \
    "qt6-main=6.8.3" "qt6-multimedia=6.8.3" libgl-devel libegl-devel libopengl-devel
fi

if [ -n "${CLAUDE_ENV_FILE:-}" ]; then
  echo "export CMAKE_PREFIX_PATH=\"$QT_ENV\"" >> "$CLAUDE_ENV_FILE"
  echo "export QT_QPA_PLATFORM=offscreen" >> "$CLAUDE_ENV_FILE"
fi
