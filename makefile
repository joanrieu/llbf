# CONFIG

JIT=yes
CXX=clang++
CXXFLAGS=-O3

# END OF CONFIG

LLVMLIBS=core bitwriter

ifeq "$(JIT)"  "yes"
CXXFLAGS+= -DLLBF_JIT
LLVMLIBS+= jit native
endif

llbf: llbf.cpp
	$(CXX) -o $@ $^ $(CXXFLAGS) $(JITFLAG) `llvm-config --cxxflags --ldflags --libs $(LLVMLIBS)`

.PHONY: clean

clean:
	rm llbf
