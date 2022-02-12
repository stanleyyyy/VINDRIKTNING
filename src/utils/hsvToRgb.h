#pragma once

namespace utils {

uint32_t Color(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0);

//
// convert Hue (0-360), Saturation (0-100) and Value (0-100) into RGB value (0-255 for each component)
//

uint32_t HSVtoRGB(float H, float S, float V);

}
