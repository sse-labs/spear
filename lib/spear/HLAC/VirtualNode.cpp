/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include <string>
#include <memory>
#include <vector>

#include "HLAC/HLACHashing.h"
#include "HLAC/hlac.h"
#include "HLAC/util.h"

std::string HLAC::VirtualNode::getDotName() {
    std::string parentAddress = parent != nullptr ? parent->getAddress() : "NoParent";

    switch (virtualNodeKind) {
        case VirtualNodeKind::Entry:
            return "VirtEntry_" + parentAddress;
        case VirtualNodeKind::NormalExit:
            return "VirtNormalExit_" + parentAddress;
        case VirtualNodeKind::ReturnExit:
            return "VirtReturnExit_" + parentAddress;
        case VirtualNodeKind::BreakExit:
            return "VirtBreakExit_" + parentAddress;
        case VirtualNodeKind::ContinueTarget:
            return "VirtContinueTarget_" + parentAddress;
        case VirtualNodeKind::Generic:
            return "VirtualPoint_" + parentAddress;
        case VirtualNodeKind::FALLBACK:
            return "VirtualPointFallback_" + parentAddress;
    }

    return "VirtualPoint_" + parentAddress;
}

std::string HLAC::VirtualNode::getVirtualNodeLabel(HLAC::VirtualNodeKind virtualNodeKind) {
    switch (virtualNodeKind) {
        case HLAC::VirtualNodeKind::Entry:
            return "VEntry";
        case HLAC::VirtualNodeKind::NormalExit:
            return "VNormalExit";
        case HLAC::VirtualNodeKind::ReturnExit:
            return "VReturnExit";
        case HLAC::VirtualNodeKind::BreakExit:
            return "VBreakExit";
        case HLAC::VirtualNodeKind::ContinueTarget:
            return "VContinueTarget";
        case HLAC::VirtualNodeKind::Generic:
            return "VirtualPoint";
        case HLAC::VirtualNodeKind::FALLBACK:
            return "VirtualPointFallback";
    }

    return "VirtualPoint";
}

void HLAC::VirtualNode::printDotRepresentation(std::ostream &os) {
    llvm::raw_os_ostream llvmOS(os);

    llvmOS << getDotName() << " ["
           << "shape=circle,"
           << "fillcolor=black,"
           << "color=black,"
           << "width=0.1,"
           << "height=0.1,"
           << "label=\"" << getVirtualNodeLabel(virtualNodeKind) << "\""
           << "];\n";

    llvmOS.flush();
}

void HLAC::VirtualNode::printDotRepresentationWithSolution(std::ostream &os, std::vector<double> result) {
    printDotRepresentation(os);
}

std::unique_ptr<HLAC::VirtualNode> HLAC::VirtualNode::makeVirtualPoint(
    VirtualNodeKind virtualNodeKind,
    GenericNode *givparent) {
    auto virtualPoint = std::make_unique<VirtualNode>();
    virtualPoint->virtualNodeKind = virtualNodeKind;
    virtualPoint->parent = givparent;
    virtualPoint->hash = virtualPoint->calculateHash();
    virtualPoint->nodeType = NodeType::VIRTUALNODE;
    return virtualPoint;
}

std::string HLAC::VirtualNode::calculateHash() {
    return Hasher::getHashForNode(this);
}
