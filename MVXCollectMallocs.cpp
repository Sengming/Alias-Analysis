////////////////////////////////////////////////////////////////////////////////

#include <MVXCollectMallocs.hpp>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

#define DEBUG_TYPE "mallocs_collect"
#define USE_SET_SIZE (32)

using namespace llvm;

bool MVXCollectMallocs::runOnModule(Module &M) {
    this->visit(M);
    return false;
}

void MVXCollectMallocs::visitCallInst(CallInst &I) {
    // If this is a malloc, then collect
    if (I.getCalledFunction()->getName() == "malloc") {
        LLVM_DEBUG(dbgs() << "MALLOC:" << I << "\n");
        this->m_mallocCalls->insert(&I);
    }
}

bool MVXCollectMallocs::doInitialization(Module &M) { return false; }

std::unique_ptr<DenseSet<CallInst *>> MVXCollectMallocs::getResult() {
    return std::move(m_mallocCalls);
}

void MVXCollectMallocs::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesAll();
}

char MVXCollectMallocs::ID = 3;
RegisterPass<MVXCollectMallocs> Z("mvxaa-cm", "Collect Mallocs");

// Automatically enable the pass.
static void registerGlobalCollectionPass(const PassManagerBuilder &PB,
                                         legacy::PassManagerBase &PM) {
    PM.add(new MVXCollectMallocs());
}
