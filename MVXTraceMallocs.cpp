////////////////////////////////////////////////////////////////////////////////
#include <CollectGlobals.hpp>
#include <MVXCollectMallocs.hpp>
#include <MVXTraceMallocs.hpp>
#include <iostream>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

// svf
#include <Graphs/PAG.h>
#include <SABER/LeakChecker.h>
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

    m_pmainmodule = &M;
    // SVF SECTION
    // Create SVF and run on module
    SVF::SVFModule *svfModule =
        SVF::LLVMModuleSet::getLLVMModuleSet()->buildSVFModule(M);
    m_pwpa = std::make_unique<SVF::WPAPass>();
    m_pwpa->runOnModule(svfModule);

    /// Build PAG
    SVF::PAGBuilder builder;
    SVF::PAG *pag = builder.build(svfModule);
    pag->dump("mallocpag");
    m_pander.reset(SVF::AndersenWaveDiff::createAndersenWaveDiff(pag));

    /// Call Graph
    SVF::PTACallGraph *callgraph = m_pander->getPTACallGraph();

    collectProtectedFunctions(*callgraph, *svfModule);
    callgraph->dump("svfcallgraph");

    /// Sparse value-flow graph (SVFG)
    SVF::SVFGBuilder svfBuilder;
    SVF::SVFG *svfg = svfBuilder.buildFullSVFG(m_pander.get());

    svfg->dump("svfg");
    initSrcs(pag, svfg, callgraph);
    collectSources(*svfModule);

    // Primary Entry point to SVFG analysis to locate globals on heap
    analyzeSvfg(*svfg, *pag, *callgraph);

    // END SVF SECTION

    // Legacy to the older approach, will replace and remove after SVFG method
    // is proven complete Iterate through callgraph of function we're interested
    // in
    CallGraph *CG = new CallGraph(M);

    if (Function *guardedFunc = M.getFunction(m_mvxFunc)) {
        CallGraphNode *guardedHead = CG->getOrInsertFunction(guardedFunc);
        for (auto IT = df_begin(guardedHead), end = df_end(guardedHead);
             IT != end; ++IT) {
            if (Function *F = IT->getFunction()) {
                // outs() << "Visiting Function: " << F->getName() << "\n";
                // this->visit(F);
            }
        }
    } else {
        llvm_unreachable("Guarded function name doesn't match any functions");
    }

    return false;
}

/**
 * @brief Attempt graph analysis of vfg to trace mallocs
 *
 * @param vfg
 * @param pag
 * @param callgraph
 */
void MVXTraceMallocs::analyzeSvfg(SVF::VFG &vfg, SVF::PAG &pag,
                                  SVF::PTACallGraph &callgraph) {
    for (auto vfgNode : vfg.getGlobalVFGNodes()) {
        // Check for global variable VFG nodes which have an edge to a store or
        // load node. This means the address was stored or loaded. If it's a
        // copy node then the value of the global was copied, which we don't
        // care about.
        for (SVF::VFGNode::const_iterator it = vfgNode->OutEdgeBegin(),
                                          eit = vfgNode->OutEdgeEnd();
             it != eit; ++it) {
            SVF::VFGEdge *edge = *it;
            SVF::VFGNode *succNode = edge->getDstNode();
            if (SVF::StoreVFGNode *storeNode =
                    dyn_cast<SVF::StoreVFGNode>(succNode)) {
                LLVM_DEBUG(dbgs()
                               << vfgNode->getId() << " points to this node: "
                               << storeNode->getId() << "\n";);
                collectDownstreamUseInProtCG(
                    *succNode, *(const_cast<SVF::VFGNode *>(vfgNode)));
            }
        }
    }
    for (auto i : *m_psourceNodes) {
        outs() << "Sources: " << i << "\n";
    }
    ifSrcUpstreamCollectOffset();
    for (auto mapentry : m_sourceToLoadOffsets) {
        outs() << *(mapentry.first) << " : \n";
        for (auto setentry : *(mapentry.second)) {
            outs() << *(setentry.first) << " -- " << setentry.second << "\n";
        }
        outs() << "==================================\n";
    }

    outs() << "----- \n";
}

/**
 * @brief Helper function, checks if the node resides in the protected function
 * set. Need to run collectProtectedFunctions() first.
 *
 * @param vfgNode
 *
 * @return
 */
