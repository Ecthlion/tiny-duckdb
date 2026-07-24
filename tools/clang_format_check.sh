#!/usr/bin/env bash
set -euo pipefail

if ! command -v clang-format >/dev/null 2>&1; then
	echo "clang-format not found in PATH" >&2
	exit 2
fi

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

files="$(git ls-files | grep -E '\.(cpp|hpp|h)$' | grep -v '^third_party/' || true)"
if [[ -z "${files}" ]]; then
	exit 0
fi

echo "${files}" | xargs clang-format --dry-run --Werror
