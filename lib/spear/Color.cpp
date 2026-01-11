/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#include "Color.h"
#include <iomanip>
#include <cmath>
#include <cstdio>
#include <string>

Color::Color(unsigned r, unsigned g, unsigned b) {
    this->red = r;
    this->green = g;
    this->blue = b;
}

Color Color::interpolate(Color a, Color b, double amount) {
    Color toCalculate = Color(0, 0, 0);

    double ared = a.red;
    double agreen = a.green;
    double ablue = a.blue;

    double bred = b.red;
    double bgreen = b.green;
    double bblue = b.blue;

    double inpRed = ared + (bred - ared) * amount;
    double inpGreen = agreen + (bgreen - agreen) * amount;
    double inpBlue = ablue + (bblue - ablue) * amount;

    toCalculate.red = std::ceil(inpRed);
    toCalculate.green = std::ceil(inpGreen);
    toCalculate.blue = std::ceil(inpBlue);

    return toCalculate;
}

uint64_t Color::toHexVal(Color c) {
    return ((c.red & 0xff) << 16) + ((c.green & 0xff) << 8) + (c.blue & 0xff);
}

std::string Color::toHtmlColor(Color c) {
    char hexColor[8];
    std::snprintf(hexColor, sizeof hexColor, "#%02x%02x%02x", c.red, c.green, c.blue);

    return hexColor;
}
