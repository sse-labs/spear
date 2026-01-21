/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include <llvm/IR/BasicBlock.h>
#include <memory>
#include "HLAC/hlac.h"
#include "HLAC/util.h"
#include <llvm/Support/raw_os_ostream.h>

std::unique_ptr<HLAC::Node> HLAC::Node::makeNode(llvm::BasicBlock *basic_block) {
    auto node = std::make_unique<HLAC::Node>();
    node->block = basic_block;
    node->name = basic_block->getName();


    return node;
}

void HLAC::Node::printDotRepresentation(std::ostream &os) {
    llvm::raw_os_ostream llvmOS(os);

    std::string rawBody;
    rawBody.reserve(512);

    for (const llvm::Instruction &I : *this->block) {
        std::string line = Util::instToString(I);

        // Only strip for calls/invokes/callbr
        if (llvm::isa<llvm::CallBase>(I)) {
            line = Util::stripParamsInInstText(std::move(line));
        }

        rawBody += line;
        rawBody += "\n";
    }

    // Ensure last line is left-aligned too
    if (rawBody.empty() || rawBody.back() != '\n')
        rawBody += "\n";

    const std::string escName =
        Util::dotRecordEscape(Util::stripParameters(this->name)) + "\\l";
    const std::string escBody =
        Util::dotRecordEscape(rawBody);

    llvmOS << getDotName() << "["
           << "shape=record,"
           << "style=filled,"
           << "fillcolor=\"#b70d2870\","
           << "color=\"#2B2B2B\","
           << "penwidth=2,"
           << "fontname=\"Courier\","
           << "label=\"{" << escName << "|" << escBody << "}\""
           << ",tooltip=\"" << Util::dotRecordEscape(Util::stripParameters(this->name)) << "\""
           << "];\n";

    llvmOS.flush();
}

std::string HLAC::Node::getDotName() {
    return "Node" + this->getAddress();
}
