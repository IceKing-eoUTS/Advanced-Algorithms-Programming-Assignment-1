#include "baseline_array.hpp"
#include "resizable_array.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Result {
    std::string name;
    std::size_t n = 0;
    std::uint64_t checksum = 0;
    long long total_push_ns = 0;
    long long max_single_push_ns = 0;
    long long indexed_scan_ns = 0;
};

std::vector<int> read_input(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open input file: " + path.string());
    }

    std::vector<int> values;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line == "index,value") {
            continue;
        }

        std::stringstream parser(line);
        std::string first;
        std::string second;
        if (!std::getline(parser, first, ',')) {
            continue;
        }

        if (std::getline(parser, second, ',')) {
            values.push_back(std::stoi(second));
        } else {
            values.push_back(std::stoi(first));
        }
    }

    return values;
}

std::filesystem::path default_input_path(char* argv0) {
    const std::filesystem::path from_cwd = "data/random_input.csv";
    if (std::filesystem::exists(from_cwd)) {
        return from_cwd;
    }

    const std::filesystem::path executable = std::filesystem::absolute(argv0);
    const std::filesystem::path from_build_tree =
        executable.parent_path().parent_path() / "data" / "random_input.csv";
    if (std::filesystem::exists(from_build_tree)) {
        return from_build_tree;
    }

    return from_cwd;
}

template <typename Array>
Result run_array_study(const std::string& name, const std::vector<int>& input) {
    Array array;
    Result result;
    result.name = name;
    result.n = input.size();

    for (int value : input) {
        const auto start = Clock::now();
        array.push_back(value);
        const auto stop = Clock::now();

        const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start)
                .count();
        result.total_push_ns += elapsed;
        result.max_single_push_ns =
            std::max(result.max_single_push_ns, elapsed);
    }

    const auto scan_start = Clock::now();
    for (std::size_t index = 0; index < array.size(); ++index) {
        result.checksum =
            result.checksum * 1315423911u +
            static_cast<std::uint32_t>(array[index]) +
            static_cast<std::uint32_t>(index);
    }
    const auto scan_stop = Clock::now();
    result.indexed_scan_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(scan_stop -
                                                             scan_start)
            .count();

    return result;
}

void print_result(const Result& result) {
    std::cout << result.name << ','
              << result.n << ','
              << result.total_push_ns << ','
              << result.max_single_push_ns << ','
              << result.indexed_scan_ns << ','
              << result.checksum << '\n';
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path input_path =
            argc > 1 ? std::filesystem::path(argv[1]) : default_input_path(argv[0]);
        const std::vector<int> input = read_input(input_path);

        std::cout << "container,n,total_push_ns,max_single_push_ns,"
                     "indexed_scan_ns,checksum\n";
        print_result(run_array_study<ResizableArray<int>>("ResizableArray",
                                                          input));
        print_result(run_array_study<BaselineArray<int>>("BaselineArray",
                                                         input));
    } catch (const std::exception& error) {
        std::cerr << "empirical_study failed: " << error.what() << '\n';
        return 1;
    }
}
