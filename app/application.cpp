#include <user_config.h>
#include <SmingCore/SmingCore.h>
#include <Adafruit_TCS34725/Adafruit_TCS34725.h>
#include <Adafruit_NeoPixel/Adafruit_NeoPixel.h>

// Which pin on the Esp8266 is connected to the NeoPixels?
#define PIN     15
// How many NeoPixels are attached to the Esp8266?
#define NUMPIXELS 12
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

// Define ColorSensor
Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X);
#define SEUIL 20

Timer runTimer;

// our RGB -> eye-recognized gamma color
byte gammatable[256];

void setPixels(float r, float g, float b);
void setup();
void loop();

void init() {
	setup();
	runTimer.initializeMs(500,loop).start();  
}

void setup() {
	Serial.begin(115200);
	strip.begin();
	setPixels(127,127,127);
	Wire.pins(5,4); // Added for pin compatibility with Wemos D1 documentation
	if (tcs.begin()) Serial.println("Found sensor");
	// thanks PhilB for this gamma table!
	// it helps convert RGB colors to what humans see
	for (int i=0; i<256; i++) {
		float x = i;
		x /= 255;
		x = pow(x, 2.5);
		x *= 255;

		gammatable[i] = x;
		Serial.println(gammatable[i]);
	}

}

void loop() {
	uint16_t clear, red, green, blue;

	tcs.setInterrupt(false);      // turn on LED

	delay(60);  // takes 50ms to read 

	tcs.getRawData(&red, &green, &blue, &clear);

	tcs.setInterrupt(true);  // turn off LED

	// Figure out some basic hex code for visualization
	uint32_t sum = clear;
	static float r, g, b, Pr, Pg, Pb;
	r = red; r /= sum;
	g = green; g /= sum;
	b = blue; b /= sum;
	r *= 256; g *= 256; b *= 256;

	Serial.print("C: "); Serial.print(clear);
	Serial.print("\tR: "); Serial.print(red);
	Serial.print("\tG: "); Serial.print(green);
	Serial.print("\tB: "); Serial.print(blue);
	Serial.print("\t");
	Serial.print((char)r, HEX); Serial.print((char)g, HEX); Serial.print((char)b, HEX);
	Serial.println(); 

	if( (abs(Pr-r)>SEUIL) || (abs(Pg-g)>SEUIL) || (abs(Pb-b)>SEUIL) )
	{
		setPixels(r,g,b);
		Pr=r; Pg=g; Pb=b;
	}
}

void setPixels(float r, float g, float b) {
	int red,green,blue;
	const float coef=2.5;
	red = gammatable[(int)r] * coef;
	green = gammatable[(int)g] * coef;
	blue = gammatable[(int)b] * coef;


	for(int i=0;i<NUMPIXELS;i++){
		strip.setPixelColor(i,strip.Color(red,green,blue));
		strip.show();
		delay(5);
	}
}
