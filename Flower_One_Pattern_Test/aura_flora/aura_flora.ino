#include <Arduino.h>
#include <FastLED.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoPixel.h>

const int GSR = A0;
int sensorValue = 0;
int gsr_average = 0;
long lastReadingTime = 0;
long readingPauseDuration = 4000; // 4 seconds
long readingDelay = 500;          // 1/2 second delay before taking the reading
int currentAnimation = 9;         // The animation that starts up when powered on
bool readingInProgress = false;
long readingStartTime = 0;
#define NUM_LEDS 383
#define DATA_PIN 6
#define STEM_LEDS 103 // 51 stem, 52 leaves
#define NUM_SMALL_PETALS 10
#define NUM_LARGE_PETALS 10
#define NUM_LEDS_SMALL_PETAL 11
#define NUM_LEDS_LARGE_PETAL 13
CRGB leds[NUM_LEDS];

// pattern specific variables
#define NUM_FLIES 3
#define FADE_RATE 1
#define BRAIN_WAVES_NOISE_SCALE 90000000.0 // Adjusts the scale of the noise. Higher values create larger patterns.
#define BRAIN_WAVES_SPEED 2                // Adjusts the speed of the animation. Lower values create slower animations.
#define NUM_DOTS 1
#define DOTS_FADE_RATE 1
#define NOISE_SCALE 25
#define COOLING 55
#define SPARKING 120

uint8_t heat[NUM_LEDS]; // one heat cell per LED

DEFINE_GRADIENT_PALETTE(fire_no_white_gp){
    0, 0, 0, 0,      // black
    64, 120, 10, 0,  // deep red-orange
    128, 200, 50, 0, // orange
    192, 255, 80, 0, // gold
    255, 180, 30, 0  // ember (no white)
};
CRGBPalette16 fireNoWhitePalette = fire_no_white_gp;

