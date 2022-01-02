/*********************************************************************
  Author: Pierre BARET
  Open source: No License
*********************************************************************/

#include <bluefruit.h>
#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
#include <avr/power.h>
#endif

#define PIN A0

Adafruit_NeoPixel strip = Adafruit_NeoPixel(8, PIN, NEO_GRB + NEO_KHZ800);
BLEUart bleuart;  // Uart over BLE service

uint32_t min_loop_millis = 50;
const uint32_t ANIM_DURATION = 5000; // Animation time in ms

#define READ_BUFSIZE                    (20)
#define PACKET_BUTTON_LEN               (5)
#define PACKET_COLOR_LEN                (6)
/* Buffer to hold incoming characters */
uint8_t packetbuffer[READ_BUFSIZE + 1];


const uint8_t nb_colors = 12;
uint8_t colors[nb_colors][3] = {
  {255, 0, 0},
  {255, 128, 0},
  {255, 255, 0},
  {128, 255, 0},
  {0, 255, 0},
  {0, 255, 128},
  {0, 255, 255},
  {0, 128, 255},
  {0, 0, 255},
  {128, 0, 255},
  {255, 0, 255},
  {255, 0, 128}
};
uint8_t current_color = 0;
uint8_t red = 255;
uint8_t green = 0;
uint8_t blue = 0;

const uint8_t nb_animations = 5;
uint8_t animation = 0;
uint32_t animation_start = 0;
uint8_t looping = 0;

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

void setup(void)
{
  Serial.begin(115200);
  while ( !Serial ) delay(10);   // for nrf52840 with native usb

  Serial.println(F("Baby Fox BLE Lamp"));
  Serial.println(F("-----------------"));

  Bluefruit.begin();
  Bluefruit.setTxPower(4);    // Check bluefruit.h for supported values

  // Configure and start the BLE Uart service
  bleuart.begin();

  // Set up and start advertising
  startAdv();
}


void startAdv(void)
{
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
}


void loop(void)
{
  uint32_t start = millis();
  uint8_t idx = 0;

  // 1. Process Inputs
  if (bleuart.available())
  {
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
  
  // 2. Update state
  switch (animation)
  {
  case 0:
    larsonScanner(60);
    break;
  case 1:
    fadeInFadeOut(strip.Color(red, green, blue));
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

  uint32_t end = millis();
  uint32_t loop_time = end - start;

  //Serial.printf("Loop time = %dms\n", loop_time);

  if (loop_time < min_loop_millis)
  {
    delay(min_loop_millis - loop_time);
  }
}


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
uint8_t q = 0;
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
