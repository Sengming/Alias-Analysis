////////////////////////////////////////////////////////////////////////////////
#include <CollectGlobals.hpp>
#include <MVXAA.hpp>

#define DEBUG_TYPE "mvxaa"
#define USE_SET_SIZE (32)

using namespace llvm;

cl::opt<std::string> MVX_FUNC("mvx-func", cl::desc("Specify function to guard"),
                              cl::value_desc("function name"));

MVXAA::MVXAA()
    : ModulePass(ID), m_pglobals(), m_pwpa(), m_targetGEPSet(),
      m_targetGlobals() {}

/**
 * @brief We only have a single module, this assumes llvm-link has been called
 * on each bc file. We only want to handle globals first.
 *
 * @param M
 *
 * @return
 */
bool MVXAA::runOnModule(Module &M) {
    // Take ownership of the globals
    m_pglobals = getAnalysis<CollectGlobals>().getResult();
    m_pmainmodule = &M;
    // Create SVF and run on module
    SVF::SVFModule *svfModule =
        SVF::LLVMModuleSet::getLLVMModuleSet()->buildSVFModule(M);
    m_pwpa = std::make_unique<SVF::WPAPass>();
    m_pwpa->runOnModule(svfModule);
    // Andersen *ander = AndersenWaveDiff::createAndersenWaveDiff(svfModule);

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

    // If load instructions's pointer are GEP, resolve their loaders, this is
    // for the case of pointers to pointers in structs
    resolveGEPParents(m_targetGEPSet);

    LLVM_DEBUG(dbgs() << "Target Globals to Move:\n");
    // For direct globals, just assume 0 offset:
    for (Value *TG : m_targetGlobals) {
        LLVM_DEBUG(dbgs() << *TG << "\n");
        m_globalsAndOffsets.insert(GlobalPair_t(TG->getName(), 0));
    }

    // Now dump the data to file:
    dumpGlobalsToFile(m_globalsAndOffsets);

    return false;
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
    // If we are loading from something that aliases a global
    Value *pointerOperand = I.getPointerOperand();
    if (Value *loadedFromAlias = aliasesGlobal(pointerOperand)) {
        // If our value that's loaded into is a pointer type, and it aliases
        // to a global:
        Value *loadVal = dyn_cast<Value>(&I);
        assert(loadVal && "Load instruction should be value");
        if (loadVal->getType()->isPointerTy()) {
            if (Value *aliasedGlobal = aliasesGlobal(loadVal)) {
                LLVM_DEBUG(dbgs() << "LOAD Alias:\n"
                                  << std::string(30, '*') << "\n"
                                  << *loadVal << "\naliases\n"
                                  << *aliasedGlobal << "\n"
                                  << std::string(30, '*') << "\n");
                processPointerOperand(pointerOperand);
            }
        }
    }
}

/**
 * @brief Check if call instructions within visited function are indirect
 calls
 * aka function ptrs.
 *
 * @param I
 */
void MVXAA::visitCallInst(CallInst &I) {
    LLVM_DEBUG(dbgs() << "CALL: " << I << "\n");
    if (I.getCalledFunction() == nullptr) {
        m_fpointers.insert(&I);

        LoadInst *loadInst = dyn_cast_or_null<LoadInst>(I.getCalledOperand());
        // assert(loadInst && "Indirect call's operand should be a load inst!");
        if (loadInst != nullptr) {

            // If the load instruction's pointer operand aliases any globals
            if (Value *aliasedGlobal =
                    aliasesGlobal(loadInst->getPointerOperand())) {
                processPointerOperand(loadInst->getPointerOperand());
            }
        }
    }
}

/**
 * @brief Helper method, common to visitCallInst and visitLoadInst
 *
 * @param ptrOperand
 */
void MVXAA::processPointerOperand(Value *ptrOperand) {
    if (m_pglobals->find(ptrOperand) != m_pglobals->end()) {
        m_targetGlobals.insert(ptrOperand);
    } else if (isa<GetElementPtrInst>(ptrOperand)) {
        m_targetGEPSet.insert(ptrOperand);
    } else {
        errs() << "LOAD: There is a with ptroperand and loadval "
                  "both aliasing globals, but the ptroperand is "
                  "not a GEP instruction.\n";
    }
}

