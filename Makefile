C_OPT = -std=c++11 

SUITE = lcs_classic lcs_hirschberg lcs_oblivious

.PHONY: lcs
lcs: $(SUITE)

lcs_classic: src/lcs_classic.c
	$(CXX) $(C_OPT) -Iinclude $< -o bin/$@
lcs_hirschberg: src/lcs_hirschberg.c
	$(CXX) $(C_OPT) -Iinclude $< -o bin/$@
lcs_oblivious: src/lcs_oblivious.c
	$(CXX) $(C_OPT) -Iinclude $< -o bin/$@

clean:
	rm -f $(addprefix bin/,$(SUITE))
