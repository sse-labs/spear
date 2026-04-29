/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include <llvm/Demangle/Demangle.h>
#include <llvm/Analysis/LazyCallGraph.h>

#include <regex>
#include <utility>
#include <string>
#include <unordered_set>
#include <vector>
#include <unordered_map>
#include <map>

#include "HLAC/util.h"


namespace HLAC {

std::string Util::stripParameters(const std::string& s) {
    // Remove everything from the first '(' to the end
    auto pos = s.find('(');
    if (pos == std::string::npos) {
        return s;
    }
    return s.substr(0, pos) + "(...)";
}

std::string Util::dotRecordEscape(llvm::StringRef s) {
    std::string out;
    out.reserve(s.size() * 2);

    for (char c : s) {
        switch (c) {
            case '\\': {
                out += "\\\\";
                break;
            }
            case '"': {
                out += "\\\"";
                break;
            }
            case '{':
            case '}':
            case '|':
            case '<':
            case '>': {
                out += '\\';
                out += c;
                break;
            }
            case '\n': {
                out += "\\l";
                break;
            }
            case '\r': {
                break;
            }
            default: {
                out += c;
                break;
            }
        }
    }
    return out;
}

std::string Util::dropReturnType(std::string s) {
    // Try to find the start of the function name by locating " operator" or "::"
    size_t op = s.find(" operator");
    size_t ns = s.find("::");

    size_t start = std::string::npos;
    if (op != std::string::npos) {
        start = op + 1;
    } else if (ns != std::string::npos) {
        size_t sp = s.rfind(' ', ns);
        if (sp != std::string::npos) {
            start = sp + 1;
        }
    }

    if (start != std::string::npos) {
        return s.substr(start);
    }
    return s;
}

std::string Util::escapeDotLabel(std::string s) {
    std::string out;
    out.reserve(s.size() + 8);

    for (char c : s) {
        switch (c) {
            case '\\': {
                out += "\\\\";
                break;
            }
            case '"': {
                out += "\\\"";
                break;
            }
            case '\n': {
                out += "\\n";
                break;
            }
            case '\r': {
                break;
            }
            case '\t': {
                out += "\\t";
                break;
            }
            default: {
                out += c;
                break;
            }
        }
    }
    return out;
}

std::string Util::dotSafeDemangledName(const std::string& mangled) {
    std::string s = llvm::demangle(mangled);
    s = prettifyOperators(std::move(s));
    s = escapeDotLabel(std::move(s));
    return s;
}

std::string Util::prettifyOperators(std::string s) {
    auto replace_all = [&](const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    };

    replace_all("operator<<", "operator pipein");
    replace_all("operator>>", "operator pipeout");
    replace_all("operator<",  "operator less");
    replace_all("operator>",  "operator greater");
    replace_all("operator==", "operator ==");
    replace_all("operator!=", "operator !=");
    replace_all("operator<=", "operator leq");
    replace_all("operator>=", "operator geq");
    replace_all("operator()", "operator ()");
    replace_all("operator[]", "operator []");
    replace_all("operator+",  "operator +");
    replace_all("operator-",  "operator -");
    replace_all("operator*",  "operator *");
    replace_all("operator/",  "operator /");
    replace_all("operator%",  "operator %");
    replace_all("operator=",  "operator =");

    return s;
}

std::string Util::shortenStdStreamOps(std::string s) {
    auto replace_all = [&](const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    };

    replace_all("operator<<", "operator <<");
    replace_all("operator>>", "operator >>");
    replace_all("operator|",  "operator |");

    // Collapse the extremely common ostream signatures
    // Return type + namespace prefixes vary, so we match loosely.
    static const std::regex ostreamNoise(
        R"(std::basic_ostream<char,\s*std::char_traits<char>\s*>\s*&\s*)");
    s = std::regex_replace(s, ostreamNoise, "ostream& ");

    static const std::regex istreamNoise(
        R"(std::basic_istream<char,\s*std::char_traits<char>\s*>\s*&\s*)");
    s = std::regex_replace(s, istreamNoise, "istream& ");

    // Also shorten parameter occurrences of those types
    s = std::regex_replace(s, ostreamNoise, "ostream& ");
    s = std::regex_replace(s, istreamNoise, "istream& ");

    s = std::regex_replace(
        s,
        std::regex(R"(std::char_traits<char>)"),
        "char_traits");

    return s;
}

std::string Util::instToString(const llvm::Instruction &I) {
    std::string s;
    llvm::raw_string_ostream rs(s);
    rs << I;
    rs.flush();
    return s;
}

std::string Util::feasibilityToString(bool feas) {
    if (feas) {
        return "⊤";
    }

    return "⊥";
}

bool Util::starts_with(const std::string& s, const std::string& prefix) {
    return s.rfind(prefix, 0) == 0;
}


std::vector<llvm::Function*> Util::getLazyCallGraphPostOrder(
    llvm::Module &M,
    llvm::FunctionAnalysisManager &FAM) {

    auto GetTLI = [&FAM](llvm::Function &F) -> llvm::TargetLibraryInfo & {
        return FAM.getResult<llvm::TargetLibraryAnalysis>(F);
    };

    llvm::LazyCallGraph LCG(M, GetTLI);
    LCG.buildRefSCCs();

    std::vector<llvm::Function*> result;
    std::unordered_set<llvm::Function*> seen;

    for (llvm::LazyCallGraph::RefSCC &RC : LCG.postorder_ref_sccs()) {
        for (llvm::LazyCallGraph::SCC &C : RC) {
            for (llvm::LazyCallGraph::Node &N : C) {
                llvm::Function &F = N.getFunction();
                if (F.isDeclaration()) {
                    continue;
                }
                if (seen.insert(&F).second) {
                    result.push_back(&F);
                }
            }
        }
    }

    return result;
}

std::map<HLAC::GenericNode *, std::vector<HLAC::Edge *>>
Util::createAdjacentList(const std::vector<std::unique_ptr<GenericNode>> &nodes,
                         const std::vector<std::unique_ptr<Edge>> &edges) {
    std::vector<HLAC::GenericNode*> nodePtrs;
    for (const auto &nodeUP : nodes) {
        nodePtrs.push_back(nodeUP.get());
    }

    return createAdjacentList(nodePtrs, edges);
}

std::map<HLAC::GenericNode*, std::vector<HLAC::Edge*>> Util::createAdjacentList(
    const std::vector<GenericNode *> &nodes,
    const std::vector<std::unique_ptr<HLAC::Edge>> &edges) {
    std::map<HLAC::GenericNode*, std::vector<HLAC::Edge*>> adjacentList;

    // Initialize all nodes
    for (const auto &nodeUP : nodes) {
        adjacentList[nodeUP] = {};
    }

    // Fill adjacency
    for (const auto &edgeUP : edges) {
        const auto *edge = edgeUP.get();
        if (!edge) {
            continue;
        }

        // An edge is adjacent if it starts in the node
        adjacentList[edge->soure].push_back(edgeUP.get());
    }

    return adjacentList;
}

std::map<HLAC::GenericNode*, std::vector<HLAC::Edge*>> Util::createIncomingList(
    const std::vector<std::unique_ptr<HLAC::GenericNode>> &nodes,
    const std::vector<std::unique_ptr<HLAC::Edge>> &edges) {
    std::map<HLAC::GenericNode*, std::vector<HLAC::Edge*>> adjacentList;

    // Initialize all nodes
    for (const auto &nodeUP : nodes) {
        adjacentList[nodeUP.get()] = {};
    }

    // Fill adjacency
    for (const auto &edgeUP : edges) {
        const auto *edge = edgeUP.get();
        if (!edge) {
            continue;
        }

        // An edge is incoming, if it ends in the viewed node
        adjacentList[edge->destination].push_back(edgeUP.get());
    }

    return adjacentList;
}

std::vector<HLAC::Edge *> HLAC::Util::findTakenEdges(
    GenericNode *entryNode,
    const std::unordered_map<HLAC::GenericNode *, HLAC::GenericNode *> &predecessors,
    std::vector<std::unique_ptr<HLAC::Edge>> &edges,
    const std::unordered_map<HLAC::LoopNode *, ILPResult> &loopResults) {

    std::vector<HLAC::Edge *> result;
    std::unordered_set<HLAC::Edge *> alreadyAddedEdges;

    // Start at the given entry node (in most cases the exit node of the underlying function)
    HLAC::GenericNode *currentNode = entryNode;

    // Walk backwards through the predecessor chain until we reach the root node
    while (currentNode != nullptr) {
        auto predecessorIterator = predecessors.find(currentNode);
        if (predecessorIterator == predecessors.end()) {
            break;
        }

        HLAC::GenericNode *parentNode = predecessorIterator->second;
        if (parentNode == nullptr) {
            break;
        }

        HLAC::Edge *takenEdge = nullptr;

        // Find the edge from parentNode to currentNode
        for (auto &edgeUP : edges) {
            HLAC::Edge *edge = edgeUP.get();
            if (!edge) {
                continue;
            }

            if (edge->soure == parentNode && edge->destination == currentNode) {
                takenEdge = edge;
                break;
            }
        }

        if (takenEdge != nullptr && alreadyAddedEdges.insert(takenEdge).second) {
            result.push_back(takenEdge);
        }

        // If the current node is a loop node, also append the inner edges selected by the loop ILP
        if (auto *loopNode = dynamic_cast<HLAC::LoopNode *>(currentNode)) {
            auto loopResultIterator = loopResults.find(loopNode);
            if (loopResultIterator != loopResults.end()) {
                std::vector<HLAC::Edge *> allContainedEdges;
                HLAC::Util::collectAllContainedEdges(loopNode, allContainedEdges);

                for (int variableIndex = 0;
                     variableIndex < static_cast<int>(loopResultIterator->second.variableValues.size());
                     ++variableIndex) {
                    if (loopResultIterator->second.variableValues[variableIndex] > 0.0) {
                        HLAC::Edge *innerTakenEdge = HLAC::Util::findEdgeByGlobalId(allContainedEdges, variableIndex);
                        if (innerTakenEdge != nullptr && alreadyAddedEdges.insert(innerTakenEdge).second) {
                            result.push_back(innerTakenEdge);
                        }
                    }
                }
            }
        }

        currentNode = parentNode;
    }

    return result;
}

Edge * Util::findEdgeByGlobalId(std::vector<Edge *> &edgeList, int globalId) {
    // Search given edge list for the given ILPIndex
    auto foundEdge = std::find_if(edgeList.begin(), edgeList.end(),
        [globalId](const Edge* edgeUP) {
            return edgeUP && edgeUP->ilpIndex == globalId;
        });

    if (foundEdge == edgeList.end()) {
        return nullptr;
    }

    return *foundEdge;
}

void Util::collectAllContainedEdges(HLAC::LoopNode *loop, std::vector<HLAC::Edge*> &allEdges) {
    // Iterate over the edges in the loop
    for (auto &edgeUP : loop->Edges) {
        // Append edge of loop to global given collection
        allEdges.push_back(edgeUP.get());
    }

    // Repeat for sub loopnodes
    for (auto &nodeUP : loop->Nodes) {
        if (auto *subLoop = dynamic_cast<HLAC::LoopNode*>(nodeUP.get())) {
            collectAllContainedEdges(subLoop, allEdges);
        }
    }
}

void Util::appendLoopContainedEdges(
    std::unordered_map<HLAC::LoopNode *, ILPResult> loopResults,
    const DAGLongestPathSolution resultpair,
    std::vector<Edge *> &resVector) {

    // Iterate over loop->ILPResult mapping
    for (auto &LN : loopResults) {
        bool loopNodeIsTaken = false;

        // Get vector of taken edges as calculated by the ILPsolver
        for (auto &dagEdges : resultpair.longestPath) {
            // Check if the viewed loopnode is being referenced as destination by the longest path
            if (dagEdges->destination->getDotName() == LN.first->getDotName()) {
                loopNodeIsTaken = true;
                break;
            }
        }

        // If the loopnode is found to be taken
        if (loopNodeIsTaken) {
            // Inser the edges inside the loop to our global edge collection
            std::vector<HLAC::Edge*> allLoopEdges;
            collectAllContainedEdges(LN.first, allLoopEdges);

            for (int i = 0; i < LN.second.variableValues.size(); i++) {
                if (LN.second.variableValues[i] > 0.0) {
                    auto foundEdge = HLAC::Util::findEdgeByGlobalId(allLoopEdges, i);
                    if (foundEdge != nullptr) {
                        resVector.push_back(foundEdge);
                    }
                }
            }
        }
    }
}

int Util::getMaxEdgeIndexInLoop(HLAC::LoopNode *loopNode) {
    int maxIndex = 0;

    // Check edges in this loop
    for (const auto &edgeUP : loopNode->Edges) {
        if (edgeUP && edgeUP->ilpIndex > maxIndex) {
            maxIndex = edgeUP->ilpIndex;
        }
    }

    // Recurse into nested loops
    for (const auto &nodeUP : loopNode->Nodes) {
        if (auto *innerLoop = dynamic_cast<HLAC::LoopNode *>(nodeUP.get())) {
            int innerMax = getMaxEdgeIndexInLoop(innerLoop);
            if (innerMax > maxIndex) {
                maxIndex = innerMax;
            }
        }
    }

    return maxIndex;
}

int Util::getMaxEdgeIndexInFunction(HLAC::FunctionNode *FN) {
    int maxIndex = 0;

    // Top-level edges
    for (const auto &edgeUP : FN->Edges) {
        if (edgeUP && edgeUP->ilpIndex > maxIndex) {
            maxIndex = edgeUP->ilpIndex;
        }
    }

    // Traverse loop nodes
    for (const auto &nodeUP : FN->Nodes) {
        if (auto *loopNode = dynamic_cast<HLAC::LoopNode *>(nodeUP.get())) {
            int loopMax = getMaxEdgeIndexInLoop(loopNode);
            if (loopMax > maxIndex) {
                maxIndex = loopMax;
            }
        }
    }

    return maxIndex;
}

void Util::markTakenEdgesInLoop(
    LoopNode *loopNode,
    const std::unordered_set<Edge *> &takenSet,
    std::vector<double> &result) {

    if (!loopNode) {
        return;
    }

    // Iterate over edges in the loop
    for (const auto &edgeUP : loopNode->Edges) {
        Edge *edge = edgeUP.get();
        if (!edge) {
            continue;
        }

        if (edge->ilpIndex < 0 || edge->ilpIndex >= static_cast<int>(result.size())) {
            continue;
        }

        // Mark the taken edge in the result vector as 1, so we interpret it as being taken
        if (takenSet.find(edge) != takenSet.end()) {
            result[edge->ilpIndex] = 1.0;
        }
    }

    // Repeat the process for sub loopNodes
    for (const auto &nodeUP : loopNode->Nodes) {
        if (auto *innerLoop = dynamic_cast<LoopNode *>(nodeUP.get())) {
            markTakenEdgesInLoop(innerLoop, takenSet, result);
        }
    }
}

void HLAC::Util::collectLoopNodeEdgeSummaries(
    const std::string &functionName,
    const std::vector<std::unique_ptr<HLAC::GenericNode>> &nodes,
    const std::vector<std::unique_ptr<HLAC::Edge>> &edges,
    std::vector<HLAC::LoopNodeEdgeSummary> &loopNodeEdgeSummaries) {
    for (const auto &nodeUniquePointer : nodes) {
        auto *loopNode = dynamic_cast<HLAC::LoopNode *>(nodeUniquePointer.get());

        if (!loopNode) {
            continue;
        }

        std::size_t incomingEdgeCount = 0;
        std::size_t outgoingEdgeCount = 0;

        for (const auto &edgeUniquePointer : edges) {
            HLAC::Edge *edge = edgeUniquePointer.get();

            if (!edge || !edge->soure || !edge->destination) {
                continue;
            }

            if (edge->destination == loopNode) {
                incomingEdgeCount++;
            }

            if (edge->soure == loopNode) {
                outgoingEdgeCount++;
            }
        }

        if (incomingEdgeCount > 1 || outgoingEdgeCount > 1) {
            loopNodeEdgeSummaries.push_back({
                functionName,
                loopNode->getDotName(),
                incomingEdgeCount,
                outgoingEdgeCount
            });
        }

        HLAC::Util::collectLoopNodeEdgeSummaries(
            functionName,
            loopNode->Nodes,
            loopNode->Edges,
            loopNodeEdgeSummaries);
    }
}

}  // namespace HLAC
