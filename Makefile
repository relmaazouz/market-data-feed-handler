CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O3 -march=native -g -Wall -Wextra -Wconversion
SRC      := data_feed

.PHONY: all clean
all: itch_feed itch_bench

itch_feed: $(SRC)/main.cpp $(SRC)/parser.hpp $(SRC)/order_book.hpp
	$(CXX) $(CXXFLAGS) $(SRC)/main.cpp -o $@

itch_bench: $(SRC)/bench.cpp $(SRC)/parser.hpp $(SRC)/order_book.hpp
	$(CXX) $(CXXFLAGS) $(SRC)/bench.cpp -o $@

clean:
	rm -f itch_feed itch_bench
