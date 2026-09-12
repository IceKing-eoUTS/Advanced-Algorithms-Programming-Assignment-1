#include "baseline_array.hpp"
#include "input_csv.hpp"
#include "resizable_array.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

// Preventing optimization is part of the benchmark, not the data structure.
// Results flow into this sink after timed regions so the compiler cannot prove
// that array construction and indexed reads are unused.
std::atomic<std::uint64_t> benchmark_sink{0};

struct Config {
    // Centralized command-line configuration. Keeping this as one struct makes
    // it clear which options affect the study design and which only affect I/O.
    std::filesystem::path input_path;
    std::filesystem::path output_path;
    std::size_t max_n = 0;
    std::vector<std::size_t> explicit_sizes;
    int repetitions = 5;
    int warmups = 1;
    std::size_t random_accesses = 0;
    std::uint64_t access_seed = 20260912ULL;
};

struct Percentiles {
    long long median = 0;
    long long p95 = 0;
    long long p99 = 0;
    long long maximum = 0;
};

struct ThroughputResult {
    long long total_ns = 0;
    double ns_per_push = 0.0;
    std::uint64_t checksum = 0;
    std::size_t estimated_bytes = 0;
};

struct ResultRow {
    // One row maps directly to one CSV output line. The throughput and latency
    // fields come from different experiments on purpose; combining them would
    // let per-operation clock overhead pollute the throughput measurement.
    std::string structure;
    std::size_t n = 0;
    int trial = 0;
    long long total_ns = 0;
    double ns_per_push = 0.0;
    long long median_push_ns = 0;
    long long p95_push_ns = 0;
    long long p99_push_ns = 0;
    long long max_push_ns = 0;
    long long random_access_ns = 0;
    std::uint64_t checksum = 0;
    std::size_t estimated_bytes = 0;
};

void consume(std::uint64_t value) {
    const std::uint64_t previous = benchmark_sink.load(std::memory_order_relaxed);
    benchmark_sink.store(previous ^ (value + 0x9e3779b97f4a7c15ULL +
                                     (previous << 6U) + (previous >> 2U)),
                         std::memory_order_relaxed);
    std::atomic_signal_fence(std::memory_order_seq_cst);
}

std::uint64_t checksum_update(std::uint64_t checksum,
                              int value,
                              std::size_t index) {
    return checksum * 1315423911u +
           static_cast<std::uint32_t>(value) +
           static_cast<std::uint32_t>(index);
}

template <typename Array>
std::uint64_t checksum_array(const Array& array) {
    // The checksum is the benchmark invariant: both structures must expose the
    // same logical sequence before their timings are considered meaningful.
    std::uint64_t checksum = 0;
    for (std::size_t index = 0; index < array.size(); ++index) {
        checksum = checksum_update(checksum, array[index], index);
    }
    return checksum;
}

template <typename Array>
std::size_t estimated_bytes_for(const Array& array) {
    if constexpr (requires { array.estimated_allocated_slots(); }) {
        return array.estimated_allocated_slots() *
               sizeof(typename Array::value_type);
    } else if constexpr (requires { array.capacity(); }) {
        return array.capacity() * sizeof(typename Array::value_type);
    } else {
        return array.size() * sizeof(typename Array::value_type);
    }
}

std::size_t parse_size_arg(std::string_view text, std::string_view name) {
    std::size_t value = 0;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const auto [pointer, error] = std::from_chars(first, last, value);
    if (error != std::errc{} || pointer != last) {
        throw std::invalid_argument("invalid " + std::string(name));
    }
    return value;
}

int parse_int_arg(std::string_view text, std::string_view name) {
    const std::size_t parsed = parse_size_arg(text, name);
    if (parsed > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument(std::string(name) + " is too large");
    }
    return static_cast<int>(parsed);
}

std::uint64_t parse_u64_arg(std::string_view text, std::string_view name) {
    std::uint64_t value = 0;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const auto [pointer, error] = std::from_chars(first, last, value);
    if (error != std::errc{} || pointer != last) {
        throw std::invalid_argument("invalid " + std::string(name));
    }
    return value;
}

std::vector<std::size_t> parse_size_list(std::string_view text) {
    std::vector<std::size_t> sizes;
    while (!text.empty()) {
        // Tricky parser detail: parse one comma-delimited field at a time, then
        // remove exactly that prefix. Forgetting remove_prefix would loop
        // forever on any multi-size argument.
        const std::size_t comma = text.find(',');
        const std::string_view item =
            comma == std::string_view::npos ? text : text.substr(0, comma);
        if (item.empty()) {
            throw std::invalid_argument("empty item in --sizes");
        }
        sizes.push_back(parse_size_arg(item, "size list item"));
        if (comma == std::string_view::npos) {
            break;
        }
        text.remove_prefix(comma + 1);
    }
    return sizes;
}