/**
 * @brief Helper function to check if value in input param aliases any
 * global variables
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
        if (m_pwpa->alias(MemoryLocation(V), MemoryLocation(GV)) &&
            !(cast<GlobalVariable>(GV)->isConstant())) {
            retVal = GV;
            break;
        }
    }
    return retVal;
}

/**
 * @brief Resolves load instructions loading from a GEP instruction, this is
 the
 * main method that handles pointers to pointers to pointers. Given two
 global
 * objects of a struct type with pointer members pointing at each other,
 such
 * that a->ptr = b and b->ptr = a. a->ptr->ptr is a double dereference, but
 llvm
 * will break this into a sequence of loads followed by GEP instructions.
 The
 * instructions should look similar to the following:
 *
 * LOAD: %1 = load %struct.struct_type*, %struct.struct_type** @a, align 8
 * GEP : %ptr = getelementptr inbounds %struct.struct_type,
 %struct.struct_type* %1,
 * i32 0, i32 3
 *
 * LOAD: %2 = load %struct.struct_type*, %struct.struct_type** %ptr,
 * align 8
 *
 * GEP : %ptr1 = getelementptr inbounds %struct.struct_type,
 * %struct.struct_type* %2, i32 0, i32 3
 *
 * All we need to do is find the GEP offset to get the member and the base
 aliased global
 * @param gepSet
 */
void MVXAA::resolveGEPParents(const DenseSet<Value *> &gepSet) {
    outs() << "GEP SET------------------------------------: \n";
    for (Value *V : gepSet) {
        GetElementPtrInst *GEPinst = dyn_cast<GetElementPtrInst>(V);
        assert(GEPinst && "Not a GEP Instruction!");
        outs() << "V: " << *V << "\n";
        // If the pointerOperand is a Load instruction, then check if that
        // load aliases any globals. If it does, that is the parent global.
        // Next, check the offset of the member and push that into the pair.
        if (LoadInst *LI = dyn_cast<LoadInst>(GEPinst->getPointerOperand())) {
            if (Value *globalAlias = aliasesGlobal(LI)) {
                if (GEPinst->getNumOperands() >= 3) {
                    if (ConstantInt *CI =
                            dyn_cast<ConstantInt>(GEPinst->getOperand(2))) {
                        LLVM_DEBUG(dbgs() << "GEP Parent Resolution: "
                                          << *globalAlias << " offset: "
                                          << CI->getZExtValue() << "\n";);
                        m_globalsAndOffsets.insert(GlobalPair_t(
                            globalAlias->getName(), CI->getZExtValue()));
                    } else {
                        // llvm_unreachable("GEP Offset is not a constant
                        // int!");
                        LLVM_DEBUG(dbgs()
                                   << "GEP Offset is not a constant int!\n");
                    }
                }
            }
        } else if (m_pglobals->find(GEPinst->getPointerOperand()) !=
                   m_pglobals->end()) {
            // The other case, where GEP doesn't have a load instruction but has
            // the global's symbol as the pointerOperand. This is the case for
            // loads preceeding function ptrs.
            Value *globalMatch = GEPinst->getPointerOperand();
            if (ConstantInt *CI =
                    dyn_cast<ConstantInt>(GEPinst->getOperand(2))) {
                LLVM_DEBUG(dbgs()
                               << "GEP Parent Resolution: " << *globalMatch
                               << " offset: " << CI->getZExtValue() << "\n";);
                m_globalsAndOffsets.insert(
                    GlobalPair_t(globalMatch->getName(), CI->getZExtValue()));
            } else {
                // llvm_unreachable("GEP Offset is not a constant int!");
                LLVM_DEBUG(dbgs() << "GEP Offset is not a constant int!\n");
            }
        }
    }
}

/**
 * @brief Helper to print all the globals pair list to the file
 *
 * @param globalsList
 */
void MVXAA::dumpGlobalsToFile(DenseSet<GlobalPair_t> &globalsList) {
    for (auto pair : globalsList) {
        // assert(pair.first.empty() && "Global doesn't have a name!");
        *m_pinfoFile << pair.first << "," << pair.second << "\n";
    }
}

void MVXAA::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesAll();
    AU.addRequired<CollectGlobals>();
}

/**
 * @brief Setup file ostream to dump global addresses and offsets to
 *
 * @param M
 *
 * @return
 */
bool MVXAA::doInitialization(Module &M) {
    m_mvxFunc = MVX_FUNC;
    LLVM_DEBUG(dbgs() << "MVX func: " << m_mvxFunc << "\n");
    std::error_code E;
    assert((m_pinfoFile =
                std::make_unique<raw_fd_ostream>("global_addresses.dump", E)) &&
           "Error opening dump file!");
    return false;
}

bool MVXAA::doFinalization(Module &M) {
    // Close file
    m_pinfoFile->close();
    return false;
}
char MVXAA::ID = 0;
RegisterPass<MVXAA> X("mvx-aa", "MVX AA Pass");

