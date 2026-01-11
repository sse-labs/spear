/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_DEMANGLER_H_
#define SRC_SPEAR_DEMANGLER_H_

#include <llvm/Demangle/Demangle.h>
#include <string>

/**
 * Demangler class used to deal with LLVM name mangling
 * 
 */
class DeMangler {
 public:
    /**
     * Demangle a given mangled name
     * 
     * @param mangledName Mangled name to be demangled
     * @return std::string Demangled name if possible
     */
    static std::string demangle(std::string mangledName);
};


#endif  // SRC_SPEAR_DEMANGLER_H_
