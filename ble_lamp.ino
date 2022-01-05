/*********************************************************************
  Author: Pierre BARET
  Open source: The Unlicense
*********************************************************************/

#include <bluefruit.h>
#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
#include <avr/power.h>
#endif

#define ENBLEPIN A2 // Enable/disable BLE
#define LEDPIN A0   // DIN of the led strip
#define VBATPIN A7  // To check battery level on feather board nRF52832 (might be different pin for other boards)

Adafruit_NeoPixel strip = Adafruit_NeoPixel(8, LEDPIN, NEO_GRB + NEO_KHZ800); // 8 Neo pixel stick in my case
BLEUart bleuart;    // Uart over BLE service
uint8_t ble_enable; // Holds wether BLE service advertising is enable or not
uint8_t ble_button_state; // Holds previous ble button state

uint32_t min_loop_millis = 50;       // Min time for the main loop (gives enough time to process inputs)
const uint32_t ANIM_DURATION = 5000; // Animation time in ms

// BUFFER TO PROCESS INPUTS COMING THROUGH BLE (AND SIZE EXPECTED FOR THOSE INPUTS)
#define READ_BUFSIZE                    (20)
#define PACKET_BUTTON_LEN               (5)
#define PACKET_COLOR_LEN                (6)
uint8_t packetbuffer[READ_BUFSIZE + 1]; // Buffer to hold incoming characters

// Default colors for animations loop (8bits RGB format)
const uint8_t nb_colors = 12;
const uint8_t colors[nb_colors][3] = {
  {0, 128, 255},
  {0, 0, 255},
  {128, 0, 255},
  {255, 0, 255},
  {255, 0, 128},
  {255, 0, 0},
  {255, 128, 0},
  {255, 255, 0},
  {128, 255, 0},
  {0, 255, 0},
  {0, 255, 128},
  {0, 255, 255}
};
int8_t current_color = 0;  // Holds current color index in the colors array (used for animations loop)
// 'red', 'green' and 'blue' holds RGB values for the current displayed color
uint8_t red = colors[current_color][0];
uint8_t green = colors[current_color][1];
uint8_t blue = colors[current_color][2];

const int8_t nb_animations = 5;  // Number of animation functions
int8_t animation = 0;            // Current animation index
uint8_t looping = 0;              // Looping automatically from one animation to the other
uint32_t animation_start = 0;     // Holds the time at start of the animation (to have a fixed animation time)

void CheckBattery(void);
void ProcessInput(uint8_t len);
void CmdNextAnim(void);
void CmdPrevAnim(void);
void CmdNextColor(void);
void CmdPrevColor(void);
void CmdSetColor(uint8_t r, uint8_t g, uint8_t b);
void CmdLoop(void);

void FixedColor(uint32_t c);
void fadeInFadeOut(uint32_t c);
void larsonScanner(uint8_t wait);
void colorWipe(uint32_t c, uint8_t wait);
void theaterChase(uint8_t wait);


// Arduino setup
void setup(void)
{
  Serial.begin(115200);
  while ( !Serial ) delay(10);   // for nrf52840 with native usb

  Serial.println(F("Baby Fox BLE Lamp"));
  Serial.println(F("-----------------"));

  pinMode(ENBLEPIN, INPUT); // Push button to enable/disable BLE
  pinMode(VBATPIN, INPUT);  // To read battery voltage
  analogReadResolution(10); // Analog read res 10 bit : 0..1023
  digitalWrite(PIN_LED1, LOW);
  ble_button_state = LOW;

  Bluefruit.begin();
  Bluefruit.setTxPower(4);    // Check bluefruit.h for supported values

  // Configure and start the BLE Uart service
  bleuart.begin();

  // Set up and start advertising
  startAdv();
}


// Start advertising BLE
void startAdv(void)
{
  Serial.println(F("BLE advertising: START"));
  Bluefruit.setName("BabyFoxLamp");

  // Advertising packet
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();

  // Include the BLE UART (AKA 'NUS') 128-bit UUID
  Bluefruit.Advertising.addService(bleuart);

  // Secondary Scan Response packet (optional)
  // Since there is no room for 'Name' in Advertising packet
  Bluefruit.ScanResponse.addName();

  /* Start Advertising
     - Enable auto advertising if disconnected
     - Interval:  fast mode = 20 ms, slow mode = 152.5 ms
     - Timeout for fast mode is 30 seconds
     - Start(timeout) with timeout = 0 will advertise forever (until connected)

     For recommended advertising interval
     https://developer.apple.com/library/content/qa/qa1931/_index.html
  */
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);    // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);      // number of seconds in fast mode
  Bluefruit.Advertising.start(0);                // 0 = Don't stop advertising after n seconds
  
  ble_enable = 1;
}

// Stop advertising BLE
void stopAdv(void)
{
  Serial.println(F("BLE advertising: STOP"));
  Bluefruit.Advertising.restartOnDisconnect(false);
  Bluefruit.Advertising.stop();
  
  ble_enable = 0;
}

