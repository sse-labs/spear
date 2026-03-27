/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */


#include "HLAC/HLACHashing.h"
#include "HLAC/hlac.h"
#include "HLAC/util.h"

std::string HLAC::VirtualNode::getDotName() {
    if (isEntry) {
        return "VirtEntry_" + parent->getAddress();
    } else if (isExit) {
        return "VirtExit" + parent->getAddress();
    } else {
        return "VirtualPoint" + parent->getAddress();
    }
}

void HLAC::VirtualNode::printDotRepresentation(std::ostream &os) {
    llvm::raw_os_ostream llvmOS(os);

    llvmOS << getDotName() << "["
            << "shape=circle," << "\n"
            << "fillcolor=black," << "\n"
            << "color=black," << "\n"
            << "width=0.1" << "\n"
            << "height=0.1" << "\n";

            if (isEntry) {
                llvmOS << "label=\"VEntry\"";
            } else if (isExit) {
                llvmOS << "label=\"VExit\"";
            } else {
                llvmOS << "label=\"VirtualPoint\"";
            }

            llvmOS << "];\n";

    llvmOS.flush();
}

void HLAC::VirtualNode::printDotRepresentationWithSolution(std::ostream &os, std::vector<double> result) {
    llvm::raw_os_ostream llvmOS(os);

    llvmOS << getDotName() << "["
            << "shape=circle," << "\n"
            << "fillcolor=black," << "\n"
            << "color=black," << "\n"
            << "width=0.1" << "\n"
            << "height=0.1" << "\n";

    if (isEntry) {
        llvmOS << "label=\"VEntry\"";
    } else if (isExit) {
        llvmOS << "label=\"VExit\"";
    } else {
        llvmOS << "label=\"VirtualPoint\"";
    }

    llvmOS << "];\n";

    llvmOS.flush();
}

std::unique_ptr<HLAC::VirtualNode> HLAC::VirtualNode::makeVirtualPoint(bool isEntry, bool isExit, GenericNode *givparent) {
    auto virtualPoint = std::make_unique<VirtualNode>();
    virtualPoint->isExit = isExit;
    virtualPoint->isEntry = isEntry;
    virtualPoint->parent = givparent;
    virtualPoint->hash = virtualPoint->calculateHash();
    return virtualPoint;
}

std::string HLAC::VirtualNode::calculateHash() {
    return Hasher::getHashForNode(this);
}
