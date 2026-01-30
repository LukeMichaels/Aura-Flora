#include <Arduino.h>
#include <FastLED.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoPixel.h>

const int GSR = A0;
int sensorValue = 0;
int gsr_average = 0;
long lastReadingTime = 0;
long readingPauseDuration = 5000; // 10 seconds
long readingDelay = 500;          // 1/2 second delay before taking the reading
int currentAnimation = 9;         // The animation that starts up when powered on
bool readingInProgress = false;
long readingStartTime = 0;
#define DATA_PIN 6
#define STEM_LEDS 163 // 85 stem, 78 leaves
#define PETALS 16
#define LEDS_PER_PETAL 9
#define BUD_LEDS 241
#define NUM_LEDS (STEM_LEDS + (PETALS * LEDS_PER_PETAL) + BUD_LEDS)
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
        // Stem: vertical line
        x = 4.5;         // Centered horizontally
        y = index * 0.1; // Adjust scaling as needed
    }
    else if (index < STEM_LEDS + PETALS * LEDS_PER_PETAL)
    {
        // Petals: arranged around center
        int petalIndex = (index - STEM_LEDS) / LEDS_PER_PETAL;
        int ledInPetal = (index - STEM_LEDS) % LEDS_PER_PETAL;
        float angle = petalIndex * (2 * PI / PETALS);
        float radius = 5.0 + ledInPetal * 0.5; // Adjust radius as needed
        x = 4.5 + radius * cos(angle);
        y = 7.5 + radius * sin(angle);
    }
    else
    {
        // Bud: circular cluster
        int budIndex = index - STEM_LEDS - PETALS * LEDS_PER_PETAL;
        int rows = 11;
        int cols = 22;
        int row = budIndex / cols;
        int col = budIndex % cols;
        x = 4.5 + (col - cols / 2) * 0.5; // Adjust scaling as needed
        y = 7.5 + (row - rows / 2) * 0.5;
    }
}

void setup()
{
    FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
    FastLED.setBrightness(120);
    Serial.begin(115200);
}

void loop()
{
    animationTwenty();
    FastLED.show();
}

void exampleAnimation()
{
    static uint8_t hueShift = 0;
    static CRGB previousLeds[NUM_LEDS];

    // --- Stem Animation (reversed bouncing hue ball: top to bottom) ---
    static float stemBallY = 0;
    static int8_t stemBallDY = 1;
    static uint8_t stemHueShift = 0;

    stemBallY += stemBallDY;
    if (stemBallY <= 0 || stemBallY >= STEM_LEDS - 1)
        stemBallDY = -stemBallDY;

    for (int i = 0; i < STEM_LEDS; i++)
    {
        // Reverse the flow by flipping the index
        float dist = abs((STEM_LEDS - 1 - i) - stemBallY);
        uint8_t hue = sin8(stemHueShift + dist * 10);
        leds[i] = CHSV(hue, 255, 255);
    }

    stemHueShift++;

    // --- Petal Animation (bouncing ball across 2D petal grid) ---
    static int8_t ballDX = 1;
    static int8_t ballDY = 1;
    static float ballX = 0;
    static float ballY = 0;

    ballX += ballDX;
    ballY += ballDY;

    if (ballX <= 0 || ballX >= 8)
        ballDX = -ballDX;
    if (ballY <= 0 || ballY >= 15)
        ballDY = -ballDY;

    float aspectRatio = 16.0 / 9.0;
    int ledIndex = STEM_LEDS;

    for (int petal = 0; petal < PETALS; petal++)
    {
        for (int i = 0; i < LEDS_PER_PETAL; i++)
        {
            float x = i * aspectRatio;
            float y = petal;
            float dist = sqrtf(sq(x - ballX) + sq(y - ballY));
            uint8_t hue = sin8(hueShift + dist * 8);

            CHSV hsv = CHSV(hue, 255, 255);
            CRGB rgb;
            hsv2rgb_spectrum(hsv, rgb);

            int idx = ledIndex + i;
            leds[idx] = blend(previousLeds[idx], rgb, 128);
            previousLeds[idx] = rgb;
        }
        ledIndex += LEDS_PER_PETAL;
    }

    // --- Bud Animation (simulate 2D grid, bouncing hue ball like petals) ---
    // We'll treat the 241 bud LEDs as an 11x22 pseudo-grid (roughly fits 241)
    const int budCols = 11;
    const int budRows = 22;

    static float budBallX = 0;
    static float budBallY = 0;
    static int8_t budBallDX = 1;
    static int8_t budBallDY = 1;

    budBallX += budBallDX;
    budBallY += budBallDY;

    if (budBallX <= 0 || budBallX >= budCols - 1)
        budBallDX = -budBallDX;
    if (budBallY <= 0 || budBallY >= budRows - 1)
        budBallDY = -budBallDY;

    for (int y = 0; y < budRows; y++)
    {
        for (int x = 0; x < budCols; x++)
        {
            int i = y * budCols + x;
            if (i >= BUD_LEDS)
                break;

            float dist = sqrtf(sq(x - budBallX) + sq(y - budBallY));
            uint8_t hue = sin8(hueShift + dist * 8);
            leds[ledIndex + i] = CHSV(hue, 150, 255);
        }
    }

    hueShift++;
    delay(100);
}

