#include "resizable_array.hpp"

#include <iostream>
#include <string>

int main() {
    ResizableArray<std::string> words;
    words.emplace_back("incremental");
    words.emplace_back("resizing");
    words.push_back("keeps");
    words.push_back("order");

    std::cout << "ResizableArray demo\n";
    std::cout << "size: " << words.size() << '\n';
    std::cout << "front: " << words.front() << '\n';
    std::cout << "back: " << words.back() << '\n';

    for (std::size_t index = 0; index < words.size(); ++index) {
        std::cout << index << ": " << words[index] << '\n';
    }
}