bool MVXTraceMallocs::isNodeInProtCG(const SVF::VFGNode *vfgNode) {

    const SVF::SVFFunction *containingFunc = vfgNode->getFun();
    assert(containingFunc &&
           "[Trace Mallocs] One of our nodes doesn't have a parent function!");

    if (m_protFunctions.find(containingFunc) != m_protFunctions.end()) {
        return true;
    }
    return false;
}

/**
 * @brief Collects any uses of a stored global's address and then stores it into
 * the global global load set and map
 *
 * @param vfgNode
 * @param globalNode
 */
void MVXTraceMallocs::collectDownstreamUseInProtCG(SVF::VFGNode &vfgNode,
                                                   SVF::VFGNode &globalNode) {
    SVFGNodeWorklist worklist;
    // Initialize BFT
    worklist.push(&vfgNode);
    while (!worklist.empty()) {
        SVF::VFGNode *vfNode = worklist.pop();

        // If the stored global address is ever used
        if (SVF::LoadVFGNode *loadNode = dyn_cast<SVF::LoadVFGNode>(vfNode)) {
            if (isNodeInProtCG(loadNode)) {
                m_globalLoadSet.insert(loadNode);
                m_globalToUseMap[&globalNode] = loadNode;
            }
        }

        for (auto edge : vfNode->getOutEdges()) {
            SVF::VFGNode *nextNode = edge->getDstNode();
            worklist.push(nextNode);
        }
    }
}

/**
 * @brief Using SABER's LeakChecker to find malloc sources. Remove later,
 * defunct.
 */
void MVXTraceMallocs::collectSources(SVF::SVFModule &svfModule) {
    m_psaber->runOnModule(&svfModule);
    // m_psourceNodes = &(m_psaber->getSources());
}

void MVXTraceMallocs::initSrcs(SVF::PAG *pag, SVF::SVFG *svfg,
                               SVF::PTACallGraph *callgraph) {
    SVF::ICFG *icfg = pag->getICFG();
    for (SVF::PAG::CSToRetMap::iterator it = pag->getCallSiteRets().begin(),
                                        eit = pag->getCallSiteRets().end();
         it != eit; ++it) {
        const SVF::RetBlockNode *cs = it->first;
        /// if this callsite return reside in a dead function then we do not
        /// care about its leaks for example instruction p = malloc is in a dead
        /// function, then program won't allocate this memory
        if (SVF::SVFUtil::isPtrInDeadFunction(cs->getCallSite()))
            continue;

        SVF::PTACallGraph::FunctionSet callees;
        callgraph->getCallees(cs->getCallBlockNode(), callees);
        for (SVF::PTACallGraph::FunctionSet::const_iterator
                 cit = callees.begin(),
                 ecit = callees.end();
             cit != ecit; cit++) {
            const SVF::SVFFunction *fun = *cit;
            if (isSourceLikeFun(fun)) {
                SVF::LeakChecker::CSWorkList worklist;
                SVF::LeakChecker::SVFGNodeBS visited;
                worklist.push(it->first->getCallBlockNode());
                while (!worklist.empty()) {
                    const SVF::CallBlockNode *cs = worklist.pop();
                    const SVF::RetBlockNode *retBlockNode =
                        icfg->getRetBlockNode(cs->getCallSite());
                    const SVF::PAGNode *pagNode =
                        pag->getCallSiteRet(retBlockNode);
                    const SVF::SVFGNode *node = svfg->getDefSVFGNode(pagNode);
                    if (visited.test(node->getId()) == 0)
                        visited.set(node->getId());
                    else
                        continue;

                    SVF::LeakChecker::CallSiteSet csSet;
                    // if this node is in an allocation wrapper, find all its
                    // call nodes
                    if (isInAWrapper(node, csSet, svfg)) {
                        for (SVF::LeakChecker::CallSiteSet::iterator
                                 it = csSet.begin(),
                                 eit = csSet.end();
                             it != eit; ++it) {
                            worklist.push(*it);
                        }
                    }
                    // otherwise, this is the source we are interested
                    else {
                        // exclude sources in dead functions
                        if (SVF::SVFUtil::isPtrInDeadFunction(
                                cs->getCallSite()) == false) {
                            m_psourceNodes->insert(node);
                            // addSrcToCSID(node, cs);
                        }
                    }
                }
            }
        }
    }

    // Initialize the source node map with empty sets:
    for (auto sourceNode : *m_psourceNodes) {
        // m_sourceToLoadOffsets[sourceNode] = new SVF::Set<NodeGEPPair>();
        m_sourceToLoadOffsets.insert(
            std::make_pair(sourceNode, new SVF::Set<NodeGEPPair>()));
    }
}

