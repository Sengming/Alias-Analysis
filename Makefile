INC=-I/usr/local/include/ -I ./include/ -I ../andersen/include
SOURCES:= $(shell find . -type f -name '*.cpp')
OBJECTS:= $(SOURCES:.cpp=.o)
#DEBUG:= -DDEBUG_BUILD

TARGET_SOURCES:=$(shell find ./tests -type f -name '*.c')
TARGET_OBJECTS:=$(TARGET_SOURCES:.c=.o)
TARGET_BC:=$(TARGET_SOURCES:.c=_m2r.bc)

CXXFLAGS = -rdynamic $(shell llvm-config --cxxflags) $(INC) -g -O0 -fPIC $(DEBUG) 
LINKFLAGS= $(shell llvm-config --ldflags --libs)


all: mvxaa.so

./tests/%_m2r.bc: ./tests/%.c
	clang -Xclang -O0 -emit-llvm -c $^ -o $(^:.c=.bc)
	opt -mem2reg $(^:.c=.bc) -o $@

# Attempt at cross-module pass
#./tests/%.o: 
#	clang -flto -Xclang -O0 -Xclang -load -Xclang ../andersen/lib/libAndersen.so -Xclang -load -Xclang ./mvxaa.so -c $^ -o $@

test: $(TARGET_BC)
	llvm-link $(TARGET_BC) -o ./tests/target_app.bc
	
#clang -flto -Xclang -O0 -Xclang -load -Xclang ../andersen/lib/libAndersen.so -Xclang -load -Xclang ./mvxaa.so -fuse-ld=gold -o ./tests/target_app $(TARGET_SOURCES)

########################################

mvxaa.so: $(OBJECTS)
	g++ $(LINKFLAGS) -dylib -shared  $^ -o $@

clean:
	rm -f *.o *~ *.so tests/*.bc tests/*.o tests/target_app

# Uncomment and fix targets below after adding test cases
# Run
run_mvxaa: $(TARGET_BC) all
	llvm-link $(TARGET_BC) -o ./tests/target_app_merged.bc
	opt -load ../andersen/lib/libAndersen.so -load ./mvxaa.so --mvx-aa -debug-only="mvxaa" -mvx-func="function_to_guard" ./tests/target_app_merged.bc -o /dev/zero

debug_mvxaa: ./tests/target_app_m2r.bc all  
	gdb opt -x "break llvm::Pass::preparePassManager" -x "run -load ../andersen/lib/libAndersen.so -load ./mvxaa.so --mvx-aa -debug-only="mvxaa" $< -o /dev/zero"

#opt -load ../andersen/lib/libAndersen.so -anders-aa -load-pass-plugin ./mvxaa.so -passes "-mvx-aa" $< -o /dev/zero

## Utils to view CFG, requires xdot and graphviz:
cfg_target: ./tests/target_app_m2r.bc
	opt -dot-cfg $^ -o /dev/zero

.PHONY: clean all
