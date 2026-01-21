//
// Created by max on 1/21/26.
//

#ifndef SPEAR_UTIL_H
#define SPEAR_UTIL_H

#include "HLAC/hlac.h"

namespace HLAC {

class Util {
public:
    static std::string stripParameters(const std::string& s);

    static std::string dotRecordEscape(llvm::StringRef s);

    static std::string instToString(const llvm::Instruction &I);

    static std::string stripParamsInInstText(std::string s);

    static std::string prettifyOperators(std::string s);

    static std::string escapeDotLabel(std::string s);

    static std::string shortenStdStreamOps(std::string s);

    static std::string dropReturnType(std::string s);

    static std::string dotSafeDemangledName(const std::string& mangled);
};

}

#endif //SPEAR_UTIL_H