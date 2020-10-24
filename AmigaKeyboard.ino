// UNCOMMENT THE NEXT LINE TO USE AN ESP32 WITH BLUETOOTH, YOU WILL NEED THE LIBRARY FROM https://github.com/T-vK/ESP32-BLE-Keyboard AND A COMPATIBLE ESP32 DEVICE.
//#define USE_ESP32_BLUETOOTH

// ====================================================================================================
//
// - KNOWN GOOD BLUETOOTH DEVICE:
//   ESP32S (select "ESP32S" in the Arduino IDE)
//   https://www.amazon.com/gp/product/B086MGH7JV/ 
//   $~25 for 5 of them ~or~ $5.00/ea (2020/10/15)
//
// - KNOWN GOOD USB DEVICE: 
//   Pro Micro ATmega32U4 (select  "Leonardo" in the Arduino IDE) 
//   https://www.amazon.com/gp/product/B08BJNV1J3/ 
//   $~18 for 4 of them ~or~ $4.50/ea (2020/10/15)
//
// ====================================================================================================

// The next lines define which pins will be connected to where. -- Note that the LEDs cannot be directly driven, but instead need a transistor to drive them.

#define A500CLK        7  // Suggested: 13 on ESP32S, 7 on Pro Micro
#define A500SP         8  // Suggested: 12 on ESP32S, 8 on Pro Micro
#define A500RES        9  // Suggested: 14 on ESP32S, 9 on Pro Micro

// The LEDS require more current than the IO pins can produce, so use another IC to drive them (:
#define LED_NUMLOCK    6 // Suggested: 26 on ESP32S, 6 on Pro Micro (-1 to disable)
#define LED_SCROLLLOCK 5 // Suggested: 27 on ESP32S, 5 on Pro Micro (-1 to disable)

// The pin state to use for on and off for the Num Lock LED.
#define LED_NUMLOCK_ON  LOW
#define LED_NUMLOCK_OFF HIGH

// The pin state to use for on and off for the Scroll Lock LED.
#define LED_SCROLLLOCK_ON  LOW
#define LED_SCROLLLOCK_OFF HIGH

// ====================================================================================================
//   OTHER DEV NOTES:
// ====================================================================================================
// - Amiga Hardware Reference Manual -- note that the spec calls for 85 usecs not 85 msecs: 
//   http://amigadev.elowar.com/read/ADCD_2.1/Hardware_Manual_guide/node0173.html
//
// - Forum post -- second from the top, by user "ross" -- "... some keyboards ... require ~200us."
//   https://eab.abime.net/showthread.php?t=98962
//
// - NUMPAD KEYS: 
//   https://forum.arduino.cc/index.php?topic=179548.0
//   - page 53 has usb hid codes -- add 136 to the value: 
//     https://www.usb.org/sites/default/files/documents/hut1_12v2.pdf
//
// - Original Code: https://www.raspberrypi.org/forums/viewtopic.php?f=40&t=10990
//
// - Less portable "Improved" Code: https://forum.arduino.cc/index.php?topic=139358.0
//
// Amiga Keyboard Connector Pinout:
// 1   A500CLK    --  Black
// 2   A500SP     --  Brown
// 3   A500RES    --  Red
// 4   5v         --  Orange
// 5   NC         --  Yellow
// 6   GND        --  Green
// 7   LED1 (RED) --  Blue
// 8   LED2 (GRN) --  Purple
//
// ====================================================================================================
//
// To Drive the LEDs I grabbed a random 7400 IC (Quad Nand gate), and hooked it up like this:
//      __    __
//  1 -|o \__/  |- 14     14  . . . . . . -> Power (+5v)
//  2 -|        |- 13     7 . . . . . . . -> Ground
//  3 -| SN7400 |- 12     1, 2, 4, 5  . . -> LED_NUMLOCK (Connect all four pins together and to the LED_NUMLOCK pin on the Arduino)
//  4 -|        |- 11     3, 6  . . . . . -> LED1 or LED2 (whichever one you want to be NUMLOCK)
//  5 -|        |- 10     9, 10, 12, 13 . -> LED_SCROLLLOCK (Connect all four pins together and to the LED_SCROLLLOCK pin on the Arduino)
//  6 -|        |- 9      8, 11 . . . . . -> LED1 or LED2 (whichever one you didn't use for NUMLOCK)
//  7 -|________|- 8
//
// ====================================================================================================

