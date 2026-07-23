#!/usr/bin/env bash
set -euo pipefail

if ! command -v clang-format >/dev/null 2>&1; then
	echo "clang-format not found in PATH" >&2
	exit 2
fi

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

readarray -t files < <(git ls-files '*.cpp' '*.hpp' '*.h')
if [[ ${#files[@]} -eq 0 ]]; then
	exit 0
fi

clang-format --dry-run --Werror "${files[@]}"
