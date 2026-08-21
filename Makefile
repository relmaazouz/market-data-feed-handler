CXX      ?= g++

CXXFLAGS ?= -std=c++20 -O3 -march=native -g -Wall -Wextra -Wconversion

SRC      := data_feed
OUTPUT      :=output
.PHONY: all clean test

all: itch_feed itch_bench itch_test

itch_feed: $(SRC)/main.cpp $(SRC)/parser.hpp $(SRC)/order_book.hpp
	$(CXX) $(CXXFLAGS) $(SRC)/main.cpp -o $(OUTPUT)/$@

itch_bench: $(SRC)/bench.cpp $(SRC)/parser.hpp $(SRC)/order_book.hpp
	$(CXX) $(CXXFLAGS) $(SRC)/bench.cpp -o $(OUTPUT)/$@

itch_test: $(SRC)/test.cpp $(SRC)/parser.hpp $(SRC)/order_book.hpp
	$(CXX) -std=c++20 -O2 -Wall -Wextra $(SRC)/test.cpp -o $(OUTPUT)/$@

test: itch_test
	./itch_test

clean:
	rm -f itch_feed itch_bench itch_test