////////////////////////////////////////////////////////////////////////////////

#include <CollectGlobals.hpp>
#include <MVXCollectMallocs.hpp>
#include <MVXTraceMallocs.hpp>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

// svf
#include <Graphs/PAG.h>
#include <SVF-FE/PAGBuilder.h>
#include <WPA/Andersen.h>
#include <WPA/WPAPass.h>

#define DEBUG_TYPE "mallocs_trace"
#define USE_SET_SIZE (32)

using namespace llvm;

extern cl::opt<std::string> MVX_FUNC;

/**
 * @brief Before we visit the primary function of interest and its subtrees, run
 * some SVF analyses on it first.
 *
 * @param M
 *
 * @return
 */
bool MVXTraceMallocs::runOnModule(Module &M) {
    // Take ownership of the globals
    m_pglobals = getAnalysis<CollectGlobals>().getResult();

    LLVM_DEBUG(dbgs() << "Number of Globals Collected: " << m_pglobals->size()
                      << "\n");
    // Take ownership of the mallocs
    m_pmallocCalls = getAnalysis<MVXCollectMallocs>().getResult();
    LLVM_DEBUG(dbgs() << "Number of Mallocs Collected: "
                      << m_pmallocCalls->size() << "\n");

    // SVF SECTION
    // Create SVF and run on module
    SVF::SVFModule *svfModule =
        SVF::LLVMModuleSet::getLLVMModuleSet()->buildSVFModule(M);
    m_pwpa = std::make_unique<SVF::WPAPass>();
    // m_pwpa->runOnModule(svfModule);

    /// Build PAG
    SVF::PAGBuilder builder;
    SVF::PAG *pag = builder.build(svfModule);

    // SVF::Andersen *ander =
    // SVF::AndersenWaveDiff::createAndersenWaveDiff(pag);
    m_pander.reset(SVF::AndersenWaveDiff::createAndersenWaveDiff(pag));
    /// Print points-to information
    /// printPts(ander, value1);

    /// Call Graph
    SVF::PTACallGraph *callgraph = m_pander->getPTACallGraph();

    // callgraph->dump("svfcallgraph");

    /// ICFG
    SVF::ICFG *icfg = pag->getICFG();

    /// Sparse value-flow graph (SVFG)
    SVF::SVFGBuilder svfBuilder;
    SVF::SVFG *svfg = svfBuilder.buildFullSVFG(m_pander.get());

    /// Collect uses of an LLVM Value
    /// collectUsesOnVFG(svfg, value);

    // END SVF SECTION

    // Iterate through callgraph of function we're interested in
    CallGraph *CG = new CallGraph(M);

    if (Function *guardedFunc = M.getFunction(m_mvxFunc)) {
        CallGraphNode *guardedHead = CG->getOrInsertFunction(guardedFunc);
        for (auto IT = df_begin(guardedHead), end = df_end(guardedHead);
             IT != end; ++IT) {
            if (Function *F = IT->getFunction()) {
                outs() << "Visiting Function: " << F->getName() << "\n";
                this->visit(F);
            }
        }
    } else {
        llvm_unreachable("Guarded function name doesn't match any functions");
    }

    return false;
}

bool MVXTraceMallocs::doInitialization(Module &M) {
    m_mvxFunc = MVX_FUNC;
    return false;
}

void MVXTraceMallocs::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesAll();
    AU.addRequired<CollectGlobals>();
    AU.addRequired<MVXCollectMallocs>();
}

void MVXTraceMallocs::visitLoadInst(LoadInst &I) {
    LLVM_DEBUG(dbgs() << "LOAD: " << I << "\n");
    // if (CallInst *originMalloc = pointsToMallocCall(&I)) {
    if (CallInst *originMalloc = aliasesMallocCall(&I)) {
        LLVM_DEBUG(dbgs() << "Aliases a malloc call: " << *originMalloc
                          << "\n");
        // if (Value *V = aliasesGlobal(I.getPointerOperand())) {
        if (Value *V = aliasesGlobal(&I)) {
            // if (Value *V = pointsToGlobal(cast<Value>(&I))) {
            LLVM_DEBUG(dbgs() << "ORIGIN MALLOC: " << *originMalloc << "\n");
            LLVM_DEBUG(dbgs() << "ORIGIN GLOBAL: " << *V << "\n");
            LLVM_DEBUG(dbgs() << "MALLOC Resides in Function: "
                              << cast<Instruction>(originMalloc)
                                     ->getParent()
                                     ->getParent()
                                     ->getName()
                              << "\n");
            LLVM_DEBUG(dbgs() << "=================================\n");
        }
    }

    // m_pander->getPAG()->getValueNode(cast<Value>(&I));
}

/**
 * @brief Checks if the value V aliases any call malloc instruction
 *
 * @param V
 *
 * @return
 */
CallInst *MVXTraceMallocs::aliasesMallocCall(Value *V) const {
    CallInst *retVal = nullptr;
    // assert(m_pwpa.get() && "WPA pointer not initialized!");
    assert(m_pander.get() && "Andersen pointer not initialized!");
    for (CallInst *I : *m_pmallocCalls) {
        // outs() << "Testing " << *V << " against " << *I << "\n";
        // if (m_pander->alias(MemoryLocation(V), MemoryLocation(I))) {
        //    retVal = I;
        //    break;
        //}
        if (m_pwpa->alias(V, cast<Value>(I))) {
            // if (m_pander->alias(V, cast<Value>(I))) {
            retVal = I;
            break;
        }
    }
    return retVal;
}

/**
 * @brief Checks if a given value V is in the pts of the set of malloc calls
 * that we collected earlier
 *
 * @param V
 *
 * @return
 */
CallInst *MVXTraceMallocs::pointsToMallocCall(Value *V) const {
    CallInst *retVal = nullptr;
    assert(m_pander.get() && "Andersen pointer not initialized!");

    SVF::NodeID pNodeId = m_pander->getPAG()->getValueNode(V);
    const SVF::NodeBS &pts = m_pander->getPts(pNodeId);
    for (SVF::NodeBS::iterator ii = pts.begin(), ie = pts.end(); ii != ie;
         ii++) {
        for (CallInst *I : *m_pmallocCalls) {
            SVF::PAGNode *targetObj = m_pander->getPAG()->getPAGNode(*ii);
            if (targetObj->hasValue()) {
                // outs() << "Target pts: " << targetObj->getValue()
                //       << "::" << *(targetObj->getValue())
                //       << " StoredMalloc: " << I << "::" << *I << "\n";
                if (targetObj->getValue() == I) {
                    retVal = I;
                    break;
                }
            }
        }
    }
    return retVal;
}

/**
 * @brief If malloc aliases a global, if you use pwpa the alias result is field
 * sensitive
 *
 * @param V
 *
 * @return
 */
Value *MVXTraceMallocs::aliasesGlobal(Value *V) const {
    Value *retVal = nullptr;
    assert(m_pwpa && "WPA pointer not initialized!");
    // assert(m_pander.get() && "Andersen pointer not initialized!");
    for (Value *GV : *m_pglobals) {
        // if (m_pander->alias(V, GV) &&
        if (m_pwpa->alias(V, GV) && !(cast<GlobalVariable>(GV)->isConstant())) {
            retVal = GV;
            break;
        }
    }
    return retVal;
}

/**
 * @brief If malloc points to any globals, currently unused
 *
 * @param V
 *
 * @return
 */
Value *MVXTraceMallocs::pointsToGlobal(Value *V) const {
    Value *retVal = nullptr;
    assert(m_pander.get() && "Andersen pointer not initialized!");
    assert(m_pglobals.get() && "Globals not instantiated");

    SVF::NodeID pNodeId = m_pander->getPAG()->getValueNode(V);
    const SVF::NodeBS &pts = m_pander->getPts(pNodeId);
    for (SVF::NodeBS::iterator ii = pts.begin(), ie = pts.end(); ii != ie;
         ii++) {
        for (Value *I : *m_pglobals) {
            SVF::PAGNode *targetObj = m_pander->getPAG()->getPAGNode(*ii);
            if (targetObj->hasValue()) {
                // outs() << "Target pts: " << targetObj->getValue()
                //       << "::" << *(targetObj->getValue()) << " Global : " <<
                //       I
                //       << "::" << *I << "\n";
                if (targetObj->getValue() == I) {
                    retVal = I;
                    break;
                }
            }
        }
    }
    return retVal;
}

/*!
 * An example to print points-to set of an LLVM value
 */
std::string MVXTraceMallocs::printPts(SVF::PointerAnalysis *pta, Value *val) {

    std::string str;
    raw_string_ostream rawstr(str);

    SVF::NodeID pNodeId = pta->getPAG()->getValueNode(val);
    const SVF::NodeBS &pts = pta->getPts(pNodeId);
    for (SVF::NodeBS::iterator ii = pts.begin(), ie = pts.end(); ii != ie;
         ii++) {
        rawstr << " " << *ii << " ";
        SVF::PAGNode *targetObj = pta->getPAG()->getPAGNode(*ii);
        if (targetObj->hasValue()) {
            rawstr << "(" << *targetObj->getValue() << ")\t ";
        }
    }

    return rawstr.str();
}

char MVXTraceMallocs::ID = 4;
RegisterPass<MVXTraceMallocs> A("mvxaa-tm", "Trace Mallocs");

// Automatically enable the pass.
static void registerMallocTracingPass(const PassManagerBuilder &PB,
                                      legacy::PassManagerBase &PM) {
    PM.add(new MVXTraceMallocs());
}
