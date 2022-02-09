#pragma once
class Display {
protected:
	Display(){}
public:
	static Display &instance();

	enum Colors {
		eColorRed,
		eColorBlue,
		eColorGreen
	};

	virtual bool init() = 0;
	virtual void setColor(uint32_t rgb, unsigned int id) = 0;
	virtual void setColors(uint32_t led1, uint32_t led2, uint32_t led3, int brightness = -1) = 0;
	virtual void fadeColors(uint32_t pmColor, uint32_t humColor, uint32_t co2Color, const int &steps) = 0;
	virtual void setLedBrightness(const uint8_t &brightness) = 0;
	virtual void alert(int id) = 0;
	virtual uint32_t rgbColor(const Colors &color) = 0;
};
