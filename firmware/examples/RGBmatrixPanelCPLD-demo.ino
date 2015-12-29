/*

This demo shows off some of the functionality of the 
RGBmatrixPanelCPLD library. Its usage is similar to 
the RGBmatrixPanel library. 

Be sure to include the following libraries 
* RGBmatrixPanelCPLD
* SparkIntervalTimer
* Adafruit_mfGFX

*/

RGBmatrixPanelCPLD display(32, 32);

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

    display.drawLine(0,16,16,16,display.Color444(0xF,0x0,0x0));
    display.fillRect(4,18,4,4,display.Color444(0x0,0x0,0xF));
    display.drawRect(16,20,16,6,display.Color444(0x0,0xF,0x0));
}

void loop() {
    static bool isOn = true;
    if (isOn) {
        display.drawPixel(2,30,display.Color444(0xF,0xF,0xF));
    } else {
        display.drawPixel(2,30,display.Color444(0x0,0x0,0x0));
    }
    isOn = !isOn;
}
