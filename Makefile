.PHONY: test test-debug test-release asan ubsan tsan benchmark benchmark-unique benchmark-scaling benchmark-layout benchmark-allocated-unique example clean

CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic

test:
	CXX="$(CXX)" ./scripts/test.sh

test-debug:
	CXX="$(CXX)" ./scripts/test.sh debug

test-release:
	CXX="$(CXX)" ./scripts/test.sh release

asan:
	CXX="$(CXX)" ./scripts/test.sh asan

ubsan:
	CXX="$(CXX)" ./scripts/test.sh ubsan

tsan:
	CXX="$(CXX)" ./scripts/test.sh tsan

benchmark:
	CXX="$(CXX)" ./scripts/benchmark.sh

benchmark-unique: test
	CXX="$(CXX)" UNIQUE_BENCH_TESTED_HEADER_SHA256="$$(sha256sum include/own/ownership.hpp | cut -d ' ' -f 1)" ./scripts/benchmark_unique.sh

benchmark-scaling:
	CXX="$(CXX)" ./scripts/benchmark_scaling.sh

benchmark-layout:
	CXX="$(CXX)" ./scripts/benchmark_layout.sh

benchmark-allocated-unique:
	CXX="$(CXX)" ./scripts/benchmark_allocated_unique.sh

example:
	mkdir -p build
	$(CXX) $(CXXFLAGS) -pthread -Iinclude examples/asset_workflow.cpp -o build/asset_workflow
	./build/asset_workflow
	$(CXX) $(CXXFLAGS) -pthread -Iinclude examples/owner_from_this.cpp -o build/owner_from_this
	./build/owner_from_this
	$(CXX) $(CXXFLAGS) -pthread -Iinclude examples/unique_workflow.cpp -o build/unique_workflow
	./build/unique_workflow

clean:
	rm -rf build benchmarks/.build
