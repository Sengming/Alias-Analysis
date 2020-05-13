CC=/usr/local/bin/clang
CXX=/usr/local/bin/clang++
INC=-I/usr/local/include/ -I ./include/ -I ../SVF/include/
SVF_LIB=../SVF/lib
SOURCES:= $(shell find . -type f -name '*.cpp')
OBJECTS:= $(SOURCES:.cpp=.o)
#DEBUG:= -DDEBUG_BUILD

# Test Targets
TARGET_SOURCES:=$(shell find ./tests -type f -name '*.c' -maxdepth 1)
TARGET_OBJECTS:=$(TARGET_SOURCES:.c=.o)
TARGET_BC:=$(TARGET_SOURCES:.c=_m2r.bc)

# Tiny Webserver Test
TINY_TARGET_SOURCES:=$(shell find ./tests/tiny-web-server -type f -name '*.c')
TINY_TARGET_OBJECTS:=$(TINY_TARGET_SOURCES:.c=.o)
TINY_TARGET_BC:=$(TINY_TARGET_SOURCES:.c=_m2r.bc)

CXXFLAGS = -rdynamic $(shell llvm-config --cxxflags) $(INC) -g -O0 -fPIC $(DEBUG) 
LINKFLAGS=$(shell llvm-config --ldflags --libs --cxxflags --system-libs) 


all: mvxaa.so

./tests/%_m2r.bc: ./tests/%.c
	clang -Xclang -O0 -emit-llvm -c $^ -o $(^:.c=.bc)
	opt -mem2reg $(^:.c=.bc) -o $@

./tests/tiny-web-server/%_m2r.bc: ./tests/tiny-web-server/%.c
	clang -Xclang -O0 -emit-llvm -c $^ -o $(^:.c=.bc)
	opt -mem2reg $(^:.c=.bc) -o $@

########################################

mvxaa.so: $(OBJECTS)
	$(CXX) $(LINKFLAGS) -dylib -shared  $^ $(SVF_LIB)/libSvf.a $(SVF_LIB)/CUDD/libCudd.a -o $@

clean:
	rm -f *.o *~ *.so tests/*.bc tests/*.o tests/target_app

# Run
run_mvxaa: $(TARGET_BC) all
	llvm-link $(TARGET_BC) -o ./tests/target_app_merged.bc
	opt -load ./mvxaa.so --mvx-aa -sfrander -debug-only="mvxaa" -mvx-func="call_other_function" ./tests/target_app_merged.bc -o /dev/zero

run_mvxaa_tiny: $(TINY_TARGET_BC) all
	llvm-link $(TINY_TARGET_BC) -o ./tests/tiny-web-server/tiny_merged.bc
	opt -load ./mvxaa.so --mvx-aa -sfrander -debug-only="mvxaa" -mvx-func="rio_readinitb" ./tests/tiny-web-server/tiny_merged.bc -o /dev/zero

# Haven't fully tested
debug_mvxaa: ./tests/target_app_m2r.bc all  
	gdb opt -x "break llvm::Pass::preparePassManager" -x "run -load ../andersen/lib/libAndersen.so -load ./mvxaa.so --mvx-aa -debug-only="mvxaa" $< -o /dev/zero"

## Utils to view CFG, requires xdot and graphviz:
cfg_target: ./tests/target_app_merged.bc
	opt -dot-cfg $^ -o /dev/zero

.PHONY: clean all
