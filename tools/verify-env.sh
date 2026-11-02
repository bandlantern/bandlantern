#!/usr/bin/env bash
# WSL dev-environment check (docs/dev/setup-dell.md, Appendix A).
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
check claude claude --version
echo "VCPKG_ROOT=${VCPKG_ROOT:-unset}"
echo "UTC clock: $(date -u)"
