# LAB 4 tests - run with: python3 -m pytest test_lakebase.py -v
import os

import pytest

from lakebase import LakeTable

SCHEMA = {"l_orderkey": "INTEGER", "l_quantity": "DOUBLE", "l_returnflag": "VARCHAR"}


def make_rows(start, count):
    return [(i, i * 1.5, "ABCN"[i % 3]) for i in range(start, start + count)]


@pytest.fixture()
def table(tmp_path):
    return LakeTable.create(str(tmp_path / "lineitem"), SCHEMA)


# --- L4.T1: create + append -------------------------------------------------

def test_create_writes_schema_commit(table):
    assert table.version() == 0
    assert table.schema() == SCHEMA
    assert os.path.exists(os.path.join(table.log_path, "00000000000000000000.json"))


def test_append_creates_one_parquet_file_per_commit(table):
    table.append(make_rows(0, 10))
    table.append(make_rows(10, 10))
    files = [f for f in os.listdir(table.path) if f.endswith(".parquet")]
    assert len(files) == 2
    assert table.version() == 2


# --- L4.T2: snapshot reads + pushdown ----------------------------------------

def test_scan_all_rows(table):
    table.append(make_rows(0, 10))
    table.append(make_rows(10, 10))
    rows = sorted(table.scan())
    assert rows == make_rows(0, 20)


def test_scan_projection(table):
    table.append(make_rows(0, 10))
    rows = table.scan(columns=["l_returnflag"])
    assert all(len(row) == 1 for row in rows)


def test_scan_filter_pushdown(table):
    for batch in range(5):
        table.append(make_rows(batch * 100, 100))
    rows = table.scan(where="l_orderkey >= 495")
    assert sorted(rows) == make_rows(495, 5)
    # the plan shows the filter pushed into the Parquet scan
    plan = table.explain_scan(where="l_orderkey >= 495")
    assert "Filters:" in plan


# --- L4.T3: history + time travel --------------------------------------------

def test_history(table):
    table.append(make_rows(0, 5))
    table.append(make_rows(5, 5))
    history = table.history()
    assert [version for version, _ in history] == [0, 1, 2]
    assert "add" in history[1][1][0]


def test_time_travel(table):
    table.append(make_rows(0, 5))
    table.append(make_rows(5, 5))
    assert sorted(table.scan_version(1)) == make_rows(0, 5)
    assert sorted(table.scan_version(2)) == make_rows(0, 10)
    # compact, then time travel to BEFORE the compaction still works
    table.compact()
    assert sorted(table.scan_version(2)) == make_rows(0, 10)


# --- L4.T4: compaction --------------------------------------------------------

def test_compact_merges_files(table):
    for batch in range(4):
        table.append(make_rows(batch * 25, 25))
    compacted = table.compact()
    assert compacted is not None
    live = [f for f in os.listdir(table.path) if f.endswith(".parquet")]
    assert live == [compacted]
    assert sorted(table.scan()) == make_rows(0, 100)


def test_compact_single_file_is_noop(table):
    table.append(make_rows(0, 10))
    assert table.compact() is None
