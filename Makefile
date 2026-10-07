.PHONY: test test-debug test-release asan ubsan tsan benchmark example clean

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

example:
	mkdir -p build
	$(CXX) $(CXXFLAGS) -pthread -Iinclude examples/asset_workflow.cpp -o build/asset_workflow
	./build/asset_workflow

clean:
	rm -rf build benchmarks/.build
