#!/usr/bin/env python3
"""End-to-end uniqueness and JSON contract checks."""
import json
import pathlib
import subprocess
import sys
import tempfile

SOLVER = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/nonogram_hybrid").resolve()
BENCH = SOLVER.with_name("nonogram_bench")


def check(payload):
    proc = subprocess.run([str(SOLVER), "--check-unique", "-"],
                          input=json.dumps(payload), text=True, capture_output=True, check=True)
    assert not proc.stderr, proc.stderr
    assert len(proc.stdout.splitlines()) == 1, proc.stdout
    return json.loads(proc.stdout)


unique = check({"rows": [[1], [0]], "columns": [[1], []]})
assert unique["solved"] and unique["unique"] and unique["lineSolvable"]
assert unique["solution"] == "1000" and unique["timeMs"] >= 0

multiple = check({"rows": [[1], [1]], "columns": [[1], [1]]})
assert multiple["solved"] and not multiple["unique"] and not multiple["lineSolvable"]
assert multiple["solution"] in ("0110", "1001")

unsat = check({"rows": [[2], []], "columns": [[2], []]})
assert not unsat["solved"] and not unsat["unique"]
assert "solution" not in unsat

non_line_path = pathlib.Path(__file__).resolve().parents[1] / "webpbn_puzzles/8.non"
non_line = json.loads(subprocess.run(
    [str(SOLVER), "--check-unique", str(non_line_path)],
    text=True, capture_output=True, check=True).stdout)
assert non_line["solved"] and non_line["unique"] and not non_line["lineSolvable"]

with tempfile.TemporaryDirectory() as directory:
    folder = pathlib.Path(directory)
    for name, rows, columns in (
        ("unique", "1\n0", "1\n0"),
        ("multiple", "1\n1", "1\n1"),
    ):
        (folder / f"{name}.non").write_text(
            f"width 2\nheight 2\nrows\n{rows}\ncolumns\n{columns}\n")
    file_result = subprocess.run([str(SOLVER), "--check-unique", str(folder / "multiple.non")],
                                 text=True, capture_output=True, check=True)
    assert json.loads(file_result.stdout)["unique"] is False
    report = folder / "results.json"
    subprocess.run([str(BENCH), str(folder), str(report)],
                   text=True, capture_output=True, check=True)
    bench_results = {row["filename"]: row for row in json.loads(report.read_text())}
    assert bench_results["unique.non"]["isUnique"] is True
    assert bench_results["multiple.non"]["isUnique"] is False
    assert bench_results["multiple.non"]["clauses"] > 0

print("PASS: unique, multiple, unique without line logic, unsatisfiable, empty clues, .non input, benchmark isUnique")
