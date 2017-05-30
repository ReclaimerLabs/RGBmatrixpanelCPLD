/*

This demo shows off some of the functionality of the 
RGBmatrixPanelCPLD library. Its usage is similar to 
the RGBmatrixPanel library. 

*/

#include "application.h"
#include "RGBmatrixPanelCPLD.h"

RGBmatrixPanelCPLD display(128, 64);

void setup() {
    display.begin();
    
    display.setCursor(0,0);
    display.setTextColor(display.Color444(0x3,0x2,0xF));
    display.write('P');
    display.write('H');
    display.write('O');
    display.write('T');
    display.write('O');
    display.write('N');

    display.drawLine(111,63,127,47,display.Color444(0x7,0x0,0x7));
    display.fillRect(72,18,4,4,display.Color444(0x0,0x0,0x7));
    display.drawRect(16,50,16,6,display.Color444(0x0,0x7,0x0));
}

void loop() {
    static bool isOn = true;
    if (isOn) {
        display.drawPixel(2,30,display.Color444(0x7,0x0,0x0));
    } else {
        display.drawPixel(2,30,display.Color444(0x0,0x0,0x0));
    }
    isOn = !isOn;
    delay(1000);
}
