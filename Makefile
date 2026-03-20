CXX      := g++
CXXFLAGS := -std=c++20 -O3 -march=native -pthread -Iinclude \
            -Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter
TARGET   := matching_engine
SRC      := main.cpp

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $< -o $@

clean:
	rm -f $(TARGET) audit.log
