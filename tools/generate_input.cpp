#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

std::uint64_t next_state(std::uint64_t state) {
    return state * 6364136223846793005ULL + 1442695040888963407ULL;
}

int generated_value(std::uint64_t state, int minimum, int maximum) {
    const std::uint64_t range =
        static_cast<std::uint64_t>(
            static_cast<long long>(maximum) - static_cast<long long>(minimum)) +
        1ULL;
    const long long offset = static_cast<long long>((state >> 16) % range);
    return static_cast<int>(static_cast<long long>(minimum) + offset);
}

int parse_int_arg(const char* text, const char* name) {
    const long long value = std::stoll(text);
    if (value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max()) {
        throw std::invalid_argument(std::string(name) + " is out of int range");
    }

    return static_cast<int>(value);
}

std::size_t parse_size_arg(const char* text, const char* name) {
    const std::string value_text = text;
    if (!value_text.empty() && value_text.front() == '-') {
        throw std::invalid_argument(std::string(name) + " must be non-negative");
    }
    const unsigned long long value = std::stoull(value_text);
    if (value > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument(std::string(name) +
                                    " is out of size_t range");
    }
    return static_cast<std::size_t>(value);
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output_path =
            argc > 1 ? std::filesystem::path(argv[1])
                     : std::filesystem::path("data/random_input.csv");
        const std::size_t count =
            argc > 2 ? parse_size_arg(argv[2], "count") : 4096U;
        const unsigned long long seed =
            argc > 3 ? std::stoull(argv[3]) : 20260912ULL;
        const int minimum = argc > 4 ? parse_int_arg(argv[4], "minimum")
                                     : -1000000;
        const int maximum = argc > 5 ? parse_int_arg(argv[5], "maximum")
                                     : 1000000;

        if (minimum > maximum) {
            throw std::invalid_argument("minimum must be <= maximum");
        }

        if (output_path.has_parent_path()) {
            std::filesystem::create_directories(output_path.parent_path());
        }

        std::ofstream output(output_path);
        if (!output) {
            throw std::runtime_error("could not open output file");
        }

        output << "index,value\n";
        std::uint64_t state = seed;
        for (std::size_t index = 0; index < count; ++index) {
            state = next_state(state);
            output << index << ',' << generated_value(state, minimum, maximum)
                   << '\n';
        }

        std::cout << "wrote " << count << " rows to "
                  << output_path.string() << " with seed " << seed
                  << " in range [" << minimum << ", " << maximum << "]\n";
    } catch (const std::exception& error) {
        std::cerr << "generate_input failed: " << error.what() << '\n';
        return 1;
    }
}
