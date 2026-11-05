CXX ?= clang++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -pthread

BUILD := build
BENCH := $(BUILD)/bench
ASM := results/search_asm.s
SRC := src/main.cpp
HDRS := $(wildcard src/*.hpp) ../../common/bench.hpp

.PHONY: all clean quick verify repeat sweep asm

all: $(BENCH)

$(BENCH): $(SRC) $(HDRS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@

asm: $(ASM)

$(ASM): micro/search_asm.cpp
	@mkdir -p $(BUILD) results
	$(CXX) -std=c++20 -O2 -S micro/search_asm.cpp -o $(ASM)

quick: $(BENCH)
	./$(BENCH) --gen-events build/events.bin --ops 200000 --max-levels 48
	./$(BENCH) --events build/events.bin --case map-clean --warmup 20000
	./$(BENCH) --events build/events.bin --case map-polluted --warmup 20000
	./$(BENCH) --events build/events.bin --case vector-front --warmup 20000
	./$(BENCH) --events build/events.bin --case vector-back --warmup 20000
	./$(BENCH) --events build/events.bin --case binary-branchy --warmup 20000
	./$(BENCH) --events build/events.bin --case binary-branchless --warmup 20000
	./$(BENCH) --events build/events.bin --case linear --warmup 20000

verify: $(BENCH)
	./$(BENCH) --gen-events build/events.bin --ops 200000 --max-levels 48
	./$(BENCH) --events build/events.bin --verify

repeat: all
	./run_repeat.sh

sweep: all
	./run_sweep.sh

clean:
	rm -rf $(BUILD)
