#include <Arduino.h>
#include <FastLED.h>

const int GSR = A0;
#define DATA_PIN 6

#define NUM_LEDS 383
#define STEM_LEDS 103 // 51 stem, 52 leaves
#define NUM_SMALL_PETALS 10
#define NUM_LARGE_PETALS 10
#define NUM_LEDS_SMALL_PETAL 11
#define NUM_LEDS_LARGE_PETAL 13

constexpr int NUM_PETALS = (NUM_SMALL_PETALS + NUM_LARGE_PETALS);
constexpr float CENTER_X = 4.5f;
constexpr float CENTER_Y = 7.5f;
constexpr float FLOW_CX = 4.5f;
constexpr float FLOW_CY = 7.5f;

CRGB leds[NUM_LEDS];
static CRGB gPrev[NUM_LEDS];

constexpr uint8_t TRUNK_SEG_LEN = 17;
constexpr uint8_t LEAF_SEG_LEN = 26;

constexpr uint8_t TRUNK0_START = 0;
constexpr uint8_t LEAF0_START = TRUNK0_START + TRUNK_SEG_LEN; // 17
constexpr uint8_t TRUNK1_START = LEAF0_START + LEAF_SEG_LEN;  // 43
constexpr uint8_t LEAF1_START = TRUNK1_START + TRUNK_SEG_LEN; // 60
constexpr uint8_t TRUNK2_START = LEAF1_START + LEAF_SEG_LEN;  // 86

constexpr uint8_t TRUNK_TOTAL_LEDS = TRUNK_SEG_LEN * 3; // 51

static bool stemIsTrunk[STEM_LEDS];
static uint8_t stemTrunkOrd[STEM_LEDS]; // 0..50 (only valid when trunk)
static uint8_t stemLeafId[STEM_LEDS];   // 0..1  (only valid when leaf)
static uint8_t stemLeafPos[STEM_LEDS];  // 0..25 (only valid when leaf)

static void buildStemMap()
{
    for (int i = 0; i < STEM_LEDS; i++)
    {
        stemIsTrunk[i] = false;
        stemTrunkOrd[i] = 0;
        stemLeafId[i] = 0;
        stemLeafPos[i] = 0;
    }

    uint8_t ord = 0;

    for (int i = TRUNK0_START; i < TRUNK0_START + TRUNK_SEG_LEN; i++)
    {
        stemIsTrunk[i] = true;
        stemTrunkOrd[i] = ord++;
    }

    for (int i = LEAF0_START; i < LEAF0_START + LEAF_SEG_LEN; i++)
    {
        stemLeafId[i] = 0;
        stemLeafPos[i] = (uint8_t)(i - LEAF0_START);
    }

    for (int i = TRUNK1_START; i < TRUNK1_START + TRUNK_SEG_LEN; i++)
    {
        stemIsTrunk[i] = true;
        stemTrunkOrd[i] = ord++;
    }

    for (int i = LEAF1_START; i < LEAF1_START + LEAF_SEG_LEN; i++)
    {
        stemLeafId[i] = 1;
        stemLeafPos[i] = (uint8_t)(i - LEAF1_START);
    }

    for (int i = TRUNK2_START; i < TRUNK2_START + TRUNK_SEG_LEN; i++)
    {
        stemIsTrunk[i] = true;
        stemTrunkOrd[i] = ord++;
    }
}

static inline float trunkU_fromStemIndex(int i)
{
    return (TRUNK_TOTAL_LEDS <= 1) ? 0.0f : (float)stemTrunkOrd[i] / (float)(TRUNK_TOTAL_LEDS - 1);
}

static inline float leafU_fromStemIndex(int i)
{
    return (LEAF_SEG_LEN <= 1) ? 0.0f : (float)stemLeafPos[i] / (float)(LEAF_SEG_LEN - 1);
}

static uint32_t nowMs;
const uint16_t SENSOR_SAMPLE_MS = 15; // ~66 Hz
const uint16_t SENSOR_PRINT_MS = 250; // debug rate
const uint16_t ANIM_MIN_HOLD_MS = 1200;
const uint32_t IDLE_TIMEOUT_MS = 30000; // 30000 - if the sensor isn't used for 30 seconds, rotate through patterns
const uint32_t IDLE_CYCLE_MS = 60000;   // 60000 - change idel pattern every minute

const uint8_t TARGET_FPS = 60;
const uint16_t FRAME_MS = (1000 / TARGET_FPS);
static uint32_t lastRenderMs = 0;

struct GsrState
{
    uint16_t raw = 0;
    float filtered = 0;
    float baseline = 0;
    float delta = 0;
    bool touched = false;

    uint32_t lastSampleMs = 0;
    uint32_t lastPrintMs = 0;

    uint32_t lastInteractionMs = 0;
};

GsrState gsr;

const float EMA_FAST = 0.20f;
const float EMA_BASE = 0.0025f;
const float TOUCH_ON = 18.0f;
const float TOUCH_OFF = 10.0f;
const float ACTIVITY_INTERACT = 8.0f;

