/*
 * lockfree-ring test suite. Self-contained, no frameworks — a CHECK macro
 * and a nonzero exit on failure, same style as the C projects. The
 * threaded tests are the interesting ones: they hammer the ring from two
 * threads and check every item arrives intact and in order.
 */
#include <cstdio>
#include <string>
#include <thread>

#include "ring.h"
#include "mutex_ring.h"

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        ++checks;                                                       \
        if (!(cond)) {                                                  \
            ++failures;                                                 \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);          \
        }                                                               \
    } while (0)

/* Basic FIFO order, single thread. */
static void test_fifo()
{
    SpscRing<int, 16> r;
    int v = 0;
    for (int i = 0; i < 10; ++i)
        CHECK(r.push(i));
    for (int i = 0; i < 10; ++i) {
        CHECK(r.pop(v));
        CHECK(v == i);
    }
    CHECK(!r.pop(v));           /* empty now */
    CHECK(r.empty());
}

/* Full really means full: capacity() items, then push refuses. */
static void test_full_boundary()
{
    SpscRing<int, 8> r;         /* holds 7 — one slot stays empty */
    int v = 0;
    CHECK((SpscRing<int, 8>::capacity() == 7));
    for (int i = 0; i < 7; ++i)
        CHECK(r.push(i));
    CHECK(r.full());
    CHECK(!r.push(99));
    CHECK(r.pop(v) && v == 0);
    CHECK(!r.full());
    CHECK(r.push(99));
}

/* Wrap the indices around the end of the buffer repeatedly, order intact. */
static void test_wrap_around()
{
    SpscRing<int, 8> r;
    int v = 0, expect = 0;
    for (int round = 0; round < 5; ++round) {
        for (int i = 0; i < 7; ++i)
            CHECK(r.push(expect + i));
        for (int i = 0; i < 7; ++i) {
            CHECK(r.pop(v));
            CHECK(v == expect + i);
        }
        expect += 7;
    }
    CHECK(r.empty());
}

/* size() tracks the count through pushes and pops. */
static void test_size()
{
    SpscRing<int, 16> r;
    int v = 0;
    CHECK(r.size() == 0);
    for (int i = 0; i < 5; ++i)
        r.push(i);
    CHECK(r.size() == 5);
    r.pop(v);
    r.pop(v);
    CHECK(r.size() == 3);
}

/* Non-trivial element type — the template isn't just for ints. */
static void test_nontrivial_type()
{
    SpscRing<std::string, 8> r;
    std::string v;
    CHECK(r.push("hello"));
    CHECK(r.push("ring"));
    CHECK(r.pop(v) && v == "hello");
    CHECK(r.pop(v) && v == "ring");
    CHECK(r.empty());
}

/* The mutex ring honors the same contract, single-threaded. */
static void test_mutex_parity()
{
    MutexRing<int, 8> m;
    int v = 0;
    CHECK((MutexRing<int, 8>::capacity() == 8));   /* no wasted slot here */
    for (int i = 0; i < 8; ++i)
        CHECK(m.push(i));
    CHECK(m.full());
    CHECK(!m.push(1));
    for (int i = 0; i < 8; ++i) {
        CHECK(m.pop(v));
        CHECK(v == i);
    }
    CHECK(m.empty());
}

/*
 * Payload integrity under real concurrency. seq and payload are written
 * together by the producer; if the acquire/release pairing were wrong the
 * consumer could see a new seq with a stale payload. SPSC must also
 * preserve order — any reorder is a bug, not just bad luck.
 */
struct Item {
    unsigned seq;
    unsigned payload;
};

template <typename Ring>
static void producer(Ring *r, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        Item it = { i, i * 0x9E3779B9u };
        while (!r->push(it)) {
            /* ring full — consumer is behind; spin like an ISR would */
        }
    }
}

template <typename Ring>
static unsigned consumer(Ring *r, unsigned n)
{
    unsigned bad = 0, got = 0;
    Item it = { 0, 0 };
    while (got < n) {
        if (r->pop(it)) {
            if (it.payload != it.seq * 0x9E3779B9u)
                ++bad;                  /* torn read */
            if (it.seq != got)
                ++bad;                  /* reorder or loss */
            ++got;
        }
    }
    return bad;
}

template <typename Ring>
static unsigned run_threaded(unsigned n)
{
    Ring r;
    unsigned bad = 0;
    std::thread prod(producer<Ring>, &r, n);
    std::thread cons([&]() { bad = consumer(&r, n); });
    prod.join();
    cons.join();
    if (!r.empty())
        ++bad;
    return bad;
}

/* Big ring, 200k items across two threads. */
static void test_threaded_spsc()
{
    CHECK((run_threaded<SpscRing<Item, 1024> >(200000) == 0));
}

/* Tiny ring, 16 slots: producer and consumer constantly trip over the
 * indices — this is the contention case, and where a memory-ordering
 * mistake would actually show up. */
static void test_threaded_contention()
{
    CHECK((run_threaded<SpscRing<Item, 16> >(50000) == 0));
}

/* Same workout for the mutex ring, as a sanity baseline. */
static void test_threaded_mutex()
{
    CHECK((run_threaded<MutexRing<Item, 1024> >(200000) == 0));
}

int main()
{
    test_fifo();
    test_full_boundary();
    test_wrap_around();
    test_size();
    test_nontrivial_type();
    test_mutex_parity();
    test_threaded_spsc();
    test_threaded_contention();
    test_threaded_mutex();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
