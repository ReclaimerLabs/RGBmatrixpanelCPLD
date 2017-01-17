/*
Copyright (c) 2017 Jason Cerundolo

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

#include "RGBmatrixPanelCPLD.h"
#include "gamma.h"
#include "SparkIntervalTimer/SparkIntervalTimer.h"

IntervalTimer displayTimer;

static RGBmatrixPanelCPLD *activePanel = NULL;

void refreshISR(void);
void rowCompleteCallback(void);
uint8_t *temp;

int8_t RGBmatrixPanelCPLD::init() {
    uint32_t bufSize;
    
    if (double_buffer_enabled) {
        bufSize = 2*depth*plane_size;
    } else {
        bufSize = depth*plane_size;
    }
    
    front_buffer = (uint8_t *)malloc(bufSize);
    
    if (front_buffer == NULL) {
        return -1;
    }

    if (double_buffer_enabled) {
      back_buffer = front_buffer + (bufSize>>1);
    } else {
      back_buffer = front_buffer;
    }
    
    pinMode(clr_pin, OUTPUT);
    pinSetFast(clr_pin);
    pinResetFast(clr_pin);
    pinSetFast(clr_pin);
    
    SPI.begin();
    SPI.setBitOrder(MSBFIRST);
    SPI.setClockSpeed(30000000);
    SPI.setDataMode(SPI_MODE0);

    if (double_buffer_enabled) {
      fillScreen(0);
      uint8_t *temp;
      temp = front_buffer;
      front_buffer = back_buffer;
      back_buffer = temp;
      fillScreen(0);
    } else {
      fillScreen(0);
    }
    
    swap_requested = false;

    pinMode(oe_pin, OUTPUT);
    digitalWrite(oe_pin, LOW);
    
    row = 15;
    plane = depth-1;
    
    return 0;
}

RGBmatrixPanelCPLD::RGBmatrixPanelCPLD(uint16_t x, uint16_t y) : Adafruit_GFX(x, y) {
    clr_pin = A0;
    oe_pin = D2;    
    width = (x>>5)<<5; // ensure width and height are multiples of 32
    height = (y>>5)<<5;
    row_size = width*(height>>5);
    plane_size = width*(height>>1);
    depth = 3;
    double_buffer_enabled = false;
    
    initStatus = init();
}

RGBmatrixPanelCPLD::RGBmatrixPanelCPLD(uint16_t x, uint16_t y, uint16_t d) : Adafruit_GFX(x, y) {
    clr_pin = A0;
    oe_pin = D2;    
    width = (x>>5)<<5; // ensure width and height are multiples of 32
    height = (y>>5)<<5;
    row_size = width*(height>>5);
    plane_size = width*(height>>1);
    depth = d;
    double_buffer_enabled = false;
    
    initStatus = init();
}

RGBmatrixPanelCPLD::RGBmatrixPanelCPLD(uint16_t x, uint16_t y, uint16_t d, bool use_double_buffer) : Adafruit_GFX(x, y) {
    clr_pin = A0;
    oe_pin = D2;    
    width = (x>>5)<<5; // ensure width and height are multiples of 32
    height = (y>>5)<<5;
    row_size = width*(height>>5);
    plane_size = width*(height>>1);
    depth = d;
    double_buffer_enabled = use_double_buffer;
    
    initStatus = init();
}

void RGBmatrixPanelCPLD::begin(void) {
    rowComplete = 0;
    activePanel = this;
    
    row = 15;
    plane = depth-1;
    
    resync();
    
    // Try to get the hardware timers in decreasing priority
    // Refer to Table 20 of the STM32F2 reference manual
    if (displayTimer.begin(refreshISR, 250, uSec, TIMER3) == true) {
        return;
    }
    
    if (displayTimer.begin(refreshISR, 250, uSec, TIMER4) == true) {
        return;
    }
    
    if (displayTimer.begin(refreshISR, 250, uSec, TIMER5) == true) {
        return;
    }
    
    if (displayTimer.begin(refreshISR, 250, uSec, TIMER6) == true) {
        return;
    }
    
    if (displayTimer.begin(refreshISR, 250, uSec, TIMER7) == true) {
        return;
    }
}

/*
 * This sets a flag that will be ready by updateDisplay()
 * It will wait for the start of the next frame to swap, 
 * then set swap_requested to false.
 */
void RGBmatrixPanelCPLD::swapBuffers(void) {
    if (double_buffer_enabled) {
        swap_requested = true;
    } else {
        swap_requested = false;
    }
}

void RGBmatrixPanelCPLD::drawPixel(int16_t x, int16_t y, uint16_t c) {
    /* 
        Color format:
            0bRRRRrGGGGggBBBBb
    */
    
    uint16_t y_int;
    if ((y >= 0) && (x >= 0) && (y < height) && (x < width)) {
        y_int = (y%32);
    } else {
        return;
    }
    /* Assumes 
     *   controller is plugged into bottom row of panels
     *   top row of panels is right-side up
     *   panels are arranged in a square configuration
    */
    
    uint16_t panel_row = y>>5;
    if ((panel_row % 2) == 0) { 
        // pixel is on odd panel
        // right side up
        x = (x%width) + panel_row*width;
    } else { 
        // pixel is on even panel
        // upside down
        y_int = (31 - y_int);
        x = ((width - 1) - (x%width)) + panel_row*width;
    }
    
    if ((y_int) < 16) {
        if (depth > 0) {
            *(back_buffer+(y_int*row_size)+x) = \
                (*(back_buffer+(y_int*row_size)+x) & 0xC7) | \
                (((c & (1<<15))>>15) << 5) | \
                (((c & (1<<10))>>10) << 4) | \
                (((c & (1<<4))>>4) << 3);
        }
        
        if (depth > 1) {
            *(back_buffer+(plane_size)+(y_int*row_size)+x) = \
                (*(back_buffer+(plane_size)+(y_int*row_size)+x) & 0xC7) | \
                (((c & (1<<14))>>14) << 5) | \
                (((c & (1<<9))>>9) << 4) | \
                (((c & (1<<3))>>3) << 3);
        }
        
        if (depth > 2) {
            *(back_buffer+(2*plane_size)+(y_int*row_size)+x) = \
                (*(back_buffer+(2*plane_size)+(y_int*row_size)+x) & 0xC7) | \
                (((c & (1<<13))>>13) << 5) | \
                (((c & (1<<8))>>8) << 4) | \
                (((c & (1<<2))>>2) << 3);
        }
        
        if (depth > 3) {
            *(back_buffer+(3*plane_size)+(y_int*row_size)+x) = \
                (*(back_buffer+(3*plane_size)+(y_int*row_size)+x) & 0xC7) | \
                (((c & (1<<12))>>12) << 5) | \
                (((c & (1<<7))>>7) << 4) | \
                (((c & (1<<1))>>1) << 3);
        }
        
    } else {
        y_int = (y_int-16);
        if (depth > 0) {
            *(back_buffer+(y_int*row_size)+x) = \
                (*(back_buffer+(y_int*row_size)+x) & 0xF8) | \
                (((c & (1<<15))>>15) << 2) | \
                (((c & (1<<10))>>10) << 1) | \
                (((c & (1<<4))>>4) << 0);
        }
        
        if (depth > 1) {
            *(back_buffer+(plane_size)+(y_int*row_size)+x) = \
                (*(back_buffer+(plane_size)+(y_int*row_size)+x) & 0xF8) | \
                (((c & (1<<14))>>14) << 2) | \
                (((c & (1<<9))>>9) << 1) | \
                (((c & (1<<3))>>3) << 0);
        }
        
        if (depth > 2) {
            *(back_buffer+(2*plane_size)+(y_int*row_size)+x) = \
                (*(back_buffer+(2*plane_size)+(y_int*row_size)+x) & 0xF8) | \
                (((c & (1<<13))>>13) << 2) | \
                (((c & (1<<8))>>8) << 1) | \
                (((c & (1<<2))>>2) << 0);
        }
        
        if (depth > 3) {
            *(back_buffer+(3*plane_size)+(y_int*row_size)+x) = \
                (*(back_buffer+(3*plane_size)+(y_int*row_size)+x) & 0xF8) | \
                (((c & (1<<12))>>12) << 2) | \
                (((c & (1<<7))>>7) << 1) | \
                (((c & (1<<1))>>1) << 0);
        }
    }
}

void RGBmatrixPanelCPLD::fillScreen(uint16_t c) {
    /* 
        Color format:
            0bRRRRrGGGGggBBBBb
    */
    uint32_t bufSize = width*(height>>1); 
    for (uint32_t p=0; p<depth; p++) {
        uint8_t color;
        color  = (( (c & (1 << (12+p))) >> (12+p)) << 2); // lower red
        color |= (( (c & (1 << (7+p))) >> (7+p)) << 1);   // lower green
        color |= (  (c & (1 << (1+p))) >> (1+p));         // lower blue
        color |= (color << 3); // duplicate lower and upper colors
        memset((back_buffer+(depth-p-1)*plane_size), color, bufSize);
    }

    // Add row and latch signals to last byte of each plane of each row
    for (uint32_t i=1; i<=(depth*16); i++) {
        *(back_buffer+(row_size*i)-1) |= (1<<6);
    }
    
    // Add row and latch signals to last byte of each plane of each row
    for (uint32_t i=1; i<=(depth*16); i++) {
        *(back_buffer+(row_size*i)-1) |= (1<<7);
    }
}

// All these color functions are copied from Adafruit's
// RGB-matrix-panel library. I use the same code to remain
// consistent (and save a bunch of effort). 

// Promote 3/3/3 RGB to Adafruit_GFX 5/6/5
uint16_t RGBmatrixPanelCPLD::Color333(uint8_t r, uint8_t g, uint8_t b) {
  // RRRrrGGGgggBBBbb
  return ((r & 0x7) << 13) |
         ((g & 0x7) <<  8) |
         ((b & 0x7) <<  2);
}

// Promote 4/4/4 RGB to Adafruit_GFX 5/6/5
uint16_t RGBmatrixPanelCPLD::Color444(uint8_t r, uint8_t g, uint8_t b) {
  // RRRRrGGGGggBBBBb
  return ((r & 0xF) << 12) |
         ((g & 0xF) <<  7) |
         ((b & 0xF) <<  1);
}

// Demote 8/8/8 to Adafruit_GFX 5/6/5
// If no gamma flag passed, assume linear color
uint16_t RGBmatrixPanelCPLD::Color888(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

// 8/8/8 -> gamma -> 5/6/5
uint16_t RGBmatrixPanelCPLD::Color888(
  uint8_t r, uint8_t g, uint8_t b, boolean gflag) {
  if(gflag) { // Gamma-corrected color?
    r = pgm_read_byte(&gamma[r]); // Gamma correction table maps
    g = pgm_read_byte(&gamma[g]); // 8-bit input to 4-bit output
    b = pgm_read_byte(&gamma[b]);
    return ((uint16_t)r << 12) | ((uint16_t)(r & 0x8) << 8) | // 4/4/4->5/6/5
           ((uint16_t)g <<  7) | ((uint16_t)(g & 0xC) << 3) |
           (          b <<  1) | (           b        >> 3);
  } // else linear (uncorrected) color
  return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

uint16_t RGBmatrixPanelCPLD::ColorHSV(
  long hue, uint8_t sat, uint8_t val, boolean gflag) {

  uint8_t  r, g, b, lo;
  uint16_t s1, v1;

  // Hue
  hue %= 1536;             // -1535 to +1535
  if(hue < 0) hue += 1536; //     0 to +1535
  lo = hue & 255;          // Low byte  = primary/secondary color mix
  switch(hue >> 8) {       // High byte = sextant of colorwheel
    case 0 : r = 255     ; g =  lo     ; b =   0     ; break; // R to Y
    case 1 : r = 255 - lo; g = 255     ; b =   0     ; break; // Y to G
    case 2 : r =   0     ; g = 255     ; b =  lo     ; break; // G to C
    case 3 : r =   0     ; g = 255 - lo; b = 255     ; break; // C to B
    case 4 : r =  lo     ; g =   0     ; b = 255     ; break; // B to M
    default: r = 255     ; g =   0     ; b = 255 - lo; break; // M to R
  }

  // Saturation: add 1 so range is 1 to 256, allowig a quick shift operation
  // on the result rather than a costly divide, while the type upgrade to int
  // avoids repeated type conversions in both directions.
  s1 = sat + 1;
  r  = 255 - (((255 - r) * s1) >> 8);
  g  = 255 - (((255 - g) * s1) >> 8);
  b  = 255 - (((255 - b) * s1) >> 8);

  // Value (brightness) & 16-bit color reduction: similar to above, add 1
  // to allow shifts, and upgrade to int makes other conversions implicit.
  v1 = val + 1;
  if(gflag) { // Gamma-corrected color?
    r = pgm_read_byte(&gamma[(r * v1) >> 8]); // Gamma correction table maps
    g = pgm_read_byte(&gamma[(g * v1) >> 8]); // 8-bit input to 4-bit output
    b = pgm_read_byte(&gamma[(b * v1) >> 8]);
  } else { // linear (uncorrected) color
    r = (r * v1) >> 12; // 4-bit results
    g = (g * v1) >> 12;
    b = (b * v1) >> 12;
  }
  return (r << 12) | ((r & 0x8) << 8) | // 4/4/4 -> 5/6/5
         (g <<  7) | ((g & 0xC) << 3) |
         (b <<  1) | ( b        >> 3);
}


void RGBmatrixPanelCPLD::updateDisplay(void) {
    /* 
        At 30 MHz, sending a row takes (1 / 30 MHz) * 8 * numCols
                                        for numCols = 128, time = 7.5 us
        
        Plane 0 is the half period, Plane 1 is the quarter, etc
        At 60 Hz, with 32 display rows, each row is displayed for (1 / 60 Hz) / 16 total
                                                                  ((1 / 60 Hz) / 16) / (2^4) for Plane 3
                                                                    = 69 us = 1 cycle
                                                                    
        Plane 0 displays for 8 cycles
        Plane 1 displays for 4 cycles
        Plane 2 displays for 2 cycles
        Plane 3 displays for 1 cycle
    */

    pinSetFast(oe_pin);
    
    if (row == 15 && plane == (depth-1)) {
        if (resync_flag) {
            pinResetFast(clr_pin);
            pinSetFast(clr_pin);
            resync_flag = false;
        }
        if (swap_requested) {
            temp = front_buffer;
            front_buffer = back_buffer;
            back_buffer = temp;
            swap_requested = false;
        }
    }
    
    displayTimer.resetPeriod_SIT((74 * (1<<(depth-plane-1))), uSec);
    
    if (row == 15) {
        row = 0;
        if (plane == (depth-1)) {
            plane = 0;        
        } else {
            plane++;
        }
    } else {
        row++;
    }
    
    SPI.transfer(front_buffer + (plane*plane_size) + (row*row_size), NULL, row_size, rowCompleteCallback);
    pinResetFast(oe_pin);
}

void RGBmatrixPanelCPLD::resync(void) {
    resync_flag = true;
}

void refreshISR(void) {
    activePanel->updateDisplay();
}

void rowCompleteCallback(void) {
    activePanel->rowComplete = 1;
}