#define SYNCH_HI 1
#define SYNCH_LO 2
#define HANDSHAKE 4
#define READ 16
#define WAIT_LO 8
#define WAIT_RES 64

#define biton(value,nbit)           ((value) |=  (1<<(nbit)))
#define bitoff(value,nbit)          ((value) &= ~(1<<(nbit)))
#define bitcheck(value,nbit)        ((value) &   (1<<(nbit)))
#define bitset(value, nbit, onoff)  { if (onoff) { biton(value,nbit); } else { bitoff(value,nbit); } }

unsigned char state;
signed char bitn = -1;
long counter;
byte mode;
unsigned short key;
unsigned short lkey;

#ifdef USE_ESP32_BLUETOOTH

  #include <BleKeyboard.h>
  BleKeyboard KeyboardDevice;

  #define KeyboardDeviceIsConnected (KeyboardDevice.isConnected())

#else

  #include <Keyboard.h>
  #define KeyboardDevice            Keyboard
  #define KeyboardDeviceIsConnected true

#endif

const unsigned char * alpha1 = (const unsigned char *)"qwertyuiop[] ";
const unsigned char * alpha2 = (const unsigned char *)"asdfghjkl;'  ";
const unsigned char * alpha3 = (const unsigned char *)"zxcvbnm,./   ";

// Tracking the state ourselves is not super accurate, because the OS can change it and we wouldn't know.
// But the Arduino keyboard libraries can't report the status back to our code. :(
bool numlock_on = false;
bool scrolllock_on = false;

bool left_ctrl = false;
bool right_ctrl = false;

void UpdateLeds()
{
  digitalWrite(LED_NUMLOCK, numlock_on ? LED_NUMLOCK_ON : LED_NUMLOCK_OFF);
  digitalWrite(LED_SCROLLLOCK, scrolllock_on ? LED_SCROLLLOCK_ON : LED_SCROLLLOCK_OFF);
}

void setup()
{
    pinMode(A500CLK,        INPUT);
    pinMode(A500SP,         INPUT);
    pinMode(A500RES,        INPUT);
    pinMode(LED_NUMLOCK,    OUTPUT);
    pinMode(LED_SCROLLLOCK, OUTPUT);

    digitalWrite(A500SP, LOW);
    UpdateLeds();
    
    state = SYNCH_HI;
    counter = 0;
    mode = 0;
    lkey = 1000;

    //Serial.begin(115200);
    KeyboardDevice.begin();
}

inline void SetKey(bool isConnected, unsigned char key, bool pressed)
{
  if (isConnected)
  {
    if (pressed)
    {
      KeyboardDevice.press(key);
    }
    else
    {
      KeyboardDevice.release(key);
    }
  }
}

