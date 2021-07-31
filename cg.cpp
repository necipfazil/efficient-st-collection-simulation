#include "cg.hpp"  

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <tuple>
#include <string>

// Get targets to callers mapping. This is an intermediate state from raw call
// graph to reverse call graph. Filtering is done at this step.
// The mapping is inclusive of all functions, i.e., a key exist even if a
// function has no caller.
void CallGraph::UpdateTargetToCallers(const CallGraphFilter& F) {
  auto Filter = F; //< Filter may be updated.

  // Transform function name filters to function pc filters.
  if (Filter.ExcludeFuncsWithKeywordInName.size()) {
    for (const auto &El : FuncAddrToName) {
      uint64_t FuncPc = El.first;
      const auto &FuncName = El.second;

      for (const auto &Keyword : Filter.ExcludeFuncsWithKeywordInName)
        if (FuncName.find(Keyword) != std::string::npos)
            Filter.ExcludeFuncs.insert(FuncPc);
    }
  }

  auto ShouldExcludeFunc = [&](uint64_t FuncPc) -> bool {
    // Don't exclude if it is specifically asked for.
    if (FuncAddrToName.count(FuncPc)) {
      auto FuncName = FuncAddrToName[FuncPc];
      for (const auto &FName : Filter.IncludeCallsToFunctionsWithName) {
        if (FuncName == FName)
          return false;
      }
    }

    // Filter based on the function pc.
    if (Filter.ExcludeFuncs.count(FuncPc))
      return true;
    // Filter based on indirect targetness.
    if (Filter.ExcludeUnknownIndirTargets &&
        IndirTargetUnknownType.count(FuncPc))
      return true;
    return false;
  };

  //
  // Precompute indirect call sites.
  //
  // Every indirect call site with UNKNOWN type id.
  std::vector<CallSite> IndirCallUnknownTypeCallSites;
  if (!Filter.ExcludeUnknownIndirCalls) {
    for (auto CallSitePc : IndirCallUnknownType) {
      uint64_t CallerPc = CallSiteToCaller.find(CallSitePc)->second;
      if (ShouldExcludeFunc(CallerPc))
        continue;
      IndirCallUnknownTypeCallSites.emplace_back(CallerPc, CallSitePc);
    }
  }
  // Type id to indirect call sites.
  std::unordered_map<uint64_t, std::vector<CallSite>> TypeIdToIndirCallSites;
  for (const auto &El : TypeIdToIndirCalls) {
    uint64_t TypeId = El.first;
    for (uint64_t CallSitePc : El.second) {
      uint64_t CallerPc = CallSiteToCaller.find(CallSitePc)->second;
      if (ShouldExcludeFunc(CallerPc))
        continue;
      TypeIdToIndirCallSites[TypeId].emplace_back(CallerPc, CallSitePc);
    }
  }

  // Add for indirect calls.
  for (const auto &El : FuncAddrToName) {
    uint64_t FuncPc = El.first;
    if (ShouldExcludeFunc(FuncPc))
      continue;

    auto &FuncTargetToCallers = TargetsToCallers[FuncPc];
    
    // Add indirect calls based on function's indirect target properties.
    bool FuncIsIndirTarget = IndirTargetToTypeId.count(FuncPc) ||
                             IndirTargetUnknownType.count(FuncPc);
    if (FuncIsIndirTarget) {
      // Add indirect calls with unknown type id.
      if (!Filter.ExcludeUnknownIndirCalls)
        FuncTargetToCallers.insert(FuncTargetToCallers.end(),
                                   IndirCallUnknownTypeCallSites.begin(),
                                   IndirCallUnknownTypeCallSites.end());

      // Add indirect calls with matching type id.
      bool FuncHasTypeId = IndirTargetToTypeId.count(FuncPc);
      if (FuncHasTypeId) { //< Function with type id.
        // Add call sites with matching type id.
        uint64_t FuncTypeId = IndirTargetToTypeId.find(FuncPc)->second;
        if (!TypeIdToIndirCallSites[FuncTypeId].empty()) {
          auto CallSites = TypeIdToIndirCallSites[FuncTypeId];
          FuncTargetToCallers.insert(FuncTargetToCallers.end(),
                                     CallSites.begin(), CallSites.end());
        }
      } else if (!Filter.ExcludeIndirCallsToUnknownTargets) {
                           //< Function with unknown type id.
        // Add all indirect calls as potential caller.
        // Only add call sites with known type ids. The rest are added or not
        // based on another filter value.
        for (const auto &El : TypeIdToIndirCallSites) {
          const auto CallSites = El.second;
          TargetsToCallers[FuncPc].insert(TargetsToCallers[FuncPc].end(),
                                         CallSites.begin(),
                                         CallSites.end());
        }
      }
    }
  }

  //
  // Add for direct calls
  //
  for (auto const &El : FuncAddrToDirCallSites) {
    uintptr_t CallerPc = El.first;
    const auto &Calls = El.second;
    if (ShouldExcludeFunc(CallerPc))
      continue;

    for (const auto &Call : Calls) {
      auto CallSitePc = std::get<0>(Call);
      auto TargetPc = std::get<1>(Call);
      if (ShouldExcludeFunc(CallSitePc) || ShouldExcludeFunc(TargetPc))
        continue;
      TargetsToCallers[TargetPc].emplace_back(CallerPc, CallSitePc);
    }
  }
}


