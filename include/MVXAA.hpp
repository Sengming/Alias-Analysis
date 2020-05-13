#ifndef __MVXAA_HPP__
#define __MVXAA_HPP__

// llvm
#include <llvm/ADT/DepthFirstIterator.h>
#include <llvm/Analysis/CallGraph.h>
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
#include <WPA/WPAPass.h>

using namespace llvm;

/**
 * @brief Main MVX AA class, we run an SVF-based AA pass initially before
 * iterating over the CallGraph of a specific guarded function
 */
class MVXAA : public ModulePass, public InstVisitor<MVXAA> {
  protected:
    DenseSet<Value *> *m_pglobals;
    StringRef m_mvxFunc;
    WPAPass *m_pwpa;
    DenseSet<Value *> m_targetGlobals;
    Value *aliasesGlobal(Value *V) const;

  public:
    static char ID;
    MVXAA();

    virtual bool runOnModule(Module &M) override;

    void visitStoreInst(StoreInst &I);
    void visitLoadInst(LoadInst &I);
    // void visitInstruction(Instruction &I);

    void getAnalysisUsage(AnalysisUsage &AU) const override;

    bool doInitialization(Module &M) override;
};

#endif
