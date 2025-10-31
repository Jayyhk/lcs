C_OPT = -std=c++11 

SUITE = lcs_classic lcs_hirschberg lcs_oblivious lcs_hirschberg_instrumented balloon

.PHONY: lcs
lcs: $(SUITE)

lcs_classic: src/lcs_classic.c
	$(CXX) $(C_OPT) -Iinclude $< -o bin/$@
lcs_hirschberg: src/lcs_hirschberg.c
	$(CXX) $(C_OPT) -Iinclude $< -o bin/$@
lcs_oblivious: src/lcs_oblivious.c
	$(CXX) $(C_OPT) -Iinclude $< -o bin/$@
lcs_hirschberg_instrumented: src/lcs_hirschberg_instrumented.c
	gcc -Iinclude $< -lm -std=c99 -o bin/lcs_hirschberg_instrumented
balloon: src/balloon.cpp
	$(CXX) $(C_OPT) -Iinclude $< -o bin/$@

clean:
	rm -f $(addprefix bin/,$(SUITE))
