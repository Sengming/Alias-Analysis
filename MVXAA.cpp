////////////////////////////////////////////////////////////////////////////////
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/InstVisitor.h>
#include <llvm/IR/Constants.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/IR/Operator.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Analysis/ScalarEvolutionAliasAnalysis.h>
#include <llvm/Support/Debug.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallSet.h>
#include <AndersenAA.h>

#define DEBUG_TYPE "mvxaa"
#define USE_SET_SIZE (32)

using namespace llvm;

namespace {

class MVXAA : public ModulePass, public InstVisitor<MVXAA> {
  public:
    static char ID;

    MVXAA() : ModulePass(ID) {}
    SmallPtrSet<Value *, USE_SET_SIZE> m_globals;
    Module *m_pmodule;
    AndersenAAResult *m_pAaResult;
    Andersen *m_pPointsToGraph;

    virtual bool runOnModule(Module &M) override {
        m_pAaResult = &getAnalysis<AndersenAAWrapperPass>().getResult();
        m_pPointsToGraph = &m_pAaResult->getPointsToSets();

        for (GlobalVariable &GL : M.globals()) {
            LLVM_DEBUG(dbgs() << "Global is: " << GL << "\n";);
            m_globals.insert(&cast<Value>(GL));
            // AliasResult result = m_pAaResult->alias(MemoryLocation::get(&GL),
            //                                        MemoryLocation::get(&GL));
        }

        // Visit Store instructions and load instructions
        visit(M);
        return false;
    }

    void visitStoreInst(StoreInst &I) {
        LLVM_DEBUG(dbgs() << "Store instruction called: " << I << "\n");

        Value *ptrOperand = I.getPointerOperand();
        outs() << "Pointer Operand of Store:: " << *ptrOperand << "\n";

        if (GEPOperator *GEP = dyn_cast<GEPOperator>(ptrOperand)) {
            for (auto op = GEP->op_begin(), end = GEP->op_end(); op != end;
                 ++op) {
                outs() << "Operands: " << **op << "\t";
            }
        }

        if (m_globals.find(ptrOperand) != m_globals.end()) {
            outs() << "found global stored: " << *I.getPointerOperand() << "\n";
            std::vector<const llvm::Value *> pointsToSet;
            if (m_pPointsToGraph->getPointsToSet(ptrOperand, pointsToSet)) {
                for (const Value *target : pointsToSet) {
                    outs() << "Global points to: " << *target << "\n";
                }
            }
        }
    }

    void visitLoadInst(LoadInst &I) {
        LLVM_DEBUG(dbgs() << "Load instruction called << I << \n");
        if (m_globals.find(I.getPointerOperand()) != m_globals.end()) {
            outs() << "found global loaded: " << *I.getPointerOperand() << "\n";
        }
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
        AU.setPreservesAll();
        AU.addRequired<AndersenAAWrapperPass>();
    }
};
char MVXAA::ID = 0;
RegisterPass<MVXAA> X("mvx-aa", "MVX AA Pass");

// class MVXAA : public llvm::PassInfoMixin<MVXAA> {
//  public:
//    static char ID;
//    MVXAA() {
//        PB.registerParseAACallback([](StringRef Name, AAManager &AA) {
//            AA.registerFunctionAnalysis<SCEVAA>();
//            return false;
//        });
//        PB.registerFunctionAnalyses(m_FAM);
//    }
//    FunctionAnalysisManager m_FAM;
//    PassBuilder PB;
//
//    PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
//        //AndersenAAResult &AAResult = MAM.getResult<AndersenAA>(M);
//        for (Function &F : M) {
//            outs() << "Function called: " << F.getName() << "\n";
//            // SCEVAAResult &SCEVResult = m_FAM.getResult<SCEVAA>(F);
//            for (BasicBlock &BB : F) {
//                outs() << BB << "\n";
//            }
//        }
//        return PreservedAnalyses::all();
//    }
//    // virtual bool runOnFunction(Function &F) {
//    //        //DenseMap<BasicBlock *, std::vector<BitVector> *> *faintMap =
//    //        //    getAnalysis<FaintnessPass>().getFaintResults();
//
//    //        //std::vector<Value *> domainSet =
//    //        //    getAnalysis<FaintnessPass>().getDomainSet();
//
//    //        // If we can fix this function in time, then add it, else keep
//    //        // commented.
//    //        // foldDirectBranches(F);
//    //        return false;
//    //}
//
//    // virtual void getAnalysisUsage(AnalysisUsage &AU) const {
//    //    AU.setPreservesCFG();
//    //    AU.addRequired<SCEVAAWrapperPass>();
//    //    // AU.addRequired<FaintnessPass>();
//    //}
//
//  private:
//};
//
// extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
// llvmGetPassPluginInfo() {
//    return {LLVM_PLUGIN_API_VERSION, "MVX AA Pass", "v0.1",
//            [](PassBuilder &PB) {
//                PB.registerPipelineParsingCallback(
//                    [](StringRef PassName, ModulePassManager &MPM, ...) {
//                        if (PassName == "-mvx-aa") {
//                            MPM.addPass(MVXAA());
//                            return true;
//                        }
//                        return false;
//                    });
//
//            }};
//}

} // namespace
