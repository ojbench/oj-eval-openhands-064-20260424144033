CXX = g++-13
CXXFLAGS = -std=c++20 -Wall -Wextra -O2

code: main.cpp printf.hpp
	$(CXX) $(CXXFLAGS) -o code main.cpp

clean:
	rm -f code

.PHONY: clean
