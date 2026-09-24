# Nonogram Solver

C++17 nonogram solver combining line constraint propagation with bundled Glucose SAT solving. It accepts `.non` puzzles and can check whether a puzzle has exactly one solution.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
python3 tests/test_check_unique.py
```

Requires CMake 3.14+, a C++17 compiler, zlib, and pthreads. No external SAT executable is needed for `nonogram_hybrid` or `nonogram_bench`.

## Solve and check uniqueness

```bash
./build/nonogram_hybrid webpbn_puzzles/1.non
./build/nonogram_hybrid --check-unique webpbn_puzzles/1.non
printf '%s\n' '{"rows":[[1],[1]],"columns":[[1],[1]]}' | ./build/nonogram_hybrid --check-unique -
```

The check command writes one JSON object to stdout:

```json
{"solved":true,"unique":false,"lineSolvable":false,"solution":"0110","timeMs":0.1}
```

`solution` is a row-major string of `0` (white) and `1` (black), present only when solved. `lineSolvable` means Settle/FullSettle alone determined every cell; it excludes 2-SAT implications and search. A fully settled grid is unique. Otherwise the solver finds one solution with SAT, blocks that exact grid, and solves again. A second solution makes `unique` false; UNSAT makes it true. An unsatisfiable puzzle reports `solved: false`, `unique: false`, and no `solution`.

For stdin JSON, provide exactly `rows` and `columns` arrays of clue arrays. `[]` and `[0]` both mean an empty line. Malformed or impossible clues exit with an error on stderr. For `.non` files, use a path instead of `-`.

## Batch benchmark

```bash
./build/nonogram_bench webpbn_puzzles results.json
python3 tools/audit_webpbn.py --timeout 30
```

`nonogram_bench` uses the same propagation and Glucose SAT check and reports the verified `isUnique` and `lineSolvable` fields for each puzzle. The audit script checks all bundled `.non` files with a per-puzzle timeout and reports counts and any exceptions.

## Algorithm

1. `Settle` finds cells shared by every valid placement in each line. `FullSettle` alternates rows and columns to a fixed point.
2. If cells remain unknown, Glucose encodes row and column clues as SAT constraints and finds a complete grid.
3. For uniqueness, one clause excludes the found grid and a second SAT run checks whether any different grid exists.

The legacy `nonogram_solver` executable uses external SAT solvers. `nonogram_verify` runs the original propagation verification suite.

## Viewer

```bash
python3 -m http.server 8080
# open http://localhost:8080/viewer.html
```

## References

- [Batenburg & Kosters, *A Discrete Tomography Approach to Japanese Puzzles* (2009)](https://liacs.leidenuniv.nl/~kosterswa/pbn/icga09.pdf)
- [Glucose SAT Solver](https://github.com/audemard/glucose)
- [nonogram-db `.non` format](https://github.com/mikix/nonogram-db)
