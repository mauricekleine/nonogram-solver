#pragma once
#include "nonogram.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>
#include "core/Solver.h"

using GlucoseSolver = Glucose::Solver;
using Glucose::Lit;
using Glucose::mkLit;
using Glucose::vec;
using Glucose::lbool;

class HybridNonogramSolver {
public:
    nonogram::Puzzle puzzle;
    int width, height;
    std::vector<int8_t> grid;  // 0=white, 1=black, 2=undecided
    std::vector<int8_t> propagated_grid;
    bool line_solvable = false;
    bool propagation_contradiction = false;
    int next_var;
    std::vector<std::vector<int>> cell_var;
    bool has_col_symmetry = false;
    
    // Stats
    int unknowns_after_prop = 0;
    int clauses = 0;
    double prop_time_ms = 0;
    double sat_time_ms = 0;
    
    HybridNonogramSolver(const nonogram::Puzzle& p) : puzzle(p), width(p.width), height(p.height) {
        grid.resize(width * height, 2);
        cell_var.resize(height, std::vector<int>(width, 0));
        next_var = 1;
        for (int r = 0; r < height; ++r)
            for (int c = 0; c < width; ++c)
                cell_var[r][c] = next_var++;
        detect_symmetry();
    }
    
    void detect_symmetry() {
        has_col_symmetry = true;
        for (int c = 0; c < width / 2; ++c) {
            if (puzzle.col_descriptions[c] != puzzle.col_descriptions[width - 1 - c]) {
                has_col_symmetry = false;
                break;
            }
        }
    }
    
    // Phase 1: Run constraint propagation
    int run_propagation(double timeout_ms) {
        auto t1 = std::chrono::high_resolution_clock::now();
        
        nonogram::Solver solver(puzzle);
        solver.set_timeout(timeout_ms);
        auto result = solver.solve_line_logic();
        line_solvable = result.is_solved;
        propagation_contradiction = result.solver_level == "contradiction";
        
        for (int r = 0; r < height; ++r)
            for (int c = 0; c < width; ++c) {
                int8_t val = static_cast<int8_t>(result.get(r, c));
                grid[r * width + c] = val;
            }
        
        int unknowns = 0;
        for (int i = 0; i < width * height; ++i) 
            if (grid[i] == 2) unknowns++;
        
        auto t2 = std::chrono::high_resolution_clock::now();
        prop_time_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
        unknowns_after_prop = unknowns;
        propagated_grid = grid;
        
        return unknowns;
    }
    
    // Phase 2: SAT solving with Glucose for remaining unknowns
    bool run_sat(const std::vector<int8_t>* blocked_solution = nullptr) {
        auto t1 = std::chrono::high_resolution_clock::now();
        next_var = width * height + 1;
        clauses = 0;
        
        GlucoseSolver solver;
        
        // Create variables
        for (int i = 0; i < next_var; i++) {
            solver.newVar();
        }
        
        // Add unit clauses for already-determined cells
        for (int r = 0; r < height; ++r) {
            for (int c = 0; c < width; ++c) {
                int var = cell_var[r][c];
                if (grid[r * width + c] == 1) {
                    vec<Lit> clause;
                    clause.push(mkLit(var, false));  // must be black
                    add_clause(solver, clause);
                } else if (grid[r * width + c] == 0) {
                    vec<Lit> clause;
                    clause.push(mkLit(var, true));   // must be white
                    add_clause(solver, clause);
                }
            }
        }
        
        // Encode row constraints
        for (int r = 0; r < height; ++r) {
            std::vector<int> cells(width);
            for (int c = 0; c < width; ++c) cells[c] = cell_var[r][c];
            if (!encode_line(solver, puzzle.row_descriptions[r], cells, width)) {
                return false;
            }
        }
        
        // Encode column constraints
        for (int c = 0; c < width; ++c) {
            std::vector<int> cells(height);
            for (int r = 0; r < height; ++r) cells[r] = cell_var[r][c];
            if (!encode_line(solver, puzzle.col_descriptions[c], cells, height)) {
                return false;
            }
        }

        if (blocked_solution) {
            vec<Lit> clause;
            for (int r = 0; r < height; ++r)
                for (int c = 0; c < width; ++c) {
                    const int index = r * width + c;
                    clause.push(mkLit(cell_var[r][c], (*blocked_solution)[index] == 1));
                }
            if (!add_clause(solver, clause)) return false;
        }
        
        const bool result = solver.solve();
        
        auto t2 = std::chrono::high_resolution_clock::now();
        sat_time_ms += std::chrono::duration<double, std::milli>(t2 - t1).count();
        
        if (!result) return false;
        
        // Extract solution
        for (int r = 0; r < height; ++r)
            for (int c = 0; c < width; ++c)
                grid[r * width + c] = solver.model[cell_var[r][c]] == l_True ? 1 : 0;
        
        return true;
    }

    bool check_unique() {
        const auto solution = grid;
        grid = propagated_grid;
        const bool another_solution = run_sat(&solution);
        grid = solution;
        return !another_solution;
    }
    
    bool add_clause(GlucoseSolver& solver, const vec<Lit>& clause) {
        ++clauses;
        return solver.addClause(clause);
    }

