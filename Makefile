INC=-I/usr/local/include/ -I ./include/ -I ../andersen/include
SOURCES:= $(shell find . -type f -name '*.cpp')
OBJECTS:= $(SOURCES:.cpp=.o)
#DEBUG:= -DDEBUG_BUILD

CXXFLAGS = -rdynamic $(shell llvm-config --cxxflags) $(INC) -g -O0 -fPIC $(DEBUG) 

all: mvxaa.so

./tests/%_m2r.bc: ./tests/%.c
	clang -Xclang -O0 -emit-llvm -c $^ -o $(^:.c=.bc)
	opt -mem2reg $(^:.c=.bc) -o $@

mvxaa.so: MVXAA.o $(OBJECTS)
	$(CXX) -dylib -shared $^ -o $@

clean:
	rm -f *.o *~ *.so tests/*.bc

# Uncomment and fix targets below after adding test cases
# Run
run_mvxaa: ./tests/target_app_m2r.bc all
	opt -load ../andersen/lib/libAndersen.so -load ./mvxaa.so --mvx-aa -debug-only="mvxaa" $< -o /dev/zero

debug_mvxaa: ./tests/target_app_m2r.bc all  
	gdb opt -x "break llvm::Pass::preparePassManager" -x "run -load ../andersen/lib/libAndersen.so -load ./mvxaa.so --mvx-aa -debug-only="mvxaa" $< -o /dev/zero"

#opt -load ../andersen/lib/libAndersen.so -anders-aa -load-pass-plugin ./mvxaa.so -passes "-mvx-aa" $< -o /dev/zero

## Utils to view CFG, requires xdot and graphviz:
cfg_target: ./tests/target_app_m2r.bc
	opt -dot-cfg $^ -o /dev/zero
#
#cfg_mod_calctest: ./tests/calctest_m2r_mod.bc
#	opt -view-cfg $^

.PHONY: clean all
