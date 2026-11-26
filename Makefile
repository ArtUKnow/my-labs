CXX ?= c++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wpedantic
CPPFLAGS = -Icommon -Ilab02_crawler -Ilab04_stemmer -Ilab08_search
LIBS = -lz -lcurl -lsqlite3
ifeq ($(shell uname -s),Linux)
LIBS += -lcrypto
endif

SOURCES = main.cpp $(wildcard common/*.cpp lab*/*.cpp)
HEADERS = $(wildcard common/*.h lab*/*.h)

.PHONY: all test test-tokenizer test-stemmer clean
all: build/ir
build/ir: $(SOURCES) $(HEADERS)
	mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) -o $@ $(LIBS)
test: build/ir
	./build/ir selftest
test-tokenizer: build/ir
	./build/ir test-tokenizer
test-stemmer: build/ir
	./build/ir test-stemmer
clean:
	rm -f build/ir
