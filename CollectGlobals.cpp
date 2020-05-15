////////////////////////////////////////////////////////////////////////////////

#include <CollectGlobals.hpp>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

#define DEBUG_TYPE "globals_collect"
#define USE_SET_SIZE (32)

using namespace llvm;

bool CollectGlobals::runOnModule(Module &M) {
    for (GlobalVariable &G : M.globals()) {
        m_globals->insert(&cast<Value>(G));
    }

    return false;
}

bool CollectGlobals::doInitialization(Module &M) { return false; }

std::unique_ptr<DenseSet<Value *>> CollectGlobals::getResult() {
    return std::move(m_globals);
}

void CollectGlobals::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesAll();
}

char CollectGlobals::ID = 2;
RegisterPass<CollectGlobals> Y("mvxaa-cg", "Collect Globals");

// RegisterPass<CollectGlobals> X("MVX_CG", "MVX Global Collection Pass");
// Automatically enable the pass.
static void registerGlobalCollectionPass(const PassManagerBuilder &PB,
                                         legacy::PassManagerBase &PM) {
    PM.add(new CollectGlobals());
}

// static RegisterStandardPasses
//    RegisterMyPass(PassManagerBuilder::EP_FullLinkTimeOptimizationEarly,
//                   registerGlobalCollectionPass);

// static RegisterStandardPasses
//    RegisterMyPass2(PassManagerBuilder::EP_FullLinkTimeOptimizationLast,
//                    registerGlobalCollectionPass);
//
// static RegisterStandardPasses
//    RegisterMyPass1(PassManagerBuilder::EP_EnabledOnOptLevel0,
//                    registerGlobalCollectionPass);

