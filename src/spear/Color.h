#ifndef SPEAR_COLOR_H
#define SPEAR_COLOR_H


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
     * @return unsigned long hex value unsigned long 
     */
    static unsigned long toHexVal(Color c);

    /**
     * Converts a given color to a html color string 
     * 
     * @param c Color to be converted
     * @return std::string Html color string
     */
    static std::string toHtmlColor(Color c);
};


#endif //SPEAR_COLOR_H
