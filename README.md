# Efficient stack trace collection and reconstruction simulation

This document describes how to run an end-to-end example for efficient stack trace collection and reconstruction.

## Setup the workspace
```
mkdir ~/callgraph-ws
export CALLGRAPH_WS=~/callgraph-ws/
```

## Install `LLVM`
Clone the llvm-project, which includes implementations for:
*  `clang -fcall-graph-section` to compute and store call graph in binary
*  `llvm-objdump --call-graph-info` to extract call graph from binary
* `clang -fsanitize=address` with hooks for collecting stack traces on alloc/dealloc.

```
cd $CALLGRAPH_WS
git clone https://github.com/necipfazil/llvm-project
cd llvm-project/
git checkout necip-call-graph
```

Build and install:
```
mkdir build && cd build
cmake -DLLVM_ENABLE_PROJECTS="clang;compiler-rt" ../llvm
make -j && sudo make install
export CALLGRAPH_BIN=$CALLGRAPH_WS/llvm-project/build/bin/
```

## Build `st_reconst`
Clone and build the stack trace reconstruction simulation tool:
```
cd $CALLGRAPH_WS
git clone https://github.com/necipfazil/efficient-st-collection-simulation
cd efficient-st-collection-simulation
clang++ -O3 -msse4.2 rcg.cpp cg.cpp st_reconst.cpp -o st_reconst
```

## Do reconstruction with example
Build with `.callgraph` section and ASAN hooks for stack trace collection:
```
$CALLGRAPH_BIN/clang -fcall-graph-section -fsanitize=address toy_example.c -o toy_example.o
```

Extract call graph:
```
$CALLGRAPH_BIN/llvm-objdump  --call-graph-info toy_example.o > callgraph.dis
```

Run to collect stack traces:
```
./toy_example.o 2> stack_traces.txt
```

Do reconstruction simulation (16 is the maximum depth; 4 and 6 are the pruning depths):
```
./st_reconst callgraph.dis stack_traces.txt 16 4 6
```

The simulation tool will:
* Deserialize the call graph from `callgraph.dis` and create a reverse call graph,
* Compress each stack trace in `stack_traces.txt`,
* Decompress the stack traces through reverse call graph traversal.

The output will include log messages per stack trace reconstructed such as:
```
FuncName: malloc
FuncEntryPc: 49c020
Stack trace hash: 32410000726d1dfc
Stack trace: 
Stack Trace (length=5): 
  0: [4cdab2] malloc_free[4cdaa0]
  1: [4cda1b] f3[4cd9e0]
  2: [4cd993] f2[4cd940]
  3: [4cda3b] f3[4cd9e0]
  4: [4cd993] f2[4cd940]
SUCCESS! Matches!
Found 0 incorrect reconstructions due to collisions
Time elapsed (sec): 0
```
