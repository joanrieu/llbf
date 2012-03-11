llbf: llbf.cpp
	clang++ -o $@ $^ -ggdb `llvm-config --cxxflags --ldflags --libs core`

.PHONY: clean

clean:
	rm llbf
