CC = gcc
CXX = g++
CFLAGS = -std=c99 -Iinclude
CXXFLAGS = -std=c++11 -Iinclude

SUITE = lcs_classic lcs_hirschberg lcs_oblivious lcs_hirschberg_instrumented balloon

.PHONY: lcs
lcs: $(SUITE)

lcs_classic: src/lcs_classic.c
	$(CC) $(CFLAGS) $< -lm -o bin/$@
lcs_hirschberg: src/lcs_hirschberg.c
	$(CC) $(CFLAGS) $< -lm -o bin/$@
lcs_oblivious: src/lcs_oblivious.c
	$(CC) $(CFLAGS) $< -lm -o bin/$@
lcs_hirschberg_instrumented: src/lcs_hirschberg_instrumented.c
	$(CC) $(CFLAGS) $< -lm -o bin/$@
balloon: src/balloon.cpp
	$(CXX) $(CXXFLAGS) $< -o bin/$@

clean:
	rm -f $(addprefix bin/,$(SUITE))
