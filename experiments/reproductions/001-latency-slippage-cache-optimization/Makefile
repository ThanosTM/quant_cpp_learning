CXX ?= clang++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -pthread

BUILD := build
BENCH := $(BUILD)/bench
PROBE := $(BUILD)/probe
SRC := src/main.cpp
HDRS := $(wildcard src/*.hpp) ../../common/bench.hpp

.PHONY: all clean quick verify repeat

all: $(BENCH) $(PROBE)

$(BENCH): $(SRC) $(HDRS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@

$(PROBE): micro/probe.cpp ../../common/bench.hpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) micro/probe.cpp -o $@

quick: $(BENCH)
	./$(BENCH) --stage 0
	./$(BENCH) --stage 1 --batch 32
	./$(BENCH) --stage 2 --batch 8 --hint t0
	./$(BENCH) --stage 3 --batch 8 --hint t0
	./$(BENCH) --stage 4a
	./$(BENCH) --stage 4b
	./$(BENCH) --stage 4c

verify: $(BENCH)
	./$(BENCH) --stage verify

repeat: all
	./run_repeat.sh

clean:
	rm -rf $(BUILD)
