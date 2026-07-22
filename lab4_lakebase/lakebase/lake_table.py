# ============================================================================
# LAB 4 (exploratory) - the lakebase: DuckDB reading and writing lake tables
#
# A "lake table" = a directory of Parquet files + a transaction log, exactly
# like Delta Lake:
#
#   my_table/
#     _lake_log/00000000000000000000.json   <- one file per commit
#     _lake_log/00000000000000000001.json
#     part-00000-<uuid>.parquet
#
# Each log file records the actions of one commit:
#   {"actions": [{"add": {"path": ..., "num_rows": ...}}, {"remove": ...}]}
#
# DuckDB is the execution engine: it writes Parquet (COPY) and reads it back
# with projection/filter pushdown (read_parquet).
# ============================================================================
import json
import os
import uuid

import duckdb

LOG_DIR = "_lake_log"


class LakeTable:
    """A mini Delta-Lake-style table powered by DuckDB."""

    def __init__(self, path):
        self.path = path
        self.log_path = os.path.join(path, LOG_DIR)

    # ------------------------------------------------------------------
    # L4.T1: create a lake table and append data as Parquet files
    # ------------------------------------------------------------------
    @staticmethod
    def create(path, schema):
        """Create an empty lake table. schema: {"col": "INTEGER", ...}."""
        os.makedirs(os.path.join(path, LOG_DIR), exist_ok=True)
        table = LakeTable(path)
        # [SOLUTION BEGIN L4.T1]
        columns = ", ".join("{} {}".format(name, sql_type) for name, sql_type in schema.items())
        # commit 0 records the schema and zero data files
        table._commit([{"schema": {"columns": schema, "ddl": columns}}])
        # [SOLUTION END]
        return table

    def append(self, rows):
        """Append a list of tuples as ONE new Parquet file (+ one commit)."""
        # [SOLUTION BEGIN L4.T1]
        schema = self.schema()
        columns = ", ".join(schema.keys())
        filename = "part-{:05d}-{}.parquet".format(self.version() + 1, uuid.uuid4().hex[:8])
        file_path = os.path.join(self.path, filename)
        con = duckdb.connect()
        placeholders = ", ".join(["?"] * len(schema))
        con.execute("CREATE TABLE batch ({})".format(
            ", ".join("{} {}".format(name, sql_type) for name, sql_type in schema.items())))
        con.executemany("INSERT INTO batch VALUES ({})".format(placeholders), rows)
        # DuckDB writes the Parquet file for us
        con.execute("COPY batch TO '{}' (FORMAT PARQUET)".format(file_path))
        con.close()
        self._commit([{"add": {"path": filename, "num_rows": len(rows)}}])
        return filename
        # [SOLUTION END]

    # ------------------------------------------------------------------
    # L4.T2: snapshot read with projection/filter pushdown
    # ------------------------------------------------------------------
    def scan(self, columns=None, where=None):
        """Read the current snapshot. Returns a list of tuples.

        columns: projected column names (None = all)
        where:   a SQL predicate string pushed down into the Parquet scan
        """
        # [SOLUTION BEGIN L4.T2]
        files = [os.path.join(self.path, f) for f in self._snapshot_files()]
        if not files:
            return []
        con = duckdb.connect()
        projection = "*" if columns is None else ", ".join(columns)
        query = "SELECT {} FROM read_parquet(?)".format(projection)
        if where is not None:
            # DuckDB pushes this predicate into the Parquet reader (row-group
            # skipping via min/max statistics - the same trick as our Lab 1!)
            query += " WHERE " + where
        result = con.execute(query, [files]).fetchall()
        con.close()
        return result
        # [SOLUTION END]

    def explain_scan(self, where=None):
        """Show the pushed-down filter in DuckDB's plan (for the README demo)."""
        files = [os.path.join(self.path, f) for f in self._snapshot_files()]
        if not files:
            return "<empty table>"
        con = duckdb.connect()
        query = "EXPLAIN SELECT * FROM read_parquet(?)"
        if where is not None:
            query += " WHERE " + where
        plan = con.execute(query, [files]).fetchall()
        con.close()
        return "\n".join(row[1] for row in plan)

    # ------------------------------------------------------------------
    # L4.T3: time travel
    # ------------------------------------------------------------------
    def history(self):
        """One entry per commit: (version, actions)."""
        # [SOLUTION BEGIN L4.T3]
        result = []
        for version in range(self.version() + 1):
            result.append((version, self._read_log(version)["actions"]))
        return result
        # [SOLUTION END]

    def scan_version(self, version, columns=None, where=None):
        """Time travel: read the snapshot as of an older commit."""
        # [SOLUTION BEGIN L4.T3]
        files = [os.path.join(self.path, f) for f in self._snapshot_files(at_version=version)]
        if not files:
            return []
        con = duckdb.connect()
        projection = "*" if columns is None else ", ".join(columns)
        query = "SELECT {} FROM read_parquet(?)".format(projection)
        if where is not None:
            query += " WHERE " + where
        result = con.execute(query, [files]).fetchall()
        con.close()
        return result
        # [SOLUTION END]

    # ------------------------------------------------------------------
    # L4.T4: compaction (OPTIMIZE): many small files -> one big file
    # ------------------------------------------------------------------
    def compact(self):
        """Rewrite all live Parquet files into a single file."""
        # [SOLUTION BEGIN L4.T4]
        live = self._snapshot_files()
        if len(live) <= 1:
            return None
        filename = "part-compacted-{}.parquet".format(uuid.uuid4().hex[:8])
        file_path = os.path.join(self.path, filename)
        files = [os.path.join(self.path, f) for f in live]
        con = duckdb.connect()
        con.execute("COPY (SELECT * FROM read_parquet(?)) TO '{}' (FORMAT PARQUET)".format(file_path),
                    [files])
        num_rows = con.execute("SELECT count(*) FROM read_parquet(?)", [files]).fetchone()[0]
        con.close()
        actions = [{"remove": {"path": f}} for f in live]
        actions.append({"add": {"path": filename, "num_rows": num_rows}})
        self._commit(actions)
        # The old files are now LOGICALLY removed from the latest snapshot,
        # but stay on disk so time travel keeps working - exactly like Delta
        # Lake, where OPTIMIZE never deletes data (VACUUM does).
        return filename
        # [SOLUTION END]

    def vacuum(self):
        """Physically delete Parquet files no longer in the current snapshot.

        This is what finally breaks time travel to versions that referenced
        the deleted files (Delta Lake's VACUUM works the same way, minus the
        retention period). Returns the list of deleted file names.
        """
        live = set(self._snapshot_files())
        removed = []
        for name in os.listdir(self.path):
            if name.endswith(".parquet") and name not in live:
                os.remove(os.path.join(self.path, name))
                removed.append(name)
        return removed

    # ------------------------------------------------------------------
    # provided helpers: the transaction log itself
    # ------------------------------------------------------------------
    def version(self):
        """Current table version = number of commits - 1."""
        if not os.path.isdir(self.log_path):
            raise RuntimeError("not a lake table: " + self.path)
        return len([f for f in os.listdir(self.log_path) if f.endswith(".json")]) - 1

    def schema(self):
        """The schema recorded in commit 0."""
        return self._read_log(0)["actions"][0]["schema"]["columns"]

    def _log_file(self, version):
        return os.path.join(self.log_path, "{:020d}.json".format(version))

    def _read_log(self, version):
        with open(self._log_file(version)) as f:
            return json.load(f)

    def _commit(self, actions):
        version = self.version() + 1 if os.path.isdir(self.log_path) and os.listdir(self.log_path) else 0
        entry = {"version": version, "actions": actions}
        # atomic-ish commit: write temp file, then rename
        tmp = self._log_file(version) + ".tmp"
        with open(tmp, "w") as f:
            json.dump(entry, f, indent=2)
        os.rename(tmp, self._log_file(version))

    def _snapshot_files(self, at_version=None):
        """Replay the log to compute the set of live data files."""
        latest = self.version() if at_version is None else at_version
        live = []
        for version in range(1, latest + 1):  # commit 0 is just the schema
            for action in self._read_log(version)["actions"]:
                if "add" in action:
                    live.append(action["add"]["path"])
                elif "remove" in action:
                    live.remove(action["remove"]["path"])
        return live
