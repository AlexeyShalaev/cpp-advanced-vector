#pragma once

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

template<typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
            : buffer_(Allocate(capacity)),
              capacity_(capacity) {
    }

    RawMemory(const RawMemory &) = delete;

    RawMemory &operator=(const RawMemory &) = delete;

    RawMemory(RawMemory &&other) noexcept
            : buffer_(std::exchange(other.buffer_, nullptr)),
              capacity_(std::exchange(other.capacity_, 0)) {
    }

    RawMemory &operator=(RawMemory &&rhs) noexcept {
        buffer_ = std::exchange(rhs.buffer_, nullptr);
        capacity_ = std::exchange(rhs.capacity_, 0);
        return *this;
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T *operator+(size_t offset) noexcept {
        // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T *operator+(size_t offset) const noexcept {
        return const_cast<RawMemory &>(*this) + offset;
    }

    const T &operator[](size_t index) const noexcept {
        return const_cast<RawMemory &>(*this)[index];
    }

    T &operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory &other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T *GetAddress() const noexcept {
        return buffer_;
    }

    T *GetAddress() noexcept {
        return buffer_;
    }

    [[nodiscard]] size_t Capacity() const {
        return capacity_;
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T *Allocate(size_t n) {
        return n != 0 ? static_cast<T *>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T *buf) noexcept {
        operator delete(buf);
    }

    T *buffer_ = nullptr;
    size_t capacity_ = 0;
};

template<typename T>
class Vector {
public:
    using iterator = T *;
    using const_iterator = const T *;

    // Constructors

    Vector() = default;

    explicit Vector(size_t size) : data_(size),
                                   size_(size) {
        std::uninitialized_value_construct_n(begin(), size);
    }

    Vector(const Vector &other) : data_(other.size_),
                                  size_(other.size_) {
        std::uninitialized_copy_n(other.begin(), size_, begin());
    }

    Vector(Vector &&other) noexcept: data_(std::move(other.data_)),
                                     size_(std::exchange(other.size_, 0)) {
    }

    // Assignments

    Vector &operator=(const Vector &rhs) {
        if (this != &rhs) {
            if (rhs.size_ > data_.Capacity()) {
                Vector rhs_copy(rhs);
                Swap(rhs_copy);
            } else {
                std::copy_n(rhs.begin(), std::min(rhs.size_, size_), begin());
                if (size_ > rhs.size_) {
                    std::destroy_n(begin() + rhs.size_,
                                   size_ - rhs.size_);
                } else {
                    std::uninitialized_copy_n(rhs.begin() + size_,
                                              rhs.size_ - size_,
                                              end());
                }
                size_ = rhs.size_;
            }
        }
        return *this;
    }

    Vector &operator=(Vector &&rhs) noexcept {
        if (this != &rhs) {
            data_ = std::move(rhs.data_);
            size_ = std::exchange(rhs.size_, 0);
        }
        return *this;
    }

    // Destructor

    ~Vector() {
        std::destroy_n(begin(), size_);
    }

    // Extra

    void Swap(Vector &other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }
        RawMemory<T> new_data(new_capacity);
        Relocate(begin(), size_, new_data.GetAddress());
        std::destroy_n(begin(), size_);
        data_.Swap(new_data);
    }

    void Resize(size_t new_size) {
        if (new_size == size_) {
            return;
        }
        if (new_size < size_) {
            std::destroy_n(data_.GetAddress() + new_size, size_ - new_size);
        } else if (new_size > size_) {
            if (new_size > data_.Capacity()) {
                Reserve(new_size);
            }
            std::uninitialized_value_construct_n(end(), new_size - size_);
        }
        size_ = new_size;
    }

    // Inserts

    void PushBack(const T &value) {
        EmplaceBack(value);
    }

    void PushBack(T &&value) {
        EmplaceBack(std::move(value));
    }

    template<typename... Args>
    T &EmplaceBack(Args &&... args) {
        return *Emplace(end(), std::forward<Args>(args)...);
    }

    template<typename... Args>
    iterator Emplace(const_iterator pos, Args &&... args) {
        assert(pos >= cbegin() && pos <= cend());
        auto dist = std::distance(cbegin(), pos);
        if (size_ != Capacity()) {
            if (pos == nullptr || pos == end()) {
                if constexpr (std::is_nothrow_move_constructible_v<T> || std::is_copy_constructible_v<T>) {
                    try {
                        ForwardConstruct(data_ + dist, std::forward<Args>(args)...);
                    }
                    catch (...) {
                        std::destroy_at(data_ + dist);
                        throw;
                    }
                } else {
                    ForwardConstruct(data_ + dist, std::forward<Args>(args)...);
                }
            } else {
                ForwardConstruct(end(), std::forward<T>(data_[size_]));
                std::move_backward(data_ + dist, end(), std::next(end()));
                std::destroy_at(data_ + size_);
                ForwardConstruct(data_ + dist, std::forward<Args>(args)...);
            }
        } else {
            RawMemory<T> new_data(size_ > 0 ? size_ * 2 : 1);
            ForwardConstruct(new_data + dist, std::forward<Args>(args)...);
            Relocate(begin(), dist, new_data.GetAddress());
            Relocate(begin() + dist, size_ - dist, new_data.GetAddress() + dist + 1);
            std::destroy_n(begin(), size_);
            data_.Swap(new_data);
        }
        ++size_;
        return begin() + dist;
    }

    iterator Insert(const_iterator pos, const T &value) {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T &&value) {
        return Emplace(pos, std::move(value));
    }

    // Deletes

    void PopBack() noexcept {
        std::destroy_at(data_ + (--size_));
    }

    iterator Erase(const_iterator pos) noexcept(std::is_nothrow_move_assignable_v<T>) {
        assert(pos >= cbegin() && pos <= cend());
        auto dist = std::distance(cbegin(), pos);
        auto iter = const_cast<iterator>(pos);
        if (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::move(std::next(iter), end(), iter);
        } else {
            std::copy(std::next(iter), end(), iter);
        }
        PopBack();
        return begin() + dist;
    }

    // Getters

    [[nodiscard]] size_t Size() const noexcept {
        return size_;
    }

    [[nodiscard]] size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    // Accesses

    const T &operator[](size_t index) const noexcept {
        return const_cast<Vector &>(*this)[index];
    }

    T &operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    // Iterators

    iterator begin() noexcept {
        return iterator(data_.GetAddress());
    }

    iterator end() noexcept {
        return iterator(data_.GetAddress() + size_);
    }

    const_iterator begin() const noexcept {
        return const_iterator(data_.GetAddress());
    }

    const_iterator end() const noexcept {
        return const_iterator(data_.GetAddress() + size_);
    }

    const_iterator cbegin() const noexcept {
        return const_iterator(data_.GetAddress());
    }

    const_iterator cend() const noexcept {
        return const_iterator(data_.GetAddress() + size_);
    }

private:
    void static Relocate(T *InFirst, size_t dist, T *OutFirst) {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(InFirst, dist, OutFirst);
        } else {
            std::uninitialized_copy_n(InFirst, dist, OutFirst);
        }
    }

    template<typename... Args>
    static void ForwardConstruct(T *Iter, Args &&... args) {
        new(Iter) T(std::forward<Args>(args)...);
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;
};
