CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra

SOURCES := $(wildcard *.cpp)
OBJECTS := $(SOURCES:.cpp=.o)

.PHONY: clean $(SOURCES)

$(SOURCES):
	$(CXX) $(CXXFLAGS) -c $@ -o $(@:.cpp=.o)

clean:
	$(RM) $(OBJECTS)