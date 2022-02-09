#pragma once

namespace utils {

//
// convert Hue (0-360), Saturation (0-100) and Value (0-100) into RGB value (0-255 for each component)
//

uint32_t HSVtoRGB(float H, float S, float V);

}
