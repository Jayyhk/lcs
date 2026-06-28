CC = g++
CXX = g++

CFLAGS = -std=c99 -O3 -DNDEBUG -Wall -Iinclude
CXXFLAGS = -std=c++11 -O3 -DNDEBUG -Wall -Iinclude
LCS_LDFLAGS = -lm

SUITE = lcs_classic lcs_hirschberg lcs_oblivious lcs_hirschberg_instrumented lcs_oblivious_instrumented mem_controller

all: $(SUITE)

lcs_classic: src/lcs_classic.c include/util.h
	$(CXX) $(CXXFLAGS) $< -o bin/$@ $(LCS_LDFLAGS)
lcs_hirschberg: src/lcs_hirschberg.c include/util.h
	$(CXX) $(CXXFLAGS) $< -o bin/$@ $(LCS_LDFLAGS)
lcs_oblivious: src/lcs_oblivious.c include/util.h
	$(CXX) $(CXXFLAGS) $< -o bin/$@ $(LCS_LDFLAGS)
lcs_hirschberg_instrumented: src/lcs_hirschberg_instrumented.cpp include/util.h
	$(CXX) $(CXXFLAGS) $< -o bin/$@ $(LCS_LDFLAGS)
lcs_oblivious_instrumented: src/lcs_oblivious_instrumented.cpp include/util.h
	$(CXX) $(CXXFLAGS) $< -o bin/$@ $(LCS_LDFLAGS)
mem_controller: src/mem_controller.cpp
	$(CXX) $(CXXFLAGS) $< -o bin/$@

clean:
	rm -f $(addprefix bin/,$(SUITE))

.PHONY: all clean