void loop()
{
    bool isConnected = KeyboardDeviceIsConnected;
    
    // Reset Detected
    if (digitalRead(A500RES) == LOW && state != WAIT_RES)
    {
        interrupts();
        if (isConnected)
        {
          KeyboardDevice.releaseAll();
          //KeyboardDevice.press2(0x4C, 0x05);
          //KeyboardDevice.release2(0x4C, 0x05);
        }
        lkey = 1000;
        delay(10);
        state = WAIT_RES;
        //Serial.println("RESET DETECTED");

        numlock_on = false;
        scrolllock_on = false;
        left_ctrl = false;
        right_ctrl = false;
        UpdateLeds();
    }
    // Waiting for the reset to end
    else if (state == WAIT_RES)
    {
        interrupts();
        if (digitalRead(A500RES) == HIGH)
        { state = SYNCH_HI; }
    }
    // Synch Pulse
    else if (state == SYNCH_HI)
    {
        if (digitalRead(A500CLK) == LOW)
            state = SYNCH_LO;
    }
    else if (state == SYNCH_LO)
    {
        if (digitalRead(A500CLK) == HIGH)
        {
            state = HANDSHAKE;
        }
    }
    else if (state == HANDSHAKE)
    {
        if (counter == 0)
        {
            pinMode(A500SP, OUTPUT);
            digitalWrite(A500SP, LOW);
            counter = micros();
        }
        else if (micros() - counter > 200)
        {
            counter = 0;
            pinMode(A500SP, INPUT);
            digitalWrite(A500SP, HIGH);
            state = WAIT_LO;
            key = 0;
            bitn = 7;
        }
    }
    else if (state == READ)
    {
        if (digitalRead(A500CLK) == HIGH)
        {
            // add bit
            key += (!digitalRead(A500SP)) << (bitn ? bitn - 1 : 7);

            // all bits read
            if (--bitn < 0)
            {
                interrupts();
                state = HANDSHAKE;
                //Serial.println(key);

                if (key == 0xF9 || key == 0xFD)
                {
                    mode = 0;
                    lkey = 1000;
                    if (isConnected) { KeyboardDevice.releaseAll(); }
                    //Serial.println("DESYNC!");
                    state = HANDSHAKE;
                    delay(10);
                    return;
                }
                else if (key == lkey)
                {
                  // do nothing for an exactly repeated code.
                }
                else
                {
                  lkey = key;

                  bool pressed = (key & 0x80) == 0;
                  unsigned short keyCode = (key & 0x7F);
                  
                  if (keyCode == 0) { SetKey(isConnected, '`', pressed); }
                  else if (keyCode <= 9) { SetKey(isConnected, '1' + (keyCode-1), pressed); }
                  else if (keyCode == 10) { SetKey(isConnected, '0', pressed); }
                  else if (keyCode == 11) { SetKey(isConnected, '-', pressed); }
                  else if (keyCode == 12) { SetKey(isConnected, '=', pressed); }
                  else if (keyCode == 13) { SetKey(isConnected, '\\', pressed); }
                  else if (keyCode == 15) { SetKey(isConnected, (98 + 136), pressed); } // NUMPAD 0
                  else if (keyCode >= 16 && keyCode <= 27) { SetKey(isConnected, alpha1[keyCode - 16], pressed); }
                  else if (keyCode >= 29 && keyCode <= 31) { SetKey(isConnected, ((keyCode - 29) + 89 + 136), pressed); } // NUMPAD 1, 2, 3
                  else if (keyCode >= 32 && keyCode <= 42) { SetKey(isConnected, alpha2[keyCode - 32], pressed); }
                  else if (keyCode >= 45 && keyCode <= 47) { SetKey(isConnected, ((keyCode - 45) + 92 + 136), pressed); } // NUMPAD 4, 5, 6
                  else if (keyCode >= 49 && keyCode <= 58) { SetKey(isConnected, alpha3[keyCode - 49], pressed); }
                  else if (keyCode == 60) { SetKey(isConnected, (99 + 136), pressed); } // NUMPAD .
                  else if (keyCode >= 61 && keyCode <= 63) { SetKey(isConnected, ((keyCode - 61) + 95 + 136), pressed); } // NUMPAD 7, 8, 9
                  else if (keyCode == 64) { SetKey(isConnected, (44 + 136), pressed); } // Space Bar
                  else if (keyCode == 65) { SetKey(isConnected, KEY_BACKSPACE, pressed); }
                  else if (keyCode == 66) { SetKey(isConnected, KEY_TAB, pressed); }
                  else if (keyCode == 67) { SetKey(isConnected, (88 + 136), pressed); } // NUMPAD ENTER
                  else if (keyCode == 68) { SetKey(isConnected, KEY_RETURN, pressed); }
                  else if (keyCode == 69) { SetKey(isConnected, KEY_ESC, pressed); }
                  else if (keyCode == 70) { SetKey(isConnected, KEY_DELETE, pressed); }
                  else if (keyCode == 74) { SetKey(isConnected, (86 + 136), pressed); } // NUMPAD MINUS
                  else if (keyCode == 76) { SetKey(isConnected, KEY_UP_ARROW, pressed); }
                  else if (keyCode == 77) { SetKey(isConnected, KEY_DOWN_ARROW, pressed); }
                  else if (keyCode == 78) { SetKey(isConnected, KEY_RIGHT_ARROW, pressed); }
                  else if (keyCode == 79) { SetKey(isConnected, KEY_LEFT_ARROW, pressed); }
                  else if (keyCode >= 80 && keyCode <= 89) { SetKey(isConnected, KEY_F1 + (keyCode - 80), pressed); }
                  else if (keyCode == 90)
                  {
                    // NUM LOCK
                    SetKey(isConnected, (83 + 136), pressed);
                    if (pressed && isConnected)
                    {
                      numlock_on = !numlock_on;
                      UpdateLeds();
                    }
                  }
                  else if (keyCode == 91)
                  {
                    // SCROLL LOCK
                    SetKey(isConnected, (71 + 136), pressed);
                    if (pressed && isConnected)
                    {
                      scrolllock_on = !scrolllock_on;
                      UpdateLeds();
                    }
                  }
                  else if (keyCode == 92) { SetKey(isConnected, (84 + 136), pressed); } // NUMPAD /
                  else if (keyCode == 93) { SetKey(isConnected, (85 + 136), pressed); } // NUMPAD *
                  else if (keyCode == 94) { SetKey(isConnected, (87 + 136), pressed); } // NUMPAD +
                  else if (keyCode == 95) 
                  { 
                    // AMIGA HELP -> PrintScreen
                    // AMIGA HELP + CTRL -> Pause Break

                    int code = 70 + 136; // Print Screen
                    if (left_ctrl || right_ctrl) 
                    { 
                      code = 72 + 136; // Pause
                    }
                    
                    SetKey(isConnected, code, pressed); 
                  } 
                  else if (keyCode == 96) { SetKey(isConnected, KEY_LEFT_SHIFT, pressed); }
                  else if (keyCode == 97) { SetKey(isConnected, KEY_RIGHT_SHIFT, pressed); }
                  else if (keyCode == 98) { SetKey(isConnected, KEY_CAPS_LOCK, true); delay(10); SetKey(isConnected, KEY_CAPS_LOCK, false); }
                  else if (keyCode == 99) { SetKey(isConnected, KEY_LEFT_GUI, pressed); } // AMIGA CTRL -> Windows Key
                  else if (keyCode == 100) { SetKey(isConnected, KEY_LEFT_CTRL, pressed); left_ctrl = pressed && isConnected; }
                  else if (keyCode == 101) { SetKey(isConnected, KEY_RIGHT_CTRL, pressed); right_ctrl = pressed && isConnected; }
                  else if (keyCode == 102) { SetKey(isConnected, KEY_LEFT_ALT, pressed); }
                  else if (keyCode == 103) { SetKey(isConnected, KEY_RIGHT_ALT, pressed); }

                  /*if (pressed) { Serial.print("DN "); }
                  else { Serial.print("UP "); }
                  Serial.println(keyCode);*/
                }
            }
            else
            {
                state = WAIT_LO;
            }
        }
    }
    else if (state == WAIT_LO)
    {
        if (digitalRead(A500CLK) == LOW)
        {
            noInterrupts();
            state = READ;
        }
    }
}
