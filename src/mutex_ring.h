/*
 * mutex_ring — the "obvious" implementation, kept around for the
 * benchmark. Same API as SpscRing, one std::mutex around everything,
 * non-blocking push/pop (no condition variables) so the comparison
 * stays apples-to-apples: same spin-on-full / spin-on-empty behavior,
 * the only difference is how the indices are protected.
 */
#ifndef MUTEX_RING_H
#define MUTEX_RING_H

#include <cstddef>
#include <mutex>

template <typename T, size_t Capacity>
class MutexRing {
public:
    MutexRing() : head_(0), tail_(0), count_(0) {}

    MutexRing(const MutexRing &) = delete;
    MutexRing &operator=(const MutexRing &) = delete;

    /* No wasted slot here — count_ disambiguates full from empty. */
    static constexpr size_t capacity() { return Capacity; }

    bool push(const T &item)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (count_ == Capacity)
            return false;
        buf_[head_] = item;
        head_ = (head_ + 1) % Capacity;
        ++count_;
        return true;
    }

    bool pop(T &out)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (count_ == 0)
            return false;
        out = buf_[tail_];
        tail_ = (tail_ + 1) % Capacity;
        --count_;
        return true;
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return count_ == 0;
    }

    bool full() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return count_ == Capacity;
    }

    size_t size() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return count_;
    }

private:
    T buf_[Capacity];
    size_t head_;
    size_t tail_;
    size_t count_;
    mutable std::mutex mtx_;
};

#endif /* MUTEX_RING_H */
