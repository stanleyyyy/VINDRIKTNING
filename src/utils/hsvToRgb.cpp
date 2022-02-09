#include <Adafruit_NeoPixel.h>
#include "../config/config.h"
#include "../utils/utils.h"
#include "hsvToRgb.h"

namespace utils {

//
// convert Hue (0-360), Saturation (0-100) and Value (0-100) into RGB value (0-255 for each component)
//

uint32_t HSVtoRGB(float H, float S, float V)
{
	if (H > 360 || H < 0 || S > 100 || S < 0 || V > 100 || V < 0) {
		LOG_PRINTF("Givem HSV values are not in valid range\n");
		return 0;
	}

	float s = S / 100;
	float v = V / 100;
	float C = s * v;
	float X = C * (1 - abs(fmod(H / 60.0, 2) - 1));
	float m = v - C;
	float r, g, b;

	if (H >= 0 && H < 60) {
		r = C, g = X, b = 0;
	} else if (H >= 60 && H < 120) {
		r = X, g = C, b = 0;
	} else if (H >= 120 && H < 180) {
		r = 0, g = C, b = X;
	} else if (H >= 180 && H < 240) {
		r = 0, g = X, b = C;
	} else if (H >= 240 && H < 300) {
		r = X, g = 0, b = C;
	} else {
		r = C, g = 0, b = X;
	}

	int R = (r + m) * 255;
	int G = (g + m) * 255;
	int B = (b + m) * 255;

	return Adafruit_NeoPixel::Color(R, G, B);
}

}
