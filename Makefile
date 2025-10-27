C_OPT = -std=c++11 

SUITE = lcs_classic lcs_hirschberg lcs_oblivious

.PHONY: lcs
lcs: $(SUITE)

% : src/%.c
	$(CXX) $(C_OPT) -Iinclude $< -o bin/$@
% : src/%.cpp
	$(CXX) $(C_OPT) -Iinclude $< -o bin/$@
clean:
	rm -f $(SUITE)
