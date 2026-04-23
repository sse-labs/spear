/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <string>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <iostream>

#include "HLAC/HLACHashing.h"
#include "HLAC/hlac.h"

namespace HLAC {

std::string Hasher::getHashForNode(GenericNode * node) {
    if (auto virtualNode = dynamic_cast<VirtualNode *>(node)) {
        return Hasher::getHashForVirtualNode(virtualNode);
    }

    if (auto callNode = dynamic_cast<CallNode *>(node)) {
        return Hasher::getHashForCallNode(callNode);
    }

    if (auto normalNode = dynamic_cast<Node *>(node)) {
        return Hasher::getHashForNormalNode(normalNode);
    }

    if (auto loopNode = dynamic_cast<LoopNode *>(node)) {
        return Hasher::getHashForLoopNode(loopNode);
    }

    if (auto funcNode = dynamic_cast<FunctionNode *>(node)) {
        return Hasher::getHashForFunctionNode(funcNode);
    }

    std::cerr << "Warning: unknown node type for hashing, returning empty hash.\n";

    return "";
}

std::string Hasher::getHashForVirtualNode(VirtualNode * node) {
    std::ostringstream outputStream;

    outputStream << "kind=virtual_node;";
    outputStream << "is_entry=" << (node->isEntry ? "1" : "0") << ";";
    outputStream << "is_exit=" << (node->isExit ? "1" : "0") << ";";

    if (node->parent != nullptr) {
        outputStream << "parent_kind=" << Hasher::nodeTypeToString(node->parent) << ";";
    }

    return outputStream.str();
}

std::string Hasher::getHashForCallNode(CallNode * node) {
    std::ostringstream outputStream;

    outputStream << "kind=call_node;";

    if (node->parentFunctionNode != nullptr && node->parentFunctionNode->function != nullptr) {
        outputStream << "parent_function=" << node->parentFunctionNode->function->getName().str() << ";";
    }

    if (node->calledFunction != nullptr) {
        outputStream << "called_function=" << node->calledFunction->getName().str() << ";";
    } else {
        outputStream << "called_function=indirect;";
    }

    outputStream << "is_linker=" << (node->isLinkerFunction ? "1" : "0") << ";";
    outputStream << "is_debug=" << (node->isDebugFunction ? "1" : "0") << ";";
    outputStream << "is_syscall=" << (node->isSyscall ? "1" : "0") << ";";

    if (node->syscallId.has_value()) {
        outputStream << "syscall_id=" << *node->syscallId << ";";
    }

    if (node->call != nullptr) {
        outputStream << "instruction_parent_block="
                     << Hasher::getBasicBlockIndexInFunction(node->call->getParent()) << ";";

        outputStream << "call_instruction={";
        outputStream << "opcode=" << node->call->getOpcodeName() << ",";
        outputStream << "result_type=" << Hasher::typeToString(node->call->getType()) << ",";
        outputStream << "num_operands=" << node->call->getNumOperands() << ",";
        outputStream << "operands=[";

        for (unsigned operandIndex = 0; operandIndex < node->call->getNumOperands(); ++operandIndex) {
            outputStream << Hasher::valueToString(node->call->getOperand(operandIndex)) << ",";
        }

        outputStream << "]};";
    }

    return outputStream.str();
}

std::string Hasher::getHashForNormalNode(Node * node) {
    return Hasher::getBasicBlockToHexString(node->block);
}

std::string Hasher::getHashForLoopNode(LoopNode *node) {
    std::ostringstream signatureStream;

    signatureStream << "kind=loop_node;";

    if (node->parentFunction != nullptr && node->parentFunction->function != nullptr) {
        signatureStream << "parent_function=" << node->parentFunction->function->getName().str() << ";";
    }

    if (node->loop != nullptr && node->loop->getHeader() != nullptr) {
        signatureStream << "llvm_loop_header=" << Hasher::getBasicBlockHash(node->loop->getHeader()) << ";";
    }

    signatureStream << "has_sub_loops=" << (node->hasSubLoops ? "1" : "0") << ";";
    signatureStream << "bounds=[" << node->bounds.getLowerBound() << "," << node->bounds.getUpperBound() << "];";

    struct ChildNodeEntry {
        GenericNode *node = nullptr;
        std::string kind;
        std::string hash;
        std::string tieBreaker;
        std::size_t originalIndex = 0;
    };

    std::vector<ChildNodeEntry> childNodes;
    childNodes.reserve(node->Nodes.size());

    for (std::size_t nodeIndex = 0; nodeIndex < node->Nodes.size(); ++nodeIndex) {
        GenericNode *childNode = node->Nodes[nodeIndex].get();

        childNodes.push_back(ChildNodeEntry{
            childNode,
            Hasher::nodeTypeToString(childNode),
            childNode->calculateHash(),
            Hasher::fallBackHashAdditions(childNode),
            nodeIndex
        });
    }

    std::sort(
        childNodes.begin(),
        childNodes.end(),
        [](const ChildNodeEntry &leftEntry, const ChildNodeEntry &rightEntry) {
            return std::tie(leftEntry.kind, leftEntry.hash, leftEntry.tieBreaker, leftEntry.originalIndex)
                 < std::tie(rightEntry.kind, rightEntry.hash, rightEntry.tieBreaker, rightEntry.originalIndex);
        });

    std::unordered_map<GenericNode *, std::string> localNodeIds;
    for (std::size_t sortedIndex = 0; sortedIndex < childNodes.size(); ++sortedIndex) {
        localNodeIds[childNodes[sortedIndex].node] = "node" + std::to_string(sortedIndex);
    }

    signatureStream << "nodes=[";
    for (const ChildNodeEntry &childEntry : childNodes) {
        signatureStream << "{";
        signatureStream << "id=" << localNodeIds.at(childEntry.node) << ",";
        signatureStream << "kind=" << childEntry.kind << ",";
        signatureStream << "hash=" << childEntry.hash;
        signatureStream << "};";
    }
    signatureStream << "];";

    std::unordered_set<const Edge *> backEdgeSet;
    backEdgeSet.reserve(node->backEdges.size());
    for (const Edge *backEdge : node->backEdges) {
        if (backEdge != nullptr) {
            backEdgeSet.insert(backEdge);
        }
    }

    std::vector<std::string> edgeDescriptions;
    edgeDescriptions.reserve(node->Edges.size());

    std::vector<std::string> explicitBackEdgeDescriptions;
    explicitBackEdgeDescriptions.reserve(node->backEdges.size());

    for (const auto &edgeUniquePtr : node->Edges) {
        const Edge *edge = edgeUniquePtr.get();

        if (edge == nullptr || edge->soure == nullptr || edge->destination == nullptr) {
            continue;
        }

        auto sourceIterator = localNodeIds.find(edge->soure);
        auto destinationIterator = localNodeIds.find(edge->destination);

        if (sourceIterator == localNodeIds.end() || destinationIterator == localNodeIds.end()) {
            continue;
        }

        std::ostringstream edgeStream;
        edgeStream << sourceIterator->second
                   << "->"
                   << destinationIterator->second
                   << ":feasible="
                   << (edge->feasibility ? "1" : "0");

        if (backEdgeSet.contains(edge)) {
            edgeStream << ":backedge=1";

            std::ostringstream backEdgeDescriptionStream;
            backEdgeDescriptionStream << sourceIterator->second
                                      << "->"
                                      << destinationIterator->second;
            explicitBackEdgeDescriptions.push_back(backEdgeDescriptionStream.str());
        }

        edgeDescriptions.push_back(edgeStream.str());
    }

    std::sort(edgeDescriptions.begin(), edgeDescriptions.end());
    std::sort(explicitBackEdgeDescriptions.begin(), explicitBackEdgeDescriptions.end());

    signatureStream << "edges=[";
    for (const std::string &edgeDescription : edgeDescriptions) {
        signatureStream << edgeDescription << ";";
    }
    signatureStream << "];";

    signatureStream << "explicit_backedges=[";
    for (const std::string &backEdgeDescription : explicitBackEdgeDescriptions) {
        signatureStream << backEdgeDescription << ";";
    }
    signatureStream << "];";

    return signatureStream.str();
}

std::string Hasher::getHashForFunctionNode(FunctionNode *node) {
    std::ostringstream signatureStream;

    signatureStream << "kind=function_node;";

    if (node->function != nullptr) {
        signatureStream << "function_name=" << node->function->getName().str() << ";";
    }

    struct ChildNodeEntry {
        GenericNode *node = nullptr;
        std::string kind;
        std::string hash;
        std::string tieBreaker;
        std::size_t originalIndex = 0;
    };

    std::vector<ChildNodeEntry> childNodes;
    childNodes.reserve(node->Nodes.size());

    for (std::size_t nodeIndex = 0; nodeIndex < node->Nodes.size(); ++nodeIndex) {
        GenericNode *childNode = node->Nodes[nodeIndex].get();

        childNodes.push_back(ChildNodeEntry{
            childNode,
            Hasher::nodeTypeToString(childNode),
            childNode->calculateHash(),
            Hasher::fallBackHashAdditions(childNode),
            nodeIndex
        });
    }

    std::sort(
        childNodes.begin(),
        childNodes.end(),
        [](const ChildNodeEntry &leftEntry, const ChildNodeEntry &rightEntry) {
            return std::tie(leftEntry.kind, leftEntry.hash, leftEntry.tieBreaker, leftEntry.originalIndex)
                 < std::tie(rightEntry.kind, rightEntry.hash, rightEntry.tieBreaker, rightEntry.originalIndex);
        });

    std::unordered_map<GenericNode *, std::string> localNodeIds;
    for (std::size_t sortedIndex = 0; sortedIndex < childNodes.size(); ++sortedIndex) {
        localNodeIds[childNodes[sortedIndex].node] = "node" + std::to_string(sortedIndex);
    }

    signatureStream << "nodes=[";
    for (const ChildNodeEntry &childEntry : childNodes) {
        signatureStream << "{";
        signatureStream << "id=" << localNodeIds.at(childEntry.node) << ",";
        signatureStream << "kind=" << childEntry.kind << ",";
        signatureStream << "hash=" << childEntry.hash;
        signatureStream << "};";
    }
    signatureStream << "];";

    std::vector<std::string> edgeDescriptions;
    edgeDescriptions.reserve(node->Edges.size());

    for (const auto &edgeUniquePtr : node->Edges) {
        const Edge *edge = edgeUniquePtr.get();

        if (edge == nullptr || edge->soure == nullptr || edge->destination == nullptr) {
            continue;
        }

        auto sourceIterator = localNodeIds.find(edge->soure);
        auto destinationIterator = localNodeIds.find(edge->destination);

        if (sourceIterator == localNodeIds.end() || destinationIterator == localNodeIds.end()) {
            continue;
        }

        std::ostringstream edgeStream;
        edgeStream << sourceIterator->second
                   << "->"
                   << destinationIterator->second
                   << ":feasible="
                   << (edge->feasibility ? "1" : "0");

        edgeDescriptions.push_back(edgeStream.str());
    }

    std::sort(edgeDescriptions.begin(), edgeDescriptions.end());

    signatureStream << "edges=[";
    for (const std::string &edgeDescription : edgeDescriptions) {
        signatureStream << edgeDescription << ";";
    }
    signatureStream << "];";

    signatureStream << "entry_index=" << node->entryIndex << ";";
    signatureStream << "exit_index=" << node->exitIndex << ";";

    return signatureStream.str();
}

std::string Hasher::getBasicBlockHash(const llvm::BasicBlock *basicBlock) {
    const llvm::Function *parentFunction = basicBlock->getParent();
    if (parentFunction == nullptr) {
        throw std::runtime_error("BasicBlock has no parent function");
    }

    std::ostringstream signatureStream;

    signatureStream << "function=" << parentFunction->getName().str() << ";";
    signatureStream << "block_index=" << getBasicBlockIndexInFunction(basicBlock) << ";";

    signatureStream << "instructions=[";
    for (const llvm::Instruction &instruction : *basicBlock) {
        signatureStream << "{";
        signatureStream << "opcode=" << instruction.getOpcodeName() << ",";
        signatureStream << "result_type=" << Hasher::typeToString(instruction.getType()) << ",";
        signatureStream << "num_operands=" << instruction.getNumOperands() << ",";
        signatureStream << "operands=[";

        for (unsigned operandIndex = 0; operandIndex < instruction.getNumOperands(); ++operandIndex) {
            signatureStream << valueToString(instruction.getOperand(operandIndex)) << ",";
        }

        signatureStream << "]";

        if (const auto *callBase = llvm::dyn_cast<llvm::CallBase>(&instruction)) {
            const llvm::Function *calledFunction = callBase->getCalledFunction();

            if (calledFunction != nullptr) {
                signatureStream << ",callee=" << calledFunction->getName().str();
            } else {
                signatureStream << ",callee=indirect";
            }
        }

        signatureStream << "};";
    }
    signatureStream << "]";

    return signatureStream.str();
}

std::string Hasher::getBasicBlockToHexString(const llvm::BasicBlock *basicBlock) {
    const std::string signature = getBasicBlockHash(basicBlock);
    const std::size_t hashValue = std::hash<std::string>{}(signature);
    return toHexString(hashValue);
}

std::string Hasher::typeToString(const llvm::Type *type) {
    std::string typeString;
    llvm::raw_string_ostream outputStream(typeString);
    type->print(outputStream);
    return outputStream.str();
}


std::string Hasher::apIntToString(const llvm::APInt &value) {
    llvm::SmallVector<char, 32> buffer;

    // Convert APInt to string (base 10, signed)
    value.toString(buffer, 10, true);

    return std::string(buffer.begin(), buffer.end());
}

std::size_t Hasher::getBasicBlockIndexInFunction(const llvm::BasicBlock *basicBlock) {
    const llvm::Function *parentFunction = basicBlock->getParent();
    if (parentFunction == nullptr) {
        throw std::runtime_error("BasicBlock has no parent function");
    }

    std::size_t blockIndex = 0;
    for (const llvm::BasicBlock &candidateBlock : *parentFunction) {
        if (&candidateBlock == basicBlock) {
            return blockIndex;
        }
        ++blockIndex;
    }

    throw std::runtime_error("BasicBlock not found in parent function");
}


std::string Hasher::fallBackHashAdditions(const HLAC::GenericNode *node) {
    if (const auto *basicBlockNode = dynamic_cast<const HLAC::Node *>(node)) {
        if (basicBlockNode->block != nullptr) {
            return getBasicBlockHash(basicBlockNode->block);
        }
    }

    if (const auto *loopNode = dynamic_cast<const HLAC::LoopNode *>(node)) {
        if (loopNode->loop != nullptr && loopNode->loop->getHeader() != nullptr) {
            return "header:" + getBasicBlockHash(loopNode->loop->getHeader());
        }
    }

    if (const auto *callNode = dynamic_cast<const HLAC::CallNode *>(node)) {
        std::ostringstream outputStream;
        outputStream << "call:";

        if (callNode->calledFunction != nullptr) {
            outputStream << callNode->calledFunction->getName().str();
        } else {
            outputStream << "indirect";
        }

        if (callNode->call != nullptr) {
            outputStream << ":parent_block_index="
                         << getBasicBlockIndexInFunction(callNode->call->getParent());
        }

        return outputStream.str();
    }

    if (const auto *virtualNode = dynamic_cast<const HLAC::VirtualNode *>(node)) {
        std::ostringstream outputStream;
        outputStream << "virtual:";
        outputStream << "entry=" << (virtualNode->isEntry ? "1" : "0") << ",";
        outputStream << "exit=" << (virtualNode->isExit ? "1" : "0");
        return outputStream.str();
    }

    return node->name;
}

std::string Hasher::valueToString(const llvm::Value *value) {
    if (const auto *constantInt = llvm::dyn_cast<llvm::ConstantInt>(value)) {
        return "const_int:" + apIntToString(constantInt->getValue());
    }

    if (const auto *constantFloat = llvm::dyn_cast<llvm::ConstantFP>(value)) {
        llvm::SmallVector<char, 32> buffer;
        constantFloat->getValueAPF().toString(buffer);

        return "const_fp:" + std::string(buffer.begin(), buffer.end());
    }

    if (const auto *argument = llvm::dyn_cast<llvm::Argument>(value)) {
        return "arg:" + std::to_string(argument->getArgNo()) + ":" + typeToString(argument->getType());
    }

    if (const auto *basicBlock = llvm::dyn_cast<llvm::BasicBlock>(value)) {
        return "basic_block:" + std::to_string(getBasicBlockIndexInFunction(basicBlock));
    }

    if (const auto *function = llvm::dyn_cast<llvm::Function>(value)) {
        return "function:" + function->getName().str();
    }

    if (const auto *globalValue = llvm::dyn_cast<llvm::GlobalValue>(value)) {
        return "global:" + globalValue->getName().str();
    }

    if (llvm::isa<llvm::Instruction>(value)) {
        return "instruction_result:" + typeToString(value->getType());
    }

    return "value:" + typeToString(value->getType());
}


std::string Hasher::nodeTypeToString(const HLAC::GenericNode *node) {
    if (dynamic_cast<const HLAC::Node *>(node) != nullptr) {
        return "basic_block_node";
    }

    if (dynamic_cast<const HLAC::LoopNode *>(node) != nullptr) {
        return "loop_node";
    }

    if (dynamic_cast<const HLAC::CallNode *>(node) != nullptr) {
        return "call_node";
    }

    if (dynamic_cast<const HLAC::VirtualNode *>(node) != nullptr) {
        return "virtual_node";
    }

    if (dynamic_cast<const HLAC::FunctionNode *>(node) != nullptr) {
        return "function_node";
    }

    return "generic_node";
}

std::string Hasher::toHexString(std::size_t value) {
    std::ostringstream outputStream;
    outputStream << std::hex << value;
    return outputStream.str();
}

std::string Hasher::toHexString(std::string value) {
    std::ostringstream outputStream;

    // Convert each byte to a two-digit hex value
    for (unsigned char character : value) {
        outputStream << std::hex << std::setw(2) << std::setfill('0')
                     << static_cast<int>(character);
    }

    return outputStream.str();
}
}  // namespace HLAC
