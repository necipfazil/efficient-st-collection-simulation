#ifndef __CALL_GRAPH_H__
#define __CALL_GRAPH_H__

#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <tuple>
#include <string>

struct CallSite {
  uint64_t CallerPc;
  uint64_t CallSitePc;

  CallSite(uint64_t CallerPc, uint64_t CallSitePc)
    : CallerPc(CallerPc), CallSitePc(CallSitePc) {}
};

struct CallGraphFilter {
  // Include calls to functions. Preceeds to any other filter.
  std::unordered_set<std::string> IncludeCallsToFunctionsWithName;

  // Exclude: direct calls from/to it, indirect calls from/to it.
  std::unordered_set<uint64_t> ExcludeFuncs;

  // Check if the keyword appears somewhere in the function name string.
  // Exclude: direct calls from/to it, indirect calls from/to it.
  std::unordered_set<std::string> ExcludeFuncsWithKeywordInName;

  // Exclude: direct calls from/to it, indirect calls from/to it.
  // Include: nothing.
  bool ExcludeUnknownIndirTargets;

  // Exclude: indirect calls to unknown targets.
  // Include: direct calls from/to it, indirect calls from it.
  bool ExcludeIndirCallsToUnknownTargets;

  // Exclude: indirect calls without a type id.
  bool ExcludeUnknownIndirCalls;
};

// Raw call graph representation that is not optimized for space but for
// convenience in bringing information.
struct CallGraph {
  // Indirect targets.
  std::unordered_map<uint64_t, std::vector<uint64_t>> TypeIdToIndirTargets;
  std::unordered_map<uint64_t, uint64_t> IndirTargetToTypeId;
  std::unordered_set<uint64_t> IndirTargetUnknownType; // those tagged "UNKNOWN".
  std::unordered_set<uint64_t> TargetsWithNoInfo; // No info on call graph section.

  // Indirect calls.
  std::unordered_map<uint64_t, std::vector<uint64_t>> TypeIdToIndirCalls;
  std::unordered_map<uint64_t, uint64_t> IndirCallToTypeId;
  std::unordered_set<uint64_t> IndirCallUnknownType;

  // Indirect call sites: { CallerFuncPc: [IndirectCallSiteAddr,] }
  std::unordered_map<uint64_t, std::vector<uint64_t>> FuncAddrToIndirCallSites;

  // Direct call sites: { CallerAddr: [(CallSiteAddr, TargetAddr),] }
  std::unordered_map<uint64_t, 
                    std::vector<std::tuple<uint64_t, uint64_t>>
                      > FuncAddrToDirCallSites;

  // Set of all call sites.
  std::unordered_set<uint64_t> DirCallSiteAddrs;
  std::unordered_set<uint64_t> IndirCallSiteAddrs;

  // Functions
  std::unordered_map<uint64_t, std::string> FuncAddrToName;
  std::unordered_map<std::string, uint64_t> FuncNameToAddr;
  std::unordered_map<uint64_t, uint64_t> CallSiteToCaller;

  std::unordered_map<uint64_t/*TargetFuncPc*/, 
                     std::vector<CallSite>/*potential calls to it*/> TargetsToCallers;

  private:
    void UpdateTargetToCallers(const CallGraphFilter& F);

  public:
    // Read from llvm-objdump output
    CallGraph(std::istream &In, const CallGraphFilter &CGF);

    void Print(std::ostream &Out) const;

    void PrintReverseCG(std::ostream &Out, bool demagle) const;

};

#endif
