/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_HLAC_UTIL_H_
#define SRC_SPEAR_HLAC_UTIL_H_

#include <string>

#include "HLAC/hlac.h"

namespace HLAC {

/**
 * Util class to implement utility functions for HLACS
 */
class Util {
 public:
    /**
     * Takes a function declaration as input and strips all parameters.
     * Returns the function name with a shorthand for the parameters "(...)"
     * @param declaration Declaration to remove parameters from
     * @return Returns declaration with shorthand instead of parameters
     */
    static std::string stripParameters(const std::string& declaration);

    /**
     * Escape the given input StringRef for usage in dot
     * @param input Input to escape
     * @return Escaped string
     */
    static std::string dotRecordEscape(llvm::StringRef input);

    /**
     * Constructs the string representation of the given llvm instruction
     * @param inst Instruction to convert
     * @return String representation of the instruction
     */
    static std::string instToString(const llvm::Instruction &inst);

    /**
     * Converts a given C++ operator declaration to a prettified name
     * @param input Input function declaration
     * @return Prettified input string
     */
    static std::string prettifyOperators(std::string input);

    /**
     * Escapes a given string to enable usage in dot labels
     * @param input Input string
     * @return Escaped string
     */
    static std::string escapeDotLabel(std::string input);

    /**
     * Replace long function declarations with shorthand
     * @param input Input declaration
     * @return Shorthand representing the declaration
     */
    static std::string shortenStdStreamOps(std::string input);

    /**
     * Remove any return type specifier from the given input string
     * @param input Input string
     * @return Input without return type
     */
    static std::string dropReturnType(std::string input);

    /**
     * Demangle the given mangled input and return it as escaped and prettified dot label
     * @param mangled Mangled function name
     * @return Demangled and prettified name for usage in labels
     */
    static std::string dotSafeDemangledName(const std::string& mangled);

    /**
     * Convert an input feasibility value to a string representation
     * @param feas Feasibility to convert
     * @return String representing the feasibility value
     */
    static std::string feasibilityToString(FEASIBILITY feas);

    static bool starts_with(const std::string& s, const std::string& prefix);
};

}  // namespace HLAC

#endif  // SRC_SPEAR_HLAC_UTIL_H_
