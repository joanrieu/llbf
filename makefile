CXX=clang++
CXXFLAGS=-O3

llbf: llbf.cpp
	$(CXX) -o $@ $^ $(CXXFLAGS) `llvm-config --cxxflags --ldflags --libs core`

.PHONY: clean

clean:
	rm llbf
