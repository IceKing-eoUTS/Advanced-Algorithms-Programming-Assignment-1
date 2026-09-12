#include "baseline_array.hpp"
#include "resizable_array.hpp"

#include <cassert>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

template <typename Array>
void exercise_common_integer_interface() {
    Array array;
    assert(array.empty());
    assert(array.size() == 0);

    for (int value = 0; value < 128; ++value) {
        array.push_back(value * 3);
        assert(array.size() == static_cast<std::size_t>(value + 1));
        assert(array.front() == 0);
        assert(array.back() == value * 3);
    }

    for (std::size_t index = 0; index < array.size(); ++index) {
        assert(array[index] == static_cast<int>(index * 3));
        assert(array.at(index) == static_cast<int>(index * 3));
    }

    bool threw = false;
    try {
        (void)array.at(array.size());
    } catch (const std::out_of_range&) {
        threw = true;
    }
    assert(threw);

    array.clear();
    assert(array.empty());
    assert(array.size() == 0);
}

void test_resizable_array_preserves_order_during_many_growths() {
    ResizableArray<int> array;
    std::vector<int> expected;

    for (int value = 0; value < 4096; ++value) {
        array.push_back(value);
        expected.push_back(value);

        assert(array.size() == expected.size());
        assert(array.front() == expected.front());
        assert(array.back() == expected.back());

        for (std::size_t probe : {
                 std::size_t{0},
                 expected.size() / 2,
                 expected.size() - 1,
             }) {
            assert(array[probe] == expected[probe]);
        }
    }

    for (std::size_t index = 0; index < expected.size(); ++index) {
        assert(array[index] == expected[index]);
    }
}

void test_copy_and_move() {
    ResizableArray<std::string> original;
    original.emplace_back("alpha");
    original.emplace_back(5, 'b');
    original.push_back(std::string("gamma"));

    ResizableArray<std::string> copied(original);
    assert(copied.size() == original.size());
    assert(copied[0] == "alpha");
    assert(copied[1] == "bbbbb");
    assert(copied[2] == "gamma");

    copied[1] = "changed";
    assert(original[1] == "bbbbb");

    ResizableArray<std::string> assigned;
    assigned = original;
    assert(assigned.size() == original.size());
    assert(assigned[2] == "gamma");

    ResizableArray<std::string> moved(std::move(assigned));
    assert(moved.size() == 3);
    assert(moved.front() == "alpha");
    assert(moved.back() == "gamma");
    assert(assigned.empty());

    ResizableArray<std::string> move_assigned;
    move_assigned.emplace_back("temporary");
    move_assigned = std::move(moved);
    assert(move_assigned.size() == 3);
    assert(move_assigned[1] == "bbbbb");
    assert(moved.empty());
}

void test_move_only_values() {
    ResizableArray<std::unique_ptr<int>> array;
    for (int value = 0; value < 64; ++value) {
        array.emplace_back(std::make_unique<int>(value));
    }

    assert(array.size() == 64);
    for (int value = 0; value < 64; ++value) {
        assert(*array[static_cast<std::size_t>(value)] == value);
    }
}

struct Counted {
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

void test_only_live_elements_are_destroyed() {
    Counted::live = 0;
    Counted::constructions = 0;
    Counted::destructions = 0;

    {
        ResizableArray<Counted> array;
        for (int value = 0; value < 257; ++value) {
            array.emplace_back(value);
        }

        assert(array.size() == 257);
        assert(Counted::live == static_cast<int>(array.size()));

        array.clear();
        assert(Counted::live == 0);
        assert(Counted::constructions == Counted::destructions);

        for (int value = 0; value < 17; ++value) {
            array.emplace_back(value);
        }
        assert(Counted::live == 17);
    }

    assert(Counted::live == 0);
    assert(Counted::constructions == Counted::destructions);
}

void test_front_back_empty_checks() {
    ResizableArray<int> array;

    bool front_threw = false;
    try {
        (void)array.front();
    } catch (const std::out_of_range&) {
        front_threw = true;
    }
    assert(front_threw);

    bool back_threw = false;
    try {
        (void)array.back();
    } catch (const std::out_of_range&) {
        back_threw = true;
    }
    assert(back_threw);
}

} // namespace

int main() {
    exercise_common_integer_interface<ResizableArray<int>>();
    exercise_common_integer_interface<BaselineArray<int>>();
    test_resizable_array_preserves_order_during_many_growths();
    test_copy_and_move();
    test_move_only_values();
    test_only_live_elements_are_destroyed();
    test_front_back_empty_checks();
}
