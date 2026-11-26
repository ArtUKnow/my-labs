CXX ?= c++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wpedantic
NATIVE_SOURCES = native/main.cpp native/text.cpp native/text_tests.cpp native/index.cpp native/index_tests.cpp native/metrics.cpp native/rank.cpp native/rank_tests.cpp

.PHONY: all test clean
all: build/ir
build/ir: $(NATIVE_SOURCES) native/text.h native/index.h native/rank.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) $(NATIVE_SOURCES) -o $@ -lz

test: build/ir
	./build/ir selftest
clean:
	rm -f build/ir