uint8_t currentAnimation = 1;
uint32_t lastAnimChangeMs = 0;
uint32_t lastIdleCycleMs = 0;
bool idleMode = false;
const uint8_t IDLE_PLAYLIST[] = {1, 20, 2, 3, 4, 5, 21, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
const uint8_t IDLE_PLAYLIST_LEN = sizeof(IDLE_PLAYLIST) / sizeof(IDLE_PLAYLIST[0]);
uint8_t idleIndex = 0;

const char *ANIM_NAME[22] = {
    "NA",
    "1 noise petals",
    "2 ripple",
    "3 spiral ripple",
    "4 bioluminescent twinkle",
    "5 noise span",
    "6 breathing aurora",
    "7 radial hue wheel",
    "8 bounce ball blend",
    "9 petal phasing mandala",
    "10 orbiting fireflies",
    "11 perlin field",
    "12 stem + radial mod",
    "13 distortion waves",
    "14 radial ripples",
    "15 swirl spiral",
    "16 flowing noise",
    "17 nebula + stars",
    "18 lava lamp field",
    "19 oil-slick lava drift",
    "20 rainbow tunnel bloom",
    "21 rainbow bloom",
};

// -------------------------
// Epoch reset system
// -------------------------
static uint32_t gAnimEpoch = 1;

#define ANIM_EPOCH_GUARD()      \
    static uint32_t _epoch = 0; \
    if (_epoch != gAnimEpoch)   \
    {                           \
        _epoch = gAnimEpoch;

#define ANIM_EPOCH_END() }

static inline void clearAllLeds()
{
    fill_solid(leds, NUM_LEDS, CRGB::Black);
}

static inline int ledsInPetal(int petal)
{
    return (petal % 2 == 0) ? NUM_LEDS_SMALL_PETAL : NUM_LEDS_LARGE_PETAL;
}

static float ledX[NUM_LEDS];
static float ledY[NUM_LEDS];
static float ledR[NUM_LEDS];        // radius from (CENTER_X,CENTER_Y)
static uint16_t ledAng16[NUM_LEDS]; // 0..65535 around (CENTER_X,CENTER_Y)

static inline float ang16_to_rad(uint16_t a16)
{
    return (float)a16 * (2.0f * PI / 65535.0f);
}

static void getLEDCoordinates(int index, float &x, float &y)
{
    if (index < STEM_LEDS)
    {
        const float trunkX = 4.5f;
        const float trunkStepY = 0.12f;
        const float leafStep = 0.12f;
        const float leafOutBase = 0.5f;

        if (stemIsTrunk[index])
        {
            x = trunkX;
            y = stemTrunkOrd[index] * trunkStepY;
            return;
        }
        else
        {
            uint8_t leafId = stemLeafId[index];
            float attachOrd = (leafId == 0) ? (TRUNK_SEG_LEN - 1) : (TRUNK_SEG_LEN * 2 - 1);
            float attachY = attachOrd * trunkStepY;

            float u = leafU_fromStemIndex(index);
            float side = (leafId == 0) ? -1.0f : 1.0f;

            x = trunkX + side * (leafOutBase + u * (LEAF_SEG_LEN * leafStep));
            y = attachY + u * 0.35f;
            return;
        }
    }

    int ledIndex = STEM_LEDS;

    for (int petal = 0; petal < NUM_PETALS; petal++)
    {
        int numLedsInPetal = ledsInPetal(petal);

        if (index < ledIndex + numLedsInPetal)
        {
            int ledInPetal = index - ledIndex;

            float angle = petal * (2.0f * PI / (float)NUM_PETALS);
            float radius = 5.0f + ledInPetal * 0.5f;

            x = 4.5f + radius * cosf(angle);
            y = 7.5f + radius * sinf(angle);
            return;
        }

        ledIndex += numLedsInPetal;
    }

    x = 0;
    y = 0;
}

static void buildLedCoords()
{
    for (int i = 0; i < NUM_LEDS; i++)
    {
        float x, y;
        getLEDCoordinates(i, x, y);
        ledX[i] = x;
        ledY[i] = y;

        float dx = x - CENTER_X;
        float dy = y - CENTER_Y;

        ledR[i] = sqrtf(dx * dx + dy * dy);

        float a = atan2f(dy, dx);
        if (a < 0)
            a += 2.0f * PI;
        ledAng16[i] = (uint16_t)(a * (65535.0f / (2.0f * PI)));
    }
}

static uint8_t chooseAnimationFromDelta(float d)
{
    if (d < 8)
        return 12;
    if (d < 16)
        return 4;
    if (d < 24)
        return 1;
    if (d < 32)
        return 5;
    if (d < 40)
        return 20;
    if (d < 48)
        return 21;
    if (d < 56)
        return 2;
    if (d < 64)
        return 3;
    if (d < 72)
        return 7;
    if (d < 80)
        return 9;
    if (d < 90)
        return 11;
    if (d < 100)
        return 10;
    if (d < 112)
        return 13;
    if (d < 125)
        return 16;
    if (d < 140)
        return 17;
    if (d < 155)
        return 18;
    if (d < 170)
        return 19;
    if (d < 185)
        return 8;
    if (d < 205)
        return 15;
    if (d < 220)
        return 14;
    return 20;
}

static void updateGsr()
{
    if (nowMs - gsr.lastSampleMs < SENSOR_SAMPLE_MS)
        return;
    gsr.lastSampleMs = nowMs;

    gsr.raw = analogRead(GSR);

    if (gsr.filtered == 0 && gsr.baseline == 0)
    {
        gsr.filtered = gsr.raw;
        gsr.baseline = gsr.raw;
        gsr.lastInteractionMs = nowMs;
    }
    else
    {
        gsr.filtered = (1.0f - EMA_FAST) * gsr.filtered + EMA_FAST * (float)gsr.raw;

        gsr.delta = fabsf(gsr.filtered - gsr.baseline);

        if (!gsr.touched && gsr.delta > TOUCH_ON)
            gsr.touched = true;
        else if (gsr.touched && gsr.delta < TOUCH_OFF)
            gsr.touched = false;

        if (!gsr.touched)
        {
            gsr.baseline = (1.0f - EMA_BASE) * gsr.baseline + EMA_BASE * gsr.filtered;
        }

        if (gsr.touched || gsr.delta > ACTIVITY_INTERACT)
        {
            gsr.lastInteractionMs = nowMs;
        }
    }
}

const uint16_t CAPTURE_SETTLE_MS = 250;
const uint16_t CAPTURE_SAMPLE_MS = 650;
static bool capturing = false;
static uint32_t captureStartMs = 0;
static float captureSum = 0.0f;
static uint16_t captureCount = 0;
static bool animLocked = false;
static uint8_t lockedAnimation = 12;

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
        lastAnimChangeMs = nowMs;

        Serial.print("ANIM (idle enter) -> ");
        Serial.print((int)currentAnimation);
        Serial.print(" : ");
        Serial.println(ANIM_NAME[currentAnimation]);
    }
    else if (!isIdle && idleMode)
    {
        idleMode = false;
        lastAnimChangeMs = nowMs;

        currentAnimation = animLocked ? lockedAnimation : 12;

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
            lastAnimChangeMs = nowMs;

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
            lastAnimChangeMs = nowMs;

            Serial.print("LOCKED -> ");
            Serial.print((int)currentAnimation);
            Serial.print(" : ");
            Serial.print(ANIM_NAME[currentAnimation]);
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
        currentAnimation = 12;

    prevTouched = gsr.touched;
}

static float dtSec = 0.0f;
static float tSec = 0.0f;

#define NUM_FLIES 3

static inline float mapf(float x, float in_min, float in_max, float out_min, float out_max)
{
    if (in_max == in_min)
        return out_min;
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Forward declarations
void animationOne();
void animationTwo();
void animationThree();
void animationFour();
void animationFive();
void animationSix();
void animationSeven();
void animationEight();
void animationNine();
void animationTen();
void animationEleven();
void animationTwelve();
void animationThirteen();
void animationFourteen();
void animationFifteen();
void animationSixteen();
void animationSeventeen();
void animationEighteen();
void animationNineteen();
void animationTwenty();
void animationTwentyOne();

void setup()
{
    FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
    FastLED.setBrightness(120);

    Serial.begin(115200);

    buildStemMap();
    buildLedCoords();

    clearAllLeds();
    fill_solid(gPrev, NUM_LEDS, CRGB::Black);

    FastLED.show();
}

void loop()
{
    nowMs = millis();

    updateGsr();
    updateModeAndAnimation();

    if (idleMode)
    {
        gsr.touched = false;
        gsr.delta = 0.0f;
    }

    static uint8_t prevAnim = 0;
    if (currentAnimation != prevAnim)
    {
        prevAnim = currentAnimation;

        if (++gAnimEpoch == 0)
            gAnimEpoch = 1;

        tSec = 0.0f;

        fill_solid(gPrev, NUM_LEDS, CRGB::Black);

        random16_set_seed((uint16_t)(0xBEEF ^ (currentAnimation * 109)));
    }

    if (nowMs - lastRenderMs < FRAME_MS)
        return;

    uint32_t dms = nowMs - lastRenderMs;
    lastRenderMs = nowMs;

    dtSec = (float)dms / 1000.0f;
    if (dtSec > 0.05f)
        dtSec = 0.05f;
    tSec += dtSec;

    switch (currentAnimation)
    {
    case 1:
        animationOne();
        break;
    case 2:
        animationTwo();
        break;
    case 3:
        animationThree();
        break;
    case 4:
        animationFour();
        break;
    case 5:
        animationFive();
        break;
    case 6:
        animationSix();
        break;
    case 7:
        animationSeven();
        break;
    case 8:
        animationEight();
        break;
    case 9:
        animationNine();
        break;
    case 10:
        animationTen();
        break;
    case 11:
        animationEleven();
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
    case 16:
        animationSixteen();
        break;
    case 17:
        animationSeventeen();
        break;
    case 18:
        animationEighteen();
        break;
    case 19:
        animationNineteen();
        break;
    case 20:
        animationTwenty();
        break;
    case 21:
        animationTwentyOne();
        break;
    default:
        animationTwelve();
        break;
    }

    FastLED.show();
}

void animationOne()
{
    static uint16_t x = 0;
    static uint16_t y = 0;

    ANIM_EPOCH_GUARD()
    x = 0;
    y = 0;
    ANIM_EPOCH_END();

    for (int i = 0; i < STEM_LEDS; i++)
        leds[i] = CRGB::Green;

    int ledIndex = STEM_LEDS;

    for (int petal = 0; petal < NUM_PETALS; petal++)
    {
        int n = ledsInPetal(petal);

        for (int i = 0; i < n; i++)
        {
            uint8_t noise = inoise8(x + (n - i) * 20, y + petal * 20);
            leds[ledIndex + i] = CHSV(noise, 255, 255);
        }

        ledIndex += n;
    }

    x += 2;
    y += 2;
}

// 2 ripple
void animationTwo()
{
    static float phase = 0.0f;

    ANIM_EPOCH_GUARD()
    phase = 0.0f;
    ANIM_EPOCH_END();

    phase += dtSec * 2.1f;

    float d = constrain(gsr.delta, 0.0f, 160.0f);
    float boost = mapf(d, 0, 160, 0.9f, 1.25f);

    for (int i = 0; i < STEM_LEDS; i++)
    {
        float dist = ledR[i];
        float w = sinf(dist * 1.15f - phase * boost);
        uint8_t brightness = (uint8_t)constrain(140.0f + 115.0f * w, 0.0f, 255.0f);

        uint8_t hue = 160 + (uint8_t)(18.0f * sinf(phase * 0.55f)) + (uint8_t)(dist * 6.0f);
        leds[i] = CHSV(hue, 210, brightness);
    }

    int ledIndex = STEM_LEDS;
    for (int petal = 0; petal < NUM_PETALS; petal++)
    {
        int n = ledsInPetal(petal);
        for (int j = 0; j < n; j++)
        {
            float dist = ledR[ledIndex];
            float w = sinf(dist * 1.15f - phase * boost);
            uint8_t brightness = (uint8_t)constrain(140.0f + 115.0f * w, 0.0f, 255.0f);

            uint8_t hue = 160 + (uint8_t)(18.0f * sinf(phase * 0.55f)) + (uint8_t)(petal * 3);
            leds[ledIndex++] = CHSV(hue, 210, brightness);
        }
    }
}

// 3 spiral ripple
void animationThree()
{
    static uint8_t t = 0;

    ANIM_EPOCH_GUARD()
    t = 0;
    ANIM_EPOCH_END();

    for (int i = 0; i < STEM_LEDS; i++)
    {
        if (!stemIsTrunk[i])
            continue;

        float u = (TRUNK_TOTAL_LEDS <= 1) ? 0.0f
                                          : (float)stemTrunkOrd[i] / (float)(TRUNK_TOTAL_LEDS - 1);

        uint8_t hue = sin8((uint8_t)(t + (uint8_t)(u * 128.0f)));
        leds[i] = CHSV(32 + hue / 4, 255, 200);
    }

    for (int i = 0; i < STEM_LEDS; i++)
    {
        if (stemIsTrunk[i])
            continue;

        float u = leafU_fromStemIndex(i);
        uint8_t n = inoise8(
            (uint16_t)(tSec * 260.0f) + (uint16_t)(stemLeafId[i] * 12000),
            (uint16_t)(u * 9000.0f) + (uint16_t)(stemLeafId[i] * 7000));

        uint8_t hue = 90 + (n >> 2);
        uint8_t v = 160 + (uint8_t)(80.0f * (0.5f + 0.5f * sinf(tSec * 2.0f + u * 6.0f)));
        leds[i] = CHSV(hue, 220, v);
    }

    int ledIndex = STEM_LEDS;
    for (int petal = 0; petal < NUM_PETALS; petal++)
    {
        int n = ledsInPetal(petal);
        for (int j = 0; j < n; j++)
        {
            float angle = ang16_to_rad(ledAng16[ledIndex]); // 0..2PI
            // Convert to -PI..PI for a similar mapping feel:
            float aSigned = angle;
            if (aSigned > PI)
                aSigned -= 2.0f * PI;

            float radius = ledR[ledIndex];

            float hf = (aSigned * 128.0f / PI) + (radius * 32.0f) + (float)t;
            while (hf < 0)
                hf += 255.0f;
            while (hf >= 255.0f)
                hf -= 255.0f;
            uint8_t hue = (uint8_t)hf;

            float vf = 235.0f - (radius * 6.0f);
            vf += 18.0f * (0.5f + 0.5f * sinf(tSec * 1.6f));
            uint8_t val = (uint8_t)constrain(vf, 90.0f, 255.0f);

            leds[ledIndex++] = CHSV(hue, 200, val);
        }
    }

    t += 2;
}

// 4 biolum twinkle
void animationFour()
{
    static uint32_t lastFrameMs = 0;
    static uint8_t sparkleBrightness[NUM_LEDS] = {0};
    static uint8_t hueBase = 180;
    static uint8_t t = 0;

    ANIM_EPOCH_GUARD()
    lastFrameMs = 0;
    memset(sparkleBrightness, 0, sizeof(sparkleBrightness));
    hueBase = 180;
    t = 0;
    clearAllLeds();
    ANIM_EPOCH_END();

    // with global FPS cap, this is usually redundant but kept for the twinkle feel
    if (nowMs - lastFrameMs < 30)
        return;
    lastFrameMs = nowMs;

    int ledIndex = 0;

    for (int i = 0; i < STEM_LEDS; i++)
    {
        if (random8() < 3 && sparkleBrightness[i] == 0)
            sparkleBrightness[i] = 170 + random8(70);

        if (sparkleBrightness[i] > 0)
        {
            leds[ledIndex] = CHSV(hueBase + i, 110, sparkleBrightness[i]);
            sparkleBrightness[i] = qsub8(sparkleBrightness[i], 2);
        }
        else
        {
            leds[ledIndex] = CRGB::Black;
        }

        ledIndex++;
    }

    for (int petal = 0; petal < NUM_PETALS; petal++)
    {
        int n = ledsInPetal(petal);

        for (int i = 0; i < n; i++)
        {
            if (random8() < 4 && sparkleBrightness[ledIndex] == 0)
                sparkleBrightness[ledIndex] = 190 + random8(60);

            if (sparkleBrightness[ledIndex] > 0)
            {
                leds[ledIndex] = CHSV(hueBase + sin8(t + petal * 10), 160, sparkleBrightness[ledIndex]);
                sparkleBrightness[ledIndex] = qsub8(sparkleBrightness[ledIndex], 2);
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
}

// 5 noise span
void animationFive()
{
    static float tt = 0.0f;

    ANIM_EPOCH_GUARD()
    tt = 0.0f;
    ANIM_EPOCH_END();

    tt += dtSec * 0.85f;

    float d = constrain(gsr.delta, 0.0f, 160.0f);
    float boost = mapf(d, 0, 160, 0.9f, 1.35f);

    const float SX = 85.0f;
    const float SY = 145.0f;

    uint16_t ox = (uint16_t)(tt * 220.0f * boost);
    uint16_t oy = (uint16_t)(tt * 140.0f * boost);

    for (int i = 0; i < NUM_LEDS; i++)
    {
        float x = ledX[i];
        float y = ledY[i];

        uint16_t nx = (uint16_t)(x * SX) + ox;
        uint16_t ny = (uint16_t)(y * SY) + oy;

        uint8_t n = inoise8(nx, ny);

        uint8_t hue = 140 + (n / 2);
        uint8_t sat = gsr.touched ? 210 : 235;
        uint8_t val = qadd8(80, scale8(n, 175));

        leds[i] = CHSV(hue, sat, val);
    }
}

// 6 breathing aurora
void animationSix()
{
    static uint16_t t = 0;

    ANIM_EPOCH_GUARD()
    t = 0;
    clearAllLeds();
    ANIM_EPOCH_END();

    t++;

    float d = constrain(gsr.delta, 0.0f, 140.0f);
    uint8_t activity = (uint8_t)map((int)d, 0, 140, 90, 255);
    uint8_t baseHue = (uint8_t)(t / 3);

    fadeToBlackBy(leds, NUM_LEDS, 7);

    for (int i = 0; i < STEM_LEDS; i++)
    {
        uint8_t posHue = baseHue + (i * 2);
        uint8_t wave = sin8((uint8_t)(t + i * 6));
        uint8_t n = inoise8((uint16_t)(t * 4), (uint16_t)(i * 40));
        uint8_t v = qadd8((wave / 2), (n / 3));
        v = qadd8(35, scale8(v, activity));
        leds[i] += CHSV(140 + posHue / 4, 200, v);
    }

    int ledIndex = STEM_LEDS;
    for (int petal = 0; petal < NUM_PETALS; petal++)
    {
        int nL = ledsInPetal(petal);

        for (int j = 0; j < nL; j++)
        {
            float dx = (float)j - CENTER_X;
            float dy = (float)petal - CENTER_Y;
            float r = sqrtf(dx * dx + dy * dy);

            uint8_t hue = baseHue + (uint8_t)(r * 18.0f) + (petal * 3);
            uint8_t wave = sin8((uint8_t)(t + r * 22.0f + petal * 10));
            uint8_t n2 = inoise8((uint16_t)(t * 5 + petal * 200), (uint16_t)(j * 80));
            uint8_t v = qadd8(wave / 2, n2 / 4);
            v = qadd8(45, scale8(v, activity));

            leds[ledIndex++] += CHSV(hue, 180, v);
        }
    }

    if (gsr.touched || gsr.delta > 30.0f)
    {
        uint8_t glints = (uint8_t)map((int)constrain(gsr.delta, 0.0f, 180.0f), 0, 180, 1, 5);
        for (uint8_t k = 0; k < glints; k++)
        {
            uint16_t idx = random16(NUM_LEDS);
            leds[idx] += CHSV(baseHue + random8(32), 80, 255);
        }
    }
}

// 7 radial hue wheel
void animationSeven()
{
    static float hueShift = 0.0f;
    static float stemPhase = 0.0f;

    ANIM_EPOCH_GUARD()
    fill_solid(gPrev, NUM_LEDS, CRGB::Black);
    hueShift = 0.0f;
    stemPhase = 0.0f;
    ANIM_EPOCH_END();

    const uint8_t SMOOTH = 70;

    hueShift += dtSec * 42.0f;

    float d = constrain(gsr.delta, 0.0f, 160.0f);
    float punch = mapf(d, 0, 160, 0.85f, 1.20f);

    stemPhase += dtSec * 2.2f;

    for (int i = 0; i < STEM_LEDS; i++)
    {
        if (stemIsTrunk[i])
        {
            float u = trunkU_fromStemIndex(i);
            float w = sinf((u * 2.0f * PI * 1.6f) + stemPhase);

            uint8_t v = (uint8_t)constrain(130.0f + 125.0f * (0.5f + 0.5f * w) * punch, 0.0f, 255.0f);
            uint8_t hue = (uint8_t)fmodf(110.0f + hueShift + (u * 80.0f), 255.0f);

            leds[i] = CHSV(hue, 215, v);
        }
        else
        {
            float u = leafU_fromStemIndex(i);
            float w = 0.5f + 0.5f * sinf((u * 2.0f * PI * 1.3f) + stemPhase * 1.25f + stemLeafId[i] * 1.7f);

            uint8_t hue = (uint8_t)fmodf(110.0f + hueShift + stemLeafId[i] * 35.0f + (u * 65.0f), 255.0f);
            uint8_t v = (uint8_t)constrain(115.0f + 140.0f * w * punch, 0.0f, 255.0f);

            leds[i] = CHSV(hue, 235, v);
        }
    }

    int ledIndex = STEM_LEDS;
    for (int petal = 0; petal < NUM_PETALS; petal++)
    {
        int n = ledsInPetal(petal);
        for (int j = 0; j < n; j++)
        {
            float ang = ang16_to_rad(ledAng16[ledIndex]); // 0..2PI
            float r = ledR[ledIndex];

            float h = (ang * 255.0f / (2.0f * PI)) + hueShift;

            float band = floorf((ang / (2.0f * PI)) * 10.0f) / 10.0f;
            h += band * 90.0f;

            h = fmodf(h, 255.0f);
            if (h < 0)
                h += 255.0f;

            float bw = 0.5f + 0.5f * sinf((ang * 3.0f) + (hueShift * 0.045f));
            float rv = 1.0f - constrain(r * 0.03f, 0.0f, 0.35f);

            uint8_t v = (uint8_t)constrain((150.0f + 105.0f * bw) * rv * punch, 0.0f, 255.0f);
            leds[ledIndex++] = CHSV((uint8_t)h, 255, v);
        }
    }

    for (int i = 0; i < NUM_LEDS; i++)
    {
        leds[i] = blend(gPrev[i], leds[i], 255 - SMOOTH);
        gPrev[i] = leds[i];
    }
}

// 8 bounce ball blend
void animationEight()
{
    static uint32_t lastFrameMs = 0;
    static uint8_t hueShift = 0;
    static CRGB previousLeds[NUM_LEDS];

    static float stemBallY = 0;
    static int8_t stemBallDY = 1;
    static uint8_t stemHueShift = 0;

    static float ballX = 0;
    static float ballY = 0;
    static int8_t ballDX = 1;
    static int8_t ballDY = 1;

    ANIM_EPOCH_GUARD()
    lastFrameMs = 0;
    hueShift = 0;
    memset(previousLeds, 0, sizeof(previousLeds));

    stemBallY = 0;
    stemBallDY = 1;
    stemHueShift = 0;

    ballX = 0;
    ballY = 0;
    ballDX = 1;
    ballDY = 1;

    clearAllLeds();
    ANIM_EPOCH_END();

    if (nowMs - lastFrameMs < 100)
        return;
    lastFrameMs = nowMs;

    stemBallY += stemBallDY;
    if (stemBallY <= 0 || stemBallY >= (TRUNK_TOTAL_LEDS - 1))
        stemBallDY = -stemBallDY;

    for (int i = 0; i < STEM_LEDS; i++)
    {
        if (!stemIsTrunk[i])
            continue;

        float dist = fabsf(((float)stemTrunkOrd[i]) - stemBallY);
        uint8_t hue = sin8(stemHueShift + (uint8_t)(dist * 10.0f));
        leds[i] = CHSV(hue, 255, 255);
    }

    for (int i = 0; i < STEM_LEDS; i++)
    {
        if (stemIsTrunk[i])
            continue;

        float attachOrd = (stemLeafId[i] == 0) ? (TRUNK_SEG_LEN - 1) : (TRUNK_SEG_LEN * 2 - 1);
        float dOrd = fabsf(attachOrd - stemBallY);
        float hit = expf(-(dOrd * dOrd) * 0.10f);
        float u = leafU_fromStemIndex(i);

        uint8_t hue = sin8(stemHueShift + stemLeafId[i] * 35 + (uint8_t)(u * 60.0f));
        uint8_t v = (uint8_t)constrain(70.0f + 185.0f * hit * (0.6f + 0.4f * sinf(tSec * 6.0f + u * 9.0f)), 0.0f, 255.0f);

        leds[i] = CHSV(hue, 255, v);
    }

    stemHueShift++;

    const float maxPetalY = (NUM_PETALS)-1;
    const float maxPetalX = NUM_LEDS_LARGE_PETAL - 1;

    ballX += ballDX;
    ballY += ballDY;

    if (ballX <= 0 || ballX >= maxPetalX)
        ballDX = -ballDX;
    if (ballY <= 0 || ballY >= maxPetalY)
        ballDY = -ballDY;

    float aspectRatio = maxPetalY / maxPetalX;

    int ledIndex = STEM_LEDS;

    for (int petal = 0; petal < NUM_PETALS; petal++)
    {
        int n = ledsInPetal(petal);

        for (int i = 0; i < n; i++)
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
        }
    }

    hueShift++;
}

// 9 Petal Mandala
void animationNine()
{
    static bool init = false;
    static float petSeed[NUM_PETALS];

    ANIM_EPOCH_GUARD()
    fill_solid(gPrev, NUM_LEDS, CRGB::Black);
    init = false;
    memset(petSeed, 0, sizeof(petSeed));
    clearAllLeds();
    ANIM_EPOCH_END();

    if (!init)
    {
        for (int p = 0; p < NUM_PETALS; p++)
        {
            uint16_t s = (uint16_t)(p * 1234 + 777);
            petSeed[p] = (float)inoise8(s, s + 9000) / 255.0f;
        }
        init = true;
    }

    const uint8_t SMOOTH_RGB = 165;
    const uint8_t STEM_FLOOR = 14;
    const uint8_t PETAL_FLOOR = 60;

    float d = constrain(gsr.delta, 0.0f, 180.0f);
    float boost = mapf(d, 0, 180, 0.90f, 1.30f);
    float spin1 = tSec * (0.080f * boost);
    float spin2 = tSec * (0.043f * boost);
    float breathe = 0.5f + 0.5f * sinf(tSec * 0.12f);
    float zoom = 1.0f + 0.060f * sinf(tSec * 0.10f);
    float sweepT = tSec * (0.095f * boost);
    float ringT = tSec * (0.065f * boost);
    float ringCenter = 6.5f + 5.0f * (0.5f + 0.5f * sinf(ringT));

    CRGBPalette16 palA = PartyColors_p;
    CRGBPalette16 palB = OceanColors_p;

    for (int i = 0; i < STEM_LEDS; i++)
    {
        float u = (float)i / (float)(STEM_LEDS - 1);

        float w1 = 0.5f + 0.5f * sinf((u * 7.2f) - (tSec * 0.85f * boost));
        float w2 = 0.5f + 0.5f * sinf((u * 3.6f) + (tSec * 0.55f * boost));
        float w3 = 0.5f + 0.5f * sinf((u * 1.8f) + (tSec * 0.22f * boost) + 1.2f);

        float glow = 0.46f * w1 + 0.34f * w2 + 0.20f * w3;
        glow = constrain(glow, 0.0f, 1.0f);

        uint8_t hueBase = (uint8_t)(tSec * 7.0f);
        uint8_t hue = hueBase + (uint8_t)(u * 80.0f);

        uint8_t v = (uint8_t)constrain(STEM_FLOOR + (int)(glow * 165.0f * (0.88f + 0.12f * breathe)), 0, 255);

        CRGB c = ColorFromPalette(palB, hue, v, LINEARBLEND);
        c = blend(c, CRGB(0, 22, 0), 90);
        leds[i] = c;
    }

    int ledIndex = STEM_LEDS;

    const int SLICES = 6;
    float wedge = (2.0f * PI) / (float)SLICES;

    uint8_t satBase = (uint8_t)constrain(185 + (int)(d * 0.25f), 185, 255);

    for (int petal = 0; petal < NUM_PETALS; petal++)
    {
        int nL = ledsInPetal(petal);

        float pSeed = petSeed[petal];
        float petPhase = (float)petal * 0.33f + pSeed * 2.4f;

        float petDrift = 0.35f * sinf(tSec * 0.08f + petPhase);
        float sweep = 0.5f + 0.5f * sinf(sweepT + petPhase);

        for (int j = 0; j < nL; j++)
        {
            float v01 = (nL <= 1) ? 0.0f : ((float)j / (float)(nL - 1));

            float r = ledR[ledIndex] * zoom;
            float ang = ang16_to_rad(ledAng16[ledIndex]);

            float a = fmodf(ang + spin1 + petDrift, wedge);
            if (a < 0)
                a += wedge;
            if (a > wedge * 0.5f)
                a = wedge - a;
            float aN = a / (wedge * 0.5f);

            float spokes1 = 0.5f + 0.5f * sinf((aN * 7.2f) + (r * 0.38f) - (tSec * 0.32f * boost));
            float spokes2 = 0.5f + 0.5f * sinf((aN * 4.8f) - (r * 0.52f) + (spin2 * 2.2f));
            float radial = 0.5f + 0.5f * sinf((r * 0.62f) + (ang * 1.5f) + (tSec * 0.22f * boost));

            float dS = fabsf(v01 - sweep);
            float sweepGlow = expf(-dS * dS * 9.0f);

            float dr = (r - ringCenter);
            float ring = expf(-(dr * dr) * 0.16f);

            float tip = v01 * v01;
            float tipGlow = 0.30f + 0.70f * tip;

            float field = 0.38f * spokes1 +
                          0.22f * spokes2 +
                          0.20f * radial +
                          0.14f * sweepGlow +
                          0.06f * ring;

            field = constrain(field, 0.0f, 1.0f);

            uint8_t vFloor = PETAL_FLOOR;
            uint8_t vRange = 185;
            float amp = 0.82f + 0.18f * breathe;

            uint8_t bri = (uint8_t)constrain(vFloor + (int)(field * (float)vRange * amp * tipGlow), 0, 255);

            uint8_t hueBase = (uint8_t)(tSec * 8.0f);
            uint8_t huePet = (uint8_t)(petal * (255 / NUM_PETALS));

            uint8_t hueS1 = (uint8_t)(spokes1 * 70.0f);
            uint8_t hueS2 = (uint8_t)(spokes2 * 55.0f);
            uint8_t hueRg = (uint8_t)(ring * 40.0f);

            uint8_t h1 = hueBase + huePet + hueS1 + (uint8_t)(v01 * 28.0f);
            uint8_t h2 = hueBase + (uint8_t)(255 - huePet) + hueS2 + hueRg + (uint8_t)(v01 * 58.0f);

            uint8_t mix = (uint8_t)constrain(85 + (int)(spokes1 * 90.0f) + (int)(ring * 55.0f), 0, 255);

            CRGB cA = ColorFromPalette(palA, h1, bri, LINEARBLEND);
            CRGB cB = ColorFromPalette(palB, h2, bri, LINEARBLEND);
            CRGB c = blend(cA, cB, mix);

            if (!gsr.touched)
            {
                uint8_t g = (uint8_t)((c.r + c.g + c.b) / 3);
                c = blend(c, CRGB(g, g, g), 12);
            }
            else
            {
                uint8_t add = (uint8_t)constrain((int)((sweepGlow + ring) * 45.0f), 0, 85);
                c += CHSV(h1 + 18, satBase - 30, add);
            }

            leds[ledIndex++] = c;
        }
    }

    for (int i = 0; i < NUM_LEDS; i++)
    {
        leds[i] = blend(gPrev[i], leds[i], 255 - SMOOTH_RGB);
        gPrev[i] = leds[i];
    }
}

// 10 orbiting fireflies
void animationTen()
{
    static bool init = false;
    static float flyAngle[NUM_FLIES];
    static float flySpeed[NUM_FLIES];
    static float flyRadius[NUM_FLIES];
    static float flyTw[NUM_FLIES];

    ANIM_EPOCH_GUARD()
    init = false;
    memset(flyAngle, 0, sizeof(flyAngle));
    memset(flySpeed, 0, sizeof(flySpeed));
    memset(flyRadius, 0, sizeof(flyRadius));
    memset(flyTw, 0, sizeof(flyTw));
    clearAllLeds();
    ANIM_EPOCH_END();

    if (!init)
    {
        for (int i = 0; i < NUM_FLIES; i++)
        {
            flyAngle[i] = random(0, 628) / 100.0f;
            flySpeed[i] = random(18, 38) / 10.0f;
            flyRadius[i] = random(34, 86) / 10.0f;
            flyTw[i] = random(0, 628) / 100.0f;
        }
        init = true;
    }

    fadeToBlackBy(leds, NUM_LEDS, 22);

    for (int i = 0; i < NUM_LEDS; i++)
        leds[i] += CRGB(2, 0, 6);

    float d = constrain(gsr.delta, 0.0f, 160.0f);
    float boost = mapf(d, 0, 160, 1.0f, 2.15f);

    const float cx = 4.5f;
    const float cy = 7.5f;

    const float K = 0.16f;
    const float CUTOFF = 0.0105f;
    const float BODY_GAIN = 1.10f;
    const float HEAD_GAIN = 1.55f;

    for (int f = 0; f < NUM_FLIES; f++)
    {
        flyAngle[f] += flySpeed[f] * boost * dtSec;
        if (flyAngle[f] > 2 * PI)
            flyAngle[f] -= 2 * PI;

        flyTw[f] += dtSec * (1.1f + 0.25f * f);

        float r = flyRadius[f] + 0.85f * sinf((tSec * 0.9f) + f * 1.7f);

        float fx = cx + r * cosf(flyAngle[f]);
        float fy = cy + r * sinf(flyAngle[f]);

        uint8_t hue = 64 + (uint8_t)(f * 12);
        uint8_t sat = 210;

        float tw = 0.72f + 0.28f * (0.5f + 0.5f * sinf(flyTw[f] * 2.0f));
        float gain = (0.9f + 0.6f * boost) * tw;

        for (int i = 0; i < NUM_LEDS; i++)
        {
            float dx = ledX[i] - fx;
            float dy = ledY[i] - fy;
            float dist2 = dx * dx + dy * dy;

            float intensity = expf(-dist2 * K) * gain;
            if (intensity < CUTOFF)
                continue;

            float v = intensity * 255.0f * BODY_GAIN;
            if (v > 255.0f)
                v = 255.0f;

            leds[i] += CHSV(hue, sat, (uint8_t)v);
        }

        uint16_t bestIdx = 0;
        float bestD2 = 1e9f;
        for (int i = 0; i < NUM_LEDS; i++)
        {
            float dx = ledX[i] - fx;
            float dy = ledY[i] - fy;
            float d2 = dx * dx + dy * dy;
            if (d2 < bestD2)
            {
                bestD2 = d2;
                bestIdx = i;
            }
        }

        uint8_t headV = (uint8_t)constrain(170.0f + 65.0f * boost * tw, 0.0f, 255.0f);
        leds[bestIdx] += CHSV(hue, 180, (uint8_t)constrain(headV * HEAD_GAIN, 0.0f, 255.0f));
    }

    if (gsr.touched || gsr.delta > 30.0f)
    {
        uint8_t sp = (uint8_t)map((int)constrain(gsr.delta, 0.0f, 180.0f), 0, 180, 1, 7);
        for (uint8_t k = 0; k < sp; k++)
        {
            uint16_t idx = random16(NUM_LEDS);
            leds[idx] += CHSV(50 + random8(30), 80, 255);
        }
    }
}

// 11 Perlin field
void animationEleven()
{
    const uint16_t STEM_Y_SCALE = 420;
    const uint16_t PETAL_X_SCALE = 260;
    const uint16_t PETAL_Y_SCALE = 520;

    const float FLOW_X = 2.2f;
    const float FLOW_Y = 1.4f;
    const float FLOW_Z = 0.45f;

    uint8_t sat = gsr.touched ? 175 : 210;
    uint8_t valBase = gsr.touched ? 235 : 200;

    uint16_t ox = (uint16_t)(tSec * FLOW_X * 256.0f);
    uint16_t oy = (uint16_t)(tSec * FLOW_Y * 256.0f);
    uint16_t oz = (uint16_t)(tSec * FLOW_Z * 256.0f);

    int ledIndex = 0;

    for (int i = 0; i < STEM_LEDS; i++)
    {
        if (stemIsTrunk[i])
        {
            uint16_t x = ox;
            uint16_t y = (uint16_t)(stemTrunkOrd[i] * STEM_Y_SCALE) + oy;

            uint8_t n = inoise8(x, y, oz);
            uint8_t hue = n;
            uint8_t val = scale8(qadd8(n, 80), valBase);

            leds[ledIndex++] = CHSV(hue, sat, val);
        }
        else
        {
            float u = leafU_fromStemIndex(i);
            uint16_t x = (uint16_t)(stemLeafId[i] * 16000) + ox;
            uint16_t y = (uint16_t)(u * (LEAF_SEG_LEN * STEM_Y_SCALE * 0.9f)) + oy;

            uint8_t n = inoise8(x, y, oz);

            uint8_t hue = n + (stemLeafId[i] ? 25 : 0);
            uint8_t val = scale8(qadd8(n, 70), valBase);

            leds[ledIndex++] = CHSV(hue, sat, val);
        }
    }

    for (int petal = 0; petal < NUM_PETALS; petal++)
    {
        int nLeds = ledsInPetal(petal);

        for (int i = 0; i < nLeds; i++)
        {
            uint16_t x = (uint16_t)(i * PETAL_X_SCALE) + ox;
            uint16_t y = (uint16_t)(petal * PETAL_Y_SCALE) + oy;

            uint8_t n = inoise8(x, y, oz);

            uint8_t hue = n + (uint8_t)(tSec * 1.5f);
            uint8_t val = scale8(qadd8(n, 60), valBase);

            leds[ledIndex++] = CHSV(hue, sat, val);
        }
    }
}

// 12 stem + radial mod
void animationTwelve()
{
    static uint8_t hueShift = 0;
    static uint16_t stemOffset = 0;

    ANIM_EPOCH_GUARD()
    hueShift = 0;
    stemOffset = 0;
    ANIM_EPOCH_END();

    for (int i = 0; i < STEM_LEDS; i++)
    {
        if (!stemIsTrunk[i])
            continue;

        int rev = (TRUNK_TOTAL_LEDS - 1) - stemTrunkOrd[i];
        uint8_t hue = sin8(stemOffset + rev * 8);
        leds[i] = CHSV(hue, 255, 255);
    }

    for (int i = 0; i < STEM_LEDS; i++)
    {
        if (stemIsTrunk[i])
            continue;

        float u = leafU_fromStemIndex(i);
        uint8_t n = inoise8(
            (uint16_t)(tSec * 220.0f) + (uint16_t)(stemLeafId[i] * 12000),
            (uint16_t)(u * 9000.0f) + (uint16_t)(stemLeafId[i] * 7000));

        uint8_t hue = n + (stemLeafId[i] ? 40 : 0);
        uint8_t v = 165 + (uint8_t)(70.0f * (0.5f + 0.5f * sinf(tSec * 2.3f + u * 5.5f)));
        leds[i] = CHSV(hue, 220, v);
    }

    stemOffset += 2;

    int ledIndex = STEM_LEDS;
    for (int petal = 0; petal < NUM_PETALS; petal++)
    {
        int n = ledsInPetal(petal);

        for (int i = 0; i < n; i++)
        {
            float angle = ang16_to_rad(ledAng16[ledIndex]);
            float radius = ledR[ledIndex];

            uint8_t baseHue = hueShift;
            float hueMod = angle * 20.0f - radius * 8.0f;
            uint8_t hue = baseHue + (int)hueMod;
            hue = sin8(hue);

            leds[ledIndex++] = CHSV(hue, 255, 255);
        }
    }

    hueShift++;
}

// 13 Distortion Waves
void animationThirteen()
{
    static uint8_t warpLP[NUM_LEDS];

    ANIM_EPOCH_GUARD()
    fill_solid(gPrev, NUM_LEDS, CRGB::Black);
    memset(warpLP, 0, sizeof(warpLP));
    clearAllLeds();
    ANIM_EPOCH_END();

    const uint8_t SMOOTH_RGB = 130;
    const uint8_t STEM_FLOOR = 12;
    const uint8_t PETAL_FLOOR = 45;
    const uint8_t WARP_ALPHA = 18;

    float d = constrain(gsr.delta, 0.0f, 180.0f);
    float boost = mapf(d, 0, 180, 0.90f, 1.45f);

    float speed = 0.85f * boost;
    float phase = tSec * speed;
    float hueT = tSec * (7.5f * boost);

    const float K1 = 0.95f;
    const float K2 = 0.62f;

    float distAmp = mapf(d, 0, 180, 0.55f, 1.35f);

    float advX = tSec * (0.22f * boost);
    float advY = tSec * (0.17f * boost);

    CRGBPalette16 pal = PartyColors_p;
    uint8_t sat = gsr.touched ? 220 : 200;

    for (int i = 0; i < NUM_LEDS; i++)
    {
        bool isStem = (i < STEM_LEDS);

        float x = ledX[i];
        float y = ledY[i];

        float xn = (x * 0.22f) + advX;
        float yn = (y * 0.22f) + advY;

        uint8_t w = inoise8(
            (uint16_t)(xn * 256.0f * 85.0f),
            (uint16_t)(yn * 256.0f * 85.0f),
            (uint16_t)(tSec * 256.0f * 18.0f));

        warpLP[i] = (uint8_t)((((uint16_t)warpLP[i] * (255 - WARP_ALPHA)) +
                               ((uint16_t)w * (WARP_ALPHA))) >>
                              8);

        float warp = ((int)warpLP[i] - 128) / 128.0f;

        uint8_t w2 = inoise8(
            (uint16_t)((xn * 256.0f * 63.0f) + 19000),
            (uint16_t)((yn * 256.0f * 63.0f) - 12000),
            (uint16_t)(tSec * 256.0f * 14.0f + 9000));

        float warp2 = ((int)w2 - 128) / 128.0f;

        float u1 = (xn * K1) + (yn * (K1 * 0.86f));
        float u2 = (xn * K2) - (yn * (K2 * 1.08f));

        float ph1 = (u1 * 6.0f) - (phase * 2.1f) + (warp * distAmp * 2.2f);
        float ph2 = (u2 * 6.8f) + (phase * 1.55f) + (warp2 * distAmp * 1.6f);

        float wave1 = sinf(ph1);
        float wave2 = sinf(ph2);

        float field = 0.62f * wave1 + 0.38f * wave2;
        field = constrain(field, -1.0f, 1.0f);

        float f01 = 0.5f + 0.5f * field;
        float shaped = f01 * f01 * (3.0f - 2.0f * f01);

        uint8_t floorV = isStem ? STEM_FLOOR : PETAL_FLOOR;
        uint8_t rangeV = isStem ? 150 : 200;
        float tipBoost = 1.0f;
        if (!isStem)
        {
            float tip = constrain((ledR[i] - 4.5f) * 0.085f, 0.0f, 1.0f);
            tipBoost = 0.78f + 0.55f * (tip * tip);
        }

        uint8_t bri = (uint8_t)constrain(floorV + (int)(shaped * rangeV * tipBoost), 0, 255);

        uint8_t idx = (uint8_t)(hueT +
                                (u1 * 120.0f) +
                                (warp * 40.0f) +
                                (wave1 * 18.0f));

        CRGB c = ColorFromPalette(pal, idx, bri, LINEARBLEND);

        if (isStem)
            c = blend(c, CRGB(0, 26, 0), 95);

        if (gsr.touched)
        {
            uint8_t lift = (uint8_t)constrain((int)(shaped * 55.0f), 0, 65);
            c += CHSV(idx + 20, sat - 30, lift);
        }
        else
        {
            uint8_t g = (uint8_t)((c.r + c.g + c.b) / 3);
            c = blend(c, CRGB(g, g, g), 10);
        }

        leds[i] = c;
    }

    for (int i = 0; i < NUM_LEDS; i++)
    {
        leds[i] = blend(gPrev[i], leds[i], 255 - SMOOTH_RGB);
        gPrev[i] = leds[i];
    }
}

// 14 radial ripples
void animationFourteen()
{
    static uint8_t hueShift = 0;
    static float t = 0;

    ANIM_EPOCH_GUARD()
    hueShift = 0;
    t = 0.0f;
    ANIM_EPOCH_END();

    for (int i = 0; i < NUM_LEDS; i++)
    {
        float angle = ang16_to_rad(ledAng16[i]); // 0..2PI
        float dist = ledR[i];

        float wave = sinf(dist - t);
        uint8_t hue = (uint8_t)(((angle + wave) * 255.0f / (2.0f * PI)) + hueShift);

        leds[i] = CHSV(hue, 255, 255);
    }

    t += 0.07f;
    hueShift += 1;
}

// 15 swirl spiral
void animationFifteen()
{
    static uint8_t hueShift = 0;
    static float time = 0;

    ANIM_EPOCH_GUARD()
    hueShift = 0;
    time = 0.0f;
    ANIM_EPOCH_END();

    for (int i = 0; i < NUM_LEDS; i++)
    {
        float angle = ang16_to_rad(ledAng16[i]);
        float radius = ledR[i];

        float swirl = angle + time * 0.5f - radius * 0.25f;
        uint8_t hue = sin8(swirl * 128.0f / PI + hueShift);

        uint8_t v = (i < STEM_LEDS) ? 200 : 255;
        leds[i] = CHSV(hue, 255, v);
    }

    hueShift += 1;
    time += 0.05f;
}

// 16 flowing noise
void animationSixteen()
{
    static uint16_t x = 0;
    static uint16_t y = 0;

    ANIM_EPOCH_GUARD()
    x = 0;
    y = 0;
    ANIM_EPOCH_END();

    for (int i = 0; i < STEM_LEDS; i++)
    {
        if (stemIsTrunk[i])
        {
            uint8_t noise = inoise8(x + stemTrunkOrd[i] * 20, y);
            leds[i] = CHSV(noise, 255, 255);
        }
        else
        {
            float u = leafU_fromStemIndex(i);
            uint16_t lx = x + (uint16_t)(u * 520.0f) + (uint16_t)(stemLeafId[i] * 9000);
            uint8_t noise = inoise8(lx, y + (uint16_t)(stemLeafId[i] * 2500));
            leds[i] = CHSV(noise, 255, 255);
        }
    }

    int ledIndex = STEM_LEDS;
    int petalOffset = 0;

    for (int petal = 0; petal < NUM_PETALS; petal++)
    {
        int n = ledsInPetal(petal);

        for (int i = 0; i < n; i++)
        {
            uint8_t noise = inoise8(x + petalOffset * 20, y);
            leds[ledIndex++] = CHSV(noise, 255, 255);
            petalOffset++;
        }
    }

    x += 2;
    y += 2;
}

// 17 nebula + stars
void animationSeventeen()
{
    static float nebT = 0.0f;

    ANIM_EPOCH_GUARD()
    nebT = 0.0f;
    clearAllLeds();
    ANIM_EPOCH_END();

    nebT += dtSec * 0.42f;
    fadeToBlackBy(leds, NUM_LEDS, 18);

    float d = constrain(gsr.delta, 0.0f, 180.0f);
    uint8_t contrast = (uint8_t)map((int)d, 0, 180, 90, 220);
    uint8_t stars = (uint8_t)map((int)d, 0, 180, 1, 8);

    const float FLOW_X = 1.10f;
    const float FLOW_Y = 0.70f;

    for (int i = 0; i < NUM_LEDS; i++)
    {
        float x = ledX[i];
        float y = ledY[i];

        uint16_t nx = (uint16_t)(x * 120.0f);
        uint16_t ny = (uint16_t)(y * 220.0f);

        uint16_t ox = (uint16_t)(nebT * FLOW_X * 256.0f);
        uint16_t oy = (uint16_t)(nebT * FLOW_Y * 256.0f);

        uint8_t n = inoise8(nx + ox, ny + oy);
        uint8_t hue = n + (uint8_t)(nebT * 14.0f);
        uint8_t val = scale8(qsub8(n, 80), contrast);

        leds[i] += CHSV(hue, 200, val);
    }

    for (uint8_t s = 0; s < stars; s++)
    {
        uint16_t idx = random16(NUM_LEDS);
        leds[idx] += CHSV(160 + random8(40), 40, 255);
    }

    uint8_t cometChance = gsr.touched ? 32 : 6;
    if (random8() < cometChance)
    {
        uint16_t head = random16(NUM_LEDS);
        uint8_t hue = 180 + random8(60);

        leds[head] += CHSV(hue, 120, 255);
        if (head + 1 < NUM_LEDS)
            leds[head + 1] += CHSV(hue, 120, 180);
        if (head + 2 < NUM_LEDS)
            leds[head + 2] += CHSV(hue, 120, 120);
    }
}

// 18 Kaleido Bloom
void animationEighteen()
{
    static uint8_t warpLP[NUM_LEDS]; // 0..255

    ANIM_EPOCH_GUARD()
    memset(gPrev, 0, sizeof(gPrev));
    memset(warpLP, 0, sizeof(warpLP));
    clearAllLeds();
    ANIM_EPOCH_END();

    const uint8_t SMOOTH_RGB = 160;

    const uint8_t STEM_FLOOR = 18;
    const uint8_t PETAL_FLOOR = 70;

    float d = constrain(gsr.delta, 0.0f, 180.0f);
    float boost = mapf(d, 0, 180, 0.95f, 1.20f);

    float rot1 = tSec * (0.085f * boost);
    float rot2 = tSec * (0.055f * boost);

    const int SLICES = 6;

    CRGBPalette16 palA = PartyColors_p;
    CRGBPalette16 palB = LavaColors_p;

    float tide = sinf(tSec * 0.12f) * 0.7f;

    const uint8_t WARP_LP_ALPHA = 22;

    float zoom = 1.0f + 0.045f * sinf(tSec * 0.10f);

    float advX = tSec * (0.22f * boost);
    float advY = tSec * (0.16f * boost);
    float advZ = tSec * (0.10f * boost);

    const float angScale = (2.0f * PI / 65535.0f);
    const float wedge = (2.0f * PI) / (float)SLICES;
    float foldRot = tSec * (0.050f * boost);

    for (int i = 0; i < NUM_LEDS; i++)
    {
        bool isStem = (i < STEM_LEDS);

        float r = ledR[i] * zoom;
        float ang = (float)ledAng16[i] * angScale;

        uint8_t warpN = inoise8(
            (uint16_t)((ledX[i] * 55.0f + advX * 18.0f) * 256.0f),
            (uint16_t)((ledY[i] * 75.0f + advY * 18.0f) * 256.0f),
            (uint16_t)(advZ * 256.0f));

        warpLP[i] = (uint8_t)((((uint16_t)warpLP[i] * (255 - WARP_LP_ALPHA)) +
                               ((uint16_t)warpN * (WARP_LP_ALPHA))) >>
                              8);

        float warp = ((int)warpLP[i] - 128) / 128.0f;

        float angW = ang + warp * 0.16f + 0.06f * sinf(tSec * 0.10f + r * 0.12f);
        float rW = r + warp * 0.55f + 0.30f * sinf(tSec * 0.08f + ang * 1.1f);

        float a = fmodf(angW + foldRot, wedge);
        if (a < 0)
            a += wedge;
        if (a > wedge * 0.5f)
            a = wedge - a;

        float aN = a / (wedge * 0.5f);

        float petalPhase = (ang / (2.0f * PI)) * (float)NUM_PETALS;
        float petalBreath = 0.5f + 0.5f * sinf(tSec * 0.16f + petalPhase * 0.65f);

        float radialTide = sinf((rW * 0.42f) - tSec * 0.13f + tide) * 0.45f;

        float bandA = sinf((rW * 0.62f) - (aN * 5.6f) + rot1 * 2.0f + radialTide);
        float bandB = sinf((rW * 0.42f) + (angW * 2.7f) - rot2 * 2.2f - 0.55f * radialTide);

        uint8_t n2 = inoise8(
            (uint16_t)((ledX[i] * 65.0f + advX * 10.0f) * 256.0f),
            (uint16_t)((ledY[i] * 90.0f + advY * 10.0f) * 256.0f),
            (uint16_t)((advZ * 1.3f) * 256.0f));
        float shimmer = (((int)n2 - 128) / 128.0f) * 0.05f;

        float field = 0.56f * bandA + 0.44f * bandB + shimmer;
        field = constrain(field, -1.0f, 1.0f);

        uint8_t floorV = isStem ? STEM_FLOOR : PETAL_FLOOR;
        uint8_t rangeV = isStem ? 110 : 170;

        uint8_t f8 = (uint8_t)((field * 0.5f + 0.5f) * 255.0f);

        uint8_t shaped = scale8(f8, 205);
        shaped = qadd8(shaped, scale8(shaped, 55));

        uint8_t breathV = (uint8_t)(36.0f * petalBreath);

        uint8_t v = qadd8(floorV, scale8(shaped, rangeV));
        v = qadd8(v, breathV);

        uint8_t a8 = (uint8_t)(aN * 255.0f);
        uint8_t r8 = (uint8_t)constrain(rW * 16.0f, 0.0f, 255.0f);

        uint8_t hueBase = (uint8_t)(tSec * 6.0f);
        uint8_t tideHue = (uint8_t)(radialTide * 14.0f);

        uint8_t hue1 = hueBase + a8 + (r8 >> 1) + tideHue;
        uint8_t hue2 = hueBase + (uint8_t)(255 - a8) + r8 - tideHue;

        uint8_t mix = (uint8_t)constrain(90 + (f8 >> 2), 0, 255);

        CRGB cA = ColorFromPalette(palA, hue1, v, LINEARBLEND);
        CRGB cB = ColorFromPalette(palB, hue2, v, LINEARBLEND);
        CRGB c = blend(cA, cB, mix);

        if (isStem)
            c = blend(c, CRGB(0, 18, 0), 70);

        leds[i] = c;
    }

    for (int i = 0; i < NUM_LEDS; i++)
    {
        leds[i] = blend(gPrev[i], leds[i], 255 - SMOOTH_RGB);
        gPrev[i] = leds[i];
    }
}

// 19 Oil-Slick Lava Drift
void animationNineteen()
{
    ANIM_EPOCH_GUARD()
    memset(gPrev, 0, sizeof(gPrev));
    clearAllLeds();
    ANIM_EPOCH_END();

    const uint8_t SMOOTH = 105;

    float d = constrain(gsr.delta, 0.0f, 180.0f);
    float boost = mapf(d, 0, 180, 0.70f, 1.20f);

    CRGBPalette16 pal = PartyColors_p;
    uint8_t sat = gsr.touched ? 190 : 210;

    float sp = 0.09f * boost;
    uint16_t ox = (uint16_t)(tSec * (45.0f * sp) * 256.0f);
    uint16_t oy = (uint16_t)(tSec * (34.0f * sp) * 256.0f);
    uint16_t oz = (uint16_t)(tSec * (18.0f * sp) * 256.0f);

    uint8_t drift = (uint8_t)(tSec * 3.5f);

    for (int i = 0; i < NUM_LEDS; i++)
        leds[i] = CHSV(160 + (uint8_t)(tSec * 1.0f), sat, 16);

    for (int i = 0; i < NUM_LEDS; i++)
    {
        uint16_t nx = (uint16_t)(ledX[i] * 140.0f * 256.0f) + ox;
        uint16_t ny = (uint16_t)(ledY[i] * 170.0f * 256.0f) + oy;

        uint8_t n1 = inoise8(nx, ny, oz);
        uint8_t n2 = inoise8(nx + 9000, ny - 7000, oz + 13000);

        uint8_t field = (uint8_t)((uint16_t)n1 * 3 / 5 + (uint16_t)n2 * 2 / 5);

        uint8_t v = 35 + scale8(field, 200);
        uint8_t idx = field + drift;
        leds[i] += ColorFromPalette(pal, idx, v, LINEARBLEND);

        if (field > 210)
        {
            uint8_t bloom = (field - 210) * 2;
            leds[i] += CHSV(idx, sat - 30, bloom);
        }
    }

    for (int i = 0; i < STEM_LEDS; i++)
    {
        if (stemIsTrunk[i])
        {
            float u = trunkU_fromStemIndex(i);
            uint16_t sx = (uint16_t)(u * 1200.0f * 256.0f);
            uint8_t s = inoise8(sx + ox, oy, oz);

            uint8_t idx = s + (uint8_t)(tSec * 4.0f);
            uint8_t v = 45 + scale8(s, 180);
            leds[i] = blend(leds[i], ColorFromPalette(pal, idx, v, LINEARBLEND), 215);
        }
        else
        {
            float u = leafU_fromStemIndex(i);
            uint16_t sx = (uint16_t)(u * 1200.0f * 256.0f) + (uint16_t)(stemLeafId[i] * 10000);
            uint8_t s = inoise8(sx + ox, oy + (uint16_t)(stemLeafId[i] * 3000), oz);

            uint8_t idx = s + (uint8_t)(tSec * 4.0f) + (stemLeafId[i] ? 18 : 0);
            uint8_t v = 35 + scale8(s, 170);
            leds[i] = blend(leds[i], ColorFromPalette(pal, idx, v, LINEARBLEND), 215);
        }
    }

    for (int i = 0; i < NUM_LEDS; i++)
    {
        leds[i] = blend(gPrev[i], leds[i], 255 - SMOOTH);
        gPrev[i] = leds[i];
    }
}

// 20 Rainbow Tunnel Bloom
void animationTwenty()
{
    ANIM_EPOCH_GUARD()
    memset(gPrev, 0, sizeof(gPrev));
    clearAllLeds();
    ANIM_EPOCH_END();

    const uint8_t SMOOTH_RGB = 165;

    float d = constrain(gsr.delta, 0.0f, 180.0f);
    float boost = mapf(d, 0, 180, 0.95f, 1.35f);

    float hueSpeed = 18.0f * boost;
    uint8_t hueCenter = (uint8_t)(tSec * hueSpeed);

    float travelSpeed = 1.10f * boost;
    float travel = tSec * travelSpeed;

    const float PETAL_R_MAX = 11.5f;
    float sweep = fmodf(travel, PETAL_R_MAX);

    const float STEM_HUE_PER_LED = 2.6f;
    const uint8_t STEM_SAT = 235;

    for (int i = 0; i < STEM_LEDS; i++)
    {
        if (!stemIsTrunk[i])
            continue;

        float distFromTop = (float)((TRUNK_TOTAL_LEDS - 1) - stemTrunkOrd[i]);
        uint8_t hue = (uint8_t)(hueCenter - (uint8_t)(distFromTop * STEM_HUE_PER_LED));

        uint8_t wave = sin8((uint8_t)(tSec * (40.0f * boost) + stemTrunkOrd[i] * 6));
        uint8_t v = (uint8_t)constrain(160 + (wave / 2), 0, 255);

        leds[i] = CHSV(hue, STEM_SAT, v);
    }

    for (int i = 0; i < STEM_LEDS; i++)
    {
        if (stemIsTrunk[i])
            continue;

        float u = leafU_fromStemIndex(i);
        float w = 0.5f + 0.5f * sinf((u * 6.0f) - tSec * (2.2f * boost));
        uint8_t hue = (uint8_t)(hueCenter + stemLeafId[i] * 35 + (int)(u * 40.0f));
        uint8_t v = (uint8_t)constrain(120.0f + 135.0f * w, 0.0f, 255.0f);
        leds[i] = CHSV(hue, 235, v);
    }

    const uint8_t PETAL_SAT = 255;
    const float RAD_HUE_PER_R = 20.0f;

    for (int i = STEM_LEDS; i < NUM_LEDS; i++)
    {
        float r = ledR[i];
        float rShift = r - sweep;

        uint8_t hue = (uint8_t)(hueCenter + (int)(rShift * RAD_HUE_PER_R));

        float r01 = constrain(r / PETAL_R_MAX, 0.0f, 1.0f);

        float band = 0.5f + 0.5f * sinf((rShift * 1.15f) * 2.0f * PI);
        float centerLift = 1.0f - (0.35f * r01);
        float vF = (150.0f + 90.0f * band) * centerLift;

        if (!gsr.touched)
            vF *= 0.92f;

        uint8_t v = (uint8_t)constrain(vF, 0.0f, 255.0f);
        leds[i] = CHSV(hue, PETAL_SAT, v);
    }

    for (int i = 0; i < NUM_LEDS; i++)
    {
        leds[i] = blend(gPrev[i], leds[i], 255 - SMOOTH_RGB);
        gPrev[i] = leds[i];
    }
}

// 21 Rainbow Bloom
void animationTwentyOne()
{
    static float localT = 0.0f;

    ANIM_EPOCH_GUARD()
    memset(gPrev, 0, sizeof(gPrev));
    clearAllLeds();
    localT = 0.0f;
    ANIM_EPOCH_END();

    localT += dtSec;

    const uint8_t SMOOTH_RGB = 175;

    float d = constrain(gsr.delta, 0.0f, 180.0f);
    float kick = mapf(d, 0, 180, 1.0f, 1.9f);

    float hueSpeed = 60.0f * kick;
    uint8_t feedHue = (uint8_t)(localT * hueSpeed);

    float flowSpeed = 4.2f * kick;
    float flow = localT * flowSpeed;

    const float PETAL_R_MAX = 11.5f;

    const float STEM_HUE_PER_LED = 2.6f;
    const uint8_t STEM_SAT = 235;
    const uint8_t STEM_V = 230;

    for (int i = 0; i < STEM_LEDS; i++)
    {
        if (!stemIsTrunk[i])
            continue;

        float distFromTop = (float)((TRUNK_TOTAL_LEDS - 1) - stemTrunkOrd[i]);
        uint8_t hue = (uint8_t)(feedHue - (uint8_t)(distFromTop * STEM_HUE_PER_LED));
        leds[i] = CHSV(hue, STEM_SAT, STEM_V);
    }

    for (int i = 0; i < STEM_LEDS; i++)
    {
        if (stemIsTrunk[i])
            continue;

        float u = leafU_fromStemIndex(i);
        uint8_t n = inoise8((uint16_t)(localT * 520.0f) + stemLeafId[i] * 14000,
                            (uint16_t)(u * 9000.0f));
        uint8_t hue = (uint8_t)(feedHue + (int)(n / 2) + stemLeafId[i] * 25);
        uint8_t v = 180 + (uint8_t)(60.0f * sinf(localT * 6.0f + u * 10.0f));
        leds[i] = CHSV(hue, 255, v);
    }

    const uint8_t PETAL_SAT = 255;
    const float RAD_HUE_PER_R = 30.0f;

    for (int i = STEM_LEDS; i < NUM_LEDS; i++)
    {
        float r = ledR[i];
        float rShift = r - flow;

        uint8_t hue = (uint8_t)(feedHue + (int)(rShift * RAD_HUE_PER_R));

        float r01 = constrain(r / PETAL_R_MAX, 0.0f, 1.0f);
        float vF = 245.0f - (r01 * 65.0f);
        if (!gsr.touched)
            vF *= 0.97f;

        uint8_t v = (uint8_t)constrain(vF, 0.0f, 255.0f);
        leds[i] = CHSV(hue, PETAL_SAT, v);
    }

    for (int i = 0; i < NUM_LEDS; i++)
    {
        leds[i] = blend(gPrev[i], leds[i], 255 - SMOOTH_RGB);
        gPrev[i] = leds[i];
    }
}
