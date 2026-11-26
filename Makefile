CXX ?= c++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wpedantic
CPPFLAGS = -Icommon -Ilab02_crawler -Ilab04_stemmer -Ilab08_search
LIBS = -lz -lcurl -lsqlite3
ifeq ($(shell uname -s),Linux)
LIBS += -lcrypto
endif

SOURCES = main.cpp common/text_commands.cpp \
 lab01_corpus/corpus.cpp \
 lab02_crawler/crawler.cpp lab02_crawler/crawler_tests.cpp \
 lab03_tokenizer/tokenizer.cpp lab03_tokenizer/test_text.cpp \
 lab04_stemmer/stemmer.cpp lab05_zipf/zipf.cpp \
 lab06_quality/metrics.cpp lab06_quality/rank.cpp lab06_quality/test_rank.cpp \
 lab07_index/index.cpp lab07_index/test_index.cpp \
 lab08_search/server.cpp lab08_search/server_tests.cpp
HEADERS = $(wildcard common/*.h lab02_crawler/*.h lab04_stemmer/*.h lab08_search/*.h)

.PHONY: all test clean
all: build/ir
build/ir: $(SOURCES) $(HEADERS)
	mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) -o $@ $(LIBS)
test: build/ir
	./build/ir selftest
clean:
	rm -f build/ir
