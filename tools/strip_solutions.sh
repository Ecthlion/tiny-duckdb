#!/usr/bin/env bash
# Generate a student edition from the answer edition.
#
# Usage: bash tools/strip_solutions.sh [output_dir]
# Default output: ../tiny-duckdb-student
set -euo pipefail

SRC_DIR="$(cd "$(dirname "$0")/.." && pwd -P)"
OUT_DIR_INPUT="${1:-$(dirname "$SRC_DIR")/tiny-duckdb-student}"
OUT_DIR="$(python3 -c 'import os, sys; print(os.path.abspath(sys.argv[1]))' "$OUT_DIR_INPUT")"

if [[ -z "$OUT_DIR" || "$OUT_DIR" == "/" || "$OUT_DIR" == "$SRC_DIR" ]]; then
	echo "refusing unsafe output directory: $OUT_DIR" >&2
	exit 2
fi
case "$OUT_DIR/" in
	"$SRC_DIR/"*)
		echo "output directory must be outside the source repository: $OUT_DIR" >&2
		exit 2
		;;
esac
case "$SRC_DIR/" in
	"$OUT_DIR/"*)
		echo "output directory must not contain the source repository: $OUT_DIR" >&2
		exit 2
		;;
esac

rm -rf -- "$OUT_DIR"
mkdir -p "$OUT_DIR"

# Copy tracked and non-ignored working-tree files only. Build products, virtual
# environments, caches, and .git internals never enter the student archive.
while IFS= read -r -d '' file; do
	mkdir -p "$OUT_DIR/$(dirname "$file")"
	cp "$SRC_DIR/$file" "$OUT_DIR/$file"
done < <(git -C "$SRC_DIR" ls-files --cached --others --exclude-standard -z)

strip_output="$(python3 "$OUT_DIR/tools/strip_solutions_inplace.py")"
echo "$strip_output"
printf 'student\n' >"$OUT_DIR/.tiny-duckdb-edition"

if grep -R -n '\[SOLUTION BEGIN\|\[SOLUTION END\]' "$OUT_DIR/src" "$OUT_DIR/lab4_lakebase" >/dev/null; then
	echo "student generation failed: solution markers remain" >&2
	exit 1
fi

echo "student edition written to $OUT_DIR"
