/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "DeMangler.h"

#include <cstring>
#include <string>

std::string DeMangler::demangle(std::string mangledName) {
    llvm::ItaniumPartialDemangler Mangler;

    if (Mangler.partialDemangle(mangledName.c_str())) {
        return mangledName;
    }

    size_t size = 0;
    // First call: query required size (buffer can be null)
    Mangler.getFunctionBaseName(nullptr, &size);

    if (size == 0) {
        return mangledName;
    }

    std::string buf(size, '\0');
    Mangler.getFunctionBaseName(buf.data(), &size);

    buf.resize(std::strlen(buf.c_str()));
    return buf;
}

