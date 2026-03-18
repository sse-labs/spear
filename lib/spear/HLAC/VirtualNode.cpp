/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */


#include "HLAC/hlac.h"

std::string HLAC::VirtualNode::getDotName() {
    if (isEntry) {
        return "Entry";
    } else if (isExit) {
        return "Exit";
    } else {
        return "VirtualPoint";
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
                llvmOS << "label=\"Entry\"";
            } else if (isExit) {
                llvmOS << "label=\"Exit\"";
            } else {
                llvmOS << "label=\"VirtualPoint\"";
            }

            llvmOS << "];\n";

    llvmOS.flush();
}

std::unique_ptr<HLAC::VirtualNode> HLAC::VirtualNode::makeVirtualPoint(bool isEntry, bool isExit) {
    auto virtualPoint = std::make_unique<VirtualNode>();
    virtualPoint->isExit = isExit;
    virtualPoint->isEntry = isEntry;
    return virtualPoint;
}
