/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include <string>
#include <ostream>

#include "HLAC/hlac.h"
#include "HLAC/util.h"

namespace HLAC {

void Edge::printDotRepresentation(std::ostream &os) {
    // Cast source and destination as LoopNode
    auto *srcLoop = dynamic_cast<LoopNode *>(this->soure);
    auto *dstLoop = dynamic_cast<LoopNode *>(this->destination);

    // Define containers for the nodes we are trying to connect
    std::string srcName;
    std::string dstName;

    // If we are starting in a LoopNode
    if (srcLoop) {
        // Search for the last real Node.
        // If we can find a real Node in the loop connect to this Node
        if (GenericNode *rep = pickNonLoopNode(srcLoop, true)) {
            srcName = rep->getDotName();
        } else {
            // Otherwise no real node is available -> use anchor node inside the cluster
            srcName = srcLoop->getAnchorDotName();
        }
    } else {
        // If the source node is no LoopNode we can just use this node
        srcName = this->soure->getDotName();
    }

    if (dstLoop) {
        // Search for the first real Node.
        // If we can find a real Node in the loop connect to this Node
        if (GenericNode *rep = pickNonLoopNode(dstLoop, false)) {
            dstName = rep->getDotName();
        } else {
            dstName = dstLoop->getAnchorDotName();
        }
    } else {
        // If the destination node is no LoopNode we can just use this node
        dstName = this->destination->getDotName();
    }

    os << srcName << " -> " << dstName;

    // Emit attributes
    bool firstAttr = true;
    auto openAttrs = [&]() {
        if (firstAttr) {
            os << " ["; firstAttr = false;
        } else {
            os << ",";
        }
    };

    openAttrs();
    os << "label=\"" << Util::feasibilityToString(this->feasibility) << "\"";

    if (srcLoop) {
        openAttrs();
        os << "ltail=\"" << srcLoop->getDotName() << "\"";
    }
    if (dstLoop) {
        openAttrs();
        os << "lhead=\"" << dstLoop->getDotName() << "\"";
    }

    if (!firstAttr) os << "]";
    os << ";\n";
}

GenericNode* Edge::pickNonLoopNode(LoopNode* loopNode, bool pickBack) {
    if (!loopNode) return nullptr;

    // If pickBack is true search at the end of the LoopNode
    if (pickBack) {
        for (auto it = loopNode->Nodes.rbegin(); it != loopNode->Nodes.rend(); ++it) {
            GenericNode* g = it->get();
            if (!g) continue;
            // Check if the found node is not a LoopNode
            if (!dynamic_cast<LoopNode*>(g)) {
                return g;
            }
        }
    } else {
        // Search at the beginning otherwise
        for (auto& up : loopNode->Nodes) {
            GenericNode* g = up.get();
            if (!g) continue;
            // Check if the found node is not a LoopNode
            if (!dynamic_cast<LoopNode*>(g)) {
                return g;
            }
        }
    }

    return nullptr;
}

}  // namespace HLAC
