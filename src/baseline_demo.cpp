#include "baseline_array.hpp"

#include <iostream>
#include <string>

int main() {
    BaselineArray<std::string> words;
    words.emplace_back("ordinary");
    words.emplace_back("std::vector");
    words.push_back("growth");

    std::cout << "BaselineArray demo\n";
    std::cout << "size: " << words.size() << '\n';
    std::cout << "front: " << words.front() << '\n';
    std::cout << "back: " << words.back() << '\n';

    for (std::size_t index = 0; index < words.size(); ++index) {
        std::cout << index << ": " << words[index] << '\n';
    }
}
