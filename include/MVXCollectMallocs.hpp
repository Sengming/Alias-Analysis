#ifndef __COLLECT_MALLOCS_HPP__
#define __COLLECT_MALLOCS_HPP__

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

using namespace llvm;

class MVXCollectMallocs : public ModulePass,
                          public InstVisitor<MVXCollectMallocs> {
  protected:
    std::unique_ptr<DenseSet<CallInst *>> m_mallocCalls;

  public:
    static char ID;

    MVXCollectMallocs()
        : ModulePass(ID), m_mallocCalls(new DenseSet<CallInst *>) {}

    std::unique_ptr<DenseSet<CallInst *>> getResult();

    bool doInitialization(Module &M) override;
    virtual bool runOnModule(Module &M) override;
    void visitCallInst(CallInst &I);

    void getAnalysisUsage(AnalysisUsage &AU) const override;
};
#endif
