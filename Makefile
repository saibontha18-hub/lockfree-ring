CXX      = g++
CXXFLAGS = -Wall -Wextra -Werror -std=c++11 -pedantic -O2 -pthread
INCDIR   = src

all: bench_ring

bench_ring: bench/bench.cpp src/ring.h src/mutex_ring.h
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -o $@ bench/bench.cpp

test_ring: tests/test_ring.cpp src/ring.h src/mutex_ring.h
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -o $@ tests/test_ring.cpp

test: test_ring
	./test_ring

bench: bench_ring
	./bench_ring

clean:
	rm -f bench_ring test_ring

.PHONY: all test bench clean