void testAnimation()
{
    static uint8_t beat = 0;
    static uint8_t pulse = 0;
    static int frame = 0;
    int ledIndex = 0;

    // Heartbeat rhythm: slow build, fast drop
    uint8_t beatWave = sin8(frame);
    if (frame < 90)
    {
        pulse = beatWave;
    }
    else
    {
        pulse = qsub8(pulse, 6);
    }

    // --- Stem: soft ambient pulse, subtle brightness modulation ---
    for (int i = 0; i < STEM_LEDS; i++)
    {
        uint8_t b = pulse / 3 + sin8(i * 3 + frame) / 6;
        leds[ledIndex++] = CHSV(96, 150, b); // soft green pulse
    }

    // --- Petals: delayed echo of the bud pulse ---
    float centerX = 4.5;
    float centerY = 7.5;

    for (int petal = 0; petal < PETALS; petal++)
    {
        for (int i = 0; i < LEDS_PER_PETAL; i++)
        {
            float x = i;
            float y = petal;
            float dx = x - centerX;
            float dy = y - centerY;
            float dist = sqrtf(dx * dx + dy * dy);

            uint8_t wave = sin8(frame - dist * 10);
            uint8_t brightness = scale8(pulse, wave);
            leds[ledIndex++] = CHSV(220, 180, brightness); // soft pink/purple pulse
        }
    }

    // --- Bud: radial pulse outward from center ---
    const int budCols = 11;
    const int budRows = 22;
    for (int y = 0; y < budRows; y++)
    {
        for (int x = 0; x < budCols; x++)
        {
            if (ledIndex >= NUM_LEDS)
                break;

            float dx = x - budCols / 2.0;
            float dy = y - budRows / 2.0;
            float dist = sqrtf(dx * dx + dy * dy);

            uint8_t wave = sin8(frame - dist * 12);
            uint8_t brightness = scale8(pulse, wave);
            leds[ledIndex++] = CHSV(0, 180, brightness); // red glow center
        }
    }

    frame++;
    if (frame > 255)
        frame = 0;
}

void animationOne()
{

    // Set stem LEDs to green
    for (int i = 0; i < STEM_LEDS; i++)
    {
        leds[i] = CRGB::Green;
    }

    static uint16_t x = 0;
    static uint16_t y = 0;
    int ledIndex = STEM_LEDS; // Start after the stem LEDs

    // Animate petals
    for (int petal = 0; petal < PETALS; petal++)
    {
        for (int i = 0; i < LEDS_PER_PETAL; i++)
        {
            uint8_t noise = inoise8(x + (LEDS_PER_PETAL - i) * 20, y + petal * 20);
            leds[ledIndex + i] = CHSV(noise, 255, 255);
        }
        ledIndex += LEDS_PER_PETAL;
    }

    // Optional: Animate bud
    for (int i = 0; i < BUD_LEDS; i++)
    {
        uint8_t noise = inoise8(x + i * 10, y);
        leds[ledIndex + i] = CHSV(noise, 200, 255); // Purple-ish noise animation
    }

    x += 2;
    y += 2;
}

// currently not used, swirly purple + Pink flower, with cascading green stem
void animationTwo()
{
    static uint8_t ripplePhase = 0;
    int ledIndex = 0;

    // --- Stem: vertical flow with a soft wave ---
    for (int i = 0; i < STEM_LEDS; i++)
    {
        float dist = (float)i / STEM_LEDS;
        uint8_t brightness = sin8(ripplePhase + dist * 128);
        leds[ledIndex++] = CHSV(96, 255, brightness); // green-ish wave
    }

    // --- Petals: radial pulse with sine ripple ---
    float centerX = 4.5;
    float centerY = 7.5;

    for (int petal = 0; petal < PETALS; petal++)
    {
        for (int i = 0; i < LEDS_PER_PETAL; i++)
        {
            float x = i;
            float y = petal;
            float dx = x - centerX;
            float dy = y - centerY;
            float distance = sqrtf(dx * dx + dy * dy);
            uint8_t brightness = sin8(ripplePhase + distance * 32);
            leds[ledIndex++] = CHSV(160 + sin8(petal * 16 + ripplePhase) / 8, 200, brightness); // pastel blue/purple
        }
    }

    // --- Bud: radiating ripples from center ---
    const int budCols = 11;
    const int budRows = 22;

    for (int y = 0; y < budRows; y++)
    {
        for (int x = 0; x < budCols; x++)
        {
            if (ledIndex >= NUM_LEDS)
                break;
            float dx = x - budCols / 2.0;
            float dy = y - budRows / 2.0;
            float distance = sqrtf(dx * dx + dy * dy);
            uint8_t brightness = sin8(ripplePhase + distance * 48);
            leds[ledIndex++] = CHSV(192 + sin8(x * 10 + ripplePhase) / 12, 180, brightness); // orange/gold tone
        }
    }

    ripplePhase += 2;
}

