/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <llvm/Demangle/Demangle.h>
#include <iostream>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ConfigParser.h"
#include "ELBs/ELPMapper.h"
#include "HLAC/HLACHashing.h"
#include "HLAC/hlac.h"
#include "HLAC/util.h"
#include "Logger.h"
#include "ProfileHandler.h"
#include "syscalls/generated_syscall_names.h"

namespace HLAC {

CallNode::CallNode(llvm::Function *calls, llvm::CallBase *call, FunctionNode *parent) {
    this->call = call;
    this->calledFunction = calls;
    this->name = "Call to " + calledFunction->getName().str();
    this->isLinkerFunction = calledFunction->isDeclarationForLinker();
    this->isSyscall = checkIfIsSyscall();
    this->isDebugFunction = calledFunction->getName().startswith("llvm.");
    this->parentFunctionNode = parent;
    this->nodeType = NodeType::CALLNODE;

    this->hash = this->CallNode::calculateHash();
}

void CallNode::collapseCalls(Node *belongingNode, std::vector<std::unique_ptr<GenericNode>> &nodeList,
                             std::vector<std::unique_ptr<Edge>> &edgeList) {
    // Check if the belonging node and the call are valid
    if (!belongingNode || !this->call) {
        return;
    }

    // Check that the belonging node's block is valid
    llvm::BasicBlock *bb = belongingNode->block;
    if (!bb) {
        return;
    }

    // Validate that our call originates from the referenced basic block
    if (this->call->getParent() != bb) {
        return;
    }

    /**
     * Collect all edges taht start in the belonging node.
     * Afterwards we delete these edges
     */
    std::vector<GenericNode *> targets;
    targets.reserve(8);

    for (auto it = edgeList.begin(); it != edgeList.end();) {
        Edge *e = it->get();
        if (!e) {
            ++it;
            continue;
        }

        if (e->soure == belongingNode) {
            // Deal with already existing edges to this CallNode
            // We only want to store one edge
            if (e->destination == this) {
                it = edgeList.erase(it);
                continue;
            }
            // Store the targets that need to be connected to our CallNode
            targets.push_back(e->destination);
            it = edgeList.erase(it);  // Remove the connection belongingNode -> target
            continue;
        }

        ++it;
    }

    // Add an edge belongingNode -> CallNode
    if (!edgeExists(edgeList, belongingNode, this)) {
        edgeList.emplace_back(std::make_unique<Edge>(belongingNode, this));
    }

    // Add CallNode -> targets
    for (GenericNode *t : targets) {
        if (!t || t == this) {
            continue;
        }
        if (!edgeExists(edgeList, this, t)) {
            edgeList.emplace_back(std::make_unique<Edge>(this, t));
        }
    }
}

std::unique_ptr<CallNode> CallNode::makeNode(llvm::Function *function, llvm::CallBase *instruction,
                                             FunctionNode *parent) {
    auto callnode = std::make_unique<CallNode>(function, instruction, parent);
    return callnode;
}

bool CallNode::edgeExists(const std::vector<std::unique_ptr<Edge>> &edgeList, GenericNode *s, GenericNode *d) {
    for (auto &eup : edgeList) {
        const Edge *e = eup.get();
        if (e && e->soure == s && e->destination == d) {
            return true;
        }
    }
    return false;
}

void CallNode::printDotRepresentation(std::ostream &os) {
    // Demangle name of the function
    std::string full = llvm::demangle(this->calledFunction->getName().str());

    // Call dot string cleaning pipeline
    std::string shortLabel = full;
    shortLabel = Util::shortenStdStreamOps(std::move(shortLabel));
    shortLabel = Util::dropReturnType(std::move(shortLabel));
    shortLabel = Util::stripParameters(std::move(shortLabel));

    // Print dot representation to the given OS
    os << getDotName() << "["
       << "shape=record,"
       << "style=filled,"
       << "fillcolor=\"#8D89A6\","
       << "color=\"#2B2B2B\","
       << "style=\"rounded,filled\","
       << "penwidth=2,"
       << "fontname=\"Courier\","
       << "label=\"{"
       << "call:\\l"
       << "| " << Util::dotRecordEscape(shortLabel) << "| { LINKERFUNC=" << isLinkerFunction
       << " | DEBUGFUNC=" << isDebugFunction << " | SYSCALL=" << isSyscall;

    if (syscallId.has_value()) {
        os << " | SID=" << syscallId.value() << " }";
    } else {
        os << " }";
    }

    os << "}\""
       << "];\n";
}

void CallNode::printDotRepresentationWithSolution(std::ostream &os, std::vector<double> result) {
    // Demangle name of the function
    std::string full = llvm::demangle(this->calledFunction->getName().str());

    // Call dot string cleaning pipeline
    std::string shortLabel = full;
    shortLabel = Util::shortenStdStreamOps(std::move(shortLabel));
    shortLabel = Util::dropReturnType(std::move(shortLabel));
    shortLabel = Util::stripParameters(std::move(shortLabel));

    // Print dot representation to the given OS
    os << getDotName() << "["
       << "shape=record,"
       << "style=filled,"
       << "fillcolor=\"#8D89A6\","
       << "color=\"#2B2B2B\","
       << "style=\"rounded,filled\","
       << "penwidth=2,"
       << "fontname=\"Courier\","
       << "label=\"{"
       << "call:\\l"
       << this->getEnergy() << " J"
       << "| " << Util::dotRecordEscape(shortLabel) << "| { LINKERFUNC=" << isLinkerFunction
       << " | DEBUGFUNC=" << isDebugFunction << " | SYSCALL=" << isSyscall;

    if (syscallId.has_value()) {
        os << " | SID=" << syscallId.value() << " }";
    } else {
        os << " }";
    }

    os << "}\""
       << "];\n";
}


std::string CallNode::getDotName() {
    return "CallNode" + this->getAddress();
}

bool CallNode::checkIfIsSyscall() {
    /**
     * We need to check two cases to determine if this is a syscall:
     * 1) The called function is the function "syscall" which does a direct call to the system call
     * 2) The called function is a wrapper function to the syscall.
     *
     */

    if (this->calledFunction->getName() == "syscall") {
        if (this->calledFunction->arg_size() < 1) {
            // syscall without arguments is not a valid syscall, so we do not consider this a syscall
            return false;
        }

        llvm::Value *arg = this->calledFunction->getArg(0);

        if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(arg)) {
            uint64_t localSyscallId = CI->getSExtValue();
            this->syscallId = localSyscallId;
        }

        return true;
    }


