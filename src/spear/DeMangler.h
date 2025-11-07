#ifndef SPEAR_DEMANGLER_H
#define SPEAR_DEMANGLER_H

#include <string>
#include <llvm/Demangle/Demangle.h>

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


#endif //SPEAR_DEMANGLER_H
