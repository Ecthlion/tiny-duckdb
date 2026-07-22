#!/usr/bin/env python3
"""Lab 4 demo: build a tiny lakehouse table and query it with DuckDB."""
import tempfile

from lakebase import LakeTable

SCHEMA = {"l_orderkey": "INTEGER", "l_quantity": "DOUBLE", "l_returnflag": "VARCHAR"}


def main():
    path = tempfile.mkdtemp(prefix="lakebase_demo_")
    table = LakeTable.create(path + "/lineitem", SCHEMA)

    # append a few batches (one Parquet file + one commit each)
    for batch in range(3):
        table.append([(i, i * 1.5, "ABCN"[i % 3]) for i in range(batch * 100, batch * 100 + 100)])

    print("== files ==")
    import os
    for name in sorted(os.listdir(table.path)):
        print(" ", name)

    print("\n== history ==")
    for version, actions in table.history():
        print(" ", version, actions)

    print("\n== filter pushdown plan ==")
    print(table.explain_scan(where="l_orderkey >= 295"))

    print("\n== snapshot read (l_orderkey >= 295) ==")
    for row in table.scan(where="l_orderkey >= 295"):
        print(" ", row)

    print("\n== time travel: version 1 had only", len(table.scan_version(1)), "rows ==")

    table.compact()
    print("\n== after compaction ==")
    print("live snapshot files:", table._snapshot_files())
    print("files on disk (old ones kept for time travel):")
    for name in sorted(os.listdir(table.path)):
        print(" ", name)

    removed = table.vacuum()
    print("\n== vacuum removed", len(removed), "stale files ==")
    for name in sorted(os.listdir(table.path)):
        print(" ", name)


if __name__ == "__main__":
    main()
