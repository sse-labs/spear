/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_COLOR_H_
#define SRC_SPEAR_COLOR_H_

#include <cstdint>
#include <string>

/**
 * Color class to enable calculations using color
 * 
 */
class Color {
 public:
    /**
     * Define the rgb color channels of the color object
     * Colors are defined as value between 0 and 255
     * 
     */
    unsigned red;
    unsigned blue;
    unsigned green;

    /**
     * Creates a new color object
     * 
     * @param r Amount of red
     * @param g Amount of green
     * @param b Amount of blue
     */
    Color(unsigned r, unsigned g, unsigned b);

    /**
     * Interpolate two colors a and b by a factor amount
     * 
     * @param a Color a
     * @param b Color b
     * @param amount interpolation factor
     * @return Color Interpolated color
     */
    static Color interpolate(Color a, Color b, double amount);

    /**
     * Converts a given color to a hex value
     * 
     * @param c Color to be converted
     * @return unsigned int64_t hex value unsigned int64_t
     */
    static uint64_t toHexVal(Color c);

    /**
     * Converts a given color to a html color string 
     * 
     * @param c Color to be converted
     * @return std::string Html color string
     */
    static std::string toHtmlColor(Color c);
};


#endif  // SRC_SPEAR_COLOR_H_
