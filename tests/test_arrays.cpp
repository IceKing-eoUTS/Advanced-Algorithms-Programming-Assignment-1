#include "baseline_array.hpp"
#include "input_csv.hpp"
#include "resizable_array.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

class TestFailure : public std::runtime_error {
public:
    explicit TestFailure(const std::string& message) : std::runtime_error(message) {}
};

// Core of the lightweight test framework: CHECK helpers throw TestFailure so a
// single failing case can be reported by name without pulling in an external
// dependency.
std::string location(const char* file, int line) {
    std::ostringstream output;
    output << file << ':' << line << ": ";
    return output.str();
}

template <typename Left, typename Right>
void check_equal(const Left& left,
                 const Right& right,
                 const char* left_text,
                 const char* right_text,
                 const char* file,
                 int line) {
    if (!(left == right)) {
        std::ostringstream output;
        output << location(file, line) << "expected " << left_text << " == "
               << right_text << ", got " << left << " and " << right;
        throw TestFailure(output.str());
    }
}

void check_true(bool value, const char* expression, const char* file, int line) {
    if (!value) {
        throw TestFailure(location(file, line) + "check failed: " + expression);
    }
}

#define CHECK(expression) check_true((expression), #expression, __FILE__, __LINE__)
#define CHECK_EQ(left, right) \
    check_equal((left), (right), #left, #right, __FILE__, __LINE__)

template <typename Exception, typename Function>
void check_throws(Function&& function,
                  const char* expression,
                  const char* file,
                  int line) {
    try {
        function();
    } catch (const Exception&) {
        return;
    } catch (const std::exception& error) {
        throw TestFailure(location(file, line) + "expected " + expression +
                          " to throw requested exception, got: " +
                          error.what());
    }

    throw TestFailure(location(file, line) + "expected " + expression +
                      " to throw");
}

#define CHECK_THROWS_AS(expression, exception_type) \
    check_throws<exception_type>([&]() { (void)(expression); }, #expression, \
                                 __FILE__, __LINE__)

template <typename Array>
void compare_to_vector(const Array& array, const std::vector<int>& expected) {
    // Reusable invariant check: the tested array must expose exactly the same
    // logical sequence as std::vector. Many tests rely on this instead of
    // retyping size/order/access assertions.
    CHECK_EQ(array.size(), expected.size());
    CHECK_EQ(array.empty(), expected.empty());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        CHECK_EQ(array[index], expected[index]);
        CHECK_EQ(array.at(index), expected[index]);
    }
}

template <typename Array>
void test_empty_array_behaviour_for() {
    Array array;
    CHECK(array.empty());
    CHECK_EQ(array.size(), std::size_t{0});
    CHECK_THROWS_AS(array.at(0), std::out_of_range);
    CHECK_THROWS_AS(array.front(), std::out_of_range);
    CHECK_THROWS_AS(array.back(), std::out_of_range);
    array.clear();
    CHECK(array.empty());
}

void test_empty_array_behaviour() {
    test_empty_array_behaviour_for<ResizableArray<int>>();
    test_empty_array_behaviour_for<BaselineArray<int>>();
}

void test_first_insertion_and_accessors() {
    ResizableArray<int> array;
    array.push_back(42);

    CHECK(!array.empty());
    CHECK_EQ(array.size(), std::size_t{1});
    CHECK_EQ(array[0], 42);
    CHECK_EQ(array.at(0), 42);
    CHECK_EQ(array.front(), 42);
    CHECK_EQ(array.back(), 42);

    array[0] = 7;
    const ResizableArray<int>& const_array = array;
    CHECK_EQ(const_array[0], 7);
    CHECK_EQ(const_array.at(0), 7);
}

void test_insertion_around_migration_boundaries() {
    // Boundary-focused test: powers of two and their neighbors are where
    // migration begins or completes in the custom implementation, so off-by-one
    // mistakes usually show up here.
    for (std::size_t boundary = 1; boundary <= 2048; boundary *= 2) {
        for (std::size_t n : {boundary > 0 ? boundary - 1 : 0,
                              boundary,
                              boundary + 1}) {
            ResizableArray<int> array;
            std::vector<int> expected;

            for (std::size_t index = 0; index < n; ++index) {
                array.push_back(static_cast<int>(index * 11 + 3));
                expected.push_back(static_cast<int>(index * 11 + 3));
                compare_to_vector(array, expected);
            }
        }
    }
}

void test_many_consecutive_migrations_preserve_order() {
    ResizableArray<int> array;
    std::vector<int> expected;

    for (int value = 0; value < 12000; ++value) {
        array.push_back(value - 6000);
        expected.push_back(value - 6000);

        if (value % 97 == 0) {
            compare_to_vector(array, expected);
        }
    }

    compare_to_vector(array, expected);
}

void test_at_front_back_and_mutation() {
    ResizableArray<int> array;
    for (int value = 0; value < 32; ++value) {
        array.emplace_back(value);
    }

    CHECK_EQ(array.front(), 0);
    CHECK_EQ(array.back(), 31);
    CHECK_EQ(array.at(15), 15);
    CHECK_THROWS_AS(array.at(32), std::out_of_range);

    array[15] = 1500;
    const ResizableArray<int>& const_array = array;
    CHECK_EQ(const_array[15], 1500);
    CHECK_EQ(const_array.front(), 0);
    CHECK_EQ(const_array.back(), 31);
}

void test_clear_and_reuse() {
    ResizableArray<int> array;
    for (int value = 0; value < 257; ++value) {
        array.push_back(value);
    }

    array.clear();
    CHECK(array.empty());
    CHECK_EQ(array.size(), std::size_t{0});

    for (int value = 0; value < 64; ++value) {
        array.push_back(value * -2);
    }

    CHECK_EQ(array.size(), std::size_t{64});
    CHECK_EQ(array.front(), 0);
    CHECK_EQ(array.back(), -126);
}

void test_copy_move_and_self_assignment() {
    ResizableArray<std::string> original;
    original.emplace_back("alpha");
    original.emplace_back(5, 'b');
    original.push_back(std::string("gamma"));

    ResizableArray<std::string> copied(original);
    CHECK_EQ(copied.size(), original.size());
    CHECK_EQ(copied[0], std::string("alpha"));
    CHECK_EQ(copied[1], std::string("bbbbb"));
    CHECK_EQ(copied[2], std::string("gamma"));

    copied[1] = "changed";
    CHECK_EQ(original[1], std::string("bbbbb"));

    ResizableArray<std::string> assigned;
    assigned = original;
    CHECK_EQ(assigned.size(), original.size());
    CHECK_EQ(assigned[2], std::string("gamma"));

    assigned = assigned;
    CHECK_EQ(assigned.size(), std::size_t{3});
    CHECK_EQ(assigned[0], std::string("alpha"));

    assigned = std::move(assigned);
    CHECK_EQ(assigned.size(), std::size_t{3});
    CHECK_EQ(assigned[1], std::string("bbbbb"));

    ResizableArray<std::string> moved(std::move(assigned));
    CHECK_EQ(moved.size(), std::size_t{3});
    CHECK_EQ(moved.front(), std::string("alpha"));
    CHECK_EQ(moved.back(), std::string("gamma"));
    CHECK(assigned.empty());

    ResizableArray<std::string> move_assigned;
    move_assigned.emplace_back("temporary");
    move_assigned = std::move(moved);
    CHECK_EQ(move_assigned.size(), std::size_t{3});
    CHECK_EQ(move_assigned[1], std::string("bbbbb"));
    CHECK(moved.empty());
}

void test_move_only_values() {
    ResizableArray<std::unique_ptr<int>> array;
    for (int value = 0; value < 128; ++value) {
        array.emplace_back(std::make_unique<int>(value));
    }

    CHECK_EQ(array.size(), std::size_t{128});
    for (int value = 0; value < 128; ++value) {
        CHECK_EQ(*array[static_cast<std::size_t>(value)], value);
    }
}

struct Counted {
    // Counted exposes ownership bugs. The invariant after every scope is
    // constructions == destructions and live == 0.
    static inline int live = 0;
    static inline int constructions = 0;
    static inline int destructions = 0;

    int value = 0;

    explicit Counted(int input) : value(input) {
        ++live;
        ++constructions;
    }

    Counted(const Counted& other) : value(other.value) {
        ++live;
        ++constructions;
    }

    Counted(Counted&& other) noexcept : value(other.value) {
        other.value = -1;
        ++live;
        ++constructions;
    }

    Counted& operator=(const Counted&) = default;
    Counted& operator=(Counted&&) noexcept = default;

    ~Counted() {
        --live;
        ++destructions;
    }
};

void reset_counted() {
    Counted::live = 0;
    Counted::constructions = 0;
    Counted::destructions = 0;
}

void test_no_leaks_or_double_destruction() {
    reset_counted();

    {
        // This passes through several migration states, then clear() destroys
        // exactly the live elements. What breaks if destroy_live_elements()
        // visits a migrated slot twice? live would go negative or the sanitizer
        // build would report invalid destruction.
        ResizableArray<Counted> array;
        for (int value = 0; value < 513; ++value) {
            array.emplace_back(value);
        }

        CHECK_EQ(Counted::live, static_cast<int>(array.size()));
        array.clear();
        CHECK_EQ(Counted::live, 0);
        CHECK_EQ(Counted::constructions, Counted::destructions);

        for (int value = 0; value < 37; ++value) {
            array.emplace_back(value);
        }
        CHECK_EQ(Counted::live, 37);
    }

    CHECK_EQ(Counted::live, 0);
    CHECK_EQ(Counted::constructions, Counted::destructions);
}

struct ThrowingCopy {
    // ThrowingCopy is designed to make migration construction fail on demand.
    // It exercises the hardest exception path in ResizableArray::emplace_back.
    static inline int live = 0;
    static inline int copies_before_throw = -1;

    int value = 0;

    explicit ThrowingCopy(int input) : value(input) {
        ++live;
    }

    ThrowingCopy(const ThrowingCopy& other) : value(other.value) {
        if (copies_before_throw == 0) {
            throw std::runtime_error("copy failed");
        }
        if (copies_before_throw > 0) {
            --copies_before_throw;
        }
        ++live;
    }

    ThrowingCopy(ThrowingCopy&& other) noexcept(false) : value(other.value) {
        other.value = -1;
        ++live;
    }

    ThrowingCopy& operator=(const ThrowingCopy&) = default;
    ThrowingCopy& operator=(ThrowingCopy&&) noexcept(false) = default;

    ~ThrowingCopy() {
        --live;
    }
};

void reset_throwing_copy() {
    ThrowingCopy::live = 0;
    ThrowingCopy::copies_before_throw = -1;
}

void test_throwing_migration_keeps_container_valid() {
    reset_throwing_copy();

    {
        ResizableArray<ThrowingCopy> array;
        array.emplace_back(10);
        CHECK_EQ(ThrowingCopy::live, 1);

        // Tricky part under test: the second insertion starts migration from
        // capacity 2. The new element must be rolled back if copying the old
        // element into the new buffer throws.
        ThrowingCopy::copies_before_throw = 0;
        CHECK_THROWS_AS(array.emplace_back(20), std::runtime_error);
        CHECK_EQ(array.size(), std::size_t{1});
        CHECK_EQ(array[0].value, 10);
        CHECK_EQ(ThrowingCopy::live, 1);

        ThrowingCopy::copies_before_throw = -1;
        array.emplace_back(20);
        CHECK_EQ(array.size(), std::size_t{2});
        CHECK_EQ(array[0].value, 10);
        CHECK_EQ(array[1].value, 20);
    }

    CHECK_EQ(ThrowingCopy::live, 0);
}

struct AlwaysThrows {
    static inline int live = 0;

    explicit AlwaysThrows(int) {
        throw std::runtime_error("construction failed");
    }

    AlwaysThrows(const AlwaysThrows&) {
        ++live;
    }

    AlwaysThrows(AlwaysThrows&&) noexcept {
        ++live;
    }

    ~AlwaysThrows() {
        --live;
    }
};

void test_throwing_new_element_constructor() {
    AlwaysThrows::live = 0;
    ResizableArray<AlwaysThrows> array;
    CHECK_THROWS_AS(array.emplace_back(1), std::runtime_error);
    CHECK(array.empty());
    CHECK_EQ(AlwaysThrows::live, 0);
}

void test_randomised_differential_against_vector() {
    constexpr std::uint64_t seed = 0xC0FFEE1234ULL;

    try {
        // Randomized differential testing finds combinations that hand-written
        // tests may miss. The fixed seed keeps failures reproducible.
        std::mt19937_64 generator(seed);
        std::uniform_int_distribution<int> operation_distribution(0, 99);
        std::uniform_int_distribution<int> value_distribution(-500000, 500000);

        ResizableArray<int> array;
        std::vector<int> expected;

        for (int step = 0; step < 10000; ++step) {
            const int operation = operation_distribution(generator);
            if (operation < 68 || expected.empty()) {
                const int value = value_distribution(generator);
                if (operation % 2 == 0) {
                    array.push_back(value);
                } else {
                    array.emplace_back(value);
                }
                expected.push_back(value);
            } else if (operation < 90) {
                const std::size_t index =
                    static_cast<std::size_t>(generator() % expected.size());
                const int value = value_distribution(generator);
                array[index] = value;
                expected[index] = value;
            } else {
                array.clear();
                expected.clear();
            }

            if (step % 17 == 0) {
                compare_to_vector(array, expected);
            }
        }

        compare_to_vector(array, expected);
    } catch (...) {
        std::cerr << "randomised differential test seed: " << seed << '\n';
        throw;
    }
}