float mapf(float x, float in_min, float in_max, float out_min, float out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

float sec = 0.0; // Time variable for animation

// Function to map LED index to 2D coordinates
void getLEDCoordinates(int index, float &x, float &y)
{
    if (index < STEM_LEDS)
    {
        x = 4.5;
        y = index * 0.1;
    }
    else
    {
        int ledIndex = STEM_LEDS;
        for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
        {
            int numLedsInPetal = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

            if (index < ledIndex + numLedsInPetal)
            {
                int ledInPetal = index - ledIndex;
                float angle = petal * (2 * PI / (NUM_SMALL_PETALS + NUM_LARGE_PETALS));
                float radius = 5.0 + ledInPetal * 0.5;
                x = 4.5 + radius * cos(angle);
                y = 7.5 + radius * sin(angle);
                return;
            }

            ledIndex += numLedsInPetal;
        }

        // If index is beyond known LEDs, fall back
        x = 0;
        y = 0;
    }
}

void setup()
{
    // FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
    // FastLED.setBrightness(120);
    Serial.begin(115200);
}

void loop()
{
    unsigned long currentMillis = millis();

    if (!readingInProgress && currentMillis - lastReadingTime >= readingPauseDuration)
    {
        readingInProgress = true;
        readingStartTime = currentMillis;
    }

    if (readingInProgress && currentMillis - readingStartTime >= readingDelay)
    {
        // Read GSR sensor data and calculate human resistance
        long sum = 0;
        for (int i = 0; i < 10; i++)
        { // Average the 10 measurements to remove the glitch
            sensorValue = analogRead(GSR);
            sum += sensorValue;
            delay(5); // Small delay to stabilize the readings
        }
        gsr_average = sum / 10;
        Serial.println(gsr_average);

        // Calculate human resistance
        int Serial_calibration = 512; // calibration value, adjust as needed
        long humanResistance = ((1024 + 2 * gsr_average) * 10000) / (Serial_calibration - gsr_average);
        Serial.print("GSR Average: ");
        Serial.println(gsr_average);
        Serial.print("Human Resistance: ");
        Serial.println(humanResistance);

        // animationFourteen();
        // FastLED.show();
    }
}

void exampleAnimation()
{
}

void testAnimation()
{
}

void animationOne()
{
    // Set stem LEDs
    for (int i = 0; i < STEM_LEDS; i++)
    {
        leds[i] = CRGB::Green;
    }

    static uint16_t x = 0;
    static uint16_t y = 0;
    int ledIndex = STEM_LEDS; // Start after the stems and leaves

    for (int petal = 0; petal < 20; petal++)
    {
        int numLedsInPetal = (petal % 2 == 0) ? 11 : 13; // Small petals have 11 LEDs, large petals have 13 LEDs

        for (int i = 0; i < numLedsInPetal; i++)
        {
            uint8_t noise = inoise8(x + (numLedsInPetal - i) * 20, y + petal * 20);
            leds[ledIndex + i] = CHSV(noise, 255, 255);
        }

        ledIndex += numLedsInPetal;
    }

    x += 2; // Increase the increment to speed up the animation
    y += 2;
}

void animationTwo()
{
    static uint8_t ripplePhase = 0;
    int ledIndex = 0;

    // --- Stem: vertical ripple wave ---
    for (int i = 0; i < STEM_LEDS; i++)
    {
        float dist = (float)i / STEM_LEDS;
        uint8_t brightness = sin8(ripplePhase + dist * 128);
        leds[ledIndex++] = CHSV(96, 255, brightness); // green wave
    }

    // --- Petals: ripple based on distance from center ---
    float centerX = NUM_LEDS_LARGE_PETAL / 2.0;
    float centerY = (NUM_SMALL_PETALS + NUM_LARGE_PETALS) / 2.0;

    for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    {
        int numLedsInPetal = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

        for (int i = 0; i < numLedsInPetal; i++)
        {
            float x = i;
            float y = petal;

            // Stretch x for better circularity
            x *= (NUM_SMALL_PETALS + NUM_LARGE_PETALS) / (float)NUM_LEDS_LARGE_PETAL;

            float dx = x - centerX;
            float dy = y - centerY;
            float distance = sqrtf(dx * dx + dy * dy);

            // Ripple pulse from center
            uint8_t brightness = sin8(ripplePhase + distance * 32);

            // Subtle hue modulation per petal
            uint8_t hue = 160 + sin8(petal * 16 + ripplePhase) / 8;

            leds[ledIndex++] = CHSV(hue, 200, brightness);
        }
    }

    ripplePhase += 2;

    //     // Set stem LEDs
    //     for (int i = 0; i < STEM_LEDS; i++)
    //     {
    //         leds[i] = CRGB::Green;
    //     }

    //     static uint8_t hue = 0;
    //     hue++;

    //     int ledIndex = STEM_LEDS; // Start after the stems and leaves

    //     // Handle small petals (rainbow in)
    //     for (int petal = 0; petal < NUM_SMALL_PETALS; petal++)
    //     {
    //         for (int i = 0; i < NUM_LEDS_SMALL_PETAL; i++)
    //         {
    //             leds[ledIndex + i] = CHSV(hue + (NUM_LEDS_SMALL_PETAL - i) * 10, 255, 255);
    //         }
    //         ledIndex += NUM_LEDS_SMALL_PETAL + NUM_LEDS_LARGE_PETAL; // Skip large petal LEDs
    //     }

    //     ledIndex = STEM_LEDS + NUM_LEDS_SMALL_PETAL; // Start after the stems and first small petal

    //     // Handle large petals (rainbow out)
    //     for (int petal = 0; petal < NUM_LARGE_PETALS; petal++)
    //     {
    //         for (int i = 0; i < NUM_LEDS_LARGE_PETAL; i++)
    //         {
    //             leds[ledIndex + i] = CHSV(hue + i * 10, 255, 255);
    //         }
    //         ledIndex += NUM_LEDS_SMALL_PETAL + NUM_LEDS_LARGE_PETAL; // Skip small petal LEDs
    //     }
}

void animationThree()
{
    static uint8_t t = 0;
    int ledIndex = 0;

    // --- Stem: slow warm wave ---
    for (int i = 0; i < STEM_LEDS; i++)
    {
        float offset = (float)i / STEM_LEDS;
        uint8_t hue = sin8(t + offset * 128);
        leds[ledIndex++] = CHSV(32 + hue / 4, 255, 200); // earthy wave
    }

    // --- Petals: spiral ripple effect ---
    float centerX = NUM_LEDS_LARGE_PETAL / 2.0;
    float centerY = (NUM_SMALL_PETALS + NUM_LARGE_PETALS) / 2.0;

    for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    {
        int numLedsInPetal = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

        for (int i = 0; i < numLedsInPetal; i++)
        {
            float x = i;
            float y = petal;

            // Scale x for elliptical symmetry
            x *= (NUM_SMALL_PETALS + NUM_LARGE_PETALS) / (float)NUM_LEDS_LARGE_PETAL;

            float dx = x - centerX;
            float dy = y - centerY;
            float angle = atan2f(dy, dx); // -PI to PI
            float radius = sqrtf(dx * dx + dy * dy);

            // Spiral hue and radial dimming
            uint8_t hue = (uint8_t)fmod((angle * 128 / PI) + radius * 32 + t, 255);
            uint8_t val = 200 - (radius * 10);
            leds[ledIndex++] = CHSV(hue, 200, val);
        }
    }

    t += 2;

    // // Set stem LEDs
    // for (int i = 0; i < STEM_LEDS; i++)
    // {
    //     leds[i] = CHSV(128, 255, 255); // Greenish-blue (teal)
    // }

    // static uint16_t x = 0;
    // static uint16_t y = 0;
    // static uint16_t z = 0;
    // int ledIndex = STEM_LEDS; // Start after the stems and leaves

    // for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    // {
    //     int numLedsInPetal = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

    //     for (int i = 0; i < numLedsInPetal; i++)
    //     {
    //         uint8_t noise = inoise8(x + i * 20, y + petal * 20, z);
    //         leds[ledIndex + i] = CHSV(noise, 255, 255);
    //     }

    //     ledIndex += numLedsInPetal;
    // }

    // x += 2; // Adjust the increments for desired effect
    // y += 1;
    // z += 1;
}

void animationFour()
{
    static uint8_t sparkleBrightness[NUM_LEDS] = {0};
    static uint8_t hueBase = 180;
    static uint8_t t = 0;
    int ledIndex = 0;

    // --- Stem: drifting sparkles upward ---
    for (int i = 0; i < STEM_LEDS; i++)
    {
        if (random8() < 4 && sparkleBrightness[i] == 0)
        {
            sparkleBrightness[i] = 180 + random8(75);
        }

        if (sparkleBrightness[i] > 0)
        {
            leds[ledIndex] = CHSV(hueBase + i, 100, sparkleBrightness[i]);
            sparkleBrightness[i] = qsub8(sparkleBrightness[i], 4);
        }
        else
        {
            leds[ledIndex] = CRGB::Black;
        }

        ledIndex++;
    }

    // --- Petals: bioluminescent twinkles ---
    for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    {
        int numLedsInPetal = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

        for (int i = 0; i < numLedsInPetal; i++)
        {
            if (random8() < 5 && sparkleBrightness[ledIndex] == 0)
            {
                sparkleBrightness[ledIndex] = 200 + random8(55);
            }

            if (sparkleBrightness[ledIndex] > 0)
            {
                leds[ledIndex] = CHSV(hueBase + sin8(t + petal * 10), 150, sparkleBrightness[ledIndex]);
                sparkleBrightness[ledIndex] = qsub8(sparkleBrightness[ledIndex], 3);
            }
            else
            {
                leds[ledIndex] = CRGB::Black;
            }

            ledIndex++;
        }
    }

    t += 1;
    hueBase += 1;

    // static uint8_t sparkleBrightness[NUM_LEDS] = {0};
    // static uint8_t hueBase = 180;
    // static uint8_t t = 0;
    // int ledIndex = 0;

    // // --- Stem: drifting sparkles upward ---
    // for (int i = 0; i < STEM_LEDS; i++)
    // {
    //     if (random8() < 4 && sparkleBrightness[ledIndex] == 0)
    //     {
    //         sparkleBrightness[ledIndex] = 180 + random8(75);
    //     }

    //     if (sparkleBrightness[ledIndex] > 0)
    //     {
    //         leds[ledIndex] = CHSV(hueBase + i, 100, sparkleBrightness[ledIndex]);
    //         sparkleBrightness[ledIndex] = qsub8(sparkleBrightness[ledIndex], 4);
    //     }
    //     else
    //     {
    //         leds[ledIndex] = CRGB::Black;
    //     }

    //     ledIndex++;
    // }

    // // --- Petals: emulate bud-style radial glow ---
    // float centerX = NUM_LEDS_LARGE_PETAL / 2.0;
    // float centerY = (NUM_SMALL_PETALS + NUM_LARGE_PETALS) / 2.0;

    // for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    // {
    //     int numLeds = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

    //     for (int i = 0; i < numLeds; i++)
    //     {
    //         float x = i;
    //         float y = petal;

    //         // stretch x for circular layout approximation
    //         x *= (NUM_SMALL_PETALS + NUM_LARGE_PETALS) / (float)NUM_LEDS_LARGE_PETAL;

    //         float dx = x - centerX;
    //         float dy = y - centerY;
    //         float dist = sqrtf(dx * dx + dy * dy);

    //         uint8_t wave = sin8(t + dist * 30);
    //         uint8_t glow = qadd8(wave / 2, sparkleBrightness[ledIndex] / 2);

    //         leds[ledIndex] = CHSV(hueBase + wave / 3, 180, glow);
    //         sparkleBrightness[ledIndex] = qsub8(sparkleBrightness[ledIndex], 2);

    //         ledIndex++;
    //     }
    // }

    // t += 1;
    // hueBase += 1;

    // // Set stem LEDs
    // for (int i = 0; i < STEM_LEDS; i++)
    // {
    //     leds[i] = CHSV(128, 255, 255); // Greenish-blue (teal)
    // }

    // static uint8_t brightness = 128; // Start with a mid-level brightness
    // static int8_t direction = 1;

    // // Update brightness
    // brightness += direction;
    // if (brightness == 100 || brightness == 255)
    // {                           // Adjust the range for lighter and darker blue
    //     direction = -direction; // Reverse direction at the ends
    // }

    // int ledIndex = STEM_LEDS; // Start after the stems and leaves

    // // Handle petals
    // for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    // {
    //     int numLedsInPetal = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

    //     for (int i = 0; i < numLedsInPetal; i++)
    //     {
    //         leds[ledIndex + i] = CHSV(160, 255, brightness); // Use a fixed hue for a consistent color
    //     }

    //     ledIndex += numLedsInPetal;
    // }
}

void animationFive()
{
    // Set stem LEDs
    for (int i = 0; i < STEM_LEDS; i++)
    {
        leds[i] = CHSV(64, 255, 255); // Natural yellow
    }

    static uint16_t x = 0;
    static uint16_t y = 0;
    int ledIndex = STEM_LEDS; // Start after the stems and leaves

    // Calculate total number of LEDs in petals
    int totalPetalLeds = (NUM_SMALL_PETALS * NUM_LEDS_SMALL_PETAL) + (NUM_LARGE_PETALS * NUM_LEDS_LARGE_PETAL);

    for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    {
        int numLedsInPetal = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

        for (int i = 0; i < numLedsInPetal; i++)
        {
            // Use a continuous noise pattern that spans across all petals
            uint8_t noise = inoise8(x + (ledIndex - STEM_LEDS + i) * 20, y);
            leds[ledIndex + i] = CHSV(noise, 255, 255);
        }

        ledIndex += numLedsInPetal;
    }

    x += 2; // Increase the increment to speed up the animation
    y += 2;
}

void animationSix()
{
    // --- Stem: static teal ---
    for (int i = 0; i < STEM_LEDS; i++)
    {
        leds[i] = CHSV(128, 255, 255);
    }

    // --- Pulsing brightness state ---
    static uint8_t brightness = 128;
    static int8_t direction = 1;

    brightness += direction;
    if (brightness <= 100 || brightness >= 255)
    {
        direction = -direction;
    }

    int ledIndex = STEM_LEDS;

    // --- Petals: pulse with soft blue-violet hue ---
    for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    {
        int numLedsInPetal = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

        for (int i = 0; i < numLedsInPetal; i++)
        {
            leds[ledIndex++] = CHSV(160, 255, brightness); // blue/violet tone
        }
    }

    // // Set stem LEDs
    // for (int i = 0; i < STEM_LEDS; i++)
    // {
    //     leds[i] = CHSV(64, 255, 255); // Natural yellow
    // }

    // static uint16_t x = 0;
    // static uint16_t y = 0;
    // int ledIndex = STEM_LEDS; // Start after the stems and leaves

    // // Calculate total number of LEDs in petals
    // int totalPetalLeds = (NUM_SMALL_PETALS * NUM_LEDS_SMALL_PETAL) + (NUM_LARGE_PETALS * NUM_LEDS_LARGE_PETAL);

    // for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    // {
    //     int numLedsInPetal = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

    //     for (int i = 0; i < numLedsInPetal; i++)
    //     {
    //         // Use a continuous noise pattern that spans across all petals
    //         uint8_t noise = inoise8(x + (ledIndex - STEM_LEDS) * 20, y);
    //         leds[ledIndex + i] = CHSV(noise, 255, 255);
    //     }

    //     ledIndex += numLedsInPetal;
    // }

    // x += 2; // Increase the increment to speed up the animation
    // y += 2;
}

void animationSeven()
{
    static uint8_t frameCounter = 0;
    frameCounter++;
    if (frameCounter % 2 != 0)
        return; // update every other frame

    // --- Stem: gentle upward wave ---
    static uint8_t stem_phase = 0;
    for (int i = 0; i < STEM_LEDS; i++)
    {
        uint8_t wave = (i * 8 + stem_phase) % 256;
        uint8_t brightness = 180 - abs8(sin8(wave) - 128);
        uint8_t hue = 96 + (i % 3);
        leds[i] = CHSV(hue, 200, brightness);
    }
    stem_phase += 2;

    // --- Petals: radial hue wheel like the original bud ---
    static uint8_t hueShift = 0;
    int ledIndex = STEM_LEDS;
    float centerX = NUM_LEDS_LARGE_PETAL / 2.0;
    float centerY = (NUM_SMALL_PETALS + NUM_LARGE_PETALS) / 2.0;

    for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    {
        int numLedsInPetal = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

        for (int i = 0; i < numLedsInPetal; i++)
        {
            float x = i;
            float y = petal;

            x *= (NUM_SMALL_PETALS + NUM_LARGE_PETALS) / (float)NUM_LEDS_LARGE_PETAL;
            float dx = x - centerX;
            float dy = y - centerY;

            float angle = atan2f(dy, dx);
            if (angle < 0)
                angle += 2 * PI;

            uint8_t hue = mapf(angle, 0, 2 * PI, hueShift, hueShift + 255);
            leds[ledIndex++] = CHSV(hue, 255, 255);
        }
    }

    hueShift++;
    // // Set stem LEDs
    // for (int i = 0; i < STEM_LEDS; i++)
    // {
    //     leds[i] = CRGB::Green;
    // }

    // static uint16_t x = 0;
    // static uint16_t y = 0;
    // int ledIndex = STEM_LEDS; // Start after the stems and leaves

    // for (int petal = 0; petal < 20; petal++)
    // {
    //     int numLedsInPetal = (petal % 2 == 0) ? 11 : 13; // Small petals have 11 LEDs, large petals have 13 LEDs

    //     for (int i = 0; i < numLedsInPetal; i++)
    //     {
    //         uint8_t noise = inoise8(x + (numLedsInPetal - i) * 20, y + petal * 20);
    //         leds[ledIndex + i] = CHSV(noise, 255, 255);
    //     }

    //     ledIndex += numLedsInPetal;
    // }

    // x += 2; // Increase the increment to speed up the animation
    // y += 2;
}

void animationEight()
{
    static uint8_t hueShift = 0;
    static CRGB previousLeds[NUM_LEDS];

    // --- Stem: bouncing hue ball ---
    static float stemBallY = 0;
    static int8_t stemBallDY = 1;
    static uint8_t stemHueShift = 0;

    stemBallY += stemBallDY;
    if (stemBallY <= 0 || stemBallY >= STEM_LEDS - 1)
        stemBallDY = -stemBallDY;

    for (int i = 0; i < STEM_LEDS; i++)
    {
        float dist = abs((STEM_LEDS - 1 - i) - stemBallY);
        uint8_t hue = sin8(stemHueShift + dist * 10);
        leds[i] = CHSV(hue, 255, 255);
    }

    stemHueShift++;

    // --- Petals: bouncing ball across grid ---
    static float ballX = 0;
    static float ballY = 0;
    static int8_t ballDX = 1;
    static int8_t ballDY = 1;

    // Estimate bounds
    const float maxPetalY = NUM_SMALL_PETALS + NUM_LARGE_PETALS - 1;
    const float maxPetalX = NUM_LEDS_LARGE_PETAL - 1;

    ballX += ballDX;
    ballY += ballDY;

    if (ballX <= 0 || ballX >= maxPetalX)
        ballDX = -ballDX;
    if (ballY <= 0 || ballY >= maxPetalY)
        ballDY = -ballDY;

    float aspectRatio = maxPetalY / maxPetalX;

    int ledIndex = STEM_LEDS;
    int petalOffset = 0;

    for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    {
        int numLedsInPetal = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

        for (int i = 0; i < numLedsInPetal; i++)
        {
            float x = i * aspectRatio;
            float y = petal;

            float dist = sqrtf(sq(x - ballX) + sq(y - ballY));
            uint8_t hue = sin8(hueShift + dist * 8);

            CHSV hsv = CHSV(hue, 255, 255);
            CRGB rgb;
            hsv2rgb_spectrum(hsv, rgb);

            leds[ledIndex] = blend(previousLeds[ledIndex], rgb, 128);
            previousLeds[ledIndex] = rgb;

            ledIndex++;
            petalOffset++;
        }
    }

    hueShift++;
    delay(100); // slow the bounce

    // // Set stem LEDs
    // for (int i = 0; i < STEM_LEDS; i++)
    // {
    //     leds[i] = CRGB::Green;
    // }

    // static uint8_t hueShift = 0;
    // int ledIndex = STEM_LEDS; // Start after the stems and leaves

    // for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    // {
    //     int numLedsInPetal = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;
    //     float centerX = numLedsInPetal / 2.0; // Center of the petal

    //     for (int i = 0; i < numLedsInPetal; i++)
    //     {
    //         float distance = abs(i - centerX);                                // Distance from the center of the petal
    //         uint8_t hue = map(distance, 0, centerX, hueShift, hueShift + 64); // Adjust hue based on distance

    //         leds[ledIndex + i] = CHSV(hue, 255, 255);
    //     }

    //     ledIndex += numLedsInPetal;
    // }

    // hueShift += 1; // Increment hue shift for the next frame
}

void animationNine()
{
    // Set stem LEDs
    for (int i = 0; i < STEM_LEDS; i++)
    {
        leds[i] = CRGB::Green;
    }

    static uint8_t hueShift = 0;
    static CRGB previousLeds[NUM_LEDS];
    static int8_t ballDX = 1; // Change in ball's x position
    static int8_t ballDY = 1; // Change in ball's y position
    static float ballX = 0;
    static float ballY = 0;

    // Update the ball position
    ballX += ballDX;
    ballY += ballDY;

    // If the ball hits the edge of the display, reverse direction
    if (ballX <= 0 || ballX >= NUM_LEDS_LARGE_PETAL - 1)
    {
        ballDX = -ballDX;
    }
    if (ballY <= 0 || ballY >= NUM_LARGE_PETALS - 1)
    {
        ballDY = -ballDY;
    }

    float aspectRatio = NUM_LARGE_PETALS / (float)NUM_LEDS_LARGE_PETAL;

    int ledIndex = STEM_LEDS; // Start after the stems and leaves

    for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    {
        int numLedsInPetal = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

        for (int i = 0; i < numLedsInPetal; i++)
        {
            float x = i;
            float y = petal;

            // Adjust the x distance for the aspect ratio
            x *= aspectRatio;

            // Calculate the distance from the ball position
            float distance = sqrt(sq(x - ballX) + sq(y - ballY));

            // Calculate the hue based on the distance
            uint8_t hue = sin8(hueShift + distance * 8);

            // Create the new color
            CHSV newColor = CHSV(hue, 255, 255);

            // Convert the new color to CRGB
            CRGB newColorRgb;
            hsv2rgb_spectrum(newColor, newColorRgb);

            // Interpolate between the previous color and the new color
            CRGB interpolatedColor = blend(previousLeds[ledIndex + i], newColorRgb, 128);

            leds[ledIndex + i] = interpolatedColor;

            // Store the new color for the next frame
            previousLeds[ledIndex + i] = newColorRgb;
        }

        ledIndex += numLedsInPetal;
    }

    hueShift += 1;
    delay(100);
}

void animationTen()
{
    static uint8_t dropIntensity[NUM_LEDS] = {0};

    // --- Fill the whole flower with base purple ---
    fill_solid(leds, NUM_LEDS, CRGB::Purple);

    // --- Spawn a few random golden drops ---
    for (uint8_t i = 0; i < NUM_FLIES; i++)
    {
        uint16_t led = random16(NUM_LEDS);
        dropIntensity[led] = 255;
    }

    // --- Apply and fade drops ---
    for (uint16_t i = 0; i < NUM_LEDS; i++)
    {
        if (dropIntensity[i] > 0)
        {
            leds[i] = CHSV(42, 255, dropIntensity[i]); // golden sparkle
            dropIntensity[i] = qsub8(dropIntensity[i], FADE_RATE);
        }
    }

    delay(100); // Slow it down for visible sparkles

    // // Set stem LEDs
    // for (int i = 0; i < STEM_LEDS; i++)
    // {
    //     leds[i] = CRGB::Green;
    // }

    // static uint8_t dropIntensity[NUM_LEDS] = {0};

    // // Set the background to purple
    // fill_solid(leds, NUM_LEDS, CRGB::Purple);

    // // Randomly select LEDs to light up in yellow
    // for (uint8_t i = 0; i < NUM_FLIES; i++)
    // {
    //     uint16_t led = random16(NUM_LEDS);
    //     dropIntensity[led] = 255; // Set the intensity of the drop to the maximum
    // }

    // // Update the LEDs
    // for (uint16_t i = 0; i < NUM_LEDS; i++)
    // {
    //     if (dropIntensity[i] > 0)
    //     {
    //         leds[i] = CHSV(42, 255, dropIntensity[i]); // 42 is the hue for yellow in the HSV color space
    //         dropIntensity[i] -= FADE_RATE;
    //     }
    // }
}

void animationEleven()
{
    static uint16_t t = 0;
    int ledIndex = 0;

    // --- Stem: vertical noise wave ---
    for (int i = 0; i < STEM_LEDS; i++)
    {
        uint8_t noise = inoise8(0, i * NOISE_SCALE, t);
        leds[ledIndex++] = CHSV(noise, 255, 200);
    }

    // --- Petals: 2D Perlin noise using grid mapping ---
    for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    {
        int numLedsInPetal = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

        for (int i = 0; i < numLedsInPetal; i++)
        {
            uint8_t x = i * NOISE_SCALE;
            uint8_t y = petal * NOISE_SCALE;
            uint8_t noise = inoise8(x, y, t);
            leds[ledIndex++] = CHSV(noise, 255, 255);
        }
    }

    t += BRAIN_WAVES_SPEED;
    // // Set stem LEDs
    // for (int i = 0; i < STEM_LEDS; i++)
    // {
    //     leds[i] = CRGB::Green;
    // }

    // static uint16_t t = 0;
    // static uint8_t hueShift = 0;
    // int ledIndex = STEM_LEDS; // Start after the stems and leaves

    // for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    // {
    //     int numLedsInPetal = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

    //     for (int i = 0; i < numLedsInPetal; i++)
    //     {
    //         // Calculate the x and y coordinates for the current LED
    //         uint16_t x = i;
    //         uint16_t y = petal;

    //         // Calculate the noise value based on Perlin noise
    //         uint8_t noise = inoise8(x * BRAIN_WAVES_NOISE_SCALE, y * BRAIN_WAVES_NOISE_SCALE, t);

    //         // Calculate the hue based on the noise value and add a unique offset for each LED
    //         uint8_t hue = noise + i;

    //         // Set the brightness to a constant value
    //         uint8_t brightness = 255;

    //         // Set the color of the current LED
    //         leds[ledIndex + i] = CHSV(hue, 255, brightness);
    //     }

    //     ledIndex += numLedsInPetal;
    // }

    // // Increment the time variable to create animation
    // t += BRAIN_WAVES_SPEED;
    // hueShift += 1;
}

void animationTwelve()
{
    static uint8_t hueShift = 0;
    static uint16_t stemOffset = 0;
    int ledIndex = 0;

    // --- Stem Animation ---
    for (int i = 0; i < STEM_LEDS; i++)
    {
        int reversedIndex = STEM_LEDS - 1 - i;
        uint8_t hue = sin8(stemOffset + reversedIndex * 8);
        leds[ledIndex++] = CHSV(hue, 255, 255);
    }
    stemOffset += 2;

    // --- Petals Animation: Radial / Angular color effect ---
    float centerX = NUM_LEDS_LARGE_PETAL / 2.0;
    float centerY = (NUM_SMALL_PETALS + NUM_LARGE_PETALS) / 2.0;

    for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    {
        int numLedsInPetal = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

        for (int i = 0; i < numLedsInPetal; i++)
        {
            float x = i;
            float y = petal;
            x *= (NUM_SMALL_PETALS + NUM_LARGE_PETALS) / (float)NUM_LEDS_LARGE_PETAL;

            float dx = x - centerX;
            float dy = y - centerY;
            float angle = atan2f(dy, dx);
            if (angle < 0)
                angle += 2 * PI;

            float radius = sqrtf(dx * dx + dy * dy);
            uint8_t baseHue = hueShift;
            float hueMod = angle * 20.0 - radius * 8.0;
            uint8_t hue = baseHue + (int)hueMod;
            hue = sin8(hue); // Reintroduce sine *after* stable mapping
            leds[ledIndex++] = CHSV(hue, 255, 255);
        }
    }

    hueShift++;

    // // Set stem LEDs
    // for (int i = 0; i < STEM_LEDS; i++)
    // {
    //     leds[i] = CRGB::Green;
    // }

    // static uint8_t hueShift = 0;
    // static uint8_t speed = 1; // *new og: 2
    // int ledIndex = STEM_LEDS; // Start after the stems and leaves

    // float centerX = NUM_LEDS_LARGE_PETAL / 2.0;
    // float centerY = (NUM_SMALL_PETALS + NUM_LARGE_PETALS) / 2.0;

    // for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    // {
    //     int numLedsInPetal = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

    //     for (int i = 0; i < numLedsInPetal; i++)
    //     {
    //         float x = i;
    //         float y = petal;

    //         // Calculate the angle of the LED relative to the center
    //         float angle = atan2(y - centerY, x - centerX);

    //         // Adjust the angle to be in the range 0 to 2π
    //         if (angle < 0)
    //         {
    //             angle += 2 * PI;
    //         }

    //         // Map the angle to a hue
    //         uint8_t hue = map(angle, 0, 2 * PI, hueShift, hueShift + 255);

    //         // Set the color of the current LED
    //         leds[ledIndex + i] = CHSV(hue, 255, 255);
    //     }

    //     ledIndex += numLedsInPetal;
    // }

    // // Increment the hue shift for the next frame
    // hueShift += speed;
}

void animationThirteen()
{
    // Set stem LEDs
    for (int i = 0; i < STEM_LEDS; i++)
    {
        leds[i] = CRGB::Green;
    }

    static uint8_t hueShift = 0;
    static uint8_t speed = 1; // *new og: 2
    int ledIndex = STEM_LEDS; // Start after the stems and leaves

    float centerX = NUM_LEDS_LARGE_PETAL / 2.0;
    float centerY = (NUM_SMALL_PETALS + NUM_LARGE_PETALS) / 2.0;

    for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    {
        int numLedsInPetal = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

        for (int i = 0; i < numLedsInPetal; i++)
        {
            float x = i;
            float y = petal;

            // Calculate the angle of the LED relative to the center
            float angle = atan2(y - centerY, x - centerX);

            // Adjust the angle to be in the range 0 to 2π
            if (angle < 0)
            {
                angle += 2 * PI;
            }

            // Map the angle to a hue, adding the hueShift to create a rotating effect
            uint8_t hue = map(angle, 0, 2 * PI, 0, 255) + hueShift;

            // Set the color of the current LED
            leds[ledIndex + i] = CHSV(hue, 255, 255);
        }

        ledIndex += numLedsInPetal;
    }

    // Increment the hueShift every 10 milliseconds to create a smoother spinning effect
    EVERY_N_MILLISECONDS(10)
    {
        hueShift += speed;
    }
}

void animationFourteen()
{
    static uint8_t hueShift = 0;
    static float t = 0;

    float centerX = NUM_LEDS_LARGE_PETAL / 2.0;
    float centerY = (NUM_SMALL_PETALS + NUM_LARGE_PETALS) / 2.0;

    int ledIndex = 0;

    // --- Stem: radiating ripple upward from center ---
    for (int i = 0; i < STEM_LEDS; i++)
    {
        float x = centerX;
        float y = centerY - 4.0 - ((float)i / STEM_LEDS) * 10.0;

        float dx = x - centerX;
        float dy = y - centerY;
        float angle = atan2f(dy, dx);
        if (angle < 0)
            angle += 2 * PI;

        float dist = sqrtf(dx * dx + dy * dy);
        float wave = sinf(dist - t);
        uint8_t hue = ((angle + wave) * 255.0 / (2 * PI)) + hueShift;

        leds[ledIndex++] = CHSV(hue, 255, 255);
    }

    // --- Petals: radial hue ripples ---
    for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    {
        int numLeds = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

        for (int i = 0; i < numLeds; i++)
        {
            float x = i;
            float y = petal;

            // Scale x for a roughly circular grid
            x *= (NUM_SMALL_PETALS + NUM_LARGE_PETALS) / (float)NUM_LEDS_LARGE_PETAL;

            float dx = x - centerX;
            float dy = y - centerY;
            float angle = atan2f(dy, dx);
            if (angle < 0)
                angle += 2 * PI;

            float dist = sqrtf(dx * dx + dy * dy);
            float wave = sinf(dist - t);
            uint8_t hue = ((angle + wave) * 255.0 / (2 * PI)) + hueShift;

            leds[ledIndex++] = CHSV(hue, 255, 255);
        }
    }

    t += 0.07;
    hueShift += 1;
    // // Set stem LEDs
    // for (int i = 0; i < STEM_LEDS; i++)
    // {
    //     leds[i] = CHSV(128, 255, 255); // Greenish-blue (teal)
    // }

    // static uint8_t hueShift = 0;
    // static uint8_t speed = 1; // *new og: 2
    // int ledIndex = STEM_LEDS; // Start after the stems and leaves

    // float centerX = NUM_LEDS_LARGE_PETAL / 2.0;
    // float centerY = (NUM_SMALL_PETALS + NUM_LARGE_PETALS) / 2.0;

    // for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    // {
    //     int numLedsInPetal = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

    //     for (int i = 0; i < numLedsInPetal; i++)
    //     {
    //         float x = i;
    //         float y = petal;

    //         // Calculate the angle of the LED relative to the center
    //         float angle = atan2(y - centerY, x - centerX);

    //         // Adjust the angle to be in the range 0 to 2π
    //         if (angle < 0)
    //         {
    //             angle += 2 * PI;
    //         }

    //         // Calculate the distance of the LED from the center
    //         float distance = sqrt(sq(x - centerX) + sq(y - centerY));

    //         // Adjust the phase of the hue based on the distance
    //         uint8_t hue = (angle + sin(distance) * 2 * PI) * 255 / (2 * PI) + hueShift;

    //         // Set the color of the current LED
    //         leds[ledIndex + i] = CHSV(hue, 255, 255);
    //     }

    //     ledIndex += numLedsInPetal;
    // }

    // // Increment the hueShift to create the rotating effect
    // hueShift += speed;
}

void animationFifteen()
{
    static uint8_t hueShift = 0;
    static float time = 0;

    float centerX = NUM_LEDS_LARGE_PETAL / 2.0;
    float centerY = (NUM_SMALL_PETALS + NUM_LARGE_PETALS) / 2.0;
    int ledIndex = 0;

    // --- Stem: spiral projection beneath flower center ---
    for (int i = 0; i < STEM_LEDS; i++)
    {
        float x = centerX;
        float y = centerY - 4.0 - ((float)i / STEM_LEDS) * 10.0;

        float dx = x - centerX;
        float dy = y - centerY;
        float angle = atan2f(dy, dx);
        if (angle < 0)
            angle += 2 * PI;
        float radius = sqrtf(dx * dx + dy * dy);

        float swirl = angle + time * 0.5 - radius * 0.25;
        uint8_t hue = sin8(swirl * 128.0 / PI + hueShift);
        leds[ledIndex++] = CHSV(hue, 255, 200);
    }

    // --- Petals: swirling hue spiral around center ---
    for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    {
        int numLeds = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

        for (int i = 0; i < numLeds; i++)
        {
            float x = i;
            float y = petal;

            // Scale x to match layout proportions
            x *= (NUM_SMALL_PETALS + NUM_LARGE_PETALS) / (float)NUM_LEDS_LARGE_PETAL;

            float dx = x - centerX;
            float dy = y - centerY;
            float angle = atan2f(dy, dx);
            if (angle < 0)
                angle += 2 * PI;
            float radius = sqrtf(dx * dx + dy * dy);

            float swirl = angle + time * 0.5 - radius * 0.25;
            uint8_t hue = sin8(swirl * 128.0 / PI + hueShift);
            leds[ledIndex++] = CHSV(hue, 255, 255);
        }
    }

    hueShift += 1;
    time += 0.05;

    // // Set stem LEDs
    // for (int i = 0; i < STEM_LEDS; i++)
    // {
    //     leds[i] = CHSV(128, 255, 255); // Greenish-blue (teal)
    // }

    // static const uint8_t exp_gamma[256] = {
    //     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1,
    //     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    //     1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3,
    //     4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 6, 6, 6, 7, 7,
    //     7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 10, 11, 11, 12, 12,
    //     12, 13, 13, 14, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 19,
    //     19, 20, 20, 21, 21, 22, 23, 23, 24, 24, 25, 26, 26, 27, 28,
    //     28, 29, 30, 30, 31, 32, 32, 33, 34, 35, 35, 36, 37, 38, 39,
    //     39, 40, 41, 42, 43, 44, 44, 45, 46, 47, 48, 49, 50, 51, 52,
    //     53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67,
    //     68, 70, 71, 72, 73, 74, 75, 77, 78, 79, 80, 82, 83, 84, 85,
    //     87, 89, 91, 92, 93, 95, 96, 98, 99, 100, 101, 102, 105, 106, 108,
    //     109, 111, 112, 114, 115, 117, 118, 120, 121, 123, 125, 126, 128, 130, 131,
    //     133, 135, 136, 138, 140, 142, 143, 145, 147, 149, 151, 152, 154, 156, 158,
    //     160, 162, 164, 165, 167, 169, 171, 173, 175, 177, 179, 181, 183, 185, 187,
    //     190, 192, 194, 196, 198, 200, 202, 204, 207, 209, 211, 213, 216, 218, 220,
    //     222, 225, 227, 229, 232, 234, 236, 239, 241, 244, 246, 249, 251, 253, 254,
    //     255};
    // int a = millis() / 32;
    // int ledIndex = STEM_LEDS; // Start after the stems and leaves

    // for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    // {
    //     int numLedsInPetal = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

    //     for (int i = 0; i < numLedsInPetal; i++)
    //     {
    //         float x = i;
    //         float y = petal;

    //         leds[ledIndex + i].b = exp_gamma[sin8((x - 8) * cos8((y + 20) * 4) / 4 + a)];
    //         leds[ledIndex + i].g = exp_gamma[(sin8(x * 16 + a / 3) + cos8(y * 8 + a / 2)) / 2];
    //         leds[ledIndex + i].r = exp_gamma[sin8(cos8(x * 8 + a / 3) + sin8(y * 8 + a / 4) + a)];
    //     }

    //     ledIndex += numLedsInPetal;
    // }

    // FastLED.show();
}

void animationSixteen()
{
    static uint16_t x = 0;
    static uint16_t y = 0;

    // --- Stem: flowing noise ---
    for (int i = 0; i < STEM_LEDS; i++)
    {
        uint8_t noise = inoise8(x + i * 20, y);
        leds[i] = CHSV(noise, 255, 255);
    }

    // --- Petals: flowing noise using flat offset ---
    int ledIndex = STEM_LEDS;
    int petalOffset = 0;

    for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    {
        int numLedsInPetal = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

        for (int i = 0; i < numLedsInPetal; i++)
        {
            uint8_t noise = inoise8(x + petalOffset * 20, y);
            leds[ledIndex++] = CHSV(noise, 255, 255);
            petalOffset++;
        }
    }

    x += 2;
    y += 2;

    // // Set stem LEDs
    // for (int i = 0; i < STEM_LEDS; i++)
    // {
    //     leds[i] = CHSV(128, 255, 255); // Greenish-blue (teal)
    // }

    // const int SCALE = 125;   // Increase the scale for more variation in the noise *new og: 50
    // const float SPEED = 0.5; // Increase the speed for faster motion *new og: 0.05
    // static uint16_t gHue = 0;

    // // Increment the time variable
    // gHue++;

    // int ledIndex = STEM_LEDS; // Start after the stems and leaves

    // for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    // {
    //     int numLedsInPetal = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

    //     for (int i = 0; i < numLedsInPetal; i++)
    //     {
    //         float x = i;
    //         float y = petal;

    //         // Calculate a noise value based on the LED's position and the current time
    //         uint8_t noise = inoise8(x * SCALE, y * SCALE, gHue * SPEED);

    //         // Map the noise value to a hue value
    //         uint8_t hue = noise + gHue; // Add gHue to the noise to get more color variation

    //         // Set the LED's color based on the hue value
    //         leds[ledIndex + i] = CHSV(hue, 128, 128); // Reduce saturation and brightness by half
    //     }

    //     ledIndex += numLedsInPetal;
    // }

    // // Show the updated LED colors
    // FastLED.show();
}

void animationSeventeen()
{
    static uint8_t dropIntensity[NUM_LEDS] = {0};
    static uint8_t ledHue[NUM_LEDS];

    fill_solid(leds, NUM_LEDS, CRGB::Black); // Clear the background

    // --- Spawn random sparkles ---
    for (uint8_t i = 0; i < NUM_DOTS; i++)
    {
        uint16_t led = random16(NUM_LEDS);
        if (dropIntensity[led] == 0)
        {
            ledHue[led] = random8(); // Assign a random hue
        }
        dropIntensity[led] = 255;
    }

    // --- Apply sparkle effect across stem + petals ---
    for (uint16_t i = 0; i < NUM_LEDS; i++)
    {
        if (dropIntensity[i] > 0)
        {
            leds[i] = CHSV(ledHue[i], 255, dropIntensity[i]);
            dropIntensity[i] = qsub8(dropIntensity[i], DOTS_FADE_RATE);
        }
    }

    FastLED.show();
    // // Set stem LEDs
    // for (int i = 0; i < STEM_LEDS; i++)
    // {
    //     leds[i] = CHSV(64, 255, 255); // Natural yellow
    // }

    // static uint8_t dropIntensity[NUM_LEDS] = {0};
    // static uint8_t ledHue[NUM_LEDS]; // The hue of each LED

    // // Set the background to black
    // fill_solid(leds, NUM_LEDS, CRGB::Black);

    // // Randomly select LEDs to light up in a random color
    // for (uint8_t i = 0; i < NUM_DOTS; i++)
    // {
    //     uint16_t led = random16(NUM_LEDS);
    //     if (dropIntensity[led] == 0) // Only assign a color if the LED is not already a fly
    //     {
    //         ledHue[led] = random8(); // Random initial hue
    //     }
    //     dropIntensity[led] = 255; // Set the intensity of the drop to the maximum
    // }

    // // Update the LEDs
    // for (uint16_t i = 0; i < NUM_LEDS; i++)
    // {
    //     if (dropIntensity[i] > 0)
    //     {
    //         leds[i] = CHSV(ledHue[i], 255, dropIntensity[i]); // Use the hue of the corresponding LED
    //         dropIntensity[i] -= DOTS_FADE_RATE;
    //     }
    // }

    // FastLED.show();
}

void animationEighteen()
{
    static uint16_t t = 0;
    float scale = 0.1;  // spatial frequency
    float speed = 0.05; // animation speed

    int ledIndex = 0;

    // --- Stem: linear sine wave along stem length ---
    for (int i = 0; i < STEM_LEDS; i++)
    {
        float x = i * scale;
        uint8_t hue = 128 + 127 * sinf(x + t * speed);
        leds[ledIndex++] = CHSV(hue, 255, 255);
    }

    // --- Petals: 2D radial sine plasma effect ---
    float centerX = NUM_LEDS_LARGE_PETAL / 2.0;
    float centerY = (NUM_SMALL_PETALS + NUM_LARGE_PETALS) / 2.0;

    for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    {
        int numLedsInPetal = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

        for (int i = 0; i < numLedsInPetal; i++)
        {
            float x = i;
            float y = petal;

            // Stretch for pseudo-circular symmetry
            x *= (NUM_SMALL_PETALS + NUM_LARGE_PETALS) / (float)NUM_LEDS_LARGE_PETAL;

            float dx = x - centerX;
            float dy = y - centerY;
            float distance = sqrtf(dx * dx + dy * dy) * scale;

            uint8_t hue = 128 + 127 * sinf(distance + t * speed);
            leds[ledIndex++] = CHSV(hue, 255, 255);
        }
    }

    t++;

    // // Set stem LEDs
    // for (int i = 0; i < STEM_LEDS; i++)
    // {
    //     leds[i] = CHSV(64, 255, 255); // Natural yellow
    // }

    // static uint8_t start = 0;
    // start += 1;
    // int ledIndex = STEM_LEDS; // Start after the stems and leaves

    // for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    // {
    //     int numLedsInPetal = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

    //     for (int i = 0; i < numLedsInPetal; i++)
    //     {
    //         uint8_t index = (sin8(i + start) + sin8(i * 16 + start)) / 2; // Average of two sine waves
    //         CHSV hsv = CHSV(index, 255, 255);
    //         leds[ledIndex + i] = hsv;
    //     }

    //     ledIndex += numLedsInPetal;
    // }

    // FastLED.show();
}

void animationNineteen()
{
    static float t = 0.0;

    for (int i = 0; i < NUM_LEDS; i++)
    {
        float x, y;
        getLEDCoordinates(i, x, y);

        float nx = x * 0.2;
        float ny = y * 0.2;

        float value =
            sin(nx * 10 + t) +
            sin(10 * (nx * sin(t / 2.0) + ny * cos(t / 3.0)) + t) +
            sin(sqrt(100 * (nx * nx + ny * ny) + 1) + t);

        uint8_t hue = (uint8_t)(fabs(sin(value * PI)) * 255);
        leds[i] = CHSV(hue, 255, 255);
    }

    t += 0.03;
    // // Set stem LEDs
    // for (int i = 0; i < STEM_LEDS; i++)
    // {
    //     leds[i] = CHSV(64, 255, 255); // Natural yellow
    // }

    // static uint8_t hueShift = 0;
    // int ledIndex = STEM_LEDS; // Start after the stems and leaves

    // float centerX = NUM_LEDS_LARGE_PETAL / 2.0;
    // float centerY = (NUM_SMALL_PETALS + NUM_LARGE_PETALS) / 2.0;

    // for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    // {
    //     int numLedsInPetal = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

    //     for (int i = 0; i < numLedsInPetal; i++)
    //     {
    //         float x = i;
    //         float y = petal;

    //         // Calculate the distance from the center
    //         float distance = sqrt(sq(x - centerX) + sq(y - centerY));

    //         // Map the distance to a hue
    //         uint8_t hue = map(distance, 0, NUM_LEDS_LARGE_PETAL / 2.0, hueShift, hueShift + 255);

    //         // Set the color of the current LED
    //         leds[ledIndex + i] = CHSV(hue, 255, 255);
    //     }

    //     ledIndex += numLedsInPetal;
    // }

    // // Increment the hue shift for the next frame
    // hueShift += 1;
}

void animationTwenty()
{
    static uint8_t hueShift = 0;
    static uint16_t stemOffset = 0;
    int ledIndex = 0;

    // --- Stem Animation ---
    for (int i = 0; i < STEM_LEDS; i++)
    {
        int reversedIndex = STEM_LEDS - 1 - i;
        uint8_t hue = sin8(stemOffset + reversedIndex * 8);
        leds[ledIndex++] = CHSV(hue, 255, 255);
    }
    stemOffset += 2;

    // --- Petals Animation: Radial / Angular color effect ---
    float centerX = NUM_LEDS_LARGE_PETAL / 2.0;
    float centerY = (NUM_SMALL_PETALS + NUM_LARGE_PETALS) / 2.0;

    for (int petal = 0; petal < NUM_SMALL_PETALS + NUM_LARGE_PETALS; petal++)
    {
        int numLedsInPetal = (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;

        for (int i = 0; i < numLedsInPetal; i++)
        {
            float x = i;
            float y = petal;
            x *= (NUM_SMALL_PETALS + NUM_LARGE_PETALS) / (float)NUM_LEDS_LARGE_PETAL;

            float dx = x - centerX;
            float dy = y - centerY;
            float angle = atan2f(dy, dx);
            if (angle < 0)
                angle += 2 * PI;

            float radius = sqrtf(dx * dx + dy * dy);
            uint8_t baseHue = hueShift;
            float hueMod = angle * 20.0 - radius * 8.0;
            uint8_t hue = baseHue + (int)hueMod;
            hue = sin8(hue); // Reintroduce sine *after* stable mapping
            leds[ledIndex++] = CHSV(hue, 255, 255);
        }
    }

    hueShift++;
}