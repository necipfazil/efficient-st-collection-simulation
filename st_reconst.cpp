#include <algorithm>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <chrono>
#include "cg.hpp"
#include "rcg.hpp"

typedef std::vector<uint64_t> StackTrace;

// Followings are set on program initialization from CLI. They are kept global
// to avoid passing them as arguments to each recursive call to DFS.
// Alternatively, a class can be implemented for DFS where these will be kept
// as instance members.
size_t MaxDepth = 0;           //< Maximum depth to search for.
uint64_t PruningDepth1 = 0;    //< Pruning depth 1.
uint64_t PruningDepth2 = 0;    //< Pruning depth 2.
uint64_t *ST = nullptr;        //< Stack trace to fill by reconstruction.
                               //< Allocated based on the maximum depth.
ReverseCallGraph *RCG = nullptr; //< Reverse call graph.

// Followings are set everytime before calling DFS based on the stack trace
// to reconstruct.
std::vector<uint64_t> WantedST; //< Wanted stack trace.
uint64_t WantedHash = 0;        //< The hash for WantedST.
uint64_t WantedHashMed1 = 0;    //< Pruning hash 1.
uint64_t WantedHashMed2 = 0;    //< Pruning hash 2.
int DoesNotMatchCount = 0;      //< Count how many incorrect reconstructions
                                //< were made.

// Pretty print a stack trace.
template<class T>
void PrettyPrintST(const CallGraph &CG, T it_begin, size_t length) {
  std::cerr << "Stack Trace (length=" << std::dec << length <<"): " << std::endl;

  for (size_t I = 0; I < length; I++) {
    uint64_t CallSitePc = *it_begin;
    uint64_t CallerPc = CG.CallSiteToCaller.find(CallSitePc)->second;
    std::string CallerName = "UNKNOWN_NAME";
    if (CG.FuncAddrToName.count(CallerPc))
      CallerName = CG.FuncAddrToName.find(CallerPc)->second;
    // Print frame.
    std::cerr << "  " << I << ": [" << std::hex << CallSitePc << "] "
              << CallerName
              << "[" << std::hex << CallerPc << "]" << std::endl;
    it_begin++;
  }
}

// Pretty print a stack trace.
void PrettyPrintST(const CallGraph &CG, std::vector<uint64_t> st) {
    PrettyPrintST(CG, st.begin(), st.size());
}

// Check whether two stack traces are the same.
template<class T1, class T2>
bool AreSTSame(T1 it1_begin, size_t size1, T2 it2_begin, size_t size2) {
  if (size1 != size2)
      return false;

  for (int I = 0; I < size1; I++) {
    if (*it1_begin != *it2_begin)
      return false;
    it1_begin++;
    it2_begin++;
  }
  return true;
}

uint64_t HashStep(uint64_t Hash, uint64_t PC, size_t Idx) {
  uint64_t CRC32 = __builtin_ia32_crc32di(Hash, PC);
  if (Idx == PruningDepth1) {
    return CRC32 | (Hash << (48));
  } else if (Idx == PruningDepth2) {
    return CRC32 | ((Hash >> 48) << 48) | ((Hash & 0xFFFFll) << 32);
  } else {
    return CRC32 | ((Hash >> 32) << 32);
  }
}

uint64_t Hash(const StackTrace &ST) {
  uint64_t Res = 0;
  for (size_t I = 0; I < ST.size(); I++)
    Res = HashStep(Res, ST[I], I);
  return Res;
}

// Reads the stack traces from input stream, and returns a vector of stack
// traces together with the name of the entry function and the hash of the
// stack trace. The first frame from the list is eliminated and used as the
// entry point.
std::vector<std::tuple<std::string/*FuncName*/, uint64_t/*Hash*/, StackTrace>>
ReadStackTracesFromASanOut(std::istream &In, const CallGraph &CG, 
                           size_t DepthLimit) {
  std::vector<std::tuple<std::string, uint64_t, StackTrace>> Res;
  std::string X;
  int CountStackTracesClipped = 0;
  int CountHashCollisions = 0;
  int CSCouldntFind = 0;
  std::unordered_set<uint64_t> HashesFound;
  while (std::getline(In, X)) {
    std::stringstream Line(X);
    std::string FirstWord;
    Line >> FirstWord;
    if (FirstWord != std::string("ST:")) continue;
    std::string FuncName;
    StackTrace ST;
    int CurrentDepth = 0;
    while (true) {
      uint64_t PC;
      Line >> std::hex >> PC;

      if (!CG.CallSiteToCaller.count(PC)) {
        CSCouldntFind++;
        break;
      }
      
      // Get the entry point
      if (CurrentDepth == 0) {
        CurrentDepth++;
        if (!CG.CallSiteToCaller.count(PC)) {
          fprintf(stderr, "WARNING: Failed to find caller for the call site at %p.\n",
                          (void*)PC);
          break;
        } else {
          auto Caller = CG.CallSiteToCaller.find(PC)->second;
          if (!CG.FuncAddrToName.count(Caller)) {
            fprintf(stderr, "WARNING: Failed to find func name for caller at %p.\n",
                            (void*)Caller);
            break;
          }
          FuncName = CG.FuncAddrToName.find(Caller)->second;
        }
        continue;
      }
      if (!Line) break;
      ST.push_back(PC);
      if (CurrentDepth++ == DepthLimit) {
        CountStackTracesClipped++;
        break;
      }
    }
    uint64_t STHash = Hash(ST);
    if (HashesFound.count(STHash)) CountHashCollisions++;
    
    Res.emplace_back(FuncName, STHash, ST);
  }
  if (CountStackTracesClipped)
    fprintf(stderr, "WARNING: %d stack traces were clipped as they exceeded "
                    "the depth limit.\n", CountStackTracesClipped);
  if (CountHashCollisions)
    fprintf(stderr, "WARNING: %d stack traces had hash collisions.\n", 
                                                      CountHashCollisions);
  if (CSCouldntFind)
    fprintf(stderr, "WARNING: %d stack traces were ignored since they included filtered frames.\n", 
                                                      CSCouldntFind);
  return Res;
}

// Returns whether the stack trace is found. If it is found, prints a success
// message and the number of incorrect reconstructions.
bool DFS(size_t CurrentDepth, uint64_t CurrentHash, FunctionNode *EntryFunc) {
  // Check hash match
  if (CurrentHash == WantedHash) {
    bool DidMatch = AreSTSame(WantedST.begin(), WantedST.size(), ST, CurrentDepth);
    if (DidMatch) {
      std::cerr << "SUCCESS: Matches!\n";
      std::cerr << "Found " << DoesNotMatchCount 
                << " incorrect reconstructions due to collisions" << std::endl;
      return true;
    } else {
      DoesNotMatchCount++;
    }
  }

  if (CurrentDepth > MaxDepth)
    return false;

  // If the current depth is one of the pruning depths, check the hash against
  // the pruning hashes.
  if (CurrentDepth == PruningDepth1+1) {
    // Pruning depth 1: prune based on the highest 16-bits bucket
    if ((CurrentHash >> 48) != WantedHashMed1)
      return false;
  } else if (CurrentDepth == PruningDepth2+1) {
    // Pruning depth 2: prune based on the second highest 16-bits bucket
    if (((CurrentHash >> 32) & 0xFFFFll) != WantedHashMed2)
      return false;
  }

  // Continue search from the callers of the current function.
  auto NumCallers = EntryFunc->NumCallers;
  auto Callers = EntryFunc->Callers;
  for (int I = 0; I < NumCallers; I++) {
    const CallSiteNode &CSN = Callers[I];

    // Fill one frame in the stack trace.
    ST[CurrentDepth] = CSN.CallSitePc;
    bool Found = DFS(
      CurrentDepth + 1,
      HashStep(CurrentHash, CSN.CallSitePc, CurrentDepth),
      CSN.Caller
    );

    if (Found)
      return true;
  }
  return false;
}

int main(int argc, char **argv) {
  if (argc != 6) {
    std::cerr << "OVERVIEW: efficient stack trace collection and reconstruction simulation tool" << std::endl;
    std::cerr << "USAGE: " << argv[0] 
              << " call_graph_disasm_file" //< 1st
              << " stack_traces_file"      //< 2nd
              << " max_depth"              //< 3rd
              << " pruning_depth_1"        //< 4th
              << " pruning_depth_2"        //< 5th
              << "\n\n";
    std::cerr << " call_graph_disasm_file     " 
              << "File containing call graph disassembly output obtained from llvm-objdump --call-graph-info\n"
              << " stack_traces_file          "
              << "File containing stack traces to compress/decompress, obtained using ASAN hooks\n"
              << " max_depth                  "
              << "Maximum depth at which to clip the stack traces and stop the reconstruction search\n"
              << " pruning_depth_1            "
              << "First pruning depth\n"
              << " pruning_depth_2            "
              << "Second pruning depth\n" << std::endl;
    return -1;
  }

  // TODO: Verify the input values. Specifically, verify the filepath inputs.

  // Read the medium indices.
  PruningDepth1 = atoi(argv[4]);
  PruningDepth2 = atoi(argv[5]);

  // Create call graph filter.
  CallGraphFilter CGF;
  // Force including the allocation/deallocation functions. These may not have
  // ids in the .callgraph section as they are linked from outside the binary.
  CGF.IncludeCallsToFunctionsWithName = {
    "free", "malloc", "calloc", "realloc",
    "_ZdlPv"/*delete*/, "_ZdaPv"/*delete[]*/,
    "_Znwm"/*new*/, "_Znam"/*new[]*/,
    "_ZnwmRKSt9nothrow_t", /* new(ulong, std::nothrow_t)*/
  };
  // Exclude ASAN-related functions or functions that are infeasible to appear
  // on the allocation/deallocation traces.
  CGF.ExcludeFuncsWithKeywordInName = {"asan", "interceptor", "@plt", 
                                       "sanitizer", "__clang_call_terminate"};
  // Exclude any node/edge related to the unknown indirect calls/targets.
  CGF.ExcludeIndirCallsToUnknownTargets = true;
  CGF.ExcludeUnknownIndirCalls = true;
  CGF.ExcludeUnknownIndirTargets = true;

  // Read the call graph.
  std::ifstream CGIn(argv[1]);
  CallGraph CG(CGIn, CGF);

  // Compute the light-weight reverse call graph.
  auto RevCG = ReverseCallGraph(CG);

  // Read the maximum depth.
  size_t Depth = atoi(argv[3]);

  // Read the stack traces.
  std::ifstream TargetStacksIn(argv[2]);
  auto STS = ReadStackTracesFromASanOut(TargetStacksIn, CG, Depth);

  // Set globals used by DFS. These are intentionally set global to avoid
  // recursively passing the same arguments for the DFS function. Alternatively,
  // a search class may be implemented where these are stored per instance.
  MaxDepth = Depth;
  ST = new uint64_t[MaxDepth+1];
  RCG = &RevCG;

  std::cerr << "Starting the reconstructions." << std::endl;
  for (const auto &STI : STS) {
    std::string FuncName = std::get<0>(STI);
    // Further set the globals used by DFS.
    WantedHash = std::get<1>(STI);
    WantedST = std::get<2>(STI);
    WantedHashMed1 = WantedHash >> 48;
    WantedHashMed2 = (WantedHash >> 32) & 0xFFFFll;
    DoesNotMatchCount = 0;
    // Print info on the stack trace that is going to be reconstructed.
    auto FuncEntryPc = CG.FuncNameToAddr[FuncName];
    std::cerr << "\nFuncName: " << FuncName
              << "\nFuncEntryPc: " << std::hex << FuncEntryPc
              << "\nStack trace hash: " << std::hex << WantedHash
              << "\nStack trace: " << std::endl;
    PrettyPrintST(CG, WantedST);

    auto start = std::chrono::high_resolution_clock::now();
    // Start reconstruction.
    bool Ret = DFS(/*CurrentDepth=*/0, /*CurrentHash=*/0,
                  /*EntryFunc=*/RCG->FuncPcToNode[FuncEntryPc]);
    
    // Print after reconstruction logs.
    auto stop = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(stop - start);
    std::cerr << "Time elapsed (sec): " << std::dec <<duration.count() << std::endl;

    if (!Ret)
      std::cerr << "\nFAIL: Could not reconstruct the stack trace.\n";
    std::cerr<< "\n=========================================\n" << std::endl;
  }

  if (MaxDepth)
    delete[] ST;

  return 0;
}