std::filesystem::path write_temp_file(const std::string& name,
                                      const std::string& contents) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / name;
    std::ofstream output(path);
    if (!output) {
        throw TestFailure("could not create temporary test file: " +
                          path.string());
    }
    output << contents;
    return path;
}

void test_loading_supplied_random_input_file() {
    CsvLoadOptions options;
    options.require_header = true;
    options.validate_sequential_index = true;

    const CsvLoadResult result =
        load_integer_csv(default_random_input_path(nullptr), options);

    CHECK_EQ(result.values.size(), std::size_t{4096});
    CHECK_EQ(result.values.front(), -279757);
    CHECK_EQ(result.values.back(), -731126);
    CHECK_EQ(result.invalid_lines, std::size_t{0});
}

void test_empty_malformed_and_partially_invalid_input_files() {
    // Input parser tests cover both strict benchmark loading and lenient
    // "collect the valid rows" behavior used to test partial failures.
    CsvLoadOptions strict;
    strict.require_header = true;
    strict.validate_sequential_index = true;

    const std::filesystem::path empty_path =
        write_temp_file("resizable_array_empty.csv", "");
    CHECK_THROWS_AS(load_integer_csv(empty_path, strict), std::runtime_error);

    const std::filesystem::path malformed_path =
        write_temp_file("resizable_array_malformed.csv",
                        "index,value\n0,10\nnot,a,record\n");
    CHECK_THROWS_AS(load_integer_csv(malformed_path, strict), std::runtime_error);

    CsvLoadOptions lenient;
    lenient.allow_invalid_lines = true;
    lenient.require_header = true;
    lenient.validate_sequential_index = false;
    const std::filesystem::path partial_path =
        write_temp_file("resizable_array_partial.csv",
                        "index,value\n0,10\nbad\n1,20\n2,xx\n3,30\n");
    const CsvLoadResult partial = load_integer_csv(partial_path, lenient);
    CHECK_EQ(partial.values.size(), std::size_t{3});
    CHECK_EQ(partial.invalid_lines, std::size_t{2});
    CHECK_EQ(partial.values[0], 10);
    CHECK_EQ(partial.values[1], 20);
    CHECK_EQ(partial.values[2], 30);
}

void test_size_and_overflow_safeguards() {
    CHECK(ResizableArray<int>::max_size() > 0);

    CsvLoadOptions strict;
    strict.require_header = true;
    strict.validate_sequential_index = true;

    const std::filesystem::path bad_index_path =
        write_temp_file("resizable_array_bad_index.csv",
                        "index,value\n999999999999999999999,1\n");
    CHECK_THROWS_AS(load_integer_csv(bad_index_path, strict), std::runtime_error);

    const std::filesystem::path bad_value_path =
        write_temp_file("resizable_array_bad_value.csv",
                        "index,value\n0,999999999999999999999\n");
    CHECK_THROWS_AS(load_integer_csv(bad_value_path, strict), std::runtime_error);
}

struct TestCase {
    const char* name;
    void (*function)();
};

} // namespace

int main() {
    // The test registry is deliberately explicit so learners can map each test
    // name to the function that checks one implementation concern.
    const std::vector<TestCase> tests = {
        {"empty array behaviour", test_empty_array_behaviour},
        {"first insertion and accessors", test_first_insertion_and_accessors},
        {"migration boundaries", test_insertion_around_migration_boundaries},
        {"many consecutive migrations", test_many_consecutive_migrations_preserve_order},
        {"at/front/back/mutation", test_at_front_back_and_mutation},
        {"clear and reuse", test_clear_and_reuse},
        {"copy, move, and self assignment", test_copy_move_and_self_assignment},
        {"move-only values", test_move_only_values},
        {"no leaks or double destruction", test_no_leaks_or_double_destruction},
        {"throwing migration", test_throwing_migration_keeps_container_valid},
        {"throwing new element", test_throwing_new_element_constructor},
        {"randomised differential", test_randomised_differential_against_vector},
        {"load supplied random input", test_loading_supplied_random_input_file},
        {"CSV error handling", test_empty_malformed_and_partially_invalid_input_files},
        {"size and overflow safeguards", test_size_and_overflow_safeguards},
    };

    int failures = 0;
    for (const TestCase& test : tests) {
        try {
            test.function();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
        }
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << tests.size() << " test(s) passed\n";
    return 0;
}