void animationThree()
{
    static uint8_t t = 0;
    int ledIndex = 0;

    // --- Stem: slow upward wave with warm tones ---
    for (int i = 0; i < STEM_LEDS; i++)
    {
        float offset = (float)i / STEM_LEDS;
        uint8_t hue = sin8(t + offset * 128);
        leds[ledIndex++] = CHSV(32 + hue / 4, 255, 200); // earthy tones
    }

    // --- Petals: spiral effect based on angular rotation and time ---
    float centerX = 4.0;
    float centerY = 7.5;
    for (int petal = 0; petal < PETALS; petal++)
    {
        for (int i = 0; i < LEDS_PER_PETAL; i++)
        {
            float x = i - centerX;
            float y = petal - centerY;
            float angle = atan2f(y, x); // -PI to PI
            float radius = sqrtf(x * x + y * y);

            // Spiral formula: hue follows angle and radius over time
            uint8_t hue = fmod((angle * 128 / PI) + radius * 32 + t, 255);
            uint8_t val = 200 - (radius * 10); // dim as it moves outward

            leds[ledIndex++] = CHSV(hue, 200, val);
        }
    }

    // --- Bud: swirling whirlpool center-out ---
    const int budCols = 11;
    const int budRows = 22;
    for (int y = 0; y < budRows; y++)
    {
        for (int x = 0; x < budCols; x++)
        {
            if (ledIndex >= NUM_LEDS)
                break;
            float dx = x - budCols / 2.0;
            float dy = y - budRows / 2.0;
            float angle = atan2f(dy, dx);
            float radius = sqrtf(dx * dx + dy * dy);

            uint8_t hue = fmod((angle * 128 / PI) + radius * 48 + t * 2, 255);
            uint8_t val = 220 - (radius * 5);

            leds[ledIndex++] = CHSV(hue, 230, val);
        }
    }

    t += 2;
}

