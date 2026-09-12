#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

std::uint64_t next_state(std::uint64_t state) {
    return state * 6364136223846793005ULL + 1442695040888963407ULL;
}

int generated_value(std::uint64_t state) {
    return static_cast<int>((state >> 16) % 2000001ULL) - 1000000;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output_path =
            argc > 1 ? std::filesystem::path(argv[1])
                     : std::filesystem::path("data/random_input.csv");
        const int count = argc > 2 ? std::stoi(argv[2]) : 4096;
        const unsigned long long seed =
            argc > 3 ? std::stoull(argv[3]) : 20260912ULL;

        if (count < 0) {
            throw std::invalid_argument("count must be non-negative");
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
        for (int index = 0; index < count; ++index) {
            state = next_state(state);
            output << index << ',' << generated_value(state) << '\n';
        }

        std::cout << "wrote " << count << " rows to "
                  << output_path.string() << " with seed " << seed << '\n';
    } catch (const std::exception& error) {
        std::cerr << "generate_input failed: " << error.what() << '\n';
        return 1;
    }
}
