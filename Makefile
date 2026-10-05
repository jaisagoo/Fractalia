CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra

SOURCES := $(wildcard *.cpp)
OBJECTS := $(SOURCES:.cpp=.o)
APP_OBJECTS := visualiser.o fractal_calculator.o

.PHONY: all clean $(SOURCES)

all: visualiser.exe

visualiser.exe: $(APP_OBJECTS)
	$(CXX) $(CXXFLAGS) -mwindows $^ -o $@

$(OBJECTS):
	$(CXX) $(CXXFLAGS) -c $(@:.o=.cpp) -o $@

$(SOURCES):
	$(CXX) $(CXXFLAGS) -c $@ -o $(@:.cpp=.o)

clean:
	$(RM) $(OBJECTS) visualiser.exe