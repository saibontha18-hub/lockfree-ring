/*
 * lockfree-ring — a single-producer / single-consumer ring buffer.
 *
 * No mutexes, no compare-and-swap loops. The trick is ownership: the
 * producer is the only thread that ever writes head_, the consumer the
 * only one that ever writes tail_. Each side only *reads* the other's
 * index, so plain atomic loads and stores with the right ordering are
 * enough. (Add a second producer and all of this breaks — that's what
 * the MPMC variants with CAS are for.)
 *
 * Capacity must be a power of two: wrapping uses a bitmask instead of
 * modulo, which is cheaper and usually a single AND instruction. One
 * slot is always left empty so "full" and "empty" stay distinguishable —
 * a SpscRing<int, 1024> therefore holds 1023 items. See capacity().
 *
 * The why behind each memory order is noted at the use site. Short
 * version: release on the write side, acquire on the read side, relaxed
 * for the index each thread owns itself.
 */
#ifndef RING_H
#define RING_H

#include <atomic>
#include <cstddef>

template <typename T, size_t Capacity>
class SpscRing {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two, at least 2");

public:
    SpscRing() : head_(0), tail_(0) {}

    /* A ring has exactly two owners by design. Copying one would silently
     * break the single-producer / single-consumer contract, so don't. */
    SpscRing(const SpscRing &) = delete;
    SpscRing &operator=(const SpscRing &) = delete;

    /* Usable slots — one is sacrificed to tell full apart from empty. */
    static constexpr size_t capacity() { return Capacity - 1; }

    /* Producer side. Returns false instead of blocking when full. */
    bool push(const T &item)
    {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & mask();

        /* Acquire the consumer's tail: without it we could read a stale
         * tail and call the ring full while slots are actually free.
         * A stale tail can only make us *under*-use the ring, never
         * corrupt it — but spurious "full" is still a bug. */
        if (next == tail_.load(std::memory_order_acquire))
            return false;

        buf_[head] = item;

        /* Release: the item write must be visible before the head update
         * is, otherwise the consumer could pop a half-written item. */
        head_.store(next, std::memory_order_release);
        return true;
    }

    /* Consumer side. Returns false instead of blocking when empty. */
    bool pop(T &out)
    {
        const size_t tail = tail_.load(std::memory_order_relaxed);

        /* Acquire pairs with the producer's release store: everything it
         * wrote before publishing head — including buf_[tail] — is
         * visible to us now. */
        if (tail == head_.load(std::memory_order_acquire))
            return false;

        out = buf_[tail];

        /* Release the slot back. The producer's acquire-load of tail_
         * then sees the free space; nothing stronger is needed. */
        tail_.store((tail + 1) & mask(), std::memory_order_release);
        return true;
    }

    bool empty() const
    {
        /* Inherently racy while the other end is moving — good enough for
         * "should I bother trying to pop", not for exact accounting. */
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    bool full() const
    {
        return ((head_.load(std::memory_order_acquire) + 1) & mask()) ==
               tail_.load(std::memory_order_acquire);
    }

    size_t size() const
    {
        /* Same racy-but-useful caveat as empty(). The subtraction wraps
         * correctly because both indices always stay below Capacity. */
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t t = tail_.load(std::memory_order_acquire);
        return (h - t) & mask();
    }

private:
    static constexpr size_t mask() { return Capacity - 1; }

    T buf_[Capacity];

    /* head_ is written only by the producer, tail_ only by the consumer.
     * (Yes, they're on the same cache line and will ping-pong it under
     * heavy use — padding them apart is a real optimization, deliberately
     * left out here to keep the code readable.) */
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
};

#endif /* RING_H */
