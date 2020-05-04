////////////////////////////////////////////////////////////////////////////////

#include <AndersenAA.h>
#include <CollectGlobals.hpp>

// llvm
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallSet.h>
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

#define DEBUG_TYPE "mvxaa"
#define USE_SET_SIZE (32)

using namespace llvm;

cl::opt<std::string> MVX_FUNC("mvx-func", cl::desc("Specify function to guard"),
                              cl::value_desc("function name"));

namespace {

class MVXAA : public ModulePass, public InstVisitor<MVXAA> {
  protected:
    DenseSet<Value *> *m_pglobals;
    Module *m_pmodule;
    AndersenAAResult *m_pAaResult;
    Andersen *m_pPointsToGraph;
    StringRef m_mvxFunc;

  public:
    static char ID;

    MVXAA() : ModulePass(ID) {}

    virtual bool runOnModule(Module &M) override {
        m_pAaResult = &getAnalysis<AndersenAAWrapperPass>().getResult();
        m_pPointsToGraph = &m_pAaResult->getPointsToSets();
        m_pglobals = getAnalysis<CollectGlobals>().getResult();

        // Visit Store instructions and load instructions
        visit(M);
        return false;
    }

    void visitStoreInst(StoreInst &I) {
        LLVM_DEBUG(dbgs() << "Store instruction called: " << I << "\n");

        Value *ptrOperand = I.getPointerOperand();
        LLVM_DEBUG(dbgs() << "Pointer Operand of Store:: " << *ptrOperand
                          << "\n";);

        if (GEPOperator *GEP = dyn_cast<GEPOperator>(ptrOperand)) {
            // for (auto op = GEP->op_begin(), end = GEP->op_end(); op != end;
            //     ++op) {
            //    outs() << "Operands: " << **op << "\t";
            //}
            Value *gepPtrOperand = GEP->getPointerOperand();
            std::vector<const llvm::Value *> pointsToSet;
            if (m_pPointsToGraph->getPointsToSet(gepPtrOperand, pointsToSet)) {
                for (const Value *target : pointsToSet) {
                    outs() << "GEP " << *target << " -> "
                           << *I.getValueOperand() << "\n";
                }
            }
        }

        if (m_pglobals->find(ptrOperand) != m_pglobals->end()) {
            LLVM_DEBUG(dbgs() << "found global stored: "
                              << *I.getPointerOperand() << "\n";);
            std::vector<const llvm::Value *> pointsToSet;
            if (m_pPointsToGraph->getPointsToSet(ptrOperand, pointsToSet)) {
                for (const Value *target : pointsToSet) {
                    // outs() << "Global " << *target << " -> "
                    //       << *I.getValueOperand() << "\n";
                    outs() << "Global " << *I.getValueOperand() << " -> "
                           << *target << "\n";
                }
            }
        }
    }

    void visitLoadInst(LoadInst &I) {
        LLVM_DEBUG(dbgs() << "Load instruction called" << I << "\n");
        if (m_pglobals->find(I.getPointerOperand()) != m_pglobals->end()) {
            LLVM_DEBUG(dbgs() << "found global loaded: "
                              << *I.getPointerOperand() << "\n";);
        }
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
        AU.setPreservesAll();
        AU.addRequired<AndersenAAWrapperPass>();
        AU.addRequired<CollectGlobals>();
    }

    bool doInitialization(Module &M) override {
        m_mvxFunc = MVX_FUNC;
        outs() << "MVX func: " << m_mvxFunc << "\n";
        return false;
    }
};
char MVXAA::ID = 0;
RegisterPass<MVXAA> X("mvx-aa", "MVX AA Pass");

} // namespace
