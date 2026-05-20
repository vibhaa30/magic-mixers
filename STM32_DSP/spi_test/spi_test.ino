#include <SPI.h>

// Wire to STM32 SPI2: PB12=NSS, PB13=SCK, PB14=MISO, PB15=MOSI
#define CS_PIN  10   // D10 → PB12 (STM32 NSS)

// Gesture codes matching spi_task.c
#define GESTURE_CLEAR   0
#define GESTURE_VOLUME  1
#define GESTURE_DELAY   2
#define GESTURE_LPF     3

// How much the value changes per arrow key press
#define STEP_SIZE 5 

uint8_t currentGesture = GESTURE_VOLUME;
int intensities[4] = {0, 128, 128, 128}; // Store current values: Clear, Vol, Delay, LPF

// State machine for reading arrow keys (ANSI escape sequences)
int ansiState = 0; 

void sendPacket(uint8_t gesture, uint8_t intensity)
{
    digitalWrite(CS_PIN, LOW);
    delayMicroseconds(10);  // give STM32 NSS time to register

    SPI.transfer(gesture);
    SPI.transfer(intensity);

    delayMicroseconds(10);
    digitalWrite(CS_PIN, HIGH);

    Serial.print("Sent: gesture=");
    Serial.print(gesture);
    Serial.print("  intensity=");
    Serial.println(intensity);
}

void setup()
{
    Serial.begin(115200);
    while (!Serial) delay(10);

    pinMode(CS_PIN, OUTPUT);
    digitalWrite(CS_PIN, HIGH);

    // STM32 SPI2: MODE_0, MSB first, 8-bit
    SPI.begin();
    SPI.beginTransaction(SPISettings(500000, MSBFIRST, SPI_MODE0));

    Serial.println("STM32 SPI tester ready.");
    Serial.println("-------------------------------------------------");
    Serial.println("1. Select an effect:");
    Serial.println("   'v' = Volume | 'd' = Delay | 'l' = LPF | 'c' = Clear");
    Serial.println("2. Adjust intensity:");
    Serial.println("   Use UP/DOWN arrow keys (in PuTTY/TeraTerm)");
    Serial.println("   Use '+' or '-' (in Arduino Serial Monitor)");
    Serial.println("-------------------------------------------------");
    
    Serial.println("Currently selected: VOLUME");
}

void adjustIntensity(int delta)
{
    if (currentGesture == GESTURE_CLEAR) {
        Serial.println("Cannot adjust intensity of CLEAR. Select v, d, or l first.");
        return;
    }

    // Calculate new value and constrain between 0 and 255
    int newVal = intensities[currentGesture] + delta;
    newVal = constrain(newVal, 0, 255);

    // Only send SPI if the value actually changed
    if (newVal != intensities[currentGesture]) {
        intensities[currentGesture] = newVal;
        sendPacket(currentGesture, intensities[currentGesture]);
    } else {
        Serial.print("Limit reached: ");
        Serial.println(newVal);
    }
}

// --- Serial command parser ---
static void handleSerial()
{
    while (Serial.available() > 0) {
        char inChar = (char)Serial.read();

        // --- ANSI Escape Sequence Parser for Arrow Keys ---
        // Arrow keys send three characters: ESC (0x1B), '[' (0x5B), and 'A' or 'B'
        if (ansiState == 0) {
            if (inChar == '\x1B') {
                ansiState = 1; // Caught an ESC character
                continue;
            }
        } else if (ansiState == 1) {
            if (inChar == '[') {
                ansiState = 2; // Caught the bracket
                continue;
            } else {
                ansiState = 0; // False alarm
            }
        } else if (ansiState == 2) {
            if (inChar == 'A') {
                adjustIntensity(STEP_SIZE);       // UP Arrow
            } else if (inChar == 'B') {
                adjustIntensity(-STEP_SIZE);      // DOWN Arrow
            }
            ansiState = 0; // Reset state
            continue;
        }

        // --- Standard Character Parser ---
        switch (inChar) {
            // Mode Selectors
            case 'v': 
            case 'V':
                currentGesture = GESTURE_VOLUME; 
                Serial.println("\n>>> Mode changed to: VOLUME <<<"); 
                break;
            case 'd': 
            case 'D':
                currentGesture = GESTURE_DELAY;  
                Serial.println("\n>>> Mode changed to: DELAY <<<"); 
                break;
            case 'l': 
            case 'L':
                currentGesture = GESTURE_LPF;    
                Serial.println("\n>>> Mode changed to: LPF <<<"); 
                break;
            case 'c': 
            case 'C':
                currentGesture = GESTURE_CLEAR;  
                Serial.println("\n>>> Mode changed to: CLEAR <<<"); 
                sendPacket(GESTURE_CLEAR, 0);
                break;

            // Fallback adjusters for Arduino Serial Monitor
            case '+':
            case '=':
                adjustIntensity(STEP_SIZE);
                break;
            case '-':
            case '_':
                adjustIntensity(-STEP_SIZE);
                break;
        }
    }
}

void loop()
{
    handleSerial();
}