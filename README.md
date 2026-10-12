# lockfree-ring

A lock-free single-producer / single-consumer ring buffer in C++11.
Explicit memory ordering, no mutexes, no compare-and-swap loops — plus
a mutex-based ring with the same API, so you can benchmark the
difference instead of taking my word for it.

## What's here

- `src/ring.h` — `SpscRing<T, Capacity>`: the lock-free ring, header-only
  (it's a template). Power-of-two capacity with bitmask wrapping, one
  wasted slot to keep full/empty distinguishable, and the reasoning
  behind every `memory_order` written at the use site.
- `src/mutex_ring.h` — `MutexRing<T, Capacity>`: the straightforward
  mutex version. Same API, non-blocking push/pop, kept around for the
  benchmark.
- `tests/test_ring.cpp` — self-contained suite, no frameworks, just a
  CHECK macro: 9 groups, 190 checks. FIFO order, full/empty boundaries,
  wrap-around, `size()`, non-trivial element types, mutex-ring parity,
  and three threaded tests — including a 16-slot contention case where
  producer and consumer constantly trip over the indices.
- `bench/bench.cpp` — ping-pong (one thread, raw per-op cost) and
  pipeline (two threads, ISR-to-task-style handoff) benchmarks.

## The idea in 30 seconds

One writer, one reader, no locks. The producer owns `head_`, the
consumer owns `tail_`; each side only *reads* the other's index. `push`
writes the item, then publishes the new head with `release`; `pop`
reads head with `acquire`, so the item it reads is guaranteed to be the
one the producer wrote. That's the whole trick — everything else is
bookkeeping.

```cpp
SpscRing<Event, 1024> q;   // holds 1023 Events

// producer thread:
while (!q.push(ev)) { /* full — spin or back off */ }

// consumer thread:
Event ev;
while (!q.pop(ev)) { /* empty — spin or sleep */ }
```

Non-blocking by design: push/pop return false instead of waiting, so it
fits ISR-to-task and thread-to-thread handoffs without priority-inversion
surprises.

## Build, test, benchmark

```sh
make          # builds ./bench_ring
make test     # builds and runs the suite — must be all green
make bench    # runs the benchmark
```

Strict build: `g++ -Wall -Wextra -Werror -std=c++11 -pedantic -pthread`.
No dependencies beyond a C++11 compiler.

## Numbers (mine, on this machine)

2M items, `-O2`:

| ring      | ping-pong (ns/item) | pipeline (ns/item) |
|-----------|--------------------:|-------------------:|
| SpscRing  |                 4.8 |               26.1 |
| MutexRing |                88.6 |              645.6 |

The uncontended case is ~18x — that's the price of taking a mutex even
when nobody's fighting over it. Under real two-thread contention it's
~25x on this run. Your numbers will differ (shared machines are noisy);
`make bench` takes a few seconds.

## A note on the ordering

`relaxed` for the index each thread owns itself (nobody else writes it),
`acquire` when reading the other side's index, `release` when publishing
it. Weaker than `seq_cst` everywhere, exactly as strong as it needs to
be — the comments at each use site say why. Add a second producer and
all of this breaks; that's what the MPMC rings with CAS loops are for.

## License

MIT — see LICENSE.
