#!/usr/bin/env bash

set -euo pipefail

configuration="${1:-Debug}"
skip_tests="${2:-}"

case "${configuration}" in
  Debug|debug)
    preset="linux-debug"
    ;;
  Release|release)
    preset="linux-release"
    ;;
  *)
    echo "Usage: ./scripts/build.sh [Debug|Release] [--skip-tests]" >&2
    exit 2
    ;;
esac

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repository_root}"

cmake --preset "${preset}"
cmake --build --preset "${preset}" --parallel

if [[ "${skip_tests}" != "--skip-tests" ]]; then
  ctest --preset "${preset}"
fi
