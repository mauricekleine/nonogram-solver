#!/usr/bin/env python3
"""Audit every bundled .non puzzle through the uniqueness CLI."""
import argparse
import concurrent.futures
import json
import pathlib
import subprocess


def check(path, solver, timeout):
    try:
        run = subprocess.run(
            [str(solver), "--check-unique", str(path)],
            capture_output=True, text=True, timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return path.name, "timeout", None
    if run.returncode:
        return path.name, "error", run.stderr.strip()
    try:
        return path.name, "ok", json.loads(run.stdout)
    except json.JSONDecodeError:
        return path.name, "error", run.stdout.strip()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--solver", type=pathlib.Path, default=pathlib.Path("build/nonogram_hybrid"))
    parser.add_argument("--directory", type=pathlib.Path, default=pathlib.Path("webpbn_puzzles"))
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    paths = sorted(args.directory.glob("*.non"))
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(lambda p: check(p, args.solver, args.timeout), paths))
    counts = {"total": len(paths), "unique": 0, "notUnique": 0,
              "lineSolvable": 0, "unsolved": 0, "timeouts": 0, "errors": 0}
    exceptions = []
    non_line_unique = []
    non_unique = []
    for name, status, result in results:
        if status == "timeout":
            counts["timeouts"] += 1
            exceptions.append(name + ": timeout")
        elif status == "error":
            counts["errors"] += 1
            exceptions.append(name + ": " + str(result))
        else:
            counts["unique" if result["unique"] else "notUnique"] += 1
            counts["lineSolvable"] += int(result["lineSolvable"])
            counts["unsolved"] += int(not result["solved"])
            if result["unique"] and not result["lineSolvable"]:
                non_line_unique.append(name)
            if result["solved"] and not result["unique"]:
                non_unique.append(name)
    print(json.dumps({"counts": counts, "uniqueWithoutLineLogic": non_line_unique,
                      "notUniqueFiles": non_unique,
                      "exceptions": exceptions}, indent=2))


if __name__ == "__main__":
    main()