void print_usage(std::ostream& output, const char* argv0) {
    output << "Usage: " << argv0 << " [input.csv] [options]\n"
           << "Options:\n"
           << "  --input PATH          Input CSV path (default data/random_input.csv)\n"
           << "  --output PATH         Output CSV path (default stdout; '-' for stdout)\n"
           << "  --max-n N             Largest prefix size to test\n"
           << "  --sizes A,B,C         Exact comma-separated input sizes to test\n"
           << "  --repetitions N       Measured repetitions per size (default 5)\n"
           << "  --warmups N           Warm-up repetitions per size (default 1)\n"
           << "  --accesses N          Random-index reads per access trial (default n)\n"
           << "  --access-seed N       Random-index seed (default 20260912)\n"
           << "  --help                Show this help text\n";
}

Config parse_config(int argc, char** argv) {
    Config config;
    config.input_path = default_random_input_path(argc > 0 ? argv[0] : nullptr);

    bool saw_positional_input = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        auto require_value = [&](std::string_view option) -> std::string_view {
            if (index + 1 >= argc) {
                throw std::invalid_argument(std::string(option) +
                                            " requires a value");
            }
            ++index;
            return argv[index];
        };

        if (argument == "--help") {
            print_usage(std::cout, argv[0]);
            std::exit(0);
        } else if (argument == "--input") {
            config.input_path =
                std::filesystem::path(std::string(require_value(argument)));
        } else if (argument == "--output") {
            config.output_path =
                std::filesystem::path(std::string(require_value(argument)));
        } else if (argument == "--max-n" || argument == "--n") {
            config.max_n = parse_size_arg(require_value(argument), argument);
        } else if (argument == "--sizes") {
            config.explicit_sizes = parse_size_list(require_value(argument));
        } else if (argument == "--repetitions") {
            config.repetitions = parse_int_arg(require_value(argument), argument);
        } else if (argument == "--warmups") {
            config.warmups = parse_int_arg(require_value(argument), argument);
        } else if (argument == "--accesses") {
            config.random_accesses =
                parse_size_arg(require_value(argument), argument);
        } else if (argument == "--access-seed") {
            config.access_seed = parse_u64_arg(require_value(argument), argument);
        } else if (!argument.empty() && argument.front() == '-') {
            throw std::invalid_argument("unknown option: " + std::string(argument));
        } else if (!saw_positional_input) {
            config.input_path = std::filesystem::path(std::string(argument));
            saw_positional_input = true;
        } else {
            throw std::invalid_argument("unexpected positional argument: " +
                                        std::string(argument));
        }
    }

    if (config.repetitions <= 0) {
        throw std::invalid_argument("--repetitions must be positive");
    }
    if (config.warmups < 0) {
        throw std::invalid_argument("--warmups must be non-negative");
    }

    return config;
}

std::vector<std::size_t> build_size_list(std::size_t available,
                                         const Config& config) {
    if (config.max_n > available) {
        throw std::invalid_argument("--max-n exceeds input rows");
    }
    const std::size_t max_n = config.max_n == 0 ? available : config.max_n;

    std::vector<std::size_t> sizes;
    if (!config.explicit_sizes.empty()) {
        for (std::size_t size : config.explicit_sizes) {
            if (size > available) {
                throw std::invalid_argument("requested size exceeds input rows");
            }
            sizes.push_back(size);
        }
    } else {
        sizes.push_back(0);
        for (std::size_t boundary = 1; boundary <= max_n;) {
            // Core study design: include powers of two and the sizes just
            // before/after them, because those are where vector growth and the
            // custom array's migration threshold are easiest to observe.
            if (boundary > 1) {
                sizes.push_back(boundary - 1);
            }
            sizes.push_back(boundary);
            if (boundary < max_n) {
                sizes.push_back(boundary + 1);
            }

            if (boundary > std::numeric_limits<std::size_t>::max() / 2) {
                break;
            }
            boundary *= 2;
        }
        sizes.push_back(max_n);
    }

    sizes.erase(std::remove_if(sizes.begin(), sizes.end(),
                               [max_n](std::size_t size) {
                                   return size > max_n;
                               }),
                sizes.end());
    std::sort(sizes.begin(), sizes.end());
    sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
    return sizes;
}

template <typename Array>
Array build_array(const std::vector<int>& values, std::size_t n) {
    Array array;
    for (std::size_t index = 0; index < n; ++index) {
        array.push_back(values[index]);
    }
    consume(checksum_array(array));
    return array;
}

