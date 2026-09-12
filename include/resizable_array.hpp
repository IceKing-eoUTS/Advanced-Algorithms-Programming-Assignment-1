#pragma once

#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

template <typename T>
class ResizableArray {
    static_assert(std::is_destructible_v<T>,
                  "ResizableArray<T> requires T to be destructible");

public:
    using value_type = T;
    using size_type = std::size_t;
    using reference = T&;
    using const_reference = const T&;

    ResizableArray() = default;

    ~ResizableArray() noexcept(std::is_nothrow_destructible_v<T>) {
        release_all();
    }

    ResizableArray(const ResizableArray& other) {
        copy_from(other);
    }

    ResizableArray(ResizableArray&& other) noexcept {
        steal_from(other);
    }

    ResizableArray& operator=(const ResizableArray& other) {
        if (this == &other) {
            return *this;
        }

        ResizableArray copy(other);
        swap(copy);
        return *this;
    }

    ResizableArray& operator=(ResizableArray&& other) noexcept(
        std::is_nothrow_destructible_v<T>) {
        if (this == &other) {
            return *this;
        }

        release_all();
        steal_from(other);
        return *this;
    }

    void push_back(const T& value) {
        emplace_back(value);
    }

    void push_back(T&& value) {
        emplace_back(std::move(value));
    }

    template <typename... Args>
    reference emplace_back(Args&&... args) {
        // Core algorithm entry point:
        // prepare_for_insert() either creates the first buffer or starts an
        // incremental migration before the current buffer is full. The new
        // element is then constructed at its final logical index in data_.
        prepare_for_insert();

        const size_type insert_index = size_;
        T* destination = data_ + insert_index;
        std::construct_at(destination, std::forward<Args>(args)...);

        try {
            // Tricky part: migration happens after constructing the new
            // element but before size_ is increased. If migrating an old
            // element throws, the just-constructed new element is destroyed
            // and the public size remains unchanged.
            migrate_some(kMigrationWorkPerInsertion);
        } catch (...) {
            std::destroy_at(destination);
            throw;
        }

        ++size_;
        return *destination;
    }

    reference operator[](size_type index) noexcept {
        return element_at(index);
    }

    const_reference operator[](size_type index) const noexcept {
        return element_at(index);
    }

    reference at(size_type index) {
        if (index >= size_) {
            throw std::out_of_range("ResizableArray::at index out of range");
        }
        return element_at(index);
    }

    const_reference at(size_type index) const {
        if (index >= size_) {
            throw std::out_of_range("ResizableArray::at index out of range");
        }
        return element_at(index);
    }

    reference front() {
        if (empty()) {
            throw std::out_of_range("ResizableArray::front on empty array");
        }
        return element_at(0);
    }

    const_reference front() const {
        if (empty()) {
            throw std::out_of_range("ResizableArray::front on empty array");
        }
        return element_at(0);
    }

    reference back() {
        if (empty()) {
            throw std::out_of_range("ResizableArray::back on empty array");
        }
        return element_at(size_ - 1);
    }

    const_reference back() const {
        if (empty()) {
            throw std::out_of_range("ResizableArray::back on empty array");
        }
        return element_at(size_ - 1);
    }

    [[nodiscard]] size_type size() const noexcept {
        return size_;
    }

    [[nodiscard]] bool empty() const noexcept {
        return size_ == 0;
    }

    [[nodiscard]] size_type estimated_allocated_slots() const noexcept {
        if (capacity_ > std::numeric_limits<size_type>::max() - old_capacity_) {
            return std::numeric_limits<size_type>::max();
        }
        return capacity_ + old_capacity_;
    }

    [[nodiscard]] static size_type max_size() noexcept {
        std::allocator<T> allocator;
        return std::allocator_traits<std::allocator<T>>::max_size(allocator);
    }

    void clear() noexcept(std::is_nothrow_destructible_v<T>) {
        release_all();
    }