CallGraph::CallGraph(std::istream &In, const CallGraphFilter &CGF) {
  std::string X;

  auto TryReadHex64 = [&](std::stringstream &SS, uint64_t &H) -> bool {
    if (!SS.good())
      return false;
    SS >> std::hex >> H;
    return true;
  };
  auto ReadHex64 = [&](std::stringstream &SS, uint64_t &H) {
    if(!TryReadHex64(SS, H)) {
      std::cerr << "cannot read hex value" << std::endl;
      exit(-1);
    };
  };

  auto ReadHex64List = [&](std::stringstream &SS, std::vector<uint64_t>& V) -> size_t {
    size_t Count = 0;
    uint64_t H;
    while (TryReadHex64(SS, H)) {
      V.push_back(H);
      Count++;
    }
    return Count;
  };

  // Read from file.
  while(std::getline(In, X)) {
    // Read indirect target types.
    if (!X.find("INDIRECT TARGET TYPES")) {
      assert (TypeIdToIndirTargets.empty()
              && "Multiple \"INDIRECT TARGETS TYPES\" sections.");
      while (std::getline(In, X)) { //< Read type id per line
        if (X == "") break;
        std::stringstream Line(X);
        // Read type id. It can be an id or string "UNKNOWN".
        std::string TypeId;
        Line >> TypeId;
        if (TypeId == "UNKNOWN") {
          std::vector<uint64_t> V;
          ReadHex64List(Line, V);
          IndirTargetUnknownType.insert(V.begin(), V.end());
        } else {
          uint64_t TypeIdVal = std::stoull(TypeId, 0, 16);
          // TODO: use these for without callgraph evaluation
          // TypeIdVal = 0;
          ReadHex64List(Line, TypeIdToIndirTargets[TypeIdVal]);
          // Reverse mapping.
          for (auto FuncPc : TypeIdToIndirTargets[TypeIdVal])
            IndirTargetToTypeId[FuncPc] = TypeIdVal;
        }
      }
    }

    // Read indirect call types.
    if (!X.find("INDIRECT CALL TYPES")) {
      assert (TypeIdToIndirCalls.empty()
              && "Multiple \"INDIRECT CALLS TYPES\" sections.");
      while (std::getline(In, X)) {
        if (X == "") break;
        std::stringstream Line(X);
        // Read type id and indirect call site pcs.
        uint64_t TypeId;
        ReadHex64(Line, TypeId);
        // TODO: use these for without callgraph evaluation
        //TypeId = 0;
        auto &CallSitePcList = TypeIdToIndirCalls[TypeId];
        ReadHex64List(Line, CallSitePcList);
        // Reverse mapping: indirect call site pc to type id.
        for (auto CallSitePc : CallSitePcList)
          IndirCallToTypeId[CallSitePc] = TypeId;
      }
    }

    // Read indirect call sites.
    if (!X.find("INDIRECT CALL SITES")) {
      assert (FuncAddrToIndirCallSites.empty() 
              && "Multiple \"INDIRECT CALL SITES\" sections.");
      while (std::getline(In, X)) {  
        if (X == "") break;
        std::stringstream Line(X);
        // Read caller pc.
        uint64_t CallerPc;
        ReadHex64(Line, CallerPc);
        // Read indirect call site pcs.
        std::vector<uint64_t> CallSitePcs;
        ReadHex64List(Line, CallSitePcs);
        FuncAddrToIndirCallSites[CallerPc] = CallSitePcs;
        // Insert to set of all indirect call site pcs.
        IndirCallSiteAddrs.insert(CallSitePcs.begin(), CallSitePcs.end());
      }
    }

    // Read direct call sites.
    if (!X.find("DIRECT CALL SITES")) {
      assert (FuncAddrToDirCallSites.empty() 
              && "Multiple \"DIRECT CALL SITES\" sections.");
      while (std::getline(In, X)) {
        if (X == "") break;
        std::stringstream Line(X);
        // Read caller pc.
        uint64_t CallerPc;
        ReadHex64(Line, CallerPc);
        // Read direct call site and target pcs.
        uint64_t CallSitePc, TargetPc;
        while (TryReadHex64(Line, CallSitePc)) {
          ReadHex64(Line, TargetPc);
          FuncAddrToDirCallSites[CallerPc].emplace_back(CallSitePc, TargetPc);
          // Insert to set of all direct call site pcs.
          DirCallSiteAddrs.insert(CallSitePc);
        }
      }
    }

    // Read functions.
    if (!X.find("FUNCTIONS")) {
      assert (FuncAddrToName.empty() 
              && "Multiple \"FUNCTION SYMBOLS\" sections.");
      while (std::getline(In, X)) {
        if (X == "") break;
        std::stringstream Line(X);
        // Read function pc.
        uint64_t FunctionPc;
        ReadHex64(Line, FunctionPc);
        // Read function name.
        std::string FuncName;
        Line >> FuncName;
        FuncAddrToName[FunctionPc] = FuncName;
      }
    }
  }

  // Set FuncNameToAddr.
  for (auto &El : FuncAddrToName)
    FuncNameToAddr[El.second] = El.first;
  
  // Set targets without any info.
  for (auto &El : FuncAddrToName) {
    uint64_t FuncPc = El.first;
    if (!IndirTargetToTypeId.count(FuncPc) && !IndirTargetUnknownType.count(FuncPc))
      TargetsWithNoInfo.insert(FuncPc);
  }
  
  // Set indirect calls without a type id.
  for (auto IndirCallSitePc : IndirCallSiteAddrs)
    if (IndirCallToTypeId.count(IndirCallSitePc))
      IndirCallUnknownType.insert(IndirCallSitePc);
  
  // Set call site to caller mappings.
  for (const auto& El: FuncAddrToDirCallSites) {
    uint64_t Func = El.first;
    for (const auto &DirCallSite : El.second) {
      uint64_t CallSite = std::get<0>(DirCallSite);
      CallSiteToCaller[CallSite] = Func;
    }
  }
  for (const auto& El: FuncAddrToIndirCallSites) {
    uint64_t Func = El.first;
    for (const auto &IndirCallSite : El.second)
      CallSiteToCaller[IndirCallSite] = Func;
  }

  // Update target to callers.
  UpdateTargetToCallers(CGF);
}