void validate_equal_outputs(const std::vector<int>& values, std::size_t n) {
    const ResizableArray<int> custom =
        build_array<ResizableArray<int>>(values, n);
    const BaselineArray<int> baseline =
        build_array<BaselineArray<int>>(values, n);

    if (custom.size() != baseline.size() || custom.size() != n) {
        throw std::runtime_error("size mismatch while validating n=" +
                                 std::to_string(n));
    }

    for (std::size_t index = 0; index < n; ++index) {
        if (custom[index] != baseline[index]) {
            throw std::runtime_error("value mismatch at index " +
                                     std::to_string(index) + " for n=" +
                                     std::to_string(n));
        }
    }

    const std::uint64_t custom_checksum = checksum_array(custom);
    const std::uint64_t baseline_checksum = checksum_array(baseline);
    if (custom_checksum != baseline_checksum) {
        throw std::runtime_error("checksum mismatch for n=" + std::to_string(n));
    }
    consume(custom_checksum ^ baseline_checksum);
}

Percentiles summarize_latencies(std::vector<long long> latencies) {
    if (latencies.empty()) {
        return {};
    }

    std::sort(latencies.begin(), latencies.end());
    auto percentile = [&](double fraction) -> long long {
        const double raw_index =
            fraction * static_cast<double>(latencies.size() - 1);
        const auto index = static_cast<std::size_t>(raw_index + 0.999999);
        return latencies[std::min(index, latencies.size() - 1)];
    };

    Percentiles result;
    result.median = percentile(0.50);
    result.p95 = percentile(0.95);
    result.p99 = percentile(0.99);
    result.maximum = latencies.back();
    return result;
}

template <typename Array>
ThroughputResult measure_throughput(const std::vector<int>& values,
                                    std::size_t n) {
    Array array;

    // Throughput experiment: there is intentionally no clock call inside the
    // insertion loop. What breaks if we time every push here? The clock-call
    // overhead becomes part of total_ns and hides the bulk insertion rate.
    std::atomic_signal_fence(std::memory_order_seq_cst);
    const auto start = Clock::now();
    for (std::size_t index = 0; index < n; ++index) {
        array.push_back(values[index]);
    }
    const auto stop = Clock::now();
    std::atomic_signal_fence(std::memory_order_seq_cst);

    const std::uint64_t checksum = checksum_array(array);
    consume(checksum);

    ThroughputResult result;
    result.total_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start)
            .count();
    result.ns_per_push =
        n == 0 ? 0.0 : static_cast<double>(result.total_ns) /
                             static_cast<double>(n);
    result.checksum = checksum;
    result.estimated_bytes = estimated_bytes_for(array);
    return result;
}

template <typename Array>
Percentiles measure_push_latency(const std::vector<int>& values, std::size_t n) {
    Array array;
    std::vector<long long> latencies;
    latencies.reserve(n);

    // Latency experiment: this is separate from throughput precisely because it
    // does pay one clock measurement per push_back. It is useful for tail
    // latency, not for aggregate insertion throughput.
    for (std::size_t index = 0; index < n; ++index) {
        std::atomic_signal_fence(std::memory_order_seq_cst);
        const auto start = Clock::now();
        array.push_back(values[index]);
        const auto stop = Clock::now();
        std::atomic_signal_fence(std::memory_order_seq_cst);

        latencies.push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start)
                .count());
    }

    consume(checksum_array(array));
    return summarize_latencies(std::move(latencies));
}

std::vector<std::size_t> make_random_indices(std::size_t n,
                                             std::size_t access_count,
                                             std::uint64_t seed) {
    std::vector<std::size_t> indices;
    if (n == 0) {
        return indices;
    }

    const std::size_t count = access_count == 0 ? n : access_count;
    indices.reserve(count);
    // Fixed-seed random indices make the access experiment reproducible. Both
    // structures receive the same index sequence for the same n.
    std::mt19937_64 generator(seed);
    std::uniform_int_distribution<std::size_t> distribution(0, n - 1);
    for (std::size_t index = 0; index < count; ++index) {
        indices.push_back(distribution(generator));
    }
    return indices;
}

template <typename Array>
long long measure_random_access(const std::vector<int>& values,
                                std::size_t n,
                                const std::vector<std::size_t>& indices) {
    const Array array = build_array<Array>(values, n);

    std::uint64_t checksum = 0;
    // The array is fully built before timing begins, so this timed region
    // isolates indexed access instead of mixing in construction cost.
    std::atomic_signal_fence(std::memory_order_seq_cst);
    const auto start = Clock::now();
    for (std::size_t probe = 0; probe < indices.size(); ++probe) {
        const std::size_t index = indices[probe];
        checksum = checksum_update(checksum, array[index], probe);
    }
    const auto stop = Clock::now();
    std::atomic_signal_fence(std::memory_order_seq_cst);

    consume(checksum);
    return std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start)
        .count();
}