    void swap(ResizableArray& other) noexcept {
        using std::swap;
        swap(data_, other.data_);
        swap(capacity_, other.capacity_);
        swap(size_, other.size_);
        swap(old_data_, other.old_data_);
        swap(old_capacity_, other.old_capacity_);
        swap(migration_size_, other.migration_size_);
        swap(migrated_, other.migrated_);
    }

private:
    static constexpr size_type kInitialCapacity = 2;
    static constexpr size_type kMigrationWorkPerInsertion = 1;

    // Core data structure organization:
    // data_ is always the current/new buffer. old_data_ is non-null only while
    // an incremental migration is active. size_ is the public logical length.
    T* data_ = nullptr;
    size_type capacity_ = 0;
    size_type size_ = 0;

    // Migration invariant:
    // - [0, migrated_) has already been constructed in data_.
    // - [migrated_, migration_size_) is still live in old_data_.
    // - [migration_size_, size_) contains new insertions in data_.
    // This invariant is established in begin_migration(), advanced in
    // migrate_one(), and relied on by element_at() and destroy_live_elements().
    T* old_data_ = nullptr;
    size_type old_capacity_ = 0;
    size_type migration_size_ = 0;
    size_type migrated_ = 0;

    [[nodiscard]] bool migrating() const noexcept {
        return old_data_ != nullptr;
    }

    static T* allocate_storage(size_type capacity) {
        if (capacity == 0) {
            return nullptr;
        }

        if (capacity > max_size()) {
            throw std::length_error("ResizableArray capacity exceeds max_size");
        }

        std::allocator<T> allocator;
        return std::allocator_traits<std::allocator<T>>::allocate(allocator,
                                                                  capacity);
    }

    static void deallocate_storage(T* pointer,
                                   size_type capacity) noexcept {
        if (pointer == nullptr) {
            return;
        }

        std::allocator<T> allocator;
        std::allocator_traits<std::allocator<T>>::deallocate(allocator, pointer,
                                                             capacity);
    }

    static size_type doubled_capacity(size_type current) {
        if (current == 0) {
            return kInitialCapacity;
        }

        if (current > max_size() / 2) {
            throw std::length_error("ResizableArray capacity overflow");
        }

        return current * 2;
    }

    static size_type capacity_for_size(size_type live_size) {
        if (live_size == 0) {
            return 0;
        }

        if (live_size > max_size()) {
            throw std::length_error("ResizableArray size exceeds max_size");
        }

        if (live_size > max_size() / 2) {
            return live_size;
        }

        const size_type doubled = live_size * 2;
        return doubled < kInitialCapacity ? kInitialCapacity : doubled;
    }

    void prepare_for_insert() {
        if (capacity_ == 0) {
            data_ = allocate_storage(kInitialCapacity);
            capacity_ = kInitialCapacity;
            return;
        }

        // What breaks if this threshold is delayed until capacity_ is full?
        // With only one migrated element per insertion, there would not be
        // enough future insertions to finish migration before the new buffer
        // needs to grow again, and worst-case O(1) push_back would be lost.
        if (!migrating() && size_ >= capacity_ / 2) {
            begin_migration();
        }
    }

    void begin_migration() {
        const size_type replacement_capacity = doubled_capacity(capacity_);
        T* replacement = allocate_storage(replacement_capacity);

        // Establish the migration invariant. No element is moved here; the
        // old buffer simply becomes the migration source and the replacement
        // buffer becomes the insertion destination.
        old_data_ = data_;
        old_capacity_ = capacity_;
        migration_size_ = size_;
        migrated_ = 0;

        data_ = replacement;
        capacity_ = replacement_capacity;
    }

    void migrate_some(size_type max_elements) {
        while (max_elements > 0 && migrating()) {
            migrate_one();
            --max_elements;
        }
    }

    void migrate_one() {
        T* source = old_data_ + migrated_;
        T* destination = data_ + migrated_;

        // If moving might throw and copying is available, copy instead. That
        // keeps the source element intact until construction in data_ succeeds.
        if constexpr (std::is_nothrow_move_constructible_v<T> ||
                      !std::is_copy_constructible_v<T>) {
            std::construct_at(destination, std::move(*source));
        } else {
            std::construct_at(destination, *source);
        }

        // Advance the invariant exactly one slot: after this destroy_at,
        // source is no longer live and destination is the live copy/move.
        std::destroy_at(source);
        ++migrated_;

        if (migrated_ == migration_size_) {
            finish_migration();
        }
    }

