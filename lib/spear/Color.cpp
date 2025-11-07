#include <iomanip>
#include <cmath>
#include "Color.h"

Color::Color(unsigned r, unsigned g, unsigned b){
    this->red = r;
    this->green = g;
    this->blue = b;
}

Color Color::interpolate(Color a, Color b, double amount){
    Color toCalculate = Color(0, 0, 0);

    double ared = (double) a.red;
    double agreen = (double) a.green;
    double ablue = (double) a.blue;

    double bred = (double) b.red;
    double bgreen = (double) b.green;
    double bblue = (double) b.blue;

    double inpRed = ared + (bred - ared) * amount;
    double inpGreen = agreen + (bgreen - agreen) * amount;
    double inpBlue = ablue + (bblue - ablue) * amount;

    toCalculate.red = std::ceil(inpRed);
    toCalculate.green = std::ceil(inpGreen);
    toCalculate.blue = std::ceil(inpBlue);

    return toCalculate;
}

unsigned long Color::toHexVal(Color c){
    return ((c.red & 0xff) << 16) + ((c.green & 0xff) << 8) + (c.blue & 0xff);
}

std::string Color::toHtmlColor(Color c){
    char hexColor[8];
    std::snprintf(hexColor, sizeof hexColor, "#%02x%02x%02x", c.red, c.green, c.blue);

    return hexColor;
}