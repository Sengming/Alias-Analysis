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
#include <WPA/Andersen.h>
#include <WPA/WPAPass.h>

using namespace llvm;

class MVXTraceMallocs : public ModulePass, public InstVisitor<MVXTraceMallocs> {
  protected:
    typedef std::pair<StringRef, unsigned> GlobalPair_t;
    Module *m_pmainmodule;

    // Collected malloc calls and globals to use
    std::unique_ptr<DenseSet<Value *>> m_pglobals;
    std::unique_ptr<DenseSet<CallInst *>> m_pmallocCalls;

    // Guarded function
    StringRef m_mvxFunc;
    std::unique_ptr<SVF::WPAPass> m_pwpa;

    // SVF Variables
    std::unique_ptr<SVF::Andersen> m_pander;

    // Helpers
    CallInst *aliasesMallocCall(Value *V) const;
    CallInst *pointsToMallocCall(Value *V) const;
    Value *aliasesGlobal(Value *V) const;
    Value *pointsToGlobal(Value *V) const;
    std::string printPts(SVF::PointerAnalysis *pta, Value *val);

  public:
    static char ID;

    MVXTraceMallocs() : ModulePass(ID) {}

    void visitLoadInst(LoadInst &I);
    bool doInitialization(Module &M) override;
    virtual bool runOnModule(Module &M) override;

    void getAnalysisUsage(AnalysisUsage &AU) const override;
};
#endif
