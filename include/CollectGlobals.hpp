#ifndef __COLLECT_GLOBALS_HPP__
#define __COLLECT_GLOBALS_HPP__

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

class CollectGlobals : public ModulePass {
  protected:
    std::unique_ptr<DenseSet<Value *>> m_globals;

  public:
    static char ID;

    CollectGlobals() : ModulePass(ID), m_globals(new DenseSet<Value *>) {}
    std::unique_ptr<DenseSet<Value *>> getResult();

    bool doInitialization(Module &M) override;
    virtual bool runOnModule(Module &M) override;

    void getAnalysisUsage(AnalysisUsage &AU) const override;
};
#endif
