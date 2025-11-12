#include "DeMangler.h"

std::string DeMangler::demangle(std::string mangledName) {
    size_t Size = 1;
    char *Buf = static_cast<char *>(std::malloc(Size));

    // Use the llvm itanium demangler to demangle the function name
    llvm::ItaniumPartialDemangler Mangler;
    if (Mangler.partialDemangle(mangledName.c_str())) {
        return mangledName;
    }else{
        return Mangler.getFunctionBaseName(Buf, &Size);
    }
}
