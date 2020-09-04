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
#include <WPA/Andersen.h>
#include <WPA/WPAPass.h>

using namespace llvm;

/**
 * @brief Main MVX AA class, we run an SVF-based AA pass initially before
 * iterating over the CallGraph of a specific guarded function
 */
class MVXAA : public ModulePass, public InstVisitor<MVXAA> {

  protected:
    typedef std::pair<StringRef, unsigned> GlobalPair_t;

    std::unique_ptr<DenseSet<Value *>> m_pglobals;

    // All calls of function pointers in program
    DenseSet<CallInst *> m_fpointers;

    Module *m_pmainmodule;

    StringRef m_mvxFunc;
    std::unique_ptr<WPAPass> m_pwpa;
    DenseSet<Value *> m_targetGlobals;
    DenseSet<Value *> m_targetGEPSet;

    // For Reporting
    std::unique_ptr<raw_fd_ostream> m_pinfoFile;
    DenseSet<GlobalPair_t> m_globalsAndOffsets;

    // Helpers
    Value *aliasesGlobal(Value *V) const;
    void dumpGlobalsToFile(DenseSet<GlobalPair_t> &globalsList);
    void processPointerOperand(Value *ptrOperand);

  public:
    static char ID;
    MVXAA();

    virtual bool runOnModule(Module &M) override;

    void visitLoadInst(LoadInst &I);
    void visitCallInst(CallInst &I);

    void resolveGEPParents(const DenseSet<Value *> &gepSet);

    void getAnalysisUsage(AnalysisUsage &AU) const override;

    bool doInitialization(Module &M) override;
    bool doFinalization(Module &M) override;
};

#endif