// Callback for when user push the button to enable/disable BLE
void EnableBLEPushCallback(void)
{
  // If BLE is disabled, we enable it by starting advertising
  if (ble_enable == 0)
  {
    startAdv();
  }
  // Else we disable BLE only if no app is currently connected
  else
  {
    if (Bluefruit.Periph.connected())
    {
      printf("Don't disable BLE if an app is currently connected\n");
    }
    else{
      stopAdv();
    }
  }
}

// Arduino main loop
void loop(void)
{
  uint32_t start = millis();

  // 1. Process Inputs
  // 1.1 BLE enable push button
  uint8_t previous_state = ble_button_state;
  ble_button_state = digitalRead(ENBLEPIN);
  if ((ble_button_state == LOW) && (previous_state == HIGH)) // detect only push button release so we ensure the callback is called only once
  {
    EnableBLEPushCallback();
  }
  // 1.2 Incoming commands from BLE UART
  if (bleuart.available())
  {
    uint8_t idx = 0; // index for incoming characters from BLE

    // Reset reception buffer
    memset(packetbuffer, 0, READ_BUFSIZE);
    while ((bleuart.available()) && (idx < READ_BUFSIZE))
    {
      char c = bleuart.read();
      packetbuffer[idx] = c;
      idx++;
    }
    bleuart.flush();
    ProcessInput(idx);
  }
  
  // 2. Update animation state
  switch (animation)
  {
  case 0:
    fadeInFadeOut(strip.Color(red, green, blue));
    break;
  case 1:
    larsonScanner(60);
    break;
  case 2:
    colorWipe(strip.Color(red, green, blue), 60);
    break;
  case 3:
    theaterChase(1);
    break;
  case 4:
    FixedColor(strip.Color(red, green, blue));
    break;  
  default:
    break;
  }

  // 3. Utils : battery level
  CheckBattery();

  uint32_t end = millis();
  uint32_t loop_time = end - start;

  //Serial.printf("Loop time = %dms\n", loop_time);

  if (loop_time < min_loop_millis)
  {
    delay(min_loop_millis - loop_time);
  }
}

// Checks battery level on pin A7
void CheckBattery(void)
{
  float measuredvbat = analogRead(VBATPIN);
  float v_per_lsb = 3.6F/1024.0F; // 10-bit ADC with 3.6V input range
  measuredvbat *= v_per_lsb;

  Serial.printf("VBat: %.2f\r\n", measuredvbat);

  if (measuredvbat < 2.65f) // Threshold set empirically
  {
    digitalWrite(PIN_LED1, HIGH);
  }
  else
  {
    digitalWrite(PIN_LED1, LOW);
  }
  delay(1);
}

// Process inputs
// @param len : lenght of packet received by BLE
void ProcessInput(uint8_t len)
{
  if (len != 0)
  {
    if (packetbuffer[0] == '!')
    {
      if ((packetbuffer[1] == 'B') && (len == PACKET_BUTTON_LEN) || (len == 2 * PACKET_BUTTON_LEN))
      {
        uint8_t buttnum = packetbuffer[2] - '0';
        boolean pressed = packetbuffer[3] - '0';

        if (pressed)
        {
          switch (buttnum)
          {
          case 5:
            CmdPrevAnim();
            break;
          case 6:
            CmdNextAnim();
            break;
          case 7:
            CmdPrevColor();
            break;
          case 8:
            CmdNextColor();
            break;
          case 1:
            CmdLoop();
            break;          
          default:
            Serial.println("Unknown button pressed");
            break;
          }
        }
      }
      else if ((packetbuffer[1] == 'C') && (len == PACKET_COLOR_LEN))
      {
        CmdSetColor(packetbuffer[2], packetbuffer[3], packetbuffer[4]);
      }
    }
  }
}

void CmdNextAnim(void)
{
  animation = (animation + 1) % nb_animations;
}
void CmdPrevAnim(void)
{
  animation = (animation - 1) % nb_animations;
}
void UpdateCurrentColor(uint8_t idx)
{
  current_color = idx;
  red = colors[current_color][0];
  green = colors[current_color][1];
  blue = colors[current_color][2];
}
void CmdNextColor(void)
{
  UpdateCurrentColor((current_color + 1) % nb_colors);
}
void CmdPrevColor(void)
{
  UpdateCurrentColor((current_color - 1) % nb_colors);
}
void CmdSetColor(uint8_t r, uint8_t g, uint8_t b)
{
  red = r;
  green = g;
  blue = b;
}
void CmdLoop(void)
{
  looping = !looping;
}

void LoopAnimations()
{
    if (looping)
    {
      CmdNextColor();
      if (current_color == 0)
      {
        CmdNextAnim();
      }
    }
}

void FixedColor(uint32_t c)
{
  for(uint8_t i=0; i < strip.numPixels(); i++)
  {
    strip.setPixelColor(i, c);
  }
  strip.show();
  
  delay(1);
  
  uint32_t time_since_anim_start = millis() - animation_start;

  if (time_since_anim_start > ANIM_DURATION)
  {
    animation_start = millis();
    LoopAnimations();
  }
}

