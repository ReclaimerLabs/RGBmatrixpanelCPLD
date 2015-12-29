/*
Copyright (c) 2015 Jason Cerundolo

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

RGBmatrixPanelCPLD Particle library for 32x32 RGB LED
matrix panels with Reclaimer Labs CPLD backpack. 

Pick up an RGB LED panel from Adafruit. 
https://www.adafruit.com/products/2026

Pick up a CPLD backpack from Reclaimer Labs. 
http://reclaimerlabs.com

Inspired by and compatible with Adafruit’s RGB matrix panel library. 
https://github.com/adafruit/RGB-matrix-Panel

This library uses hardware timers and DMA to achieve low overhead
transfers of information to the matrix panels. The CPLD on the 
backpack converts SPI data into the parallel format expected by 
the matrix panel. The only ongoing computation needed from the CPU 
is to set up the next DMA transfer and compute the time to display 
the next row of bit data. This take the CPU time needed from ~40% 
to <1% for two 32x32 panels. The limited factor is now RAM and 
transfer speed. 
*/

#ifndef _RGBMATRIXPANELCPLD_H
#define _RGBMATRIXPANELCPLD_H

#include "Adafruit_mfGFX/Adafruit_mfGFX.h"
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#define PROGMEM

class RGBmatrixPanelCPLD : public Adafruit_GFX {
    
    public: 
    
        /* 
            Constructor for the matrix panel
            
            Assumptions: 32 x 32 panels, 4 color planes (ie 12 bit color per pixel)
            
        */
        RGBmatrixPanelCPLD(uint16_t x, uint16_t y);
        
        void begin(void);
        void drawPixel(int16_t x, int16_t y, uint16_t c);
        void fillScreen(uint16_t c);
        void updateDisplay(void);
        void resync(void);
        
        uint16_t Color333(uint8_t r, uint8_t g, uint8_t b);
        uint16_t Color444(uint8_t r, uint8_t g, uint8_t b);
        uint16_t Color888(uint8_t r, uint8_t g, uint8_t b);
        uint16_t Color888(uint8_t r, uint8_t g, uint8_t b, boolean gflag);
        uint16_t ColorHSV(long hue, uint8_t sat, uint8_t val, boolean gflag);
        
        volatile uint8_t rowComplete;
        int8_t initStatus;
    
    private:
        int8_t init(uint16_t x, uint16_t y, uint16_t p);
        volatile uint16_t row, plane;
        uint16_t height, width, depth;
        uint16_t transfer_row_width;
        volatile uint8_t *buffPtr;
        uint8_t *matrixbuff[2];
        int clr_pin, oe_pin;
};

#endif //_RGBMATRIXPANELCPLD_H