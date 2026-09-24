// Hybrid Nonogram Solver CLI
#include "hybrid_engine.hpp"
#include "json_puzzle.hpp"
#include <fstream>
#include <sstream>
#include <string>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

// Glucose prints some progress messages even at zero verbosity. Keep the
// machine-readable check mode's stdout reserved for its single JSON object.
class QuietStdout {
public:
    QuietStdout() {
        std::fflush(stdout);
        saved_ = dup(STDOUT_FILENO);
        const int sink = open("/dev/null", O_WRONLY);
        if (saved_ >= 0 && sink >= 0) dup2(sink, STDOUT_FILENO);
        if (sink >= 0) close(sink);
    }
    ~QuietStdout() {
        std::fflush(stdout);
        if (saved_ >= 0) {
            dup2(saved_, STDOUT_FILENO);
            close(saved_);
        }
    }
private:
    int saved_ = -1;
};

static bool valid_solution(const nonogram::Puzzle& puzzle, const std::vector<int8_t>& grid) {
    nonogram::SolverResult result{};
    result.width = puzzle.width;
    result.height = puzzle.height;
    result.is_solved = true;
    for (int8_t pixel : grid) result.grid.push_back(static_cast<nonogram::Pixel>(pixel));
    return result.validate(puzzle.row_descriptions, puzzle.col_descriptions);
}

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "--check-unique") {
        if (argc != 3) {
            std::cerr << "Usage: nonogram_hybrid --check-unique <puzzle.non|->\n";
            return 2;
        }
        const std::string path = argv[2];
        std::ifstream file;
        if (path != "-") file.open(path);
        if (path != "-" && !file) {
            std::cerr << "Cannot open: " << path << "\n";
            return 2;
        }
        std::stringstream buffer;
        buffer << (path == "-" ? std::cin.rdbuf() : file.rdbuf());
        auto puzzle = path == "-" ? JsonPuzzleParser(buffer.str()).parse()
                                  : nonogram::parse_non_file(buffer.str());
        if (!puzzle) {
            std::cerr << "Invalid puzzle input\n";
            return 2;
        }
        const auto start = std::chrono::steady_clock::now();
        HybridNonogramSolver solver(*puzzle);
        bool solved = false;
        bool unique = false;
        {
            QuietStdout quiet;
            const int unknowns = solver.run_propagation(0);
            if (!solver.propagation_contradiction) {
                solved = unknowns == 0 || solver.run_sat();
                if (solved && !valid_solution(*puzzle, solver.grid)) {
                    std::cerr << "Solver produced an invalid grid\n";
                    return 3;
                }
                if (solved) unique = solver.line_solvable || solver.check_unique();
            }
        }
        const auto end = std::chrono::steady_clock::now();
        const double time_ms = std::chrono::duration<double, std::milli>(end - start).count();
        std::cout << "{\"solved\":" << (solved ? "true" : "false")
                  << ",\"unique\":" << (unique ? "true" : "false")
                  << ",\"lineSolvable\":" << (solver.line_solvable ? "true" : "false");
        if (solved) {
            std::cout << ",\"solution\":\"";
            for (int8_t pixel : solver.grid) std::cout << char('0' + pixel);
            std::cout << '"';
        }
        std::cout << ",\"timeMs\":" << time_ms << "}\n";
        return 0;
    }

    std::string filename = argc > 1 ? argv[1] : "webpbn_puzzles/1.non";
    // Default 500ms FullSettle limit for the legacy display mode.
    double prop_timeout = argc > 2 ? std::stod(argv[2]) : 500;
    
    std::ifstream file(filename);
    if (!file) { 
        std::cerr << "Cannot open: " << filename << "\n"; 
        return 1; 
    }
    std::stringstream buf;
    buf << file.rdbuf();
    
    auto info = nonogram::parse_non_file_with_info(buf.str());
    if (!info) { 
        std::cerr << "Parse failed\n"; 
        return 1; 
    }
    
    std::cerr << "Puzzle: " << info->title << " (" << info->puzzle.width << "x" 
              << info->puzzle.height << ")\n";
    
    HybridNonogramSolver solver(info->puzzle);
    auto t_start = std::chrono::high_resolution_clock::now();
    
    // Phase 1: Propagation
    int unknowns = solver.run_propagation(prop_timeout);
    std::cerr << "After propagation: " << unknowns << " unknowns (" 
              << solver.prop_time_ms << "ms)\n";
    
    bool solved = false;
    if (unknowns == 0) {
        solved = true;
        std::cerr << "SOLVED by propagation alone!\n";
    } else {
        // Phase 2: SAT
        std::cerr << "Running Glucose SAT solver...\n";
        solved = solver.run_sat();
        std::cerr << "SAT phase: " << solver.sat_time_ms << "ms\n";
    }
    
    auto t_end = std::chrono::high_resolution_clock::now();
    double total = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    
    std::cerr << (solved ? "SOLVED" : "FAILED") << " in " << total << "ms\n";
    
    if (solved && info->puzzle.width <= 60) {
        solver.print_grid();
    }
    
    return solved ? 0 : 1;
}