/*!
 * Credits to SVFG for this. Taken from SABER. Had to copy instead of use SABER
 * as we need to iterate on the same SVFG. determine whether a SVFGNode n is in
 * a allocation wrapper function, if so, return all SVFGNodes which receive the
 * value of node n
 */
bool MVXTraceMallocs::isInAWrapper(const SVF::SVFGNode *src,
                                   SVF::LeakChecker::CallSiteSet &csIdSet,
                                   SVF::SVFG *svfg) {

    bool reachFunExit = false;

    SVF::LeakChecker::WorkList worklist;
    worklist.push(src);
    SVF::LeakChecker::SVFGNodeBS visited;
    while (!worklist.empty()) {
        const SVF::SVFGNode *node = worklist.pop();

        if (visited.test(node->getId()) == 0)
            visited.set(node->getId());
        else
            continue;

        for (SVF::SVFGNode::const_iterator it = node->OutEdgeBegin(),
                                           eit = node->OutEdgeEnd();
             it != eit; ++it) {
            const SVF::SVFGEdge *edge = (*it);
            assert(edge->isDirectVFGEdge() &&
                   "the edge should always be direct VF");
            // if this is a call edge
            if (edge->isCallDirectVFGEdge()) {
                return false;
            }
            // if this is a return edge
            else if (edge->isRetDirectVFGEdge()) {
                reachFunExit = true;
                csIdSet.insert(svfg->getCallSite(
                    SVF::SVFUtil::cast<SVF::RetDirSVFGEdge>(edge)
                        ->getCallSiteId()));
            }
            // if this is an intra edge
            else {
                const SVF::SVFGNode *succ = edge->getDstNode();
                if (SVF::SVFUtil::isa<SVF::CopySVFGNode>(succ) ||
                    SVF::SVFUtil::isa<SVF::GepSVFGNode>(succ) ||
                    SVF::SVFUtil::isa<SVF::PHISVFGNode>(succ) ||
                    SVF::SVFUtil::isa<SVF::FormalRetSVFGNode>(succ) ||
                    SVF::SVFUtil::isa<SVF::ActualRetSVFGNode>(succ)) {
                    worklist.push(succ);
                } else {
                    return false;
                }
            }
        }
    }
    if (reachFunExit)
        return true;
    else
        return false;
}

/**
 * @brief Perform BFT and collect all the functions in the protected subtree
 *
 * @param callgraph
 * @param svfModule
 */
void MVXTraceMallocs::collectProtectedFunctions(SVF::PTACallGraph &callgraph,
                                                SVF::SVFModule &svfModule) {
    assert(m_pmainmodule && "Main module pointer not initialized!");
    if (Function *protfunc = m_pmainmodule->getFunction(m_mvxFunc)) {
        CGNodeWorklist worklist;
        SVF::PTACallGraphNode *initNode =
            callgraph.getCallGraphNode(svfModule.getSVFFunction(protfunc));

        // Initialize BFT
        worklist.push(initNode);
        while (!worklist.empty()) {
            const SVF::PTACallGraphNode *cgNode = worklist.pop();
            m_protFunctions.insert(cgNode->getFunction());
            for (auto edge : cgNode->getOutEdges()) {
                SVF::PTACallGraphNode *nextNode = edge->getDstNode();
                m_protFunctions.insert(nextNode->getFunction());
                worklist.push(nextNode);
            }
        }
    }
}

void MVXTraceMallocs::ifSrcUpstreamCollectOffset() {
    // Iterate through all the sourcenodes from SABER
    for (auto source : *m_psourceNodes) {
        SVFGNodeWorkstack workstack;

        // Initialize DFT, note that we are using a pair of the node + the total
        // GEP offset at that DFT level. In this case at source there are no
        // GEP, so offset == 0.
        workstack.push(std::make_pair(const_cast<SVF::VFGNode *>(source), 0));
        while (!workstack.empty()) {
            NodeGEPPair nodeGepPair = workstack.pop();
            SVF::VFGNode *vfNode = nodeGepPair.first;
            int64_t gepOffsetAtLvl = nodeGepPair.second;

            // outs() << "VISITING: " << *vfNode << "\n";

            // If the stored global address is ever used
            if (SVF::LoadVFGNode *loadNode =
                    dyn_cast<SVF::LoadVFGNode>(vfNode)) {
                if (m_globalLoadSet.find(loadNode) != m_globalLoadSet.end()) {
                    // Found a downstream load which is also in our global load
                    // set!
                    // outs() << "FOUND LOAD : " << *loadNode << "\n";
                    SVF::Set<NodeGEPPair> *loadOffsetSet =
                        m_sourceToLoadOffsets.at(source);
                    assert(
                        loadOffsetSet &&
                        "[Trace Mallocs] Set should be initialized to empty!");
                    loadOffsetSet->insert(
                        std::make_pair(vfNode, gepOffsetAtLvl));
                }
            } else if (SVF::GepVFGNode *gepNode =
                           dyn_cast<SVF::GepVFGNode>(vfNode)) {
                // If we are a GEP node, add to the offset tracking:
                if (ConstantInt *CI =
                        dyn_cast<ConstantInt>(gepNode->getInst()->getOperand(
                            gepNode->getInst()->getNumOperands() - 1))) {
                    gepOffsetAtLvl += CI->getSExtValue();
                } else {
                    LLVM_DEBUG(dbgs() << "[Trace Mallocs] Warning: Found "
                                         "non-const GEP offset at inst: "
                                      << *(gepNode->getInst()) << "\n");
                }
            }

            for (auto edge : vfNode->getOutEdges()) {
                SVF::VFGNode *nextNode = edge->getDstNode();
                workstack.push(std::make_pair(nextNode, gepOffsetAtLvl));
            }
        }
    }
}

bool MVXTraceMallocs::doInitialization(Module &M) {
    m_mvxFunc = MVX_FUNC;
    m_psaber = new SVF::LeakChecker();
    m_psourceNodes = new SVFGNodeSet();
    return false;
}

void MVXTraceMallocs::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesAll();
    AU.addRequired<CollectGlobals>();
    AU.addRequired<MVXCollectMallocs>();
}

void MVXTraceMallocs::visitLoadInst(LoadInst &I) {
    if (CallInst *originMalloc = pointsToMallocCall(&I)) {
        // if (CallInst *originMalloc = aliasesMallocCall(&I)) {
        LLVM_DEBUG(dbgs() << "Load Aliases a malloc call: " << *originMalloc
                          << "\n");
        // if (Value *V = aliasesGlobal(I.getPointerOperand())) {
        if (Value *V = aliasesGlobal(&I)) {
            // if (Value *V = pointsToGlobal(&I)) {
            LLVM_DEBUG(dbgs() << "LOAD: " << I << "\n");
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
        LLVM_DEBUG(dbgs() << "LOAD: " << I << "\n");
        if (m_pander->getPAG()->hasValueNode(&I)) {
            SVF::NodeID nodenum =
                m_pander->getPAG()->getValueNode(cast<Value>(&I));
            outs() << "Node Number: " << nodenum << "\n";
        }
    }

    // m_pander->getPAG()->getValueNode(cast<Value>(&I));
}

void MVXTraceMallocs::visitStoreInst(StoreInst &I) {
    if (CallInst *originMalloc = pointsToMallocCall(I.getPointerOperand())) {
        // if (CallInst *originMalloc = aliasesMallocCall(&I)) {
        LLVM_DEBUG(dbgs() << "Store Aliases a malloc call: " << *originMalloc
                          << "\n");
        // if (Value *V = aliasesGlobal(I.getPointerOperand())) {
        // if (Value *V = aliasesGlobal(&I)) {
        if (Value *V = aliasesGlobal(I.getValueOperand())) {
            LLVM_DEBUG(dbgs() << "STORE: " << I << "\n");
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
 * @brief If malloc aliases a global, if you use pwpa the alias result is
 * field sensitive
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
        if (m_pander->alias(V, GV) &&
            // if (m_pwpa->alias(V, GV) &&
            !(cast<GlobalVariable>(GV)->isConstant())) {
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

/*!
 * An example to print points-to set of an LLVM value
 */
std::string MVXTraceMallocs::printRevPts(SVF::PointerAnalysis *pta,
                                         Value *val) {

    std::string str;
    raw_string_ostream rawstr(str);

    SVF::NodeID pNodeId = pta->getPAG()->getValueNode(val);
    const SVF::NodeSet &pts = pta->getRevPts(pNodeId);
    for (auto ii = pts.begin(), ie = pts.end(); ii != ie; ii++) {
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
