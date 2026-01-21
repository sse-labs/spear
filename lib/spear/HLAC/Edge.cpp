
#include "HLAC/hlac.h"

void HLAC::Edge::printDotRepresentation(std::ostream &os) {
    auto *srcLoop = dynamic_cast<HLAC::LoopNode *>(this->soure);
    auto *dstLoop = dynamic_cast<HLAC::LoopNode *>(this->destination);

    // Pick representative endpoint nodes (DOT requires actual nodes as endpoints)
    HLAC::GenericNode *srcRep = this->soure;
    HLAC::GenericNode *dstRep = this->destination;

    if (srcLoop && !srcLoop->Nodes.empty()) {
        srcRep = srcLoop->Nodes.back().get();   // exit-ish representative
    }
    if (dstLoop && !dstLoop->Nodes.empty()) {
        dstRep = dstLoop->Nodes.front().get();  // entry-ish representative
    }

    // Emit edge
    os << srcRep->getDotName() << " -> " << dstRep->getDotName();

    // Emit attributes (lhead/ltail must reference CLUSTER ids, needs compound=true in graph)
    bool firstAttr = true;
    auto openAttrs = [&]() {
        if (firstAttr) { os << " ["; firstAttr = false; }
        else { os << ","; }
    };

    if (srcLoop) {
        // Must be something like "cluster_L123"
        openAttrs();
        os << "ltail=" << srcLoop->getDotName();
    }
    if (dstLoop) {
        openAttrs();
        os << "lhead=" << dstLoop->getDotName();
    }

    if (!firstAttr) os << "]";
    os << ";\n";
}
