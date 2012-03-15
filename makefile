# CONFIG

JIT=yes
CXX=clang++
CXXFLAGS=-O4

# END OF CONFIG

LLVMLIBS=core bitwriter ipo

ifeq "$(JIT)"  "yes"
CXXFLAGS+= -DLLBF_JIT
LLVMLIBS+= jit native
endif

llbf: llbf.cpp
	$(CXX) -o $@ $^ $(CXXFLAGS) `llvm-config --cxxflags --ldflags --libs $(LLVMLIBS)`

.PHONY: clean

clean:
	rm llbf
