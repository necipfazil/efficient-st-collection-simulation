#include "rcg.hpp"
#include "cg.hpp"

ReverseCallGraph::~ReverseCallGraph() {
  for (auto &El : FuncPcToNode) {
    if (El.second->NumCallers)
      delete[] El.second->Callers;
    El.second->Callers = nullptr;

    delete El.second;
  }
  FuncPcToNode.clear();
  CallSitePcToNode.clear();
}

ReverseCallGraph::ReverseCallGraph(const CallGraph& RawCG) {
  // Get the filtered target to callers mapping.
  auto &TargetToCallers = RawCG.TargetsToCallers;

  // Create function nodes.
  for (const auto &El : TargetToCallers) {
    uint64_t FuncPc = El.first;
    FuncPcToNode[FuncPc] = new FunctionNode(FuncPc);
  }

  // Set callers.
  for (const auto &El : TargetToCallers) {
    uint64_t FuncPc = El.first;
    FunctionNode *FuncNode = FuncPcToNode[FuncPc];
    const std::vector<CallSite> &Callers = El.second;
    uint64_t NumCallers = Callers.size();
    // Allocate for callers.
    FuncNode->NumCallers = NumCallers;
    if (NumCallers)
      FuncNode->Callers = new CallSiteNode[NumCallers];

    // Set callers.
    for (int I = 0; I < NumCallers; I++) {
      const CallSite CS = Callers[I];           //< Get info from.
      CallSiteNode &CSN = FuncNode->Callers[I]; //< Fill info to.
      // Set caller.
      CSN.CallSitePc = CS.CallSitePc;
      CSN.Caller = FuncPcToNode[CS.CallerPc];
      // Set CallSiteToPcNode mapping for reverse call graph.
      CallSitePcToNode[CS.CallSitePc] = &CSN;
    }
  }
}
