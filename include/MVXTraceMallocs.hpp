#ifndef __TRACE_MALLOCS_HPP__
#define __TRACE_MALLOCS_HPP__

#include <llvm/ADT/DenseSet.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/Analysis/ScalarEvolutionAliasAnalysis.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/InstVisitor.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Operator.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Debug.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>

// svf
#include <SABER/LeakChecker.h>
#include <WPA/Andersen.h>
#include <WPA/WPAPass.h>

using namespace llvm;

class MVXTraceMallocs : public ModulePass, public InstVisitor<MVXTraceMallocs> {
    typedef SVF::Set<const SVF::SVFGNode *> SVFGNodeSet;
    typedef SVF::Set<const SVF::SVFFunction *> ProtFuncSet;
    typedef SVF::Set<const SVF::LoadVFGNode *> GlobalLoadSet;
    typedef SVF::Map<SVF::VFGNode *, SVF::LoadVFGNode *> GlobalToUseNodeMap;
    typedef SVF::FIFOWorkList<const SVF::PTACallGraphNode *> CGNodeWorklist;
    typedef SVF::FIFOWorkList<SVF::VFGNode *> SVFGNodeWorklist;
    typedef std::pair<SVF::VFGNode *, int64_t> NodeGEPPair;
    typedef SVF::FILOWorkList<NodeGEPPair> SVFGNodeWorkstack;
    typedef SVF::Map<const SVF::VFGNode *, SVF::Set<NodeGEPPair> *>
        SourceLoadOffsetMap;

  protected:
    typedef std::pair<StringRef, unsigned> GlobalPair_t;
    Module *m_pmainmodule;

    // [Legacy] Collected malloc calls and globals to use
    std::unique_ptr<DenseSet<Value *>> m_pglobals;
    std::unique_ptr<DenseSet<CallInst *>> m_pmallocCalls;

    // Collected mem allocation sources
    SVFGNodeSet *m_psourceNodes;
    SVF::LeakChecker *m_psaber;

    // Guarded function
    StringRef m_mvxFunc;
    std::unique_ptr<SVF::WPAPass> m_pwpa;
    ProtFuncSet m_protFunctions;

    // SVF Variables
    std::unique_ptr<SVF::Andersen> m_pander;

    // Set of loads the make use of global values in the VFG
    GlobalLoadSet m_globalLoadSet;

    // Map Between globals and the corresponding load instructions which use a
    // global
    GlobalToUseNodeMap m_globalToUseMap;

    // Map Between memory sources and loads, with their corresponding total GEP
    // offset of the path
    SourceLoadOffsetMap m_sourceToLoadOffsets;

    // Helpers
    CallInst *aliasesMallocCall(Value *V) const;
    CallInst *pointsToMallocCall(Value *V) const;
    Value *aliasesGlobal(Value *V) const;
    Value *pointsToGlobal(Value *V) const;
    std::string printPts(SVF::PointerAnalysis *pta, Value *val);
    std::string printRevPts(SVF::PointerAnalysis *pta, Value *val);
    void analyzeSvfg(SVF::VFG &vfg, SVF::PAG &pag,
                     SVF::PTACallGraph &callgraph);
    void collectSources(SVF::SVFModule &svfModule); // defunct
    void initSrcs(SVF::PAG *pag, SVF::SVFG *svfg, SVF::PTACallGraph *callgraph);
    void collectProtectedFunctions(SVF::PTACallGraph &callgraph,
                                   SVF::SVFModule &svfModule);
    bool isNodeInProtCG(const SVF::VFGNode *vfgNode);
    void collectDownstreamUseInProtCG(SVF::VFGNode &vfgNode,
                                      SVF::VFGNode &globalNode);
    void ifSrcUpstreamCollectOffset();

    /// Whether the function is a heap allocator/reallocator (allocate memory)
    inline bool isSourceLikeFun(const SVF::SVFFunction *fun) {
        return SVF::SaberCheckerAPI::getCheckerAPI()->isMemAlloc(fun);
    }
    bool isInAWrapper(const SVF::SVFGNode *src,
                      SVF::LeakChecker::CallSiteSet &csIdSet, SVF::SVFG *svfg);

  public:
    static char ID;

    MVXTraceMallocs() : ModulePass(ID) {}

    void visitLoadInst(LoadInst &I);
    void visitStoreInst(StoreInst &I);
    bool doInitialization(Module &M) override;
    virtual bool runOnModule(Module &M) override;

    void getAnalysisUsage(AnalysisUsage &AU) const override;
};
#endif