    bool encode_line(GlucoseSolver& solver, const nonogram::Description& desc,
                     const std::vector<int>& cells, int len) {
        int ns = desc.size();
        
        // Empty description: all white
        if (ns == 0) {
            for (int c : cells) {
                vec<Lit> clause;
                clause.push(mkLit(c, true));
                if (!add_clause(solver, clause)) return false;
            }
            return true;
        }
        
        // Compute valid ranges for each segment
        std::vector<int> mn(ns), mx(ns);
        int p = 0;
        for (int i = 0; i < ns; ++i) { mn[i] = p; p += desc[i] + 1; }
        p = len;
        for (int i = ns - 1; i >= 0; --i) { p -= desc[i]; mx[i] = p; p--; }
        
        // Check feasibility
        for (int i = 0; i < ns; ++i) {
            if (mn[i] > mx[i]) return false;
        }
        
        // Create segment-start variables
        std::vector<std::vector<int>> sv(ns);
        for (int i = 0; i < ns; ++i) {
            sv[i].resize(len, 0);
            for (int q = mn[i]; q <= mx[i]; ++q) {
                sv[i][q] = next_var++;
                solver.newVar();
            }
        }
        
        // Exactly-one constraint for each segment
        for (int i = 0; i < ns; ++i) {
            // At least one
            vec<Lit> alo;
            for (int q = mn[i]; q <= mx[i]; ++q) {
                alo.push(mkLit(sv[i][q], false));
            }
            if (!add_clause(solver, alo)) return false;
            
            // At most one (pairwise for small, sequential counter for large)
            std::vector<int> vars;
            for (int q = mn[i]; q <= mx[i]; ++q) vars.push_back(sv[i][q]);
            
            if (vars.size() <= 5) {
                for (size_t a = 0; a < vars.size(); ++a) {
                    for (size_t b = a + 1; b < vars.size(); ++b) {
                        vec<Lit> clause;
                        clause.push(mkLit(vars[a], true));
                        clause.push(mkLit(vars[b], true));
                        if (!add_clause(solver, clause)) return false;
                    }
                }
            } else {
                // Sequential counter encoding
                int n = vars.size();
                std::vector<int> s(n - 1);
                for (int j = 0; j < n - 1; ++j) {
                    s[j] = next_var++;
                    solver.newVar();
                }
                vec<Lit> c;
                c.clear(); c.push(mkLit(vars[0], true)); c.push(mkLit(s[0], false));
                add_clause(solver, c);
                for (int j = 1; j < n - 1; ++j) {
                    c.clear(); c.push(mkLit(s[j-1], true)); c.push(mkLit(s[j], false));
                    add_clause(solver, c);
                    c.clear(); c.push(mkLit(vars[j], true)); c.push(mkLit(s[j], false));
                    add_clause(solver, c);
                    c.clear(); c.push(mkLit(vars[j], true)); c.push(mkLit(s[j-1], true));
                    add_clause(solver, c);
                }
                c.clear(); c.push(mkLit(vars[n-1], true)); c.push(mkLit(s[n-2], true));
                add_clause(solver, c);
            }
        }
        
        // Ordering constraints between segments
        for (int i = 0; i < ns - 1; ++i) {
            for (int q = mn[i]; q <= mx[i]; ++q) {
                int min_next = q + desc[i] + 1;
                for (int q2 = mn[i+1]; q2 < min_next && q2 <= mx[i+1]; ++q2) {
                    vec<Lit> clause;
                    clause.push(mkLit(sv[i][q], true));
                    clause.push(mkLit(sv[i+1][q2], true));
                    if (!add_clause(solver, clause)) return false;
                }
            }
        }
        
        // Link cell variables to segment positions
        for (int c = 0; c < len; ++c) {
            std::vector<int> covering;
            for (int i = 0; i < ns; ++i) {
                int pm = std::max(mn[i], c - desc[i] + 1);
                int px = std::min(mx[i], c);
                for (int q = pm; q <= px; ++q) {
                    if (sv[i][q]) covering.push_back(sv[i][q]);
                }
            }
            
            if (covering.empty()) {
                // No segment can cover this cell -> must be white
                vec<Lit> clause;
                clause.push(mkLit(cells[c], true));
                if (!add_clause(solver, clause)) return false;
            } else {
                // cell BLACK <-> at least one covering segment placed
                // Forward: segment placed -> cell BLACK
                for (int v : covering) {
                    vec<Lit> clause;
                    clause.push(mkLit(v, true));
                    clause.push(mkLit(cells[c], false));
                    if (!add_clause(solver, clause)) return false;
                }
                // Backward: cell BLACK -> some covering segment
                vec<Lit> clause;
                clause.push(mkLit(cells[c], true));
                for (int v : covering) {
                    clause.push(mkLit(v, false));
                }
                if (!add_clause(solver, clause)) return false;
            }
        }
        
        return true;
    }
    
    void print_grid() const {
        for (int r = 0; r < height; ++r) {
            for (int c = 0; c < width; ++c) {
                int8_t v = grid[r * width + c];
                std::cout << (v == 1 ? "█" : v == 0 ? "·" : "?");
            }
            std::cout << "\n";
        }
    }
};