template <typename Array>
ResultRow run_trial(const std::string& structure,
                    const std::vector<int>& values,
                    std::size_t n,
                    int trial,
                    const std::vector<std::size_t>& access_indices) {
    const ThroughputResult throughput = measure_throughput<Array>(values, n);
    const Percentiles latency = measure_push_latency<Array>(values, n);
    const long long access_ns =
        measure_random_access<Array>(values, n, access_indices);

    ResultRow row;
    row.structure = structure;
    row.n = n;
    row.trial = trial;
    row.total_ns = throughput.total_ns;
    row.ns_per_push = throughput.ns_per_push;
    row.median_push_ns = latency.median;
    row.p95_push_ns = latency.p95;
    row.p99_push_ns = latency.p99;
    row.max_push_ns = latency.maximum;
    row.random_access_ns = access_ns;
    row.checksum = throughput.checksum;
    row.estimated_bytes = throughput.estimated_bytes;
    return row;
}

void run_warmups(const std::vector<int>& values,
                 std::size_t n,
                 const std::vector<std::size_t>& access_indices,
                 int warmups) {
    for (int warmup = 0; warmup < warmups; ++warmup) {
        (void)run_trial<ResizableArray<int>>("ResizableArray", values, n, -1,
                                             access_indices);
        (void)run_trial<BaselineArray<int>>("BaselineArray", values, n, -1,
                                            access_indices);
    }
}

std::vector<ResultRow> run_study(const std::vector<int>& values,
                                 const Config& config) {
    // Core benchmark orchestration: choose sizes, validate correctness for each
    // size, warm up, then record paired custom/baseline rows per trial.
    const std::vector<std::size_t> sizes = build_size_list(values.size(), config);
    std::vector<ResultRow> rows;
    rows.reserve(sizes.size() * static_cast<std::size_t>(config.repetitions) *
                 2U);

    for (std::size_t n : sizes) {
        // What breaks if validation is removed? A faster result from a broken
        // container could look like a performance win while returning the wrong
        // logical array.
        validate_equal_outputs(values, n);
        const std::vector<std::size_t> access_indices =
            make_random_indices(n, config.random_accesses,
                                config.access_seed ^
                                    (n * 0x9e3779b97f4a7c15ULL));

        run_warmups(values, n, access_indices, config.warmups);

        for (int trial = 0; trial < config.repetitions; ++trial) {
            ResultRow custom =
                run_trial<ResizableArray<int>>("ResizableArray", values, n,
                                               trial, access_indices);
            ResultRow baseline =
                run_trial<BaselineArray<int>>("BaselineArray", values, n,
                                              trial, access_indices);

            if (custom.checksum != baseline.checksum) {
                throw std::runtime_error("trial checksum mismatch for n=" +
                                         std::to_string(n));
            }

            rows.push_back(std::move(custom));
            rows.push_back(std::move(baseline));
        }
    }

    return rows;
}

void write_results(std::ostream& output, const std::vector<ResultRow>& rows) {
    // Machine-readable output is written only after all measurements are done,
    // keeping file I/O and formatting out of timed regions.
    output << "structure,n,trial,total_ns,ns_per_push,median_push_ns,"
              "p95_push_ns,p99_push_ns,max_push_ns,random_access_ns,"
              "checksum,estimated_bytes\n";
    output << std::fixed << std::setprecision(3);

    for (const ResultRow& row : rows) {
        output << row.structure << ','
               << row.n << ','
               << row.trial << ','
               << row.total_ns << ','
               << row.ns_per_push << ','
               << row.median_push_ns << ','
               << row.p95_push_ns << ','
               << row.p99_push_ns << ','
               << row.max_push_ns << ','
               << row.random_access_ns << ','
               << row.checksum << ','
               << row.estimated_bytes << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Config config = parse_config(argc, argv);

        CsvLoadOptions options;
        options.require_header = true;
        options.validate_sequential_index = true;
        const CsvLoadResult loaded = load_integer_csv(config.input_path, options);
        if (loaded.values.empty()) {
            throw std::runtime_error("input file contains no values");
        }

        const std::vector<ResultRow> rows = run_study(loaded.values, config);

        if (config.output_path.empty() || config.output_path.string() == "-") {
            write_results(std::cout, rows);
        } else {
            if (config.output_path.has_parent_path()) {
                std::filesystem::create_directories(
                    config.output_path.parent_path());
            }
            std::ofstream output(config.output_path);
            if (!output) {
                throw std::runtime_error("could not open output file: " +
                                         config.output_path.string());
            }
            write_results(output, rows);
        }

        consume(benchmark_sink.load(std::memory_order_relaxed));
    } catch (const std::exception& error) {
        std::cerr << "empirical_study failed: " << error.what() << '\n';
        return 1;
    }
}
