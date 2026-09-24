// Batch benchmark through the hybrid SAT engine, with verified uniqueness.
#include "nonogram.hpp"
#include "hybrid_engine.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <thread>
#include <future>
#include <mutex>
#include <filesystem>

using namespace nonogram;
namespace fs = std::filesystem;

struct PuzzleResult {
    std::string filename;
    std::string title;
    std::string author;
    int width, height;
    bool solved;
    bool unique;
    bool line_solvable;
    double time_ms;
    double sat_time_ms;
    int unknowns_after_prop;
    int clauses;
    bool symmetric;
    std::string solver_used;
    std::vector<int8_t> grid;
    std::vector<Description> row_desc, col_desc;
};


std::mutex print_mutex;
std::atomic<int> completed{0};
int total_puzzles = 0;

PuzzleResult solve_puzzle(const std::string& filepath) {
    PuzzleResult result{};
    result.filename = fs::path(filepath).filename().string();
    result.solved = false;
    result.unique = false;
    result.line_solvable = false;
    result.time_ms = 0;
    result.sat_time_ms = 0;
    result.unknowns_after_prop = 0;
    result.clauses = 0;
    result.symmetric = false;
    
    std::ifstream file(filepath);
    if (!file) return result;
    
    std::stringstream buf;
    buf << file.rdbuf();
    
    auto info = parse_non_file_with_info(buf.str());
    if (!info) return result;
    
    result.title = info->title;
    result.author = info->author;
    result.width = info->puzzle.width;
    result.height = info->puzzle.height;
    result.row_desc = info->puzzle.row_descriptions;
    result.col_desc = info->puzzle.col_descriptions;
    
    auto t_start = std::chrono::high_resolution_clock::now();
    
    HybridNonogramSolver solver(info->puzzle);
    result.symmetric = solver.has_col_symmetry;
    result.unknowns_after_prop = solver.run_propagation(0);
    result.line_solvable = solver.line_solvable;
    if (!solver.propagation_contradiction) {
        result.solved = result.unknowns_after_prop == 0 || solver.run_sat();
        if (result.solved) {
            result.grid = solver.grid;
            result.clauses = solver.clauses;
            result.unique = result.line_solvable || solver.check_unique();
        }
    }
    result.sat_time_ms = solver.sat_time_ms;
    result.solver_used = result.line_solvable ? "Propagation" : "Glucose";

    auto t_end = std::chrono::high_resolution_clock::now();
    result.time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    
    int done = ++completed;
    {
        std::lock_guard<std::mutex> lock(print_mutex);
        std::cerr << "\r[" << done << "/" << total_puzzles << "] " 
                  << result.filename << " - " << (result.solved ? "OK" : "FAIL")
                  << " (" << (int)result.time_ms << "ms)          " << std::flush;
    }
    
    return result;
}

std::string escape_json(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

int main(int argc, char* argv[]) {
    std::string dir = argc > 1 ? argv[1] : "webpbn_puzzles";
    std::string output_file = argc > 2 ? argv[2] : "results.json";
    
    std::vector<std::string> files;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == ".non") {
            files.push_back(entry.path().string());
        }
    }
    std::sort(files.begin(), files.end());
    
    total_puzzles = files.size();
    int max_threads = std::min(4u, std::max(1u, std::thread::hardware_concurrency()));
    std::cerr << "Running " << total_puzzles << " puzzles with "
              << max_threads << " workers...\n";
    std::vector<std::future<PuzzleResult>> futures;
    std::vector<PuzzleResult> results;
    
    for (size_t i = 0; i < files.size(); ++i) {
        if (futures.size() >= (size_t)max_threads) {
            for (auto& f : futures) results.push_back(f.get());
            futures.clear();
        }
        futures.push_back(std::async(std::launch::async, solve_puzzle, files[i]));
    }
    for (auto& f : futures) results.push_back(f.get());
    
    std::cerr << "\n\nWriting " << output_file << "...\n";
    
    // Output JSON (format matching viewer.html expectations)
    std::ofstream out(output_file);
    out << "[\n";
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        
        // Convert grid to string format (0/1/2)
        std::string grid_str;
        for (auto v : r.grid) grid_str += ('0' + v);
        
        // Determine status
        std::string status = r.solved ? "SOLVED" : "FAILED";
        
        // Determine solver level
        std::string solver_level = r.unknowns_after_prop == 0 ? "Propagation" : 
                                   (r.solver_used.empty() ? "Unknown" : r.solver_used);
        
        out << "  {\n";
        out << "    \"filename\": \"" << escape_json(r.filename) << "\",\n";
        out << "    \"title\": \"" << escape_json(r.title) << "\",\n";
        out << "    \"author\": \"" << escape_json(r.author) << "\",\n";
        out << "    \"width\": " << r.width << ",\n";
        out << "    \"height\": " << r.height << ",\n";
        out << "    \"status\": \"" << status << "\",\n";
        out << "    \"solveTimeMs\": " << r.time_ms << ",\n";
        out << "    \"satTimeMs\": " << r.sat_time_ms << ",\n";
        out << "    \"solverLevel\": \"" << solver_level << "\",\n";
        out << "    \"isUnique\": " << (r.unique ? "true" : "false") << ",\n";
        out << "    \"lineSolvable\": " << (r.line_solvable ? "true" : "false") << ",\n";
        out << "    \"clauses\": " << r.clauses << ",\n";
        out << "    \"symmetric\": " << (r.symmetric ? "true" : "false") << ",\n";
        
        // Row clues (viewer expects rowClues)
        out << "    \"rowClues\": [";
        for (size_t ri = 0; ri < r.row_desc.size(); ++ri) {
            out << "[";
            for (size_t ci = 0; ci < r.row_desc[ri].size(); ++ci) {
                out << r.row_desc[ri][ci];
                if (ci + 1 < r.row_desc[ri].size()) out << ",";
            }
            out << "]";
            if (ri + 1 < r.row_desc.size()) out << ",";
        }
        out << "],\n";
        
        // Col clues (viewer expects colClues)
        out << "    \"colClues\": [";
        for (size_t ci = 0; ci < r.col_desc.size(); ++ci) {
            out << "[";
            for (size_t ri = 0; ri < r.col_desc[ci].size(); ++ri) {
                out << r.col_desc[ci][ri];
                if (ri + 1 < r.col_desc[ci].size()) out << ",";
            }
            out << "]";
            if (ci + 1 < r.col_desc.size()) out << ",";
        }
        out << "],\n";
        
        // Solution as string (viewer expects expectedSolution/actualSolution)
        out << "    \"expectedSolution\": \"" << grid_str << "\",\n";
        out << "    \"actualSolution\": \"" << grid_str << "\"\n";
        
        out << "  }";
        if (i + 1 < results.size()) out << ",";
        out << "\n";
    }
    out << "]\n";
    
    // Summary
    int solved = 0, failed = 0;
    double total_time = 0;
    for (const auto& r : results) {
        if (r.solved) solved++; else failed++;
        total_time += r.time_ms;
    }
    
    std::cerr << "\nResults: " << solved << "/" << results.size() << " solved, "
              << failed << " failed\n";
    std::cerr << "Total time: " << (total_time/1000) << "s\n";
    std::cerr << "Output written to: " << output_file << "\n";
    
    return 0;
}