    void finish_migration() noexcept {
        deallocate_storage(old_data_, old_capacity_);
        old_data_ = nullptr;
        old_capacity_ = 0;
        migration_size_ = 0;
        migrated_ = 0;
    }

    reference element_at(size_type index) noexcept {
        if (!migrating()) {
            return data_[index];
        }

        // Reliance on the migration invariant:
        // already-migrated old elements and newly inserted elements are in
        // data_, while the not-yet-migrated middle region remains in old_data_.
        if (index < migrated_ || index >= migration_size_) {
            return data_[index];
        }

        return old_data_[index];
    }

    const_reference element_at(size_type index) const noexcept {
        if (!migrating()) {
            return data_[index];
        }

        // This branch mirrors the mutable overload so const and non-const
        // indexing observe the same logical sequence during migration.
        if (index < migrated_ || index >= migration_size_) {
            return data_[index];
        }

        return old_data_[index];
    }

    void destroy_live_elements() noexcept(std::is_nothrow_destructible_v<T>) {
        if (!migrating()) {
            for (size_type index = 0; index < size_; ++index) {
                std::destroy_at(data_ + index);
            }
            return;
        }

        for (size_type index = 0; index < migrated_; ++index) {
            std::destroy_at(data_ + index);
        }
        // What breaks if this loop is changed to start at 0? Elements already
        // moved to data_ would be destroyed twice: once in data_ above and once
        // again through their old storage here.
        for (size_type index = migrated_; index < migration_size_; ++index) {
            std::destroy_at(old_data_ + index);
        }
        for (size_type index = migration_size_; index < size_; ++index) {
            std::destroy_at(data_ + index);
        }
    }

    void release_all() noexcept(std::is_nothrow_destructible_v<T>) {
        destroy_live_elements();
        deallocate_storage(data_, capacity_);
        deallocate_storage(old_data_, old_capacity_);

        data_ = nullptr;
        capacity_ = 0;
        size_ = 0;
        old_data_ = nullptr;
        old_capacity_ = 0;
        migration_size_ = 0;
        migrated_ = 0;
    }

    void copy_from(const ResizableArray& other) {
        // Copies are normalized into one fresh non-migrating buffer. That keeps
        // the copied object simple while preserving the logical order observed
        // through other[index], whether other is currently migrating or not.
        const size_type desired_capacity = capacity_for_size(other.size_);
        T* storage = allocate_storage(desired_capacity);
        size_type constructed = 0;

        try {
            for (; constructed < other.size_; ++constructed) {
                std::construct_at(storage + constructed, other[constructed]);
            }
        } catch (...) {
            for (size_type index = 0; index < constructed; ++index) {
                std::destroy_at(storage + index);
            }
            deallocate_storage(storage, desired_capacity);
            throw;
        }

        data_ = storage;
        capacity_ = desired_capacity;
        size_ = other.size_;
    }

    void steal_from(ResizableArray& other) noexcept {
        // Move construction/assignment transfers both buffers and all migration
        // counters. Resetting other prevents two arrays from owning and later
        // destroying the same raw storage.
        data_ = other.data_;
        capacity_ = other.capacity_;
        size_ = other.size_;
        old_data_ = other.old_data_;
        old_capacity_ = other.old_capacity_;
        migration_size_ = other.migration_size_;
        migrated_ = other.migrated_;

        other.data_ = nullptr;
        other.capacity_ = 0;
        other.size_ = 0;
        other.old_data_ = nullptr;
        other.old_capacity_ = 0;
        other.migration_size_ = 0;
        other.migrated_ = 0;
    }
};

template <typename T>
void swap(ResizableArray<T>& left, ResizableArray<T>& right) noexcept {
    left.swap(right);
}
