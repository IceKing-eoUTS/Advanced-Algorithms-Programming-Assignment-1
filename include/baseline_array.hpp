#pragma once

#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

template <typename T>
class BaselineArray {
public:
    using value_type = T;
    using size_type = std::size_t;
    using reference = T&;
    using const_reference = const T&;

    BaselineArray() = default;
    ~BaselineArray() = default;

    BaselineArray(const BaselineArray&) = default;
    BaselineArray(BaselineArray&&) noexcept = default;
    BaselineArray& operator=(const BaselineArray&) = default;
    BaselineArray& operator=(BaselineArray&&) noexcept = default;

    void push_back(const T& value) {
        data_.push_back(value);
    }

    void push_back(T&& value) {
        data_.push_back(std::move(value));
    }

    template <typename... Args>
    reference emplace_back(Args&&... args) {
        return data_.emplace_back(std::forward<Args>(args)...);
    }

    reference operator[](size_type index) noexcept {
        return data_[index];
    }

    const_reference operator[](size_type index) const noexcept {
        return data_[index];
    }

    reference at(size_type index) {
        return data_.at(index);
    }

    const_reference at(size_type index) const {
        return data_.at(index);
    }

    reference front() {
        if (empty()) {
            throw std::out_of_range("BaselineArray::front on empty array");
        }
        return data_.front();
    }

    const_reference front() const {
        if (empty()) {
            throw std::out_of_range("BaselineArray::front on empty array");
        }
        return data_.front();
    }

    reference back() {
        if (empty()) {
            throw std::out_of_range("BaselineArray::back on empty array");
        }
        return data_.back();
    }

    const_reference back() const {
        if (empty()) {
            throw std::out_of_range("BaselineArray::back on empty array");
        }
        return data_.back();
    }

    [[nodiscard]] size_type size() const noexcept {
        return data_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return data_.empty();
    }

    void clear() noexcept {
        data_.clear();
    }

    [[nodiscard]] size_type capacity() const noexcept {
        return data_.capacity();
    }

private:
    std::vector<T> data_;
};
