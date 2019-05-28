CXXFLAGS += -std=c++17 -Wall -Wextra -Wcast-qual -Wcast-align -Wstrict-aliasing=1 -Wswitch-enum -Wundef -pedantic  -Wfatal-errors -Wshadow

CXXFLAGS += -I./src

CXXFLAGS += `pkg-config opencv --cflags`
LFLAGS += `pkg-config opencv --libs`

LFLAGS += -lboost_system -lboost_program_options -lboost_serialization
LFLAGS += -lz -lrt

LCODECS += -lsnappy -lCharLS -lzstd -llz4 -llzo2


CXXFLAGS += -fopenmp
LFLAGS += -lgomp

CXXFLAGS += -Ofast

CXXFLAGS += -g
#CXXFLAGS += -g -O0
CXXFLAGS += -g -Ofast
CXXFLAGS += -march=native

CXXFLAGS += -I./ext
LFLAGS += $(wildcard ./ext/*.a)


#CXXFLAGS += -DNDEBUG
#CXXFLAGS += -frename-registers -fopenmp
#CXXFLAGS += -fno-unroll-loops
#CXXFLAGS += -funroll-all-loops
#CXXFLAGS += -fno-align-loops
#CXXFLAGS += -fno-align-labels
#CXXFLAGS += -fno-tree-vectorize
#CXXFLAGS += -falign-functions -falign-labels -falign-jumps -falign-loops -frename-registers -finline-functions
#CXXFLAGS += -fomit-frame-pointer
#CXXFLAGS += -fmerge-all-constants -fmodulo-sched -fmodulo-sched-allow-regmoves -funsafe-loop-optimizations -floop-unroll-and-jam

CODECS :=  $(patsubst %.cc,%.o,$(wildcard ./src/codecs/*.cc))


# CXX = g++-7
CXX = g++-7
#CXX = clang++-3.3 -D__extern_always_inline=inline -fslp-vectorize
#CXX = icpc -fast -auto-ilp32 -xHost -fopenmp

all: data ./bin/benchmark

.PHONY: data ext show prof clean realclean

.SECONDARY: $(CODECS)

data:
	@$(MAKE) -C rawzor --no-print-directory
	
ext:
	@$(MAKE) -C ext --no-print-directory
	
./src/codecs/marlin2018.o: ./src/codecs/marlin2018.cc ./src/codecs/marlin2018.hpp ./src/util/*.hpp  ./src/marlinlib/marlin.hpp
	@echo "CREATING $@"
	@$(CXX) -c -o $@ $< $(CXXFLAGS)

./src/codecs/marlinWiP.o: ./src/codecs/marlinWiP.cc ./src/codecs/marlinWiP.hpp ./src/util/*.hpp  ./src/marlinlib/marlin.hpp
	@echo "CREATING $@"
	@$(CXX) -c -o $@ $< $(CXXFLAGS)

./src/codecs/%.o: ./src/codecs/%.cc ./src/codecs/%.hpp ./src/util/*.hpp
	@echo "CREATING $@"
	@$(CXX) -c -o $@ $< $(CXXFLAGS)

./bin/benchmark: ./src/benchmark.cc $(CODECS) ext
	@echo "CREATING $@" $(CODECS) ext
	@$(CXX) -o $@ $< $(CODECS) $(LCODECS) $(CXXFLAGS) $(LFLAGS)

./bin/%: ./src/%.cc ext
	@echo "CREATING $@" ext
	@$(CXX) -o $@ $< $(CXXFLAGS) $(LFLAGS)

prof: ./bin/dcc2017
	 valgrind --dsymutil=yes --cache-sim=yes --branch-sim=yes --dump-instr=yes --trace-jump=no --tool=callgrind --callgrind-out-file=callgrind.out ./eval 
	 kcachegrind callgrind.out

clean:
	rm -f $(CODECS) eval out.tex out.aux out.log out.pdf callgrind.out bin/*

realclean:
	@make -C rawzor clean
	@make -C ext clean
	rm -f $(CODECS) eval out.tex out.aux out.log out.pdf callgrind.out bin/*
