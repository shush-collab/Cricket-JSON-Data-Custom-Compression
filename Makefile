CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic -Iinclude -Ithird_party

CODEC_SRC := src/cjdc_codec.cpp

.PHONY: all clean test

all: compress decompress

compress: compress.cpp $(CODEC_SRC) include/cjdc_codec.hpp
	$(CXX) $(CXXFLAGS) compress.cpp $(CODEC_SRC) -o $@

decompress: decompress.cpp $(CODEC_SRC) include/cjdc_codec.hpp
	$(CXX) $(CXXFLAGS) decompress.cpp $(CODEC_SRC) -o $@

test: all
	./scripts/roundtrip.sh

clean:
	rm -f compress decompress
	rm -rf build
