////////////////////////////////////////////////////////////////////////////////
#include <CollectGlobals.hpp>
#include <MVXAA.hpp>

#define DEBUG_TYPE "mvxaa"
#define USE_SET_SIZE (32)

using namespace llvm;

cl::opt<std::string> MVX_FUNC("mvx-func", cl::desc("Specify function to guard"),
                              cl::value_desc("function name"));

MVXAA::MVXAA()
    : ModulePass(ID), m_pglobals(nullptr), m_pwpa(nullptr), m_targetGlobals() {}

/**
 * @brief We only have a single module, this assumes llvm-link has been called
 * on each bc file. We only want to handle globals first.
 *
 * @param M
 *
 * @return
 */
bool MVXAA::runOnModule(Module &M) {
    m_pglobals = getAnalysis<CollectGlobals>().getResult();

    // Create SVF and run on module
    SVFModule *svfModule = LLVMModuleSet::getLLVMModuleSet()->buildSVFModule(M);

    m_pwpa = new WPAPass();
    m_pwpa->runOnModule(svfModule);

    // Iterate through callgraph of function we're interested in
    CallGraph *CG = new CallGraph(M);

    if (Function *guardedFunc = M.getFunction(m_mvxFunc)) {
        CallGraphNode *guardedHead = CG->getOrInsertFunction(guardedFunc);
        for (auto IT = df_begin(guardedHead), end = df_end(guardedHead);
             IT != end; ++IT) {
            if (Function *F = IT->getFunction()) {
                this->visit(F);
            }
        }
    } else {
        llvm_unreachable("Guarded function name doesn't match any functions");
    }

    outs() << "Target Globals to Move:\n";
    for (Value *TG : m_targetGlobals) {
        outs() << *TG << "\n";
    }

    return false;
}

/**
 * @brief Stores are added for a more conservative estimate, because a Store
 * does not mean a use of the stored-to location and if the FROM location was a
 * pointer from a global to a global, it would have been loaded in a prior
 * instruction, captured by the visitLoadInst
 *
 * @param I
 */
void MVXAA::visitStoreInst(StoreInst &I) {
    LLVM_DEBUG(dbgs() << "STORE:" << I << "\n");

    Value *ptrOperand = I.getPointerOperand();
    Value *valueOperand = I.getValueOperand();

    if (GEPOperator *GEP = dyn_cast<GEPOperator>(ptrOperand)) {
        // GEP essentially indexes into a container, so pointer operand of GEP
        // is the base Value ptr
        Value *gepPtrOperand = GEP->getPointerOperand();
        // Must not be undef value
        assert(!dyn_cast<UndefValue>(gepPtrOperand) &&
               "No undef values allowed");

        if (m_pglobals->find(gepPtrOperand) != m_pglobals->end()) {
            if (valueOperand->getType()->isPointerTy()) {
                if (Value *aliasedGlobal = aliasesGlobal(valueOperand)) {
                    LLVM_DEBUG(dbgs() << "STORE GEP Alias:\n"
                                      << std::string(30, '*') << "\n"
                                      << *valueOperand << "\naliases\n"
                                      << *aliasedGlobal << "\n"
                                      << std::string(30, '*') << "\n");
                    m_targetGlobals.insert(ptrOperand);
                }
            }
        }
    } else {
        // If the pointer operand, where the store instruction is storing to is
        // a global, then get that that operand is pointing to
        if (m_pglobals->find(ptrOperand) != m_pglobals->end()) {
            if (valueOperand->getType()->isPointerTy()) {
                if (Value *aliasedGlobal = aliasesGlobal(valueOperand)) {
                    LLVM_DEBUG(dbgs() << "STORE Alias:\n"
                                      << std::string(30, '*') << "\n"
                                      << *valueOperand << "\naliases\n"
                                      << *aliasedGlobal << "\n"
                                      << std::string(30, '*') << "\n");
                    m_targetGlobals.insert(ptrOperand);
                }
            }
        }
    }
}

/**
 * @brief Load instructions have a LHS and a RHS. We check if the LHS is a
 * pointer type and if it is, does it alias any other global, which are pointers
 * too by LLVM's definition. Before any use of a global->global it will have to
 * be loaded.
 *
 * @param I
 */
void MVXAA::visitLoadInst(LoadInst &I) {
    LLVM_DEBUG(dbgs() << "LOAD:" << I << "\n");
    // If we are loading from a global
    if (m_pglobals->find(I.getPointerOperand()) != m_pglobals->end()) {
        // If our value that's loaded into is a pointer type, and it aliases to
        // a global:
        Value *loadVal = dyn_cast<Value>(&I);
        assert(loadVal && "Load instruction should be value");
        if (loadVal->getType()->isPointerTy()) {
            if (Value *aliasedGlobal = aliasesGlobal(loadVal)) {
                LLVM_DEBUG(dbgs() << "LOAD Alias:\n"
                                  << std::string(30, '*') << "\n"
                                  << *loadVal << "\naliases\n"
                                  << *aliasedGlobal << "\n"
                                  << std::string(30, '*') << "\n");
                m_targetGlobals.insert(I.getPointerOperand());
            }
        }
    }
}

/**
 * @brief Helper function to check if value in input param aliases any global
 * variables
 *
 * @param V Value to check against all known globals
 *
 * @return nullptr if no aliases, if there is an alias, the global which we
 * alias to
 */
Value *MVXAA::aliasesGlobal(Value *V) const {
    Value *retVal = nullptr;
    assert(m_pwpa && "WPA pointer not initialized!");
    for (Value *GV : *m_pglobals) {
        if (m_pwpa->alias(MemoryLocation(V), MemoryLocation(GV))) {
            retVal = GV;
            break;
        }
    }
    return retVal;
}

void MVXAA::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesAll();
    AU.addRequired<CollectGlobals>();
}

bool MVXAA::doInitialization(Module &M) {
    m_mvxFunc = MVX_FUNC;
    LLVM_DEBUG(dbgs() << "MVX func: " << m_mvxFunc << "\n");
    return false;
}
char MVXAA::ID = 0;
RegisterPass<MVXAA> X("mvx-aa", "MVX AA Pass");

