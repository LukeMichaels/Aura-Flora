#include <Arduino.h>
#include <FastLED.h>
#include <string.h> // memset

const uint8_t GSR_PIN = A0;
#define DATA_PIN 6

#define STEM_LEDS 163
#define PETALS 16
#define LEDS_PER_PETAL 9
#define BUD_LEDS 241
#define NUM_LEDS (STEM_LEDS + (PETALS * LEDS_PER_PETAL) + BUD_LEDS)

static constexpr int BUD_ROWS = 11;
static constexpr int BUD_COLS = 22; // 11*22 = 242, we use first 241

CRGB leds[NUM_LEDS];

static bool gFrameDirty = true;

static inline float mapf(float x, float in_min, float in_max, float out_min, float out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

enum StemSegType : uint8_t
{
    STEM_STRAIGHT = 0,
    STEM_LEAF = 1
};

struct StemSeg
{
    StemSegType type;
    uint16_t len;
};

static const StemSeg STEM_LAYOUT[] = {
    {STEM_STRAIGHT, 17},
    {STEM_LEAF, 26},
    {STEM_STRAIGHT, 17},
    {STEM_LEAF, 26},
    {STEM_STRAIGHT, 17},
    {STEM_STRAIGHT, 17},
    {STEM_LEAF, 26},
    {STEM_STRAIGHT, 17},
};
static const uint8_t STEM_LAYOUT_LEN = sizeof(STEM_LAYOUT) / sizeof(STEM_LAYOUT[0]);

static int16_t stemTrunkPos[STEM_LEDS]; // 0..(STEM_TRUNK_TOTAL-1) for straights, -1 for leaves
static uint8_t stemIsLeaf[STEM_LEDS];   // 1 if leaf, 0 otherwise
static uint16_t STEM_TRUNK_TOTAL = 0;   // should become 85

// Leaf segmentation (so leaf animations are seamless within each leaf run)
static int8_t stemLeafSegId[STEM_LEDS]; // -1 if not leaf, else 0..(STEM_LEAF_SEGS-1)
static uint8_t stemLeafPos[STEM_LEDS];  // 0..(len-1) within that leaf segment
static uint8_t STEM_LEAF_SEGS = 0;

static void buildStemMap()
{
    for (int i = 0; i < STEM_LEDS; i++)
    {
        stemTrunkPos[i] = -1;
        stemIsLeaf[i] = 0;
        stemLeafSegId[i] = -1;
        stemLeafPos[i] = 0;
    }

    uint16_t idx = 0;
    uint16_t trunk = 0;

    STEM_LEAF_SEGS = 0;
    int8_t curLeafSeg = -1;
    uint8_t curLeafPos = 0;

    for (uint8_t s = 0; s < STEM_LAYOUT_LEN; s++)
    {
        for (uint16_t j = 0; j < STEM_LAYOUT[s].len; j++)
        {
            if (idx >= STEM_LEDS)
                break;

            if (STEM_LAYOUT[s].type == STEM_STRAIGHT)
            {
                // leaving any leaf block
                curLeafSeg = -1;
                curLeafPos = 0;

                stemTrunkPos[idx] = (int16_t)trunk;
                stemIsLeaf[idx] = 0;
                stemLeafSegId[idx] = -1;
                stemLeafPos[idx] = 0;
                trunk++;
            }
            else
            {
                // entering a new leaf block
                if (curLeafSeg < 0)
                {
                    curLeafSeg = (int8_t)STEM_LEAF_SEGS;
                    STEM_LEAF_SEGS++;
                    curLeafPos = 0;
                }

                stemTrunkPos[idx] = -1;
                stemIsLeaf[idx] = 1;
                stemLeafSegId[idx] = curLeafSeg;
                stemLeafPos[idx] = curLeafPos++;
            }

            idx++;
        }
    }

    STEM_TRUNK_TOTAL = trunk; // should be 85

    Serial.print("Stem trunk total: ");
    Serial.println(STEM_TRUNK_TOTAL);
    Serial.print("Stem leaf segs: ");
    Serial.println(STEM_LEAF_SEGS);
}

static uint32_t nowMs = 0;
static uint32_t lastAnimMs = 0;
static float dtSec = 0.0f;
static float tSec = 0.0f;

const uint16_t SENSOR_SAMPLE_MS = 15;   // ~66 Hz
const uint32_t IDLE_TIMEOUT_MS = 30000; // 30000 - if the sensor isn't used for 30 seconds, rotate through patterns
const uint32_t IDLE_CYCLE_MS = 60000;   // 60000 - change idle pattern every minute

struct GsrState
{
    uint16_t raw = 0;
    float filtered = 0;
    float baseline = 0;
    float delta = 0;
    bool touched = false;

    uint32_t lastSampleMs = 0;
    uint32_t lastInteractionMs = 0;
};

GsrState gsr;

// Tunables
const float EMA_FAST = 0.20f;
const float EMA_BASE = 0.0025f;
const float TOUCH_ON = 18.0f;
const float TOUCH_OFF = 10.0f;
const float ACTIVITY_INTERACT = 8.0f;

static constexpr float TRUNK_UNIT_PER_LED = 0.085f;

static inline float trunkPhaseCoordForStemIndex(int i)
{
    int16_t trunkPos = stemTrunkPos[i];
    if (trunkPos < 0)
        trunkPos = 0;

    const int16_t trunkTopPos = (int16_t)STEM_TRUNK_TOTAL - 1; // should be 84
    float distFromTop = (float)(trunkTopPos - trunkPos);
    return -(distFromTop * TRUNK_UNIT_PER_LED);
}

static inline float leafPhaseCoordForStemIndex(int i)
{
    if (!stemIsLeaf[i])
        return 0.0f;
    return -((float)stemLeafPos[i] * TRUNK_UNIT_PER_LED);
}

static inline uint8_t trunkHueFlow(uint8_t feedHue, float travel, float radHuePerUnit, int stemIndex)
{
    float phaseCoord = trunkPhaseCoordForStemIndex(stemIndex);
    float shifted = phaseCoord - travel;
    int16_t h = (int16_t)feedHue + (int16_t)(shifted * radHuePerUnit);
    return (uint8_t)h;
}

static inline uint8_t leafHueFlow(uint8_t feedHue, float travel, float radHuePerUnit, int stemIndex)
{
    float phaseCoord = leafPhaseCoordForStemIndex(stemIndex);
    float shifted = phaseCoord - travel;

    uint8_t seg = (stemLeafSegId[stemIndex] < 0) ? 0 : (uint8_t)stemLeafSegId[stemIndex];
    int16_t h = (int16_t)feedHue + (int16_t)(shifted * radHuePerUnit) + (int16_t)(seg * 24);
    return (uint8_t)h;
}

uint8_t currentAnimation = 21;
uint32_t lastIdleCycleMs = 0;
bool idleMode = false;

// Idle playlist
const uint8_t IDLE_PLAYLIST[] = {22, 18, 21, 5, 2, 12, 8, 18, 13, 19, 14, 9, 15, 4};
const uint8_t IDLE_PLAYLIST_LEN = sizeof(IDLE_PLAYLIST) / sizeof(IDLE_PLAYLIST[0]);
uint8_t idleIndex = 0;

const char *ANIM_NAME[23] = {
    "NA",
    "1 (disabled)",
    "2 ripple",
    "3 (disabled)",
    "4 bioluminescent twinkle",
    "5 noise span",
    "6 (disabled)",
    "7 (disabled)",
    "8 bounce ball blend",
    "9 fire",
    "10 (disabled)",
    "11 (disabled)",
    "12 stem + radial mod",
    "13 noise 3d",
    "14 radial ripples",
    "15 swirl spiral",
    "16 (disabled)",
    "17 (disabled)",
    "18 sine plasma",
    "19 plasma coords",
    "20 (disabled)",
    "21 rainbow bloom",
    "22 rainbow bloom v2"};

static uint32_t gAnimEpoch = 1;
static uint8_t gPrevAnim = 255;

static constexpr float CENTER_X = 4.5f;
static constexpr float CENTER_Y = 7.5f;

// Q8.8 scale (256)
static constexpr int FP = 256;
static constexpr float INV_FP = 1.0f / (float)FP;

static int16_t ledX16[NUM_LEDS];
static int16_t ledY16[NUM_LEDS];
static uint16_t ledR16[NUM_LEDS];   // radius in Q8.8 (always >=0)
static uint16_t ledAng16[NUM_LEDS]; // 0..65535

static inline float fx(int16_t v) { return (float)v * INV_FP; }
static inline float fu16(uint16_t v) { return (float)v * INV_FP; }

static void getLEDCoordinates(int index, float &x, float &y)
{
    if (index < STEM_LEDS)
    {
        x = CENTER_X;
        y = index * 0.1f;
        return;
    }

    int petalStart = STEM_LEDS;
    int petalEnd = STEM_LEDS + (PETALS * LEDS_PER_PETAL);

    if (index < petalEnd)
    {
        int local = index - petalStart;
        int petalIndex = local / LEDS_PER_PETAL;
        int ledInPetal = local % LEDS_PER_PETAL;

        float angle = petalIndex * (2.0f * PI / (float)PETALS);
        float radius = 5.0f + ledInPetal * 0.5f;

        x = CENTER_X + radius * cosf(angle);
        y = CENTER_Y + radius * sinf(angle);
        return;
    }

    int budIndex = index - petalEnd;

    static const uint8_t RINGS = 11;
    static const uint8_t ringCount[RINGS] = {1, 6, 12, 18, 24, 30, 36, 40, 32, 24, 18}; // sum = 241

    int ring = 0;
    int offset = budIndex;

    while (ring < RINGS && offset >= ringCount[ring])
    {
        offset -= ringCount[ring];
        ring++;
    }
    if (ring >= RINGS)
        ring = RINGS - 1;

    float ring01 = (RINGS <= 1) ? 0.0f : (float)ring / (float)(RINGS - 1);

    const float BUD_RADIUS = 4.6f;
    float radius = ring01 * BUD_RADIUS;

    float ang = (ringCount[ring] <= 1) ? 0.0f : (2.0f * PI) * ((float)offset / (float)ringCount[ring]);

    x = CENTER_X + radius * cosf(ang);
    y = CENTER_Y + radius * sinf(ang);
}

static void buildLedCoords()
{
    for (int i = 0; i < NUM_LEDS; i++)
    {
        float x, y;
        getLEDCoordinates(i, x, y);

        ledX16[i] = (int16_t)lroundf(x * FP);
        ledY16[i] = (int16_t)lroundf(y * FP);

        float dx = x - CENTER_X;
        float dy = y - CENTER_Y;

        float r = sqrtf(dx * dx + dy * dy);
        if (r < 0.0f)
            r = 0.0f;
        ledR16[i] = (uint16_t)lroundf(r * FP);

        float a = atan2f(dy, dx);
        if (a < 0)
            a += 2.0f * PI;
        ledAng16[i] = (uint16_t)(a * (65535.0f / (2.0f * PI)));
    }
}

static CRGB gPrevFrame[NUM_LEDS];
static uint8_t gScratchU8A[NUM_LEDS];

// GSR and mode selection
static uint8_t chooseAnimationFromDelta(float d)
{
    if (d < 24)
        return 21; // rainbow bloom (calm / baseline touch)
    if (d < 32)
        return 12; // stem + radial mod
    if (d < 40)
        return 4; // bioluminescent twinkle
    if (d < 52)
        return 2; // ripple
    if (d < 66)
        return 5; // noise span
    if (d < 82)
        return 18; // sine plasma
    if (d < 98)
        return 13; // noise 3d
    if (d < 116)
        return 19; // plasma coords
    if (d < 136)
        return 14; // radial ripples
    if (d < 156)
        return 8; // bounce ball blend
    if (d < 176)
        return 15; // swirl spiral
    if (d < 196)
        return 9; // fire
    return 22;    // rainbow bloom v2
}

static void updateGsr()
{
    if (nowMs - gsr.lastSampleMs < SENSOR_SAMPLE_MS)
        return;
    gsr.lastSampleMs = nowMs;

    gsr.raw = analogRead(GSR_PIN);

    if (gsr.filtered == 0 && gsr.baseline == 0)
    {
        gsr.filtered = gsr.raw;
        gsr.baseline = gsr.raw;
        gsr.lastInteractionMs = nowMs;
        return;
    }

    gsr.filtered = (1.0f - EMA_FAST) * gsr.filtered + EMA_FAST * (float)gsr.raw;
    gsr.delta = fabsf(gsr.filtered - gsr.baseline);

    if (!gsr.touched && gsr.delta > TOUCH_ON)
        gsr.touched = true;
    else if (gsr.touched && gsr.delta < TOUCH_OFF)
        gsr.touched = false;

    if (!gsr.touched && gsr.delta < TOUCH_OFF)
        gsr.baseline = (1.0f - EMA_BASE) * gsr.baseline + EMA_BASE * gsr.filtered;

    if (gsr.touched || gsr.delta > ACTIVITY_INTERACT)
        gsr.lastInteractionMs = nowMs;
}

const uint16_t CAPTURE_SETTLE_MS = 250;
const uint16_t CAPTURE_SAMPLE_MS = 650;

static bool capturing = false;
static uint32_t captureStartMs = 0;
static float captureSum = 0.0f;
static uint16_t captureCount = 0;

static bool animLocked = false;
static uint8_t lockedAnimation = 21;

static void updateModeAndAnimation()
{
    static bool prevTouched = false;

    const bool isIdle = (nowMs - gsr.lastInteractionMs) > IDLE_TIMEOUT_MS;

    if (isIdle && !idleMode)
    {
        idleMode = true;
        capturing = false;
        lastIdleCycleMs = nowMs;

        idleIndex = (idleIndex + 1) % IDLE_PLAYLIST_LEN;
        currentAnimation = IDLE_PLAYLIST[idleIndex];

        Serial.print("ANIM (idle enter) -> ");
        Serial.print((int)currentAnimation);
        Serial.print(" : ");
        Serial.println(ANIM_NAME[currentAnimation]);
    }
    else if (!isIdle && idleMode)
    {
        idleMode = false;

        currentAnimation = animLocked ? lockedAnimation : 21;

        Serial.print("ANIM (idle exit) -> ");
        Serial.print((int)currentAnimation);
        Serial.print(" : ");
        Serial.println(ANIM_NAME[currentAnimation]);
    }

    if (idleMode)
    {
        if (nowMs - lastIdleCycleMs > IDLE_CYCLE_MS)
        {
            lastIdleCycleMs = nowMs;
            idleIndex = (idleIndex + 1) % IDLE_PLAYLIST_LEN;
            currentAnimation = IDLE_PLAYLIST[idleIndex];

            Serial.print("ANIM (idle cycle) -> ");
            Serial.print((int)currentAnimation);
            Serial.print(" : ");
            Serial.println(ANIM_NAME[currentAnimation]);
        }

        prevTouched = gsr.touched;
        return;
    }

    if (gsr.touched && !prevTouched)
    {
        capturing = true;
        captureStartMs = nowMs;
        captureSum = 0.0f;
        captureCount = 0;

        Serial.println("CAPTURE start (new touch)");
    }

    if (capturing)
    {
        uint32_t elapsed = nowMs - captureStartMs;

        if (elapsed >= CAPTURE_SETTLE_MS)
        {
            captureSum += gsr.delta;
            captureCount++;
        }

        if (elapsed >= (uint32_t)(CAPTURE_SETTLE_MS + CAPTURE_SAMPLE_MS))
        {
            capturing = false;

            float avgDelta = (captureCount > 0) ? (captureSum / (float)captureCount) : gsr.delta;
            uint8_t chosen = chooseAnimationFromDelta(avgDelta);

            animLocked = true;
            lockedAnimation = chosen;
            currentAnimation = chosen;

            Serial.print("LOCKED -> ");
            Serial.print((int)chosen);
            Serial.print(" : ");
            Serial.print(ANIM_NAME[chosen]);
            Serial.print(" (avg d=");
            Serial.print(avgDelta, 1);
            Serial.println(")");
        }

        prevTouched = gsr.touched;
        return;
    }

    if (animLocked)
        currentAnimation = lockedAnimation;
    else
        currentAnimation = 21;

    prevTouched = gsr.touched;
}

void animationTwo();       // 2 ripple
void animationFour();      // 4 bioluminescent twinkle
void animationFive();      // 5 noise span
void animationEight();     // 8 bounce ball blend
void animationNine();      // 9 fire
void animationTwelve();    // 12 stem + radial mod
void animationThirteen();  // 13 noise 3d
void animationFourteen();  // 14 radial ripples
void animationFifteen();   // 15 swirl spiral
void animationEighteen();  // 18 sine plasma
void animationNineteen();  // 19 plasma coords
void animationTwentyOne(); // 21 rainbow bloom
void animationTwentyTwo(); // 22 rainbow bloom v2

static void runAnimation(uint8_t id)
{
    switch (id)
    {
    case 2:
        animationTwo();
        break;
    case 4:
        animationFour();
        break;
    case 5:
        animationFive();
        break;
    case 8:
        animationEight();
        break;
    case 9:
        animationNine();
        break;
    case 12:
        animationTwelve();
        break;
    case 13:
        animationThirteen();
        break;
    case 14:
        animationFourteen();
        break;
    case 15:
        animationFifteen();
        break;
    case 18:
        animationEighteen();
        break;
    case 19:
        animationNineteen();
        break;
    case 21:
        animationTwentyOne();
        break;
    case 22:
        animationTwentyTwo();
        break;
    default:
        animationTwentyOne();
        break;
    }
}

void setup()
{
    Serial.begin(115200);

    FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
    FastLED.setBrightness(120);

    // Power safety: tune this to PSU capability.
    FastLED.setMaxPowerInVoltsAndMilliamps(5, 3000);

    buildLedCoords();
    buildStemMap();

    // Initialize shared buffers
    memset(gPrevFrame, 0, sizeof(gPrevFrame));
    memset(gScratchU8A, 0, sizeof(gScratchU8A));

    // Ensure we start on 21 (power-up default)
    currentAnimation = 21;
    gPrevAnim = 255;

    gFrameDirty = true;
}

void loop()
{
    nowMs = millis();

    if (lastAnimMs == 0)
        lastAnimMs = nowMs;
    uint32_t dms = nowMs - lastAnimMs;
    lastAnimMs = nowMs;

    dtSec = (float)dms / 1000.0f;
    if (dtSec > 0.05f)
        dtSec = 0.05f;
    tSec += dtSec;

    updateGsr();
    updateModeAndAnimation();

    if (currentAnimation != gPrevAnim)
    {
        gPrevAnim = currentAnimation;
        gAnimEpoch++;

        fill_solid(leds, NUM_LEDS, CRGB::Black);
        memset(gPrevFrame, 0, sizeof(gPrevFrame));
        memset(gScratchU8A, 0, sizeof(gScratchU8A));

        gFrameDirty = true; // force a show on mode change
    }

    // Only show when something actually updated this loop
    gFrameDirty = false;
    runAnimation(currentAnimation);
    if (gFrameDirty)
        FastLED.show();
}

#define NOISE_SCALE 25

DEFINE_GRADIENT_PALETTE(fire_no_white_gp){
    0, 0, 0, 0,
    64, 120, 10, 0,
    128, 200, 50, 0,
    192, 255, 80, 0,
    255, 180, 30, 0};
CRGBPalette16 fireNoWhitePalette = fire_no_white_gp;

// 2 ripple
void animationTwo()
{
    static uint32_t _epoch = 0;
    static float phase = 0.0f;
    if (_epoch != gAnimEpoch)
    {
        _epoch = gAnimEpoch;
        phase = 0.0f;
    }

    phase += dtSec * 2.1f;

    float d = constrain(gsr.delta, 0.0f, 160.0f);
    float boost = mapf(d, 0, 160, 0.9f, 1.25f);

    // Stem
    for (int i = 0; i < STEM_LEDS; i++)
    {
        float u = stemIsLeaf[i] ? leafPhaseCoordForStemIndex(i) : trunkPhaseCoordForStemIndex(i);
        float dist = fabsf(u) * 14.0f;

        float w = sinf(dist * 1.15f - phase * boost);
        uint8_t bri = (uint8_t)constrain(140.0f + 115.0f * w, 0.0f, 255.0f);

        // leaf segments get a small hue offset so they don't clone the trunk
        uint8_t seg = stemIsLeaf[i] ? (uint8_t)max(0, stemLeafSegId[i]) : 0;
        uint8_t hue = 160 + (uint8_t)(18.0f * sinf(phase * 0.55f)) + (uint8_t)(dist * 2.0f) + seg * 12;

        leds[i] = CHSV(hue, 210, bri);
    }

    // Petals
    int idx = STEM_LEDS;
    for (int petal = 0; petal < PETALS; petal++)
    {
        for (int j = 0; j < LEDS_PER_PETAL; j++)
        {
            float dist = fu16(ledR16[idx]);

            float w = sinf(dist * 1.15f - phase * boost);
            uint8_t bri = (uint8_t)constrain(140.0f + 115.0f * w, 0.0f, 255.0f);
            uint8_t hue = 160 + (uint8_t)(18.0f * sinf(phase * 0.55f)) + (uint8_t)(petal * 3);

            leds[idx++] = CHSV(hue, 200, bri);
        }
    }

    // Bud
    for (int i = 0; i < BUD_LEDS; i++)
    {
        float dist = fu16(ledR16[idx]);

        float w = sinf(dist * 1.15f - phase * boost);
        uint8_t bri = (uint8_t)constrain(140.0f + 115.0f * w, 0.0f, 255.0f);
        uint8_t hue = 192 + (uint8_t)(10.0f * sinf(phase * 0.35f)) + (uint8_t)(dist * 4.0f);

        leds[idx++] = CHSV(hue, 160, bri);
    }

    gFrameDirty = true;
}

// 4 bioluminescent twinkle
void animationFour()
{
    static uint32_t _epoch = 0;
    static uint32_t lastFrameMs = 0;
    static uint8_t hueBase = 180;
    static uint8_t t = 0;

    if (_epoch != gAnimEpoch)
    {
        _epoch = gAnimEpoch;
        lastFrameMs = 0;
        memset(gScratchU8A, 0, sizeof(gScratchU8A)); // sparkleBrightness
        hueBase = 180;
        t = 0;
    }

    if (nowMs - lastFrameMs < 30)
        return; // ~33 FPS
    lastFrameMs = nowMs;

    int ledIndex = 0;

    // Stem
    for (int i = 0; i < STEM_LEDS; i++)
    {
        if (random8() < 4 && gScratchU8A[ledIndex] == 0)
            gScratchU8A[ledIndex] = 180 + random8(75);

        if (gScratchU8A[ledIndex] > 0)
        {
            // Small per-segment offset so leaf twinkles don't mirror trunk exactly
            uint8_t seg = stemIsLeaf[i] ? (uint8_t)max(0, stemLeafSegId[i]) : 0;
            leds[ledIndex] = CHSV(hueBase + i + seg * 9, 100, gScratchU8A[ledIndex]);
            gScratchU8A[ledIndex] = qsub8(gScratchU8A[ledIndex], 3);
        }
        else
            leds[ledIndex] = CRGB::Black;

        ledIndex++;
    }

    // Petals
    for (int petal = 0; petal < PETALS; petal++)
    {
        for (int i = 0; i < LEDS_PER_PETAL; i++)
        {
            if (random8() < 5 && gScratchU8A[ledIndex] == 0)
                gScratchU8A[ledIndex] = 200 + random8(55);

            if (gScratchU8A[ledIndex] > 0)
            {
                leds[ledIndex] = CHSV(hueBase + sin8(t + petal * 10), 150, gScratchU8A[ledIndex]);
                gScratchU8A[ledIndex] = qsub8(gScratchU8A[ledIndex], 2);
            }
            else
                leds[ledIndex] = CRGB::Black;

            ledIndex++;
        }
    }

    // Bud
    for (int i = 0; i < BUD_LEDS; i++)
    {
        if (random8() < 3 && gScratchU8A[ledIndex] == 0)
            gScratchU8A[ledIndex] = 140 + random8(90);

        if (gScratchU8A[ledIndex] > 0)
        {
            float r = fu16(ledR16[ledIndex]);
            uint8_t wave = sin8(t + (uint8_t)(r * 18.0f));
            leds[ledIndex] = CHSV(hueBase + wave / 3, 180, gScratchU8A[ledIndex]);
            gScratchU8A[ledIndex] = qsub8(gScratchU8A[ledIndex], 2);
        }
        else
            leds[ledIndex] = CRGB::Black;

        ledIndex++;
    }

    t += 1;
    hueBase += 1;

    gFrameDirty = true;
}

// 5 noise span
void animationFive()
{
    static uint32_t _epoch = 0;
    static uint16_t x = 0;
    static uint16_t y = 0;
    static float travel = 0.0f;

    if (_epoch != gAnimEpoch)
    {
        _epoch = gAnimEpoch;
        x = 0;
        y = 0;
        travel = 0.0f;
    }

    travel += dtSec * 2.2f;

    const uint8_t STEM_SAT = 235;
    const uint8_t STEM_V = 230;

    const uint8_t LEAF_SAT = 240;
    const uint8_t LEAF_V = 235;

    // stem
    for (int i = 0; i < STEM_LEDS; i++)
    {
        if (stemIsLeaf[i])
        {
            float phase = leafPhaseCoordForStemIndex(i);
            float shifted = phase - travel;

            uint8_t seg = (stemLeafSegId[i] < 0) ? 0 : (uint8_t)stemLeafSegId[i];

            uint16_t nx = (uint16_t)((shifted * 900.0f) + x);
            uint8_t n = inoise8(nx, (uint16_t)(y + seg * 97));

            uint8_t bri = qadd8(150, (n >> 2));
            uint8_t h = (uint8_t)(n + (uint8_t)(x >> 3) + seg * 16);

            leds[i] = CHSV(h, LEAF_SAT, (uint8_t)constrain(bri, 0, LEAF_V));
        }
        else
        {
            float phase = trunkPhaseCoordForStemIndex(i);
            float shifted = phase - travel;

            uint16_t nx = (uint16_t)((shifted * 900.0f) + x);
            uint8_t n = inoise8(nx, y);

            uint8_t bri = qadd8(160, (n >> 2));
            uint8_t h = (uint8_t)(n + (uint8_t)(x >> 3));
            leds[i] = CHSV(h, STEM_SAT, (uint8_t)constrain(bri, 0, STEM_V));
        }
    }

    // Petals
    int ledIndex = STEM_LEDS;
    for (int petal = 0; petal < PETALS; petal++)
    {
        for (int i = 0; i < LEDS_PER_PETAL; i++)
        {
            int indexOffset = (ledIndex - STEM_LEDS) + i;
            uint8_t noise = inoise8(x + indexOffset * 20, y);
            leds[ledIndex + i] = CHSV(noise, 255, 255);
        }
        ledIndex += LEDS_PER_PETAL;
    }

    // Bud
    for (int i = 0; i < BUD_LEDS; i++)
    {
        uint8_t noise = inoise8(x + i * 10, y + 50);
        leds[ledIndex + i] = CHSV(noise, 255, 255);
    }

    x += 2;
    y += 2;

    gFrameDirty = true;
}

// 8 bounce ball blend
void animationEight()
{
    static uint32_t _epoch = 0;
    static uint32_t lastFrameMs = 0;

    static uint8_t hueShift = 0;

    // Stem ball
    static float stemBallY = 0;
    static int8_t stemBallDY = 1;
    static uint8_t stemHueShift = 0;

    // Petal grid ball
    static float ballX = 0, ballY = 0;
    static int8_t ballDX = 1, ballDY = 1;

    // Bud ball
    static float budBallX = 0, budBallY = 0;
    static int8_t budBallDX = 1, budBallDY = 1;

    if (_epoch != gAnimEpoch)
    {
        _epoch = gAnimEpoch;
        lastFrameMs = 0;

        hueShift = 0;

        memset(gPrevFrame, 0, sizeof(gPrevFrame));

        stemBallY = 0;
        stemBallDY = 1;
        stemHueShift = 0;

        ballX = 0;
        ballY = 0;
        ballDX = 1;
        ballDY = 1;

        budBallX = 0;
        budBallY = 0;
        budBallDX = 1;
        budBallDY = 1;
    }

    if (nowMs - lastFrameMs < 60)
        return; // ~16 FPS
    lastFrameMs = nowMs;

    stemBallY += stemBallDY;
    if (stemBallY <= 0 || stemBallY >= STEM_LEDS - 1)
        stemBallDY = -stemBallDY;

    for (int i = 0; i < STEM_LEDS; i++)
    {
        float dist = fabsf((STEM_LEDS - 1 - i) - stemBallY);
        uint8_t hue = sin8(stemHueShift + (uint8_t)(dist * 10.0f));
        leds[i] = CHSV(hue, 255, 255);
        // optional: keep gPrevFrame for stem too, but not required
        gPrevFrame[i] = leds[i];
    }
    stemHueShift++;

    ballX += ballDX;
    ballY += ballDY;

    if (ballX <= 0 || ballX >= (LEDS_PER_PETAL - 1))
        ballDX = -ballDX;
    if (ballY <= 0 || ballY >= (PETALS - 1))
        ballDY = -ballDY;

    float aspect = (float)PETALS / (float)LEDS_PER_PETAL;

    int idx = STEM_LEDS;
    for (int p = 0; p < PETALS; p++)
    {
        for (int j = 0; j < LEDS_PER_PETAL; j++)
        {
            float xx = j * aspect;
            float yy = (float)p;

            float dist = sqrtf((xx - ballX) * (xx - ballX) + (yy - ballY) * (yy - ballY));
            uint8_t hue = sin8(hueShift + (uint8_t)(dist * 8.0f));

            CRGB rgb;
            hsv2rgb_spectrum(CHSV(hue, 255, 255), rgb);

            // Blend with previous frame stored in gPrevFrame
            leds[idx] = blend(gPrevFrame[idx], rgb, 128);
            gPrevFrame[idx] = rgb;
            idx++;
        }
    }

    budBallX += budBallDX;
    budBallY += budBallDY;

    if (budBallX <= 0 || budBallX >= (BUD_COLS - 1))
        budBallDX = -budBallDX;
    if (budBallY <= 0 || budBallY >= (BUD_ROWS - 1))
        budBallDY = -budBallDY;

    for (int y = 0; y < BUD_ROWS; y++)
    {
        for (int x = 0; x < BUD_COLS; x++)
        {
            int bi = y * BUD_COLS + x;
            if (bi >= BUD_LEDS)
                break;

            float dist = sqrtf((x - budBallX) * (x - budBallX) + (y - budBallY) * (y - budBallY));
            uint8_t hue = sin8(hueShift + (uint8_t)(dist * 8.0f));
            leds[idx + bi] = CHSV(hue, 150, 255);
            gPrevFrame[idx + bi] = leds[idx + bi];
        }
    }

    hueShift++;

    gFrameDirty = true;
}

// 9 fire
void animationNine()
{
    static uint32_t _epoch = 0;
    static const CRGBPalette16 pal = LavaColors_p;
    static float zf = 0.0f;
    if (_epoch != gAnimEpoch)
    {
        _epoch = gAnimEpoch;
        zf = 0.0f;
    }

    zf += dtSec * 22.0f;
    const float S = 22.0f;
    const float ZS = 35.0f;

    for (int i = 0; i < NUM_LEDS; i++)
    {
        float x = fx(ledX16[i]);
        float y = fx(ledY16[i]);

        uint8_t n = inoise8(
            (uint16_t)(x * S * 8.0f),
            (uint16_t)(y * S * 8.0f),
            (uint16_t)(zf * ZS));

        uint8_t idx = n;
        uint8_t bri = qadd8(80, scale8(n, 175));

        float r = fu16(ledR16[i]);
        uint8_t vign = (uint8_t)constrain(255.0f - r * 10.0f, 160.0f, 255.0f);
        bri = scale8(bri, vign);

        leds[i] = ColorFromPalette(pal, idx, bri, LINEARBLEND);
    }

    gFrameDirty = true;
}

// 12 stem + radial mod
void animationTwelve()
{
    static uint32_t _epoch = 0;
    static uint8_t hueShift = 0;
    static float travel = 0.0f;

    if (_epoch != gAnimEpoch)
    {
        _epoch = gAnimEpoch;
        hueShift = 0;
        travel = 0.0f;
    }

    travel += dtSec * 2.6f;

    const uint8_t STEM_SAT = 235;
    const uint8_t STEM_V = 230;

    const uint8_t LEAF_SAT = 245;
    const uint8_t LEAF_V = 235;

    const float RAD_HUE_PER_UNIT = 38.0f;

    for (int i = 0; i < STEM_LEDS; i++)
    {
        if (stemIsLeaf[i])
        {
            uint8_t h = leafHueFlow(hueShift, travel, RAD_HUE_PER_UNIT, i);
            leds[i] = CHSV(h, LEAF_SAT, LEAF_V);
        }
        else
        {
            uint8_t h = trunkHueFlow(hueShift, travel, RAD_HUE_PER_UNIT, i);
            leds[i] = CHSV(h, STEM_SAT, STEM_V);
        }
    }

    for (int i = STEM_LEDS; i < NUM_LEDS; i++)
    {
        float ang = (float)ledAng16[i] * (2.0f * PI / 65535.0f);
        float r = fu16(ledR16[i]);

        uint8_t baseHue = hueShift;
        float hueMod = ang * 20.0f - r * 8.0f;
        uint8_t hue = sin8(baseHue + (int)hueMod);

        leds[i] = CHSV(hue, 255, 255);
    }

    hueShift++;

    gFrameDirty = true;
}

// 13 noise 3d
void animationThirteen()
{
    static uint32_t _epoch = 0;
    static float zf = 0.0f;
    if (_epoch != gAnimEpoch)
    {
        _epoch = gAnimEpoch;
        zf = 0.0f;
    }

    const float Z_SCROLL_SPEED = 18.0f;
    const float HUE_DRIFT_SPEED = 3.0f;

    zf += dtSec * Z_SCROLL_SPEED;

    const float S = 18.0f;
    const float ZS = 60.0f;
    const uint8_t POSTER = 6;

    uint8_t hueBase = (uint8_t)fmodf(tSec * HUE_DRIFT_SPEED, 255.0f);

    for (int i = 0; i < NUM_LEDS; i++)
    {
        float x = fx(ledX16[i]);
        float y = fx(ledY16[i]);
        float r = fu16(ledR16[i]);

        uint8_t n = inoise8(
            (uint16_t)(x * S * 10.0f),
            (uint16_t)(y * S * 10.0f),
            (uint16_t)(zf * ZS));

        uint8_t ridge = qsub8(255, abs8((int)n - 128) * 2);

        uint8_t band = (ridge * POSTER) >> 8;
        uint8_t bri = (band * (255 / (POSTER - 1)));
        bri = qadd8(45, scale8(bri, 210));

        uint8_t hue = hueBase + (uint8_t)(r * 18.0f) + (uint8_t)(n >> 3);
        leds[i] = CHSV(hue, 210, bri);
    }

    gFrameDirty = true;
}

// 14 radial ripples
void animationFourteen()
{
    static uint32_t _epoch = 0;
    static uint8_t hueShift = 0;
    static float tt = 0.0f;
    if (_epoch != gAnimEpoch)
    {
        _epoch = gAnimEpoch;
        hueShift = 0;
        tt = 0.0f;
    }

    for (int i = 0; i < NUM_LEDS; i++)
    {
        // angle 0..65535 and radius in float (small)
        float ang = (float)ledAng16[i] * (2.0f * PI / 65535.0f);
        float dist = fu16(ledR16[i]) * 2.0f;

        float wave = sinf(dist - tt);
        uint8_t hue = (uint8_t)(((ang + wave) * 255.0f / (2 * PI)) + hueShift);

        leds[i] = CHSV(hue, 255, 255);
    }

    tt += 0.07f;
    hueShift += 1;

    gFrameDirty = true;
}

// 15 swirl spiral
void animationFifteen()
{
    static uint32_t _epoch = 0;
    static uint8_t hueShift = 0;
    static float time = 0.0f;
    if (_epoch != gAnimEpoch)
    {
        _epoch = gAnimEpoch;
        hueShift = 0;
        time = 0.0f;
    }

    // Convert time into an 8-bit phase (keeps trig fast via sin8)
    uint8_t t8 = (uint8_t)(time * 40.0f); // speed knob

    for (int i = 0; i < NUM_LEDS; i++)
    {
        uint8_t ang8 = (uint8_t)(ledAng16[i] >> 8);            // 0..255
        uint8_t r8 = (uint8_t)min(255, (int)(ledR16[i] >> 3)); // radius-ish 0..255

        // swirl: angle + time - radius
        uint8_t swirl = ang8 + (t8 >> 1) - (r8 >> 2);
        uint8_t hue = sin8(swirl + hueShift);

        uint8_t sat = (i < STEM_LEDS) ? 255 : (i < (STEM_LEDS + PETALS * LEDS_PER_PETAL) ? 255 : 150);
        uint8_t val = (i < STEM_LEDS) ? 200 : (i < (STEM_LEDS + PETALS * LEDS_PER_PETAL) ? 255 : 220);

        leds[i] = CHSV(hue, sat, val);
    }

    hueShift += 1;
    time += 0.05f;

    gFrameDirty = true;
}

// 18 sine plasma
void animationEighteen()
{
    static uint32_t _epoch = 0;
    static float phase = 0.0f;
    if (_epoch != gAnimEpoch)
    {
        _epoch = gAnimEpoch;
        phase = 0.0f;
    }

    phase += dtSec * 1.25f;

    int idx = 0;

    // Stem
    for (int i = 0; i < STEM_LEDS; i++)
    {
        float u = stemIsLeaf[i] ? leafPhaseCoordForStemIndex(i) : trunkPhaseCoordForStemIndex(i);
        float x = (-u) * 6.0f; // frequency on stem

        uint8_t seg = stemIsLeaf[i] ? (uint8_t)max(0, stemLeafSegId[i]) : 0;
        uint8_t hue = (uint8_t)(128 + 127 * sinf(x + phase + seg * 0.35f));
        leds[idx++] = CHSV(hue, 255, 255);
    }

    // Petals / Bud
    float scale = 0.10f;

    float cx = LEDS_PER_PETAL / 2.0f;
    float cy = PETALS / 2.0f;

    for (int petal = 0; petal < PETALS; petal++)
    {
        for (int i = 0; i < LEDS_PER_PETAL; i++)
        {
            float dx = i - cx;
            float dy = petal - cy;
            float dist = sqrtf(dx * dx + dy * dy) * scale;
            uint8_t hue = (uint8_t)(128 + 127 * sinf(dist + phase));
            leds[idx++] = CHSV(hue, 255, 255);
        }
    }

    for (int y = 0; y < BUD_ROWS; y++)
    {
        for (int x = 0; x < BUD_COLS; x++)
        {
            int bi = y * BUD_COLS + x;
            if (bi >= BUD_LEDS)
                break;

            float dx = x - (BUD_COLS / 2.0f);
            float dy = y - (BUD_ROWS / 2.0f);
            float dist = sqrtf(dx * dx + dy * dy) * scale;

            uint8_t hue = (uint8_t)(128 + 127 * sinf(dist + phase));
            leds[idx++] = CHSV(hue, 255, 255);
        }
    }

    gFrameDirty = true;
}

// 19 plasma coords
void animationNineteen()
{
    static uint32_t _epoch = 0;
    static float tt = 0.0f;

    if (_epoch != gAnimEpoch)
    {
        _epoch = gAnimEpoch;
        tt = 0.0f;
        memset(gPrevFrame, 0, sizeof(gPrevFrame));
    }

    tt += dtSec * 0.28f;
    const uint8_t SMOOTH = 200;

    for (int i = 0; i < NUM_LEDS; i++)
    {
        float nx = fx(ledX16[i]) * 0.2f;
        float ny = fx(ledY16[i]) * 0.2f;

        float v =
            sinf(nx * 10.0f + tt) +
            sinf(10.0f * (nx * sinf(tt / 2.0f) + ny * cosf(tt / 3.0f)) + tt) +
            sinf(sqrtf(100.0f * (nx * nx + ny * ny) + 1.0f) + tt);

        uint8_t hue = (uint8_t)(fabsf(sinf(v * PI)) * 255.0f);
        CRGB cur = CHSV(hue, 255, 255);

        leds[i] = blend(gPrevFrame[i], cur, 255 - SMOOTH);
        gPrevFrame[i] = leds[i];
    }

    gFrameDirty = true;
}

// 21 Rainbow Bloom
void animationTwentyOne()
{
    static uint32_t _epoch = 0;

    static float huePhase = 0.0f;
    static float travel = 0.0f;
    static float kickSm = 1.0f;

    if (_epoch != gAnimEpoch)
    {
        _epoch = gAnimEpoch;
        memset(gPrevFrame, 0, sizeof(gPrevFrame));
        huePhase = 0.0f;
        travel = 0.0f;
        kickSm = 1.0f;
    }

    const uint8_t SMOOTH_RGB = 175;

    float d = constrain(gsr.delta, 0.0f, 180.0f);
    float kick = mapf(d, 0, 180, 1.0f, 1.55f);

    const float KICK_SMOOTH_HZ = 6.0f;
    float kk = 1.0f - expf(-dtSec * KICK_SMOOTH_HZ);
    kickSm += (kick - kickSm) * kk;

    const float HUE_SPEED_BASE = 48.0f;
    const float FLOW_SPEED_BASE = 3.2f;

    huePhase += dtSec * (HUE_SPEED_BASE * kickSm);
    if (huePhase >= 255.0f)
        huePhase -= 255.0f * floorf(huePhase / 255.0f);

    travel += dtSec * (FLOW_SPEED_BASE * kickSm);

    const float BUD_R_MAX = 4.6f;
    const float PETAL_R_MIN = 5.0f;
    const float PETAL_R_MAX = 9.5f;

    float travelPetal = travel - (BUD_R_MAX * 1.02f);
    if (travelPetal < 0.0f)
        travelPetal = 0.0f;

    uint8_t feedHue = (uint8_t)huePhase;

    // Stem
    const uint8_t STEM_SAT = 235;
    const uint8_t STEM_V = 230;

    const uint8_t LEAF_SAT = 240;
    const uint8_t LEAF_V = 235;

    const float RAD_HUE_PER_UNIT_STEM = 34.0f;

    for (int i = 0; i < STEM_LEDS; i++)
    {
        if (stemIsLeaf[i])
        {
            uint8_t h = leafHueFlow(feedHue, travel, RAD_HUE_PER_UNIT_STEM, i);
            leds[i] = CHSV(h, LEAF_SAT, LEAF_V);
        }
        else
        {
            uint8_t h = trunkHueFlow(feedHue, travel, RAD_HUE_PER_UNIT_STEM, i);
            leds[i] = CHSV(h, STEM_SAT, STEM_V);
        }
    }

    // Bud
    const uint8_t BUD_SAT = 230;
    const float BUD_RAD_HUE_PER_R = 22.0f;

    const int petalStart = STEM_LEDS;
    const int petalEnd = STEM_LEDS + (PETALS * LEDS_PER_PETAL);

    for (int i = petalEnd; i < NUM_LEDS; i++)
    {
        float r = fu16(ledR16[i]);
        float rShift = r - (travel * 0.95f);

        int16_t h = (int16_t)feedHue + (int16_t)(rShift * BUD_RAD_HUE_PER_R);

        float r01 = constrain(r / BUD_R_MAX, 0.0f, 1.0f);
        float vF = 240.0f - (r01 * 55.0f);
        if (!gsr.touched)
            vF *= 0.985f;
        uint8_t v = (uint8_t)constrain(vF, 0.0f, 255.0f);

        leds[i] = CHSV((uint8_t)h, BUD_SAT, v);
    }

    // Petals
    const uint8_t PETAL_SAT = 255;
    const float RAD_HUE_PER_R = 30.0f;

    for (int i = petalStart; i < petalEnd; i++)
    {
        float r = fu16(ledR16[i]);

        float pr = r - PETAL_R_MIN;
        if (pr < 0.0f)
            pr = 0.0f;

        float rShift = pr - travelPetal;

        int16_t h = (int16_t)feedHue + (int16_t)(rShift * RAD_HUE_PER_R);

        float pr01 = constrain(pr / (PETAL_R_MAX - PETAL_R_MIN), 0.0f, 1.0f);
        float vF = 245.0f - (pr01 * 65.0f);
        if (!gsr.touched)
            vF *= 0.98f;
        uint8_t v = (uint8_t)constrain(vF, 0.0f, 255.0f);

        leds[i] = CHSV((uint8_t)h, PETAL_SAT, v);
    }

    for (int i = 0; i < NUM_LEDS; i++)
    {
        leds[i] = blend(gPrevFrame[i], leds[i], 255 - SMOOTH_RGB);
        gPrevFrame[i] = leds[i];
    }

    gFrameDirty = true;
}

// 22 rainbow bloom v2
void animationTwentyTwo()
{
    static uint32_t _epoch = 0;

    static float huePhase = 0.0f;
    static float travel = 0.0f;
    static float kickSm = 1.0f;

    static float budRMax = 4.6f;
    static float petalRMin = 5.0f;
    static float petalRMax = 9.5f;

    const int petalStart = STEM_LEDS;
    const int petalEnd = STEM_LEDS + (PETALS * LEDS_PER_PETAL);

    if (_epoch != gAnimEpoch)
    {
        _epoch = gAnimEpoch;
        memset(gPrevFrame, 0, sizeof(gPrevFrame));

        huePhase = 0.0f;
        travel = 0.0f;
        kickSm = 1.0f;

        budRMax = 0.0f;
        for (int i = petalEnd; i < NUM_LEDS; i++)
        {
            float r = fu16(ledR16[i]);
            if (r > budRMax)
                budRMax = r;
        }

        petalRMin = 1e9f;
        petalRMax = 0.0f;
        for (int i = petalStart; i < petalEnd; i++)
        {
            float r = fu16(ledR16[i]);
            if (r < petalRMin)
                petalRMin = r;
            if (r > petalRMax)
                petalRMax = r;
        }

        if (petalRMin > petalRMax)
        {
            petalRMin = 5.0f;
            petalRMax = 9.5f;
        }
        if (budRMax <= 0.01f)
            budRMax = 4.6f;
    }

    const uint8_t SMOOTH_RGB = 175;

    float d = constrain(gsr.delta, 0.0f, 180.0f);
    float kick = mapf(d, 0, 180, 1.0f, 1.55f);

    const float KICK_SMOOTH_HZ = 6.0f;
    float kk = 1.0f - expf(-dtSec * KICK_SMOOTH_HZ);
    kickSm += (kick - kickSm) * kk;

    const float HUE_SPEED_BASE = 48.0f;
    const float FLOW_SPEED_BASE = 3.2f;

    huePhase += dtSec * (HUE_SPEED_BASE * kickSm);
    if (huePhase >= 255.0f)
        huePhase -= 255.0f * floorf(huePhase / 255.0f);

    travel += dtSec * (FLOW_SPEED_BASE * kickSm);

    const uint8_t feedHue = (uint8_t)huePhase;

    auto advectStemPetal = [&](float phaseCoord) -> float
    { return (phaseCoord - travel); };
    auto advectBud = [&](float phaseCoord) -> float
    { return (phaseCoord + travel); };

    const float RAD_HUE_PER_UNIT = 28.0f;

    const uint8_t STEM_SAT = 235;
    const uint8_t STEM_V = 230;

    const uint8_t LEAF_SAT = 245;
    const uint8_t LEAF_V = 235;

    const int16_t trunkTopPos = (int16_t)STEM_TRUNK_TOTAL - 1; // should be 84

    for (int i = 0; i < STEM_LEDS; i++)
    {
        if (stemIsLeaf[i])
        {
            uint8_t h = leafHueFlow(feedHue, travel, RAD_HUE_PER_UNIT, i);
            leds[i] = CHSV(h, LEAF_SAT, LEAF_V);
        }
        else
        {
            int16_t trunkPos = stemTrunkPos[i];
            if (trunkPos < 0)
                trunkPos = 0;

            float distFromTop = (float)(trunkTopPos - trunkPos);
            float phaseCoord = -(distFromTop * TRUNK_UNIT_PER_LED); // trunk top == 0

            float shifted = advectStemPetal(phaseCoord);
            int16_t h = (int16_t)feedHue + (int16_t)(shifted * RAD_HUE_PER_UNIT);

            leds[i] = CHSV((uint8_t)h, STEM_SAT, STEM_V);
        }
    }

    const uint8_t BUD_SAT = 220;

    for (int i = petalEnd; i < NUM_LEDS; i++)
    {
        float r = fu16(ledR16[i]);
        if (r > budRMax)
            r = budRMax;

        float phaseCoord = r;
        float shifted = advectBud(phaseCoord);

        int16_t h = (int16_t)feedHue + (int16_t)(shifted * RAD_HUE_PER_UNIT);

        float r01 = constrain(r / budRMax, 0.0f, 1.0f);
        float vF = 240.0f - (r01 * 55.0f);
        if (!gsr.touched)
            vF *= 0.985f;
        uint8_t v = (uint8_t)constrain(vF, 0.0f, 255.0f);

        leds[i] = CHSV((uint8_t)h, BUD_SAT, v);
    }

    const uint8_t PETAL_SAT = 255;
    const float petalSpan = (petalRMax - petalRMin);

    for (int i = petalStart; i < petalEnd; i++)
    {
        float r = fu16(ledR16[i]);

        float pr = r - petalRMin;
        if (pr < 0.0f)
            pr = 0.0f;
        if (pr > petalSpan)
            pr = petalSpan;

        float phaseCoord = budRMax + pr;
        float shifted = advectStemPetal(phaseCoord);

        int16_t h = (int16_t)feedHue + (int16_t)(shifted * RAD_HUE_PER_UNIT);

        float pr01 = (petalSpan > 0.001f) ? constrain(pr / petalSpan, 0.0f, 1.0f) : 0.0f;
        float vF = 245.0f - (pr01 * 65.0f);
        if (!gsr.touched)
            vF *= 0.98f;
        uint8_t v = (uint8_t)constrain(vF, 0.0f, 255.0f);

        leds[i] = CHSV((uint8_t)h, PETAL_SAT, v);
    }

    for (int i = 0; i < NUM_LEDS; i++)
    {
        leds[i] = blend(gPrevFrame[i], leds[i], 255 - SMOOTH_RGB);
        gPrevFrame[i] = leds[i];
    }

    gFrameDirty = true;
}
