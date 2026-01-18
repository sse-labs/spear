/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include <llvm/IR/BasicBlock.h>
#include <memory>
#include "HLAC/hlac.h"

std::unique_ptr<HLAC::Node> HLAC::Node::makeNode(llvm::BasicBlock *basic_block) {
    auto node = std::make_unique<HLAC::Node>();
    node->block = basic_block;
    node->name = basic_block->getName();


    return node;
}
