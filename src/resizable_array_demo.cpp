#include "resizable_array.hpp"

#include <iostream>
#include <string>

int main() {
    // This tiny program exercises the core ResizableArray interface without
    // exposing implementation details. Internally, some of these insertions may
    // trigger incremental migration, but the visible order stays unchanged.
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
        // What breaks if ResizableArray::element_at chooses the wrong buffer
        // during migration? This loop would print missing, moved-from, or
        // out-of-order values even though size() still looks correct.
        std::cout << index << ": " << words[index] << '\n';
    }
}
