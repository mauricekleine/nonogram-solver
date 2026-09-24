#pragma once

#include "nonogram.hpp"
#include <cctype>
#include <climits>
#include <optional>
#include <string>
#include <utility>

// Deliberately small parser for the documented clues-only JSON input.
class JsonPuzzleParser {
public:
    explicit JsonPuzzleParser(const std::string& input) : input_(input) {}

    std::optional<nonogram::Puzzle> parse() {
        nonogram::Puzzle puzzle{};
        bool seen_rows = false, seen_columns = false;
        if (!take('{')) return std::nullopt;
        do {
            auto key = string();
            if (!key || !take(':')) return std::nullopt;
            if (*key == "rows" && !seen_rows) {
                if (!clues(puzzle.row_descriptions)) return std::nullopt;
                seen_rows = true;
            } else if (*key == "columns" && !seen_columns) {
                if (!clues(puzzle.col_descriptions)) return std::nullopt;
                seen_columns = true;
            } else {
                return std::nullopt;
            }
        } while (take(','));
        if (!take('}') || !at_end() || !seen_rows || !seen_columns) return std::nullopt;
        puzzle.height = static_cast<int>(puzzle.row_descriptions.size());
        puzzle.width = static_cast<int>(puzzle.col_descriptions.size());
        if (!puzzle.height || !puzzle.width) return std::nullopt;
        long long row_total = 0, column_total = 0;
        for (const auto& row : puzzle.row_descriptions) {
            if (!valid_line(row, puzzle.width)) return std::nullopt;
            for (int n : row) row_total += n;
        }
        for (const auto& column : puzzle.col_descriptions) {
            if (!valid_line(column, puzzle.height)) return std::nullopt;
            for (int n : column) column_total += n;
        }
        if (row_total != column_total) return std::nullopt;
        return puzzle;
    }

private:
    const std::string& input_;
    size_t pos_ = 0;

    void space() {
        while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) ++pos_;
    }
    bool take(char ch) {
        space();
        if (pos_ < input_.size() && input_[pos_] == ch) { ++pos_; return true; }
        return false;
    }
    bool at_end() { space(); return pos_ == input_.size(); }
    std::optional<std::string> string() {
        if (!take('"')) return std::nullopt;
        std::string result;
        while (pos_ < input_.size() && input_[pos_] != '"') {
            if (input_[pos_] == '\\' || static_cast<unsigned char>(input_[pos_]) < 32) return std::nullopt;
            result += input_[pos_++];
        }
        if (!take('"')) return std::nullopt;
        return result;
    }
    std::optional<int> integer() {
        space();
        if (pos_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[pos_]))) return std::nullopt;
        int value = 0;
        do {
            int digit = input_[pos_++] - '0';
            if (value > (INT_MAX - digit) / 10) return std::nullopt;
            value = value * 10 + digit;
        } while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_])));
        return value;
    }
    bool clues(std::vector<nonogram::Description>& result) {
        if (!take('[')) return false;
        if (take(']')) return true;
        do {
            nonogram::Description line;
            if (!take('[')) return false;
            if (!take(']')) {
                do {
                    auto n = integer();
                    if (!n) return false;
                    line.push_back(*n);
                } while (take(','));
                if (!take(']')) return false;
            }
            if (line.size() == 1 && line[0] == 0) line.clear();
            result.push_back(std::move(line));
        } while (take(','));
        return take(']');
    }
    static bool valid_line(const nonogram::Description& line, int length) {
        long long used = line.empty() ? 0 : static_cast<long long>(line.size()) - 1;
        for (int n : line) {
            if (n <= 0) return false;
            used += n;
        }
        return used <= length;
    }
};
