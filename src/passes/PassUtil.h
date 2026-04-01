
/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_PASSUTIL_H
#define SPEAR_PASSUTIL_H
#include <iomanip>
#include <sstream>
#include <string>

// Convert a double value to scientific notation with configurable precision
inline std::string formatScientific(double value, int precision = 12) {
    std::ostringstream outputStream;
    outputStream << std::scientific << std::setprecision(precision) << value;
    return outputStream.str();
}

#endif //SPEAR_PASSUTIL_H
