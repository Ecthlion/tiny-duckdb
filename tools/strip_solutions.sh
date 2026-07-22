#!/usr/bin/env bash
# strip_solutions.sh - produce the STUDENT edition of tiny-duckdb.
#
# Removes everything between
#   // [SOLUTION BEGIN Lx.Ty]   (C++)   or   # [SOLUTION BEGIN Lx.Ty] (Python)
# and the matching
#   // [SOLUTION END]           (C++)   or   # [SOLUTION END]         (Python)
# replacing it with a NotImplementedException stub (C++) or a `raise` (Python),
# so students get a compiling skeleton with tasks to fill in.
#
# Usage: ./tools/strip_solutions.sh [output_dir]   (default: ../tiny-duckdb-student)
set -euo pipefail

SRC_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="${1:-$(dirname "$SRC_DIR")/tiny-duckdb-student}"

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

# copy everything except build artifacts and VCS internals
(cd "$SRC_DIR" && find . -type f \
	-not -path './build/*' -not -path './.git/*' \
	-not -name '*.o' -not -name '*.a' \
	-not -name 'tdbtest' -not -name 'tiny_duckdb_shell' \
	-not -name '__pycache__' -print | while read -r f; do
	mkdir -p "$OUT_DIR/$(dirname "$f")"
	cp "$f" "$OUT_DIR/$f"
done)

python3 - "$OUT_DIR" << 'PYEOF'
import os
import re
import sys

root = sys.argv[1]

CPP_RE = re.compile(
    r"(?P<indent>[ \t]*)// \[SOLUTION BEGIN (?P<task>L\d+\.T\d+(?:-T\d+)?)\]\n.*?\n[ \t]*// \[SOLUTION END\]",
    re.DOTALL,
)
PY_RE = re.compile(
    r"(?P<indent>[ \t]*)# \[SOLUTION BEGIN (?P<task>L\d+\.T\d+(?:-T\d+)?)\]\n.*?\n[ \t]*# \[SOLUTION END\]",
    re.DOTALL,
)


def cpp_stub(match):
    indent = match.group("indent")
    task = match.group("task")
    return ("{}// TODO({}): implement this (see the corresponding docs/labN.md)\n"
            "{}throw NotImplementedException(\"task {} not implemented yet\");").format(indent, task,
                                                                                        indent, task)


def py_stub(match):
    indent = match.group("indent")
    task = match.group("task")
    return ('{}# TODO({}): implement this (see the lab handout)\n'
            '{}raise NotImplementedError("task {} not implemented yet")').format(indent, task, indent,
                                                                                 task)


count = 0
for dirpath, _, filenames in os.walk(root):
    for filename in filenames:
        path = os.path.join(dirpath, filename)
        if filename.endswith((".cpp", ".hpp", ".h")):
            pattern, stub = CPP_RE, cpp_stub
        elif filename.endswith(".py"):
            pattern, stub = PY_RE, py_stub
        else:
            continue
        with open(path) as f:
            source = f.read()
        source, n = pattern.subn(stub, source)
        if n > 0:
            count += n
            with open(path, "w") as f:
                f.write(source)

print("stripped {} solution blocks".format(count))
PYEOF

echo "student edition written to $OUT_DIR"
