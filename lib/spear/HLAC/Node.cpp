/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include <llvm/IR/BasicBlock.h>
#include <llvm/Support/raw_os_ostream.h>

#include <memory>
#include <vector>
#include <string>

#include "HLAC/HLACHashing.h"
#include "HLAC/hlac.h"
#include "HLAC/util.h"
#include "Logger.h"
#include "ProfileHandler.h"

namespace HLAC {

std::unique_ptr<Node> Node::makeNode(llvm::BasicBlock *basic_block) {
    // Store node parameters
    auto node = std::make_unique<Node>();
    node->block = basic_block;
    node->name = basic_block->getName();
    node->hash = node->calculateHash();
    node->nodeType = NodeType::NODE;

    return node;
}

void Node::printDotRepresentation(std::ostream &os) {
    llvm::raw_os_ostream llvmOS(os);

    std::string rawBody;
    rawBody.reserve(512);

    for (const llvm::Instruction &I : *this->block) {
        std::string line = Util::instToString(I);

        // Only strip for calls/invokes/callbr
        if (llvm::isa<llvm::CallBase>(I)) {
            line = Util::stripParameters(line);
        }

        rawBody += line;
        rawBody += "\n";
    }

    // Ensure last line is left-aligned too
    if (rawBody.empty() || rawBody.back() != '\n')
        rawBody += "\n";

    const std::string escName = Util::dotRecordEscape(Util::stripParameters(this->name)) + "\\l";
    const std::string escBody = Util::dotRecordEscape(rawBody);

    llvmOS << getDotName() << "["
           << "shape=record," << "\n"
           << "style=filled," << "\n"
           << "fillcolor=\"#b70d2870\"," << "\n"
           << "color=\"#2B2B2B\"," << "\n"
           << "penwidth=2," << "\n"
           << "style=\"rounded,filled\"," << "\n"
           << "fontname=\"Courier\"," << "\n"
           << "label=\"{" << escName << "|" << escBody << "}\""
           << ",tooltip=\"" << Util::dotRecordEscape(Util::stripParameters(this->name)) << "\""
           << "];\n";

    llvmOS.flush();
}

void Node::printDotRepresentationWithSolution(std::ostream &os, std::vector<double> result) {
    llvm::raw_os_ostream llvmOS(os);

    std::string rawBody;
    rawBody.reserve(512);

    for (const llvm::Instruction &I : *this->block) {
        std::string line = Util::instToString(I);

        // Only strip for calls/invokes/callbr
        if (llvm::isa<llvm::CallBase>(I)) {
            line = Util::stripParameters(line);
        }

        rawBody += line;
        rawBody += "\n";
    }

    // Ensure last line is left-aligned too
    if (rawBody.empty() || rawBody.back() != '\n')
        rawBody += "\n";

    const std::string escName = Util::dotRecordEscape(Util::stripParameters(this->name)) + "\\l";
    const std::string escBody = Util::dotRecordEscape(rawBody);

    llvmOS << getDotName() << "["
           << "shape=record," << "\n"
           << "style=filled," << "\n"
           << "fillcolor=\"#b70d2870\"," << "\n"
           << "color=\"#2B2B2B\"," << "\n"
           << "penwidth=2," << "\n"
           << "style=\"rounded,filled\"," << "\n"
           << "fontname=\"Courier\"," << "\n"
           << "label=\"{" << escName << "|" << escBody << "}\""
           << ",tooltip=\"" << Util::dotRecordEscape(Util::stripParameters(this->name)) << "\""
           << "];\n";

    llvmOS.flush();
}

std::string Node::getDotName() {
    return "Node" + this->getAddress();
}

double Node::getEnergy() {
    double energy = 0.0;
    auto &pHandler = ProfileHandler::get_instance();

    for (const llvm::Instruction &I : *this->block) {
        std::string instname = I.getOpcodeName();

        if (auto icmpinst = llvm::dyn_cast<llvm::ICmpInst>(&I)) {
            instname = std::string("icmp ") + llvm::ICmpInst::getPredicateName(icmpinst->getPredicate()).str();
        }

        auto candiate = pHandler.getEnergyForInstruction(instname);
        if (candiate.has_value()) {
            energy += candiate.value();
        } else {
            // If we do not have an energy value for the instruction, we log this and continue with the next instruction
            /*Logger::getInstance().log(
                    "No energy value found for instruction: " + std::string(instname)
                    + " Using unknown value if exists!",
                    LOGLEVEL::WARNING);*/

            auto unknownCost = pHandler.getUnknownCost();
            if (unknownCost.has_value()) {
                energy += unknownCost.value();
            } else {
                Logger::getInstance().log(
                    "No unknown value specified by the profile! Recreate the profile!",
                    LOGLEVEL::ERROR);
            }
        }
    }

    return energy;
}

std::string Node::calculateHash() {
    return Hasher::getHashForNode(this);
}

}  // namespace HLAC
