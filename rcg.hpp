#ifndef __REVERSE_CALL_GRAPH_H__
#define __REVERSE_CALL_GRAPH_H__

#include "cg.hpp"

struct FunctionNode;

struct CallSiteNode {
  FunctionNode* Caller; //< Owner of the call site node.
  uint64_t CallSitePc;  //< Call site pc.

  CallSiteNode() : Caller(nullptr), CallSitePc(0) {}
};

struct FunctionNode {
  uint64_t EntryPc;      //< Function entry pc.
  CallSiteNode* Callers; //< Callers of this function.
  uint64_t NumCallers;   //< Length of the num callers.

  FunctionNode(uint64_t EntryPc) 
    : EntryPc(EntryPc), Callers(nullptr), NumCallers(0) {}
};

// A compact and efficient reverse call graph representation.
struct ReverseCallGraph {
  std::unordered_map<uint64_t, FunctionNode*> FuncPcToNode;
  std::unordered_map<uint64_t, CallSiteNode*> CallSitePcToNode;

  ReverseCallGraph(const CallGraph&);

  // Deallocate for FunctionNode and CallSiteNode instances.
  ~ReverseCallGraph();
};

#endif
