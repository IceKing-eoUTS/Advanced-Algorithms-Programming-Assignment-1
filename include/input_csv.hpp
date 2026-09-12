#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

struct CsvLoadOptions {
    // These switches let the same parser serve two jobs: strict benchmark
    // loading and tests that intentionally feed malformed input.
    bool allow_invalid_lines = false;
    bool require_header = false;
    bool validate_sequential_index = false;
};

struct CsvLoadResult {
    std::vector<int> values;
    std::size_t invalid_lines = 0;
    std::size_t physical_lines = 0;
};

namespace input_csv_detail {

inline std::string_view trim(std::string_view text) {
    // Small parser helper: trim views instead of allocating new strings for
    // every field. The original line storage stays alive for the duration of
    // parsing that line.
    while (!text.empty() &&
           (text.front() == ' ' || text.front() == '\t' ||
            text.front() == '\r')) {
        text.remove_prefix(1);
    }

    while (!text.empty() &&
           (text.back() == ' ' || text.back() == '\t' ||
            text.back() == '\r')) {
        text.remove_suffix(1);
    }

    return text;
}

template <typename Integer>
Integer parse_integer(std::string_view text, std::size_t line_number) {
    text = trim(text);
    if (text.empty()) {
        throw std::runtime_error("empty integer field on line " +
                                 std::to_string(line_number));
    }

    Integer value{};
    const char* first = text.data();
    const char* last = text.data() + text.size();
    // Tricky parser detail: from_chars reports where parsing stopped. Checking
    // pointer == last rejects partially valid fields like "123abc".
    const auto [pointer, error] = std::from_chars(first, last, value);
    if (error != std::errc{} || pointer != last) {
        throw std::runtime_error("invalid integer field on line " +
                                 std::to_string(line_number));
    }

    return value;
}

inline int parse_value_line(std::string_view line,
                            std::size_t expected_index,
                            const CsvLoadOptions& options,
                            std::size_t line_number) {
    line = trim(line);
    const std::size_t first_comma = line.find(',');
    if (first_comma == std::string_view::npos) {
        // Accept a single-column integer line so tests and ad hoc data files can
        // use either "value" or "index,value" rows.
        return parse_integer<int>(line, line_number);
    }

    if (line.find(',', first_comma + 1) != std::string_view::npos) {
        throw std::runtime_error("too many CSV fields on line " +
                                 std::to_string(line_number));
    }

    const std::string_view index_field = line.substr(0, first_comma);
    const std::string_view value_field = line.substr(first_comma + 1);
    const std::int64_t parsed_index =
        parse_integer<std::int64_t>(index_field, line_number);

    if (parsed_index < 0 ||
        static_cast<std::uint64_t>(parsed_index) >
            std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("CSV index is out of range on line " +
                                 std::to_string(line_number));
    }

    if (options.validate_sequential_index &&
        static_cast<std::size_t>(parsed_index) != expected_index) {
        // Invariant for checked-in generator output: row indices must match the
        // number of values already accepted. The empirical study relies on this
        // to know that both arrays receive exactly the intended input prefix.
        throw std::runtime_error("CSV index is not sequential on line " +
                                 std::to_string(line_number));
    }

    return parse_integer<int>(value_field, line_number);
}

inline bool is_header(std::string_view line) {
    return trim(line) == "index,value";
}

} // namespace input_csv_detail

inline CsvLoadResult load_integer_csv(const std::filesystem::path& path,
                                      CsvLoadOptions options = {}) {
    // Core loader organization: read line by line, validate optional header and
    // index invariants, then append only accepted values to the result vector.
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open input file: " + path.string());
    }

    CsvLoadResult result;
    bool saw_first_non_empty_line = false;
    bool saw_header = false;

    std::string line;
    while (std::getline(input, line)) {
        ++result.physical_lines;
        const std::string_view trimmed = input_csv_detail::trim(line);
        if (trimmed.empty()) {
            if (options.allow_invalid_lines) {
                ++result.invalid_lines;
                continue;
            }
            throw std::runtime_error("empty CSV line " +
                                     std::to_string(result.physical_lines));
        }

        if (!saw_first_non_empty_line) {
            saw_first_non_empty_line = true;
            if (input_csv_detail::is_header(trimmed)) {
                saw_header = true;
                continue;
            }

            if (options.require_header) {
                throw std::runtime_error("missing CSV header in " +
                                         path.string());
            }
        } else if (input_csv_detail::is_header(trimmed)) {
            if (options.allow_invalid_lines) {
                ++result.invalid_lines;
                continue;
            }
            throw std::runtime_error("unexpected CSV header on line " +
                                     std::to_string(result.physical_lines));
        }

        try {
            result.values.push_back(input_csv_detail::parse_value_line(
                trimmed, result.values.size(), options, result.physical_lines));
        } catch (...) {
            // What breaks if this continue-on-error path appended a dummy value?
            // A partially invalid file would shift later indices and no longer
            // represent the original sequence of valid values.
            if (!options.allow_invalid_lines) {
                throw;
            }
            ++result.invalid_lines;
        }
    }

    if (options.require_header && !saw_header) {
        throw std::runtime_error("missing CSV header in " + path.string());
    }

    return result;
}

inline std::filesystem::path default_random_input_path(const char* argv0) {
    const std::filesystem::path from_cwd = "data/random_input.csv";
    if (std::filesystem::exists(from_cwd)) {
        return from_cwd;
    }

    if (argv0 != nullptr) {
        const std::filesystem::path executable = std::filesystem::absolute(argv0);
        const std::filesystem::path from_build_tree =
            executable.parent_path().parent_path() / "data" / "random_input.csv";
        if (std::filesystem::exists(from_build_tree)) {
            return from_build_tree;
        }

        const std::filesystem::path from_config_dir =
            executable.parent_path() / "data" / "random_input.csv";
        if (std::filesystem::exists(from_config_dir)) {
            return from_config_dir;
        }
    }

    return from_cwd;
}
