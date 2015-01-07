// driver for the GSL1680 touch panel
// Information gleaned from https://github.com/rastersoft/gsl1680.git and various other sources
// firmware for the specific panel was found here:- http://www.buydisplay.com/default/5-inch-tft-lcd-module-800x480-display-w-controller-i2c-serial-spi
// As was some test code.
// This is for that 800X480 display and the 480x272 from buydisplay.com

/*
Pin outs
the FPC on the touch panel is six pins, pin 1 is to the left pin 6 to the right with the display facing up

pin | function  | Arduino Uno
-----------------------------
1   | SCL       | A5
2   | SDA       | A4
3   | VDD (3v3) | 3v3
4   | Wake      | 4 (seems to be inverted SLEEP)
5   | Int       | 2
6   | Gnd       | gnd
*/
#include <Wire.h>
#include "Arduino.h"

// TODO define for other resolution
#include "gslfw.h" // this is compacted format made by compress_data.c

// Pins
#define WAKE 4
#define INTRPT 2

#define SCREEN_MAX_X 		800
#define SCREEN_MAX_Y 		480

#define GSLX680_I2C_ADDR 	0x40

#define GSL_DATA_REG		0x80
#define GSL_STATUS_REG		0xe0
#define GSL_PAGE_REG		0xf0

#define delayus delayMicroseconds

struct _coord { uint32_t x, y; uint8_t finger; };

struct _ts_event
{
	uint8_t  n_fingers;
	struct _coord coords[5];
};

struct _ts_event ts_event;

static inline void wiresend(uint8_t x) {
#if ARDUINO >= 100
  Wire.write((uint8_t)x);
#else
  Wire.send(x);
#endif
}

static inline uint8_t wirerecv(void) {
#if ARDUINO >= 100
  return Wire.read();
#else
  return Wire.receive();
#endif
}

bool i2c_write(uint8_t reg, uint8_t *buf, int cnt)
{
	Wire.beginTransmission(GSLX680_I2C_ADDR);
    wiresend(reg);
    for(int i=0; i<cnt; i++){
        wiresend(buf[i]);
    }
    return Wire.endTransmission();
}

int i2c_read(uint8_t reg, uint8_t *buf, int cnt)
{
	Wire.beginTransmission(GSLX680_I2C_ADDR);
  	wiresend(reg);
  	Wire.endTransmission();

  	int n= Wire.requestFrom(GSLX680_I2C_ADDR, cnt);

  	for(int i=0; i<n; i++){
  	    buf[i]= wirerecv();
  	}
  	return n;
}

// Note the CTP.h does this but rastersoft driver does not
void clr_reg(void)
{
	uint8_t buf[4];

	buf[0] = 0x88;
	i2c_write(0xe0, buf, 1);
	delayus(200);

	buf[0] = 0x01;
	i2c_write(0x80, buf, 1);
	delayus(50);

	buf[0] = 0x04;
	i2c_write(0xe4, buf, 1);
	delayus(50);

	buf[0] = 0x00;
	i2c_write(0xe0, buf, 1);
	delayus(50);
}

void reset_chip()
{
	uint8_t buf[4];

	buf[0] = 0x88;
    i2c_write(GSL_STATUS_REG, buf, 1);
	delay(10);

	buf[0] = 0x04;
    i2c_write(0xe4,buf, 1);
	delay(10);

	buf[0] = 0x00;
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = 0x00;
    i2c_write(0xbc,buf, 4);
	delay(10);
}

// the data is in blocks of 128 bytes, each one preceded by the page number
// we first send the page number then we send the data in blocks of 32 until the entire page is sent
// NOTE that the firmware data is stored in flash as it is huge! around 28kBytes
void load_fw(void)
{
	uint8_t buf[32];
	size_t source_len = sizeof(gslx680_fw);
	int blockstart= 1;
	int reg= 0;
	int off= 0;
	size_t source_line;
	for (source_line=0; source_line < source_len; source_line++) {
		if(off == 32){
			i2c_write(reg, buf, 32); // write accumulated block
			reg += 32;
			off= 0;
			if(reg >= 128) blockstart= 1;
		}

		if(blockstart) {
			blockstart= 0;
			buf[0] = pgm_read_byte_near(gslx680_fw + source_line); // gslx680_fw[source_line];
			buf[1] = 0;
			buf[2] = 0;
			buf[3] = 0;
			i2c_write(GSL_PAGE_REG, buf, 4);
			reg= 0;

		}else{
			buf[off++] = pgm_read_byte_near(gslx680_fw + source_line); // gslx680_fw[source_line];
		}
	}
	if(off == 32){ // write last accumulated block
		i2c_write(reg, buf, 32);
	}
}

void startup_chip(void)
{
	uint8_t buf[4];

	buf[0] = 0x00;
    i2c_write(0xe0, buf, 1);
}

void init_chip()
{
#if 1
	digitalWrite(WAKE, 1);
	delay(20);
	digitalWrite(WAKE, 0);
	delay(20);
	digitalWrite(WAKE, 1);
	delay(20);

	// CTP startup sequence
	clr_reg();
	reset_chip();
	load_fw();
//	startup_chip();
	reset_chip();
	startup_chip();

#else
	// rastersoft int sequence
	reset_chip();
	load_fw();
	startup_chip();
	reset_chip();

	digitalWrite(WAKE, 0);
	delay(50);
	digitalWrite(WAKE, 1);
	delay(30);
	digitalWrite(WAKE, 0);
	delay(5);
	digitalWrite(WAKE, 1);
	delay(20);

	reset_chip();
	startup_chip();
#endif
}

int read_data(void)
{

	uint8_t touch_data[24] = {0};
	i2c_read(GSL_DATA_REG, touch_data, 24);

	ts_event.n_fingers= touch_data[0];
	for(int i=0; i<ts_event.n_fingers; i++){
		ts_event.coords[i].x = ( (((uint32_t)touch_data[(i*4)+5])<<8) | (uint32_t)touch_data[(i*4)+4] ) & 0x00000FFF; // 12 bits of X coord
		ts_event.coords[i].y = ( (((uint32_t)touch_data[(i*4)+7])<<8) | (uint32_t)touch_data[(i*4)+6] ) & 0x00000FFF;
		ts_event.coords[i].finger = (uint32_t)touch_data[(i*4)+7] >> 4; // finger that did the touch
	}

	return ts_event.n_fingers;
}

void setup() {
	Serial.begin(9600);
	Wire.begin();
	delay(5);
	init_chip();
}

void loop() {
	if(digitalRead(INTRPT) == 1) {
		int n= read_data();
		for(int i=0; i<n; i++){
			Serial.print(ts_event.coords[i].finger); Serial.print(" "), Serial.print(ts_event.coords[i].x); Serial.print(" "), Serial.print(ts_event.coords[i].y);
		 	Serial.println("");
		}
		Serial.println("---");
	}
}