void animationFour()
{
    static uint8_t sparkleBrightness[NUM_LEDS] = {0};
    static uint8_t hueBase = 180; // Start with a cool hue
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
    for (int petal = 0; petal < PETALS; petal++)
    {
        for (int i = 0; i < LEDS_PER_PETAL; i++)
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

    // --- Bud: smooth drifting glows ---
    const int budCols = 11;
    const int budRows = 22;
    for (int y = 0; y < budRows; y++)
    {
        for (int x = 0; x < budCols; x++)
        {
            if (ledIndex >= NUM_LEDS)
                break;

            float dx = x - budCols / 2.0;
            float dy = y - budRows / 2.0;
            float dist = sqrtf(dx * dx + dy * dy);

            uint8_t wave = sin8(t + dist * 30);
            uint8_t glow = qadd8(wave / 2, sparkleBrightness[ledIndex] / 2);

            leds[ledIndex] = CHSV(hueBase + wave / 3, 180, glow);
            sparkleBrightness[ledIndex] = qsub8(sparkleBrightness[ledIndex], 2);

            ledIndex++;
        }
    }

    t += 1;
    hueBase += 1; // slowly shift hues over time
}

void animationFive()
{
    // Set stem LEDs to yellow
    for (int i = 0; i < STEM_LEDS; i++)
    {
        leds[i] = CHSV(64, 255, 255);
    }

    static uint16_t x = 0;
    static uint16_t y = 0;
    int ledIndex = STEM_LEDS;

    for (int petal = 0; petal < PETALS; petal++)
    {
        for (int i = 0; i < LEDS_PER_PETAL; i++)
        {
            int indexOffset = ledIndex - STEM_LEDS + i;
            uint8_t noise = inoise8(x + indexOffset * 20, y);
            leds[ledIndex + i] = CHSV(noise, 255, 255);
        }
        ledIndex += LEDS_PER_PETAL;
    }

    // Optional: Noise bud animation
    for (int i = 0; i < BUD_LEDS; i++)
    {
        uint8_t noise = inoise8(x + i * 10, y + 50);
        leds[ledIndex + i] = CHSV(noise, 255, 255);
    }

    x += 2;
    y += 2;
}

void animationSix()
{
    // Set stem LEDs to teal
    for (int i = 0; i < STEM_LEDS; i++)
    {
        leds[i] = CHSV(128, 255, 255);
    }

    static uint8_t brightness = 128;
    static int8_t direction = 1;

    brightness += direction;
    if (brightness <= 100 || brightness >= 255)
    {
        direction = -direction;
    }

    int ledIndex = STEM_LEDS;

    // Apply pulsing brightness to petals
    for (int petal = 0; petal < PETALS; petal++)
    {
        for (int i = 0; i < LEDS_PER_PETAL; i++)
        {
            leds[ledIndex + i] = CHSV(160, 255, brightness);
        }
        ledIndex += LEDS_PER_PETAL;
    }

    // Optional: pulse bud too
    for (int i = 0; i < BUD_LEDS; i++)
    {
        leds[ledIndex + i] = CHSV(180, 255, brightness);
    }
}

void animationSeven()
{
    // Animated upward-traveling stem wave
    static uint8_t stem_phase = 0;

    for (int i = 0; i < STEM_LEDS; i++)
    {
        // Create vertical wave pattern moving up the stem
        uint8_t wave = (i * 8 + stem_phase) % 256;

        // Generate brightness pulse
        uint8_t brightness = 180 - abs8(sin8(wave) - 128);

        // Slight hue variation for a more natural green feel
        uint8_t hue = 96 + (i % 3); // Hue ~96 = green

        leds[i] = CHSV(hue, 200, brightness);
    }

    stem_phase += 4; // Controls the speed of the upward motion

    static uint16_t x = 0;
    static uint16_t y = 0;
    int ledIndex = STEM_LEDS;

    for (int petal = 0; petal < PETALS; petal++)
    {
        for (int i = 0; i < LEDS_PER_PETAL; i++)
        {
            uint8_t noise = inoise8(x + (LEDS_PER_PETAL - i) * 20, y + petal * 20);
            leds[ledIndex + i] = CHSV(noise, 255, 255);
        }
        ledIndex += LEDS_PER_PETAL;
    }

    // Bud tunnel animation - pastel pink <-> blue shift inward
    const uint8_t bud_rings = 20; // Approximate number of concentric rings
    const uint8_t leds_per_ring = BUD_LEDS / bud_rings;
    static uint8_t tunnel_phase = 0;
    static uint8_t hue_shift = 0;

    for (int i = 0; i < BUD_LEDS; i++)
    {
        // Estimate ring index (outer ring = 0, center = max)
        uint8_t ring_index = bud_rings - 1 - (i / leds_per_ring);

        // Create a tunnel wave moving inward
        uint8_t wave = (ring_index * 10 + tunnel_phase) % 256;

        // Create color pulse based on sine wave
        uint8_t brightness = 255 - abs8(sin8(wave) - 128); // Pulse inwards

        // Animate hue between pastel blue and pastel pink
        // pastel blue ≈ hue 160, pastel pink ≈ hue 220
        uint8_t pastelHue = lerp8by8(160, 220, sin8(hue_shift));

        // Low saturation for pastel look (e.g., 100–150)
        leds[STEM_LEDS + PETALS * LEDS_PER_PETAL + i] = CHSV(pastelHue, 120, brightness);
    }

    tunnel_phase += 4; // Speed of tunnel motion
    hue_shift += 1;    // Speed of hue blending

    x += 2;
    y += 2;
}

void animationEight()
{
    static uint8_t hueShift = 0;
    static CRGB previousLeds[NUM_LEDS];

    // --- Stem Animation (reversed bouncing hue ball: top to bottom) ---
    static float stemBallY = 0;
    static int8_t stemBallDY = 1;
    static uint8_t stemHueShift = 0;

    stemBallY += stemBallDY;
    if (stemBallY <= 0 || stemBallY >= STEM_LEDS - 1)
        stemBallDY = -stemBallDY;

    for (int i = 0; i < STEM_LEDS; i++)
    {
        // Reverse the flow by flipping the index
        float dist = abs((STEM_LEDS - 1 - i) - stemBallY);
        uint8_t hue = sin8(stemHueShift + dist * 10);
        leds[i] = CHSV(hue, 255, 255);
    }

    stemHueShift++;

    // --- Petal Animation (bouncing ball across 2D petal grid) ---
    static int8_t ballDX = 1;
    static int8_t ballDY = 1;
    static float ballX = 0;
    static float ballY = 0;

    ballX += ballDX;
    ballY += ballDY;

    if (ballX <= 0 || ballX >= 8)
        ballDX = -ballDX;
    if (ballY <= 0 || ballY >= 15)
        ballDY = -ballDY;

    float aspectRatio = 16.0 / 9.0;
    int ledIndex = STEM_LEDS;

    for (int petal = 0; petal < PETALS; petal++)
    {
        for (int i = 0; i < LEDS_PER_PETAL; i++)
        {
            float x = i * aspectRatio;
            float y = petal;
            float dist = sqrtf(sq(x - ballX) + sq(y - ballY));
            uint8_t hue = sin8(hueShift + dist * 8);

            CHSV hsv = CHSV(hue, 255, 255);
            CRGB rgb;
            hsv2rgb_spectrum(hsv, rgb);

            int idx = ledIndex + i;
            leds[idx] = blend(previousLeds[idx], rgb, 128);
            previousLeds[idx] = rgb;
        }
        ledIndex += LEDS_PER_PETAL;
    }

    // --- Bud Animation (simulate 2D grid, bouncing hue ball like petals) ---
    // We'll treat the 241 bud LEDs as an 11x22 pseudo-grid (roughly fits 241)
    const int budCols = 11;
    const int budRows = 22;

    static float budBallX = 0;
    static float budBallY = 0;
    static int8_t budBallDX = 1;
    static int8_t budBallDY = 1;

    budBallX += budBallDX;
    budBallY += budBallDY;

    if (budBallX <= 0 || budBallX >= budCols - 1)
        budBallDX = -budBallDX;
    if (budBallY <= 0 || budBallY >= budRows - 1)
        budBallDY = -budBallDY;

    for (int y = 0; y < budRows; y++)
    {
        for (int x = 0; x < budCols; x++)
        {
            int i = y * budCols + x;
            if (i >= BUD_LEDS)
                break;

            float dist = sqrtf(sq(x - budBallX) + sq(y - budBallY));
            uint8_t hue = sin8(hueShift + dist * 8);
            leds[ledIndex + i] = CHSV(hue, 150, 255);
        }
    }

    hueShift++;
    delay(100);
}

void animationNine()
{
#define COOLING 14  // much slower cooling
#define SPARKING 20 // very few sparks for relaxed feel

    // --- Stem Fire (relaxed but more active across full height) ---
    static byte stemHeat[STEM_LEDS];
    for (int i = 0; i < STEM_LEDS; i++)
    {
        stemHeat[i] = qsub8(stemHeat[i], random8(0, ((COOLING * 10) / STEM_LEDS) + 2));
    }

    // Improve upward spread: pull heat from 3 prior cells
    for (int i = STEM_LEDS - 1; i >= 3; i--)
    {
        stemHeat[i] = (stemHeat[i - 1] + stemHeat[i - 2] + stemHeat[i - 3]) / 3;
    }

    // Spark near bottom
    if (random8() < SPARKING)
    {
        int y = random8(10); // was 4 — now sparks can go up to LED 10
        stemHeat[y] = qadd8(stemHeat[y], random8(100, 160));
    }

    // Occasionally spark mid-stem for life higher up
    if (random8() < 5)
    {
        int y = random8(40, 90); // mid-stem zone
        stemHeat[y] = qadd8(stemHeat[y], random8(60, 100));
    }

    // Map to color
    for (int i = 0; i < STEM_LEDS; i++)
    {
        CRGB color = ColorFromPalette(fireNoWhitePalette, stemHeat[i]);
        leds[i] = color.fadeToBlackBy(60);
    }

    // --- Petal Fire ---
    static byte petalHeat[PETALS][LEDS_PER_PETAL];
    int ledIndex = STEM_LEDS;
    for (int p = 0; p < PETALS; p++)
    {
        for (int i = 0; i < LEDS_PER_PETAL; i++)
        {
            petalHeat[p][i] = qsub8(petalHeat[p][i], random8(0, ((COOLING * 8) / LEDS_PER_PETAL) + 1));
        }
        for (int i = LEDS_PER_PETAL - 1; i >= 2; i--)
        {
            petalHeat[p][i] = (petalHeat[p][i - 1] + petalHeat[p][i - 2] + petalHeat[p][i - 2]) / 3;
        }
        if (random8() < SPARKING / 2)
        {
            int y = random8(1);
            petalHeat[p][y] = qadd8(petalHeat[p][y], random8(80, 140));
        }
        for (int i = 0; i < LEDS_PER_PETAL; i++)
        {
            CRGB color = ColorFromPalette(fireNoWhitePalette, petalHeat[p][i]);
            leds[ledIndex++] = color.fadeToBlackBy(80); // extra chill
        }
    }

    // --- Bud Fire (2D style) ---
    const int budCols = 11;
    const int budRows = 22;
    static byte budHeat[budCols][budRows];
    for (int x = 0; x < budCols; x++)
    {
        for (int y = 0; y < budRows; y++)
        {
            budHeat[x][y] = qsub8(budHeat[x][y], random8(0, ((COOLING * 10) / budRows) + 2));
        }
        for (int y = budRows - 1; y >= 2; y--)
        {
            budHeat[x][y] = (budHeat[x][y - 1] + budHeat[x][y - 2] + budHeat[x][y - 2]) / 3;
        }
        if (random8() < SPARKING / 2)
        {
            int y = random8(2);
            budHeat[x][y] = qadd8(budHeat[x][y], random8(80, 140));
        }
    }
    for (int y = 0; y < budRows; y++)
    {
        for (int x = 0; x < budCols; x++)
        {
            int budIndex = STEM_LEDS + PETALS * LEDS_PER_PETAL + y * budCols + x;
            if (budIndex < NUM_LEDS)
            {
                CRGB color = ColorFromPalette(fireNoWhitePalette, budHeat[x][y]);
                leds[budIndex] = color.fadeToBlackBy(60);
            }
        }
    }
}

void animationTen()
{
    static uint8_t dropIntensity[NUM_LEDS] = {0};

    // --- Fill all LEDs (stem, petals, bud) with base purple ---
    fill_solid(leds, NUM_LEDS, CRGB::Purple);

    // --- Spawn a few random golden drops ---
    for (uint8_t i = 0; i < NUM_FLIES; i++)
    {
        uint16_t led = random16(NUM_LEDS);
        dropIntensity[led] = 255;
    }

    // --- Apply and fade drops across all sections ---
    for (uint16_t i = 0; i < NUM_LEDS; i++)
    {
        if (dropIntensity[i] > 0)
        {
            leds[i] = CHSV(42, 255, dropIntensity[i]); // golden spark
            dropIntensity[i] = qsub8(dropIntensity[i], FADE_RATE);
        }
    }

    delay(100);
}

void animationEleven()
{
    static uint16_t t = 0;
    int ledIndex = 0;

    // --- Stem (sync noise with petals & bud) ---
    for (int i = 0; i < STEM_LEDS; i++)
    {
        // Vertical-only movement
        uint8_t noise = inoise8(0, i * NOISE_SCALE, t);
        leds[ledIndex++] = CHSV(noise, 255, 200); // Matching bud brightness
    }

    // --- Petals (2D field using x/y and shared Z=t) ---
    for (int petal = 0; petal < 16; petal++)
    {
        for (int i = 0; i < 9; i++)
        {
            uint8_t x = i * NOISE_SCALE;
            uint8_t y = petal * NOISE_SCALE;
            uint8_t noise = inoise8(x, y, t);
            leds[ledIndex++] = CHSV(noise, 255, 255);
        }
    }

    // --- Bud (simulate 2D using 11x22 grid layout) ---
    const int budCols = 11;
    const int budRows = 22;

    for (int y = 0; y < budRows; y++)
    {
        for (int x = 0; x < budCols; x++)
        {
            int i = y * budCols + x;
            if (i >= BUD_LEDS)
                break;

            uint8_t nx = x * NOISE_SCALE;
            uint8_t ny = y * NOISE_SCALE;
            uint8_t noise = inoise8(nx, ny, t);
            leds[ledIndex++] = CHSV(noise, 255, 200);
        }
    }

    t += BRAIN_WAVES_SPEED; // Shared motion speed
}

void animationTwelve()
{
    static uint8_t hueShift = 0;
    static uint16_t stemOffset = 0;
    int ledIndex = 0;

    // --- Stem Animation: Top-to-bottom seamless hue wave ---
    for (int i = 0; i < STEM_LEDS; i++)
    {
        int reversedIndex = STEM_LEDS - 1 - i;
        uint8_t hue = sin8(stemOffset + reversedIndex * 8);
        leds[ledIndex++] = CHSV(hue, 255, 255);
    }

    stemOffset += 2;

    float centerX = 4.0;
    float centerY = 7.5;

    for (int petal = 0; petal < PETALS; petal++)
    {
        for (int i = 0; i < LEDS_PER_PETAL; i++)
        {
            float x = i;
            float y = petal;

            float dx = x - centerX;
            float dy = y - centerY;
            float angle = atan2f(dy, dx);
            if (angle < 0)
                angle += 2 * PI;

            float radius = sqrtf(dx * dx + dy * dy);

            // Reversed radial wave, but keep angular motion intact
            uint8_t hue = sin8(hueShift + angle * 64 - radius * 12);
            leds[ledIndex++] = CHSV(hue, 255, 255);
        }
    }

    // --- Bud: Radial hue wheel ---
    for (int i = 0; i < BUD_LEDS; i++)
    {
        float angle = (float)i / BUD_LEDS * 2 * PI;
        uint8_t hue = mapf(angle, 0, 2 * PI, hueShift, hueShift + 255);
        leds[ledIndex++] = CHSV(hue, 255, 255);
    }

    hueShift++;
}

void animationThirteen()
{
    // Set stem LEDs to teal
    for (int i = 0; i < STEM_LEDS; i++)
    {
        leds[i] = CHSV(128, 255, 255);
    }

    static uint16_t x = 0;
    static uint16_t y = 0;
    static uint16_t z = 0;
    int ledIndex = STEM_LEDS;

    for (int petal = 0; petal < PETALS; petal++)
    {
        for (int i = 0; i < LEDS_PER_PETAL; i++)
        {
            uint8_t noise = inoise8(x + i * 20, y + petal * 20, z);
            leds[ledIndex + i] = CHSV(noise, 255, 255);
        }
        ledIndex += LEDS_PER_PETAL;
    }

    // Optional: Animate bud with slower noise
    for (int i = 0; i < BUD_LEDS; i++)
    {
        uint8_t noise = inoise8(x + i * 5, y, z);
        leds[ledIndex + i] = CHSV(noise, 255, 200);
    }

    x += 2;
    y += 1;
    z += 1;
}

void animationFourteen()
{
    static uint8_t hueShift = 0;
    static float t = 0; // time variable for smooth animation

    float centerX = 4.5;
    float centerY = 7.5;

    int ledIndex = 0;

    // --- Stem ---
    for (int i = 0; i < STEM_LEDS; i++)
    {
        float x = centerX;
        float y = -((float)i / STEM_LEDS) * 10.0;

        float angle = atan2f(y - centerY, x - centerX);
        if (angle < 0)
            angle += 2 * PI;

        float dist = sqrtf((x - centerX) * (x - centerX) + (y - centerY) * (y - centerY));

        // Radiating wave using distance and time
        float wave = sinf(dist - t); // smooth ripple moving outward
        uint8_t hue = ((angle + wave) * 255.0 / (2 * PI)) + hueShift;

        leds[ledIndex++] = CHSV(hue, 255, 255);
    }

    // --- Petals ---
    for (int petal = 0; petal < PETALS; petal++)
    {
        for (int i = 0; i < LEDS_PER_PETAL; i++)
        {
            float x = i;
            float y = petal;

            float angle = atan2f(y - centerY, x - centerX);
            if (angle < 0)
                angle += 2 * PI;

            float dist = sqrtf((x - centerX) * (x - centerX) + (y - centerY) * (y - centerY));

            float wave = sinf(dist - t);
            uint8_t hue = ((angle + wave) * 255.0 / (2 * PI)) + hueShift;

            leds[ledIndex++] = CHSV(hue, 255, 255);
        }
    }

    // --- Bud (11×22 grid) ---
    const int budCols = 11;
    const int budRows = 22;

    for (int y = 0; y < budRows; y++)
    {
        for (int x = 0; x < budCols; x++)
        {
            int i = y * budCols + x;
            if (i >= BUD_LEDS)
                break;

            float fx = (float)x / budCols * 9.0;
            float fy = (float)y / budRows * 16.0;

            float angle = atan2f(fy - centerY, fx - centerX);
            if (angle < 0)
                angle += 2 * PI;

            float dist = sqrtf((fx - centerX) * (fx - centerX) + (fy - centerY) * (fy - centerY));

            float wave = sinf(dist - t);
            uint8_t hue = ((angle + wave) * 255.0 / (2 * PI)) + hueShift;

            leds[ledIndex++] = CHSV(hue, 255, 255);
        }
    }

    // Advance time and hue
    t += 0.07;     // ripple scroll speed
    hueShift += 1; // subtle color cycling
}

void animationFifteen()
{
    static uint8_t hueShift = 0;
    static float time = 0;

    float centerX = 4.5;
    float centerY = 7.5;
    int ledIndex = 0;

    // --- Stem: projected below flower center, in sync with swirl ---
    for (int i = 0; i < STEM_LEDS; i++)
    {
        float x = centerX;
        float y = centerY - 4.0 - ((float)i / STEM_LEDS) * 10.0; // project below bud/petals

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

    // --- Petals: swirling hue spiral ---
    for (int petal = 0; petal < PETALS; petal++)
    {
        for (int i = 0; i < LEDS_PER_PETAL; i++)
        {
            float x = i;
            float y = petal;

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

    // --- Bud: soft pulsing glow with angular rotation ---
    const int budCols = 11;
    const int budRows = 22;

    for (int y = 0; y < budRows; y++)
    {
        for (int x = 0; x < budCols; x++)
        {
            int i = y * budCols + x;
            if (i >= BUD_LEDS)
                break;

            float fx = (float)x / budCols * 9.0;
            float fy = (float)y / budRows * 16.0;

            float dx = fx - centerX;
            float dy = fy - centerY;
            float angle = atan2f(dy, dx);
            if (angle < 0)
                angle += 2 * PI;
            float radius = sqrtf(dx * dx + dy * dy);

            float rotation = angle + time * 0.8;
            uint8_t hue = sin8(rotation * 128.0 / PI + radius * 12 + hueShift);
            leds[ledIndex++] = CHSV(hue, 150, 220);
        }
    }

    // Animate
    hueShift += 1;
    time += 0.05;
}

void animationSixteen()
{
    static uint16_t x = 0;
    static uint16_t y = 0;

    // --- Stem: animate with noise pattern like petals ---
    for (int i = 0; i < STEM_LEDS; i++)
    {
        uint8_t noise = inoise8(x + i * 20, y);
        leds[i] = CHSV(noise, 255, 255);
    }

    // --- Petals ---
    int ledIndex = STEM_LEDS;
    for (int petal = 0; petal < PETALS; petal++)
    {
        for (int i = 0; i < LEDS_PER_PETAL; i++)
        {
            int indexOffset = ledIndex - STEM_LEDS;
            uint8_t noise = inoise8(x + indexOffset * 20, y);
            leds[ledIndex + i] = CHSV(noise, 255, 255);
        }
        ledIndex += LEDS_PER_PETAL;
    }

    // --- Bud ---
    for (int i = 0; i < BUD_LEDS; i++)
    {
        leds[ledIndex + i] = CHSV((i + x) % 255, 255, 180);
    }

    x += 2;
    y += 2;
}

void animationSeventeen()
{
    static uint8_t dropIntensity[NUM_LEDS] = {0};
    static uint8_t ledHue[NUM_LEDS];

    fill_solid(leds, NUM_LEDS, CRGB::Black); // Clear background

    // Spawn new sparkles
    for (uint8_t i = 0; i < NUM_DOTS; i++)
    {
        uint16_t led = random16(NUM_LEDS);
        if (dropIntensity[led] == 0)
        {
            ledHue[led] = random8(); // Random hue
        }
        dropIntensity[led] = 255;
    }

    // Apply sparkle pattern to ALL sections (stem, petals, bud)
    for (uint16_t i = 0; i < NUM_LEDS; i++)
    {
        if (dropIntensity[i] > 0)
        {
            leds[i] = CHSV(ledHue[i], 255, dropIntensity[i]);
            dropIntensity[i] = qsub8(dropIntensity[i], DOTS_FADE_RATE);
        }
    }

    FastLED.show();
}

void animationEighteen()
{
    static uint16_t t = 0;
    float scale = 0.1;  // Adjusts the spatial frequency of the plasma
    float speed = 0.05; // Controls the animation speed

    int ledIndex = 0;

    // Animate Stem
    for (int i = 0; i < STEM_LEDS; i++)
    {
        float x = i * scale;
        float y = 0; // Assuming stem is linear along the x-axis
        uint8_t hue = 128 + 127 * sin(x + t * speed);
        leds[ledIndex++] = CHSV(hue, 255, 255);
    }

    // Animate Petals
    float centerX = LEDS_PER_PETAL / 2.0;
    float centerY = PETALS / 2.0;
    for (int petal = 0; petal < PETALS; petal++)
    {
        for (int i = 0; i < LEDS_PER_PETAL; i++)
        {
            float x = i - centerX;
            float y = petal - centerY;
            float distance = sqrt(x * x + y * y) * scale;
            uint8_t hue = 128 + 127 * sin(distance + t * speed);
            leds[ledIndex++] = CHSV(hue, 255, 255);
        }
    }

    // Animate Bud
    int budCols = 11;
    int budRows = 22;
    for (int y = 0; y < budRows; y++)
    {
        for (int x = 0; x < budCols; x++)
        {
            if (ledIndex >= NUM_LEDS)
                break;
            float dx = x - budCols / 2.0;
            float dy = y - budRows / 2.0;
            float distance = sqrt(dx * dx + dy * dy) * scale;
            uint8_t hue = 128 + 127 * sin(distance + t * speed);
            leds[ledIndex++] = CHSV(hue, 255, 255);
        }
    }

    t++;
}

void animationNineteen()
{
    static float t = 0.0; // Time variable
    for (int i = 0; i < NUM_LEDS; i++)
    {
        float x, y;
        getLEDCoordinates(i, x, y);

        // Normalize coordinates
        float nx = x * 0.2;
        float ny = y * 0.2;

        // Plasma calculation
        float value = sin(nx * 10 + t) + sin(10 * (nx * sin(t / 2.0) + ny * cos(t / 3.0)) + t) + sin(sqrt(100 * (nx * nx + ny * ny) + 1) + t);

        // Map value to color
        uint8_t hue = (uint8_t)(fabs(sin(value * PI)) * 255);
        leds[i] = CHSV(hue, 255, 255);
    }
    t += 0.03; // Adjust speed as needed
}

// heart pulse
void animationTwenty()
{
    static uint8_t beat = 0;
    static uint8_t pulse = 0;
    static int frame = 0;
    int ledIndex = 0;

    // Heartbeat rhythm: slow build, fast drop
    uint8_t beatWave = sin8(frame);
    if (frame < 90)
    {
        pulse = beatWave;
    }
    else
    {
        pulse = qsub8(pulse, 6);
    }

    // --- Stem: soft ambient pulse, subtle brightness modulation ---
    for (int i = 0; i < STEM_LEDS; i++)
    {
        uint8_t b = pulse / 3 + sin8(i * 3 + frame) / 6;
        leds[ledIndex++] = CHSV(96, 150, b); // soft green pulse
    }

    // --- Petals: delayed echo of the bud pulse ---
    float centerX = 4.5;
    float centerY = 7.5;

    for (int petal = 0; petal < PETALS; petal++)
    {
        for (int i = 0; i < LEDS_PER_PETAL; i++)
        {
            float x = i;
            float y = petal;
            float dx = x - centerX;
            float dy = y - centerY;
            float dist = sqrtf(dx * dx + dy * dy);

            uint8_t wave = sin8(frame - dist * 10);
            uint8_t brightness = scale8(pulse, wave);
            leds[ledIndex++] = CHSV(220, 180, brightness); // soft pink/purple pulse
        }
    }

    // --- Bud: radial pulse outward from center ---
    const int budCols = 11;
    const int budRows = 22;
    for (int y = 0; y < budRows; y++)
    {
        for (int x = 0; x < budCols; x++)
        {
            if (ledIndex >= NUM_LEDS)
                break;

            float dx = x - budCols / 2.0;
            float dy = y - budRows / 2.0;
            float dist = sqrtf(dx * dx + dy * dy);

            uint8_t wave = sin8(frame - dist * 12);
            uint8_t brightness = scale8(pulse, wave);
            leds[ledIndex++] = CHSV(0, 180, brightness); // red glow center
        }
    }

    frame++;
    if (frame > 255)
        frame = 0;
}