/*

This demo displays time to the next rocket launch.
Powered by launchlibrary.net. 

Be sure to include the following libraries 
* RGBmatrixPanelCPLD
* SparkIntervalTimer
* Adafruit_mfGFX
* HttpClient
* SparkJson

*/

RGBmatrixPanelCPLD display(32, 32);

StaticJsonBuffer<2000> jsonBuffer;

long netstamp;

TCPClient launch_lib;
//IntervalTimer scrollingTimers;
//String scrollingString;
//uint16_t scrollingX, scrollingY, scrollingI, scrollingC, scrollingBG;

void setup() {
    display.begin();
    /*
    display.fillScreen(display.Color444(0b1001, 0b0101, 0b0011));
    while (1) {
        delay(1000);
    }
    */
    
    display.setTextColor(display.Color444(0xF, 0xF, 0xF));
    display.fillScreen(0x0000);
    
    String currentTimeStr = "" + String(Time.now());
    for (int i=0; i < 20; i++) {
        if (currentTimeStr.charAt(i)==0) {
            break;
        } else {
            display.write(currentTimeStr.charAt(i));
        }
    }
    
    if (launch_lib.connect("launchlibrary.net", 80)) {
        display.fillScreen(0x0000);
        display.setCursor(0,0);
        display.setTextColor(display.Color444(0x0, 0xF, 0x0));
        display.write('G');
        display.write('O');
        display.write('O');
        display.write('D');
        display.resync();
        
        launch_lib.println("GET /1.1/launch/next/1 HTTP/1.0");
        launch_lib.println("Host: launchlibrary.net");
        launch_lib.println("Content-Length: 0");
        launch_lib.println("User-Agent: Mozilla/4.0");
        launch_lib.println();
        delay(1000);
        int int_avail = launch_lib.available();
        String char_avail = String(int_avail);
        display.fillScreen(0x0000);
        display.setCursor(0,0);
        display.setTextColor(display.Color444(0x0, 0x0, 0xF));
        display.write(char_avail.charAt(0));
        if (int_avail >= 10) {
            display.write(char_avail.charAt(1));
        }
        if (int_avail >= 100) {
            display.write(char_avail.charAt(2));
        }
        if (int_avail >= 1000) {
            display.write(char_avail.charAt(3));
        }
        //delay(1000);
        
        while (1) {
            String next_line = readline(&launch_lib, 0, 10000);
            printString(next_line);
            if ((next_line.charAt(0) == '\r') && (next_line.charAt(1) == '\n')) {
                break;
            }
            delay(100);
        }
        
        display.fillScreen(display.Color444(0x3, 0x0, 0x0));
        String next_line = readline(&launch_lib, 0, 10000);
        display.fillScreen(0x0000);
        char *json;
        next_line.toCharArray(json, 2000);
        JsonObject& root = jsonBuffer.parseObject(json);
        if (!root.success())
        {
            printString("parseObject() failed");
        } else {
            printString("Success");
            delay(1000);
            
            int total_launches = root["total"];
            printString("total");
            delay(1000);
            printString( String(total_launches, DEC) );
            delay(1000);
            
            JsonArray& launches = root["launches"];
            JsonObject& nextLaunch = launches[0];
            netstamp = nextLaunch["netstamp"];
            printString("netstamp");
            delay(1000);
            printString( String(netstamp, DEC) );
            delay(1000);
            
            const char *next_name = nextLaunch["name"];
            printString("name");
            delay(1000);
            
            //scrollingString = NULL;
            //scrollingTimer.begin(scrollingStringCallback, 250, hmSec);
            //printScrolling( String(next_name),  0, 0, display.Color444(0xF, 0xF, 0xF), 0x0000 );
            delay(1000);
            
            //printString( String ((netstamp - Time.now()), DEC) );
            //delay(1000);
        }
    } else {
        display.fillScreen(0x0000);
        display.setCursor(0,0);
        display.setTextColor(display.Color444(0xF, 0x0, 0x0));
        display.write('F');
        display.write('A');
        display.write('I');
        display.write('L');
    }
    
    display.fillScreen(0x0000);
}

void loop() {
    delay(1000);
    printString( String ((netstamp - Time.now()), DEC) );
}

void launch_handler(const char *event, const char *data) {
    display.setCursor(0,0);
    display.setTextColor(display.Color444(0xF, 0xF, 0xF));
    display.fillScreen(0x0000);
    for (int i=0; i < 20; i++) {
        if (data[i]==0) {
            break;
        } else {
            display.write(data[i]);
        }
    }
}


String readline(TCPClient *client, int max_size, unsigned long timeout_ms) {
    unsigned long start_ms = millis();
    //char return_str[max_size];
    String return_str = String("");
    int index = 0;
    
    while ( ((millis() - start_ms)<timeout_ms) && ((index < max_size) || (max_size == 0)) ) {
        if (client->available()) {
            char read_next = client->read();
            if (read_next > 0) {
                return_str.concat(String((char)read_next));
                //return_str[index] = read_next;
                index++;
                if (read_next == 10) {
                    break;
                }
            } else {
                continue;
            }
        } else {
            delay(10);
        }
    }
    
    //return_str[index] = 0;
    //return String(return_str);
    
    return return_str;
}

void printString(String s) {
    //display.fillScreen(0x0000);
    display.setCursor(0,4);
    display.setTextColor(display.Color444(0xF, 0xF, 0xF), 0x0000);
    display.resync();
    for (int i=0; i<20 && i < s.length(); i++) {
        if (s.charAt(i) == 10) {
            display.write('*');
        } else {
            display.write(s.charAt(i));
        }
    }
}

/*
void printScrolling(String s, uint16_t x, uint16_t y, uint16_t c, uint16_t bg) {
    scrollingString = s;
    scrollingX = x;
    scrollingY = y;
    scrollingC = c;
    scrollingBG = bg;
    
    display.setTextColor(c, bg);
    display.setCursor(x, y);
    for (uint16_t i=0; i<5 && i < s.length(); i++) {
        if (s.charAt(i) == 10) {
            display.write('*');
        } else {
            display.write(s.charAt(i));
        }
    }
}

void scrollingStringCallback(void) {
    if (scrollingString != NULL) {
        display.setCursor(scrollingX,scrollingY);
        for (int j=0; j<5; j++) {
            if (scrollingString.charAt(scrollingI+j-4) == 10) {
                display.write('*');
            } else {
                display.write(scrollingString.charAt(scrollingI+j-4));
            }
        }
        scrollingI++;
        if ( scrollingI == scrollingString.length() ) {
            scrollingTimer.resetPeriod_SIT(2000, hmSec);
            scrollingI = 0;
        } else {
            scrollingTimer.resetPeriod_SIT(250, hmSec);
        }
        display.resync();
    }
}
*/