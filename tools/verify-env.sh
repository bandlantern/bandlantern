#!/usr/bin/env bash
# Dev-environment check for the Mac and the Dell/WSL (docs/dev/WORKFLOW.md §4.1).
set -u
check() {
  local label="$1"; shift
  printf "%-14s" "$label"
  if command -v "$1" >/dev/null 2>&1; then "$@" 2>&1 | head -n1; else echo "MISSING"; fi
}
check gcc gcc --version
check clang clang --version
check cmake cmake --version
check ninja ninja --version
check ccache ccache --version
check git git --version
check gh gh --version
check gitleaks gitleaks version
check node node --version
check npm npm --version
check python python3 --version
check vcpkg vcpkg version
check nvidia-smi nvidia-smi -L
check rtl_tcp sh -c "command -v rtl_tcp"
printf "%-14s" "py venv"
if [ -x .venv/bin/python ]; then .venv/bin/python -c "import numpy, scipy, matplotlib, pytest; print('numpy', numpy.__version__, 'scipy', scipy.__version__)" 2>&1 | tail -n1; else echo "MISSING (.venv; see CLAUDE.md Commands)"; fi
check claude claude --version
echo "VCPKG_ROOT=${VCPKG_ROOT:-unset}"
echo "UTC clock: $(date -u)"