void fadeInFadeOut(uint32_t c)
{
  uint32_t time_since_anim_start = millis() - animation_start;

  float f = (float)time_since_anim_start/ (float)ANIM_DURATION;
  if (f > 1) f = 1;
  float r = red;
  float g = green;
  float b = blue;  

  if (f < 0.5f)
  {
    r = 2 * f * red;
    g = 2 * f * green;
    b = 2 * f * blue;
  }
  else
  {
    r = red - (2 * (f - 0.5)) * red;
    g = green - (2 * (f - 0.5)) * green;
    b = blue - (2 * (f - 0.5)) * blue;
  }

  for(uint8_t i=0; i < strip.numPixels(); i++)
  {
    strip.setPixelColor(i, strip.Color((uint8_t)r,(uint8_t)g,(uint8_t)b));
  }
  strip.show();
  
  delay(1);

  if (time_since_anim_start > ANIM_DURATION)
  {
    animation_start = millis();
    LoopAnimations();
  }
}

int pos = 0; // Position of "eye" for larson scanner animation
int dir = 1; // Direction of "eye" for larson scanner animation
void larsonScanner(uint8_t wait)
{
  uint32_t time_since_anim_start = millis() - animation_start;

  float f = (float)time_since_anim_start/ (float)ANIM_DURATION;

  int i = (int)(f * (float)(strip.numPixels() + 5));
  uint32_t bright_color = strip.Color(red, green, blue);
  int16_t medium_red = max((int16_t)(red) - 128, 0);
  int16_t medium_green = max((int16_t)(green) - 128, 0);
  int16_t medium_blue = max((int16_t)(blue) - 128, 0);
  uint32_t medium_color = strip.Color(medium_red, medium_green, medium_blue);
  int16_t dark_red = max((int16_t)(red) - 245, 0);
  int16_t dark_green = max((int16_t)(green) - 245, 0);
  int16_t dark_blue = max((int16_t)(blue) - 245, 0);
  uint32_t dark_color = strip.Color(dark_red, dark_green, dark_blue);
  // Draw 5 pixels centered on pos.  setPixelColor() will clip any
  // pixels off the ends of the strip, we don't need to watch for that.
  strip.setPixelColor(pos - 2, dark_color); // Dark red
  strip.setPixelColor(pos - 1, medium_color); // Medium red
  strip.setPixelColor(pos , bright_color); // Center pixel is brightest
  strip.setPixelColor(pos + 1, medium_color); // Medium red
  strip.setPixelColor(pos + 2, dark_color); // Dark red

  strip.show();
  delay(wait);

  // Rather than being sneaky and erasing just the tail pixel,
  // it's easier to erase it all and draw a new one next time.
  for(int j = -2; j <= 2; j++)
  {
    strip.setPixelColor(pos + j, 0);
  }

  // Bounce off ends of strip
  pos += dir;
  if (pos < 0)
  {
    pos = 1;
    dir = -dir;
  }
  else if (pos >= strip.numPixels())
  {
    pos = strip.numPixels() - 2;
    dir = -dir;
  }
  
  delay(1);

  if (time_since_anim_start > ANIM_DURATION)
  {
    animation_start = millis();
    LoopAnimations();
  }
}

void colorWipe(uint32_t c, uint8_t wait)
{
  uint32_t time_since_anim_start = millis() - animation_start;

  float f = (float)time_since_anim_start/ (float)ANIM_DURATION;

  int i = (int)(f * (float)(strip.numPixels() + 8));

  if (i > strip.numPixels())
  {
    for(uint16_t p = 0; p < strip.numPixels(); p++)
    {
      strip.setPixelColor(p, 0);
    }
    strip.show();

    uint32_t col = (i % 2 == 0) ? 0 : c;
    for(uint16_t p = 0; p < strip.numPixels(); p++)
    {
      strip.setPixelColor(p, col);
    } 
    strip.show();
  }
  else
  {
    strip.setPixelColor(i, c);
    strip.show();
  }
  
  
  delay(wait);

  if (time_since_anim_start > ANIM_DURATION)
  {
    animation_start = millis();
    LoopAnimations();
  }
}

//Theatre-style crawling lights
uint8_t q = 0;  // Current step for the theaterChase animation
void theaterChase(uint8_t wait)
{
  uint32_t time_since_anim_start = millis() - animation_start;

  for (uint16_t i = 0; i < strip.numPixels(); i++)
  {
    if (i % 3 == q)
    {
      strip.setPixelColor(i, strip.Color(red, green, blue));    //turn every third pixel on
    }
    else
    {
      strip.setPixelColor(i, 0);
    }
  }
  strip.show();

  delay(wait);

  q = (q + 1) % 3;
  if (time_since_anim_start > ANIM_DURATION)
  {
    animation_start = millis();
    LoopAnimations();
  }
}