    if (this->calledFunction->isDeclarationForLinker()) {
        std::string name = this->calledFunction->getName().str();
        int localSyscallId = getSyscallId(name);
        if (localSyscallId != -1) {
            this->syscallId = localSyscallId;
            return true;
        }
    }

    return false;
}

double CallNode::getEnergy() {
    // If we encounter a syscall, we can just return the energy
    if (this->isSyscall) {
        if (syscallId.has_value()) {
            auto candidate =
                    ProfileHandler::get_instance().getEnergyForSyscall(std::string(getSyscallName(syscallId.value())));
            if (candidate.has_value()) {
                return candidate.value();
            }
        }
    }

    // If we have a normal call we need to calculate the energy of the called function and return this as the energy of
    // the call
    if (isLinkerFunction) {
        // if we have a linker function first check if there is a ELB lookup
        if (ConfigParser::getAnalysisConfiguration().elbMappingActivated) {
            // Try to resolve the value through the ELBMapper
            auto lookUpCandidate = ELBMapper::getInstance().lookup(this->calledFunction->getName().str());
            if (lookUpCandidate.has_value()) {
                // If we found a value return it
                this->resolvedByELB = true;
                return lookUpCandidate.value();
            }
        }

        // If the linker function could not be resolved using the ELBMapper, we need to fallback to the config val
        double fallbackEnergy = ConfigParser::getAnalysisConfiguration().fallback["calls"]["UNKNOWN_FUNCTION"];

        std::ostringstream outputStream;
        outputStream << std::scientific << std::setprecision(8) << fallbackEnergy;

        /*Logger::getInstance().log("CallNode " + this->calledFunction->getName().str() +
            " is a linker function. We do not have the body of the function and thus cannot analyze it. Returning energy " +
            outputStream.str() + " J", LOGLEVEL::WARNING);*/

        return fallbackEnergy;
    }

    // Assume the function has been analyzed beforehand, so we can just look up the energy in the cache of the parent
    // graph
    // Logger::getInstance().log("Caller:" + this->parentFunctionNode->function->getName().str());
    auto callername = this->parentFunctionNode->function->getName().str();
    auto calleename = this->calledFunction->getName().str();

    auto energyOfCallee = parentFunctionNode->parentGraph->getEnergyPerFunction(calleename,
        this->parentFunctionNode->isRecursive);

    return energyOfCallee;
}

std::string CallNode::calculateHash() {
    return Hasher::getHashForNode(this);
}

}  // namespace HLAC
