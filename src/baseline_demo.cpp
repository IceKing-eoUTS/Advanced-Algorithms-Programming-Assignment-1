#include "baseline_array.hpp"

#include <iostream>
#include <string>

int main() {
    // The baseline demo uses the same public operations as the custom demo.
    // That makes it easy to see that BaselineArray is an interface-compatible
    // comparison point, not a second custom data structure.
    BaselineArray<std::string> words;
    words.emplace_back("ordinary");
    words.emplace_back("std::vector");
    words.push_back("growth");

    std::cout << "BaselineArray demo\n";
    std::cout << "size: " << words.size() << '\n';
    std::cout << "front: " << words.front() << '\n';
    std::cout << "back: " << words.back() << '\n';

    for (std::size_t index = 0; index < words.size(); ++index) {
        // Invariant relied on by both demos: logical index order is insertion
        // order. For the baseline, std::vector provides this directly.
        std::cout << index << ": " << words[index] << '\n';
    }
}
