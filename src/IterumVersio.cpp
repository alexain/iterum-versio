// =============================================================================
//  ITERUM VERSIO  -  delay firmware for the Noise Engineering Versio (Daisy Seed)
//
//  Clockable stereo delay with clock division/multiplication, ping-pong, skew,
//  probabilistic reverse, freeze (FSU button) and a DJ filter (LP/HP) in the
//  feedback path. External clock on the FSU jack.
//  Delay times up to 60 s forward (about 43 s in reverse).
//
//  Copyright (c) 2026 Alessandro Capano - MIT License (see LICENSE)
// =============================================================================

#include "daisy_versio.h"
#include <cmath>
#include <cstring>

using namespace daisy;

// -----------------------------------------------------------------------------
//  Constants
// -----------------------------------------------------------------------------
static constexpr float    SR         = 48000.f;
static constexpr size_t   BLOCK      = 32;
static constexpr uint32_t BUF_SIZE   = 1u << 22; // 4,194,304 samples = 87 s
static constexpr uint32_t BUF_MASK   = BUF_SIZE - 1;
static constexpr float    MIN_DELAY  = 8.f;              // samples
static constexpr float    MAX_DELAY  = 60.f * SR;        // 60 s forward
static constexpr uint32_t REV_MAX    = BUF_SIZE / 2 - 4096; // ~43 s in reverse
static constexpr uint32_t REV_MIN    = 256;
static constexpr float    PI_F       = 3.14159265358979f;
static constexpr float    HALF_PI    = 1.57079632679490f;

// Audio buffers in the external SDRAM (64 MB): 2 x 16 MB
static float DSY_SDRAM_BSS bufL[BUF_SIZE];
static float DSY_SDRAM_BSS bufR[BUF_SIZE];

DaisyVersio hw;

// -----------------------------------------------------------------------------
//  Panel map (number printed on the panel = KNOB_(n-1))
//
//        [1 MIX]      [3 TIME]      [5 FEEDBACK]
//        [2 SKEW]                   [6 RANGE]
//                     [4 FILTER]
//   ABC = routing                   [7 REVERSE]
//   XYZ = time change    [FSU] = freeze  (FSU jack = clock)
// -----------------------------------------------------------------------------
static constexpr int K_MIX      = DaisyVersio::KNOB_0; // panel 1
static constexpr int K_SKEW     = DaisyVersio::KNOB_1; // panel 2
static constexpr int K_TIME     = DaisyVersio::KNOB_2; // panel 3
static constexpr int K_FILTER   = DaisyVersio::KNOB_3; // panel 4
static constexpr int K_FEEDBACK = DaisyVersio::KNOB_4; // panel 5
static constexpr int K_RANGE    = DaisyVersio::KNOB_5; // panel 6
static constexpr int K_REVERSE  = DaisyVersio::KNOB_6; // panel 7
static constexpr int SW_ROUTING = DaisyVersio::SW_0;   // ABC
static constexpr int SW_TIME    = DaisyVersio::SW_1;   // XYZ

// -----------------------------------------------------------------------------
//  Utilities
// -----------------------------------------------------------------------------
static inline float clampf(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

// Soft saturation in the feedback path: unity gain for small signals,
// saturates smoothly at +/-1
static inline float SoftClip(float x)
{
    if(x > 1.5f)
        return 1.f;
    if(x < -1.5f)
        return -1.f;
    return x - (x * x * x) * (1.f / 6.75f);
}

// Output limiter: transparent below 0.8, then a soft knee towards 1
static inline float OutLimit(float x)
{
    float a = fabsf(x);
    if(a <= 0.8f)
        return x;
    float y = 0.8f + 0.2f * tanhf((a - 0.8f) * 5.f);
    return x < 0.f ? -y : y;
}

static uint32_t rng_state = 0x1234567u;
static inline float Rand01()
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return (rng_state >> 8) * (1.f / 16777216.f);
}

// Hysteresis for stepped selections (ranges, clock ratios)
struct Stepper
{
    int idx = 0;
    int Process(float knob, int n)
    {
        float f = knob * n;
        if(fabsf(f - (idx + 0.5f)) > 0.65f)
            idx = (int)f;
        if(idx < 0)
            idx = 0;
        if(idx > n - 1)
            idx = n - 1;
        return idx;
    }
};

// -----------------------------------------------------------------------------
//  DJ filter: 2-pole SVF (12 dB/octave, TPT topology), LP or HP,
//  Butterworth response (Q 0.707, no resonant peak).
//  It sits inside the feedback path, so it acts on every repeat.
// -----------------------------------------------------------------------------
struct DjFilter
{
    float ic1 = 0.f, ic2 = 0.f;
    float k = 1.41421356f, a1 = 0.f, a2 = 0.f, a3 = 0.f;
    int   mode = 0; // 0 off, 1 lowpass, 2 highpass

    void Set(int m, float fc)
    {
        mode    = m;
        fc      = clampf(fc, 10.f, SR * 0.45f);
        float g = tanf(PI_F * fc / SR);
        a1      = 1.f / (1.f + g * (g + k));
        a2      = g * a1;
        a3      = g * a2;
    }

    float Process(float x)
    {
        float v3 = x - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1      = 2.f * v1 - ic1;
        ic2      = 2.f * v2 - ic2;
        if(mode == 1)
            return v2;              // low pass
        if(mode == 2)
            return x - k * v1 - v2; // high pass
        return x; // off (state keeps updating: no clicks when switching)
    }
};

// -----------------------------------------------------------------------------
//  Reverse head (grain with a sin^2 window)
//  A grain of length L plays backwards the L samples recorded before it
//  started: offset from the write head = 2*pos + 1
// -----------------------------------------------------------------------------
struct RevHead
{
    uint32_t pos = 0;
    uint32_t len = 4800;
};

// -----------------------------------------------------------------------------
//  Delay line (one per channel)
// -----------------------------------------------------------------------------
// Coefficients (computed at startup)
static const float kSlew       = 1.f - expf(-1.f / (0.25f * SR)); // tape: ~250 ms
static const float kMaxStep    = 4.f;                  // tape speed between -3x and +5x
static const float kFadeInc    = 1.f / (0.06f * SR);  // crossfade 60 ms
static const float kJumpInc    = 1.f / (0.003f * SR); // jump 3 ms
static const float kRevSlew    = 1.f - expf(-1.f / (0.012f * SR));
static const float kFreezeStep = 1.f / (0.015f * SR);

enum TimeMode
{
    TM_REPITCH,
    TM_CROSSFADE,
    TM_JUMP
};

struct Line
{
    float* buf = nullptr;

    // --- forward read
    float cur      = 4800.f; // current delay (repitch mode)
    float tapA     = 4800.f; // crossfade: active tap
    float tapB     = 4800.f; // crossfade: target tap
    float fadePos  = 0.f;
    bool  fading   = false;

    // --- reverse
    RevHead h[2];
    float   revGain   = 0.f;
    float   revTarget = 0.f;

    // --- freeze
    float    freezeMix = 0.f;
    uint32_t loopLen   = 48000;

    // --- filter + DC blocker
    DjFilter filt;
    float    dcx = 0.f, dcy = 0.f;

    void Init(float* b)
    {
        buf      = b;
        h[0].pos = 0;
        h[0].len = 4800;
        h[1].pos = 2400;
        h[1].len = 4800;
    }

    inline float At(uint32_t w, uint32_t off) const
    {
        return buf[(w - off) & BUF_MASK];
    }

    // Interpolated read (4-point Hermite), d >= 2
    inline float ReadHermite(uint32_t w, float d) const
    {
        uint32_t i  = (uint32_t)d;
        float    f  = d - (float)i;
        float    xm = At(w, i - 1);
        float    x0 = At(w, i);
        float    x1 = At(w, i + 1);
        float    x2 = At(w, i + 2);
        float    c0 = x0;
        float    c1 = 0.5f * (x1 - xm);
        float    c2 = xm - 2.5f * x0 + 2.f * x1 - 0.5f * x2;
        float    c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }

    // Forward read, with tape-style or crossfaded time changes
    float ReadForward(uint32_t w, float target, int tmode)
    {

        if(tmode == TM_REPITCH)
        {
            float step = (target - cur) * kSlew;
            cur += clampf(step, -kMaxStep, kMaxStep);
            cur    = clampf(cur, MIN_DELAY, MAX_DELAY);
            tapA   = cur;
            fading = false;
            return ReadHermite(w, cur);
        }

        // crossfade / jump: the taps are fixed, a time change is a crossfade
        if(!fading && fabsf(target - tapA) > 1.f)
        {
            tapB    = target;
            fadePos = 0.f;
            fading  = true;
        }
        float out;
        if(fading)
        {
            float a = ReadHermite(w, tapA);
            float b = ReadHermite(w, tapB);
            float t = fadePos * HALF_PI;
            out     = a * cosf(t) + b * sinf(t); // equal power
            fadePos += (tmode == TM_JUMP) ? kJumpInc : kFadeInc;
            if(fadePos >= 1.f)
            {
                tapA   = tapB;
                fading = false;
            }
        }
        else
        {
            out = ReadHermite(w, tapA);
        }
        cur = tapA;
        return out;
    }

    // Reverse read: two grains overlapping by half, sin^2 window
    float ReadReverse(uint32_t w, uint32_t L, float revAmount)
    {
        float acc = 0.f, wsum = 0.f;
        for(int i = 0; i < 2; i++)
        {
            RevHead& hd  = h[i];
            float    ph  = (float)hd.pos / (float)hd.len;
            float    s   = sinf(PI_F * ph);
            float    win = s * s;
            acc += win * At(w, 2 * hd.pos + 1);
            wsum += win;

            if(++hd.pos >= hd.len)
            {
                hd.pos      = 0;
                RevHead& ot = h[1 - i];
                if(i == 0)
                {
                    hd.len = L;
                    // new segment: choose the direction of this repeat
                    if(revAmount < 0.02f)
                        revTarget = 0.f;
                    else if(revAmount > 0.98f)
                        revTarget = 1.f;
                    else
                        revTarget = (Rand01() < revAmount) ? 1.f : 0.f;
                }
                else
                {
                    // head B realigns to the middle of head A's grain
                    uint32_t dist
                        = (ot.pos < ot.len / 2) ? (ot.len / 2 - ot.pos)
                                                : (ot.len - ot.pos + L / 2);
                    hd.len = (dist > L / 2 && dist < L + L / 2) ? dist : L;
                    if(hd.len > REV_MAX)
                        hd.len = REV_MAX;
                }
            }
        }
        return acc / (wsum > 1e-4f ? wsum : 1e-4f);
    }

    float DcBlock(float x)
    {
        float y = x - dcx + 0.9987f * dcy;
        dcx     = x;
        dcy     = y;
        return y;
    }
};

Line lineL, lineR;
static uint32_t wpos = 0;

// -----------------------------------------------------------------------------
//  Clock (FSU jack) and freeze (FSU button)
// -----------------------------------------------------------------------------
static bool     clocked       = false;
static float    clockPeriod   = SR; // samples
static uint32_t sinceEdge     = 0;
static uint32_t edgeCount     = 0;
static bool     lastGate      = false;
static bool     longPressDone = false;
static bool     freezeOn      = false;

// Delay/clock-period ratios: divisions on the left, multiplications on the right
static const float kRatios[] = {1.f / 16, 1.f / 12, 1.f / 8, 1.f / 6, 1.f / 4,
                                1.f / 3,  3.f / 8,  1.f / 2, 2.f / 3, 3.f / 4,
                                1.f,      4.f / 3,  3.f / 2, 2.f,     3.f,
                                4.f,      6.f,      8.f,     12.f,    16.f};
static constexpr int kNumRatios = sizeof(kRatios) / sizeof(kRatios[0]);

// Clocked skew: 9 steps. Negative = right shorter, positive = left shorter
static const float kSkewRatios[] = {1.f, 7.f / 8, 3.f / 4, 2.f / 3, 1.f / 2};
static constexpr int kNumSkewSteps = 9; // -4..+4

// Free-running time ranges (seconds)
struct Zone
{
    float lo, hi;
};
static const Zone kZones[] = {{0.005f, 0.1f}, // comb, flanger, Karplus
                              {0.05f, 0.6f},  // slapback, rhythmic
                              {0.3f, 3.f},    // long echoes
                              {1.5f, 60.f}};  // loops and soundscapes
static constexpr int kNumZones = 4;

static Stepper stTime, stZone, stSkew;
static uint32_t lockIdx = 0;     // edges counted since the clock locked
static bool     resync  = false; // an edge arrived in this block

static void OnClockEdge()
{
    if(edgeCount > 0 && sinceEdge > (uint32_t)(0.02f * SR)
       && sinceEdge < (uint32_t)MAX_DELAY)
    {
        float p = (float)sinceEdge;
        // small hysteresis: ignore jitter below 1%
        if(!clocked || fabsf(p - clockPeriod) > 0.01f * clockPeriod)
            clockPeriod = p;
        else
            clockPeriod = 0.5f * (clockPeriod + p);
        if(!clocked)
            lockIdx = 0;
        else
            lockIdx++;
        clocked = true;
        resync  = true;
    }
    edgeCount++;
    sinceEdge = 0;
}

// -----------------------------------------------------------------------------
//  LED state (written in the callback, read in the main loop)
// -----------------------------------------------------------------------------
static volatile float ledBlink[2] = {0.f, 0.f}; // 1 = LED on (pulse)
static volatile float ledBlinkRev[2] = {0.f, 0.f}; // 1 = reversed repeat
static volatile float ledFiltLP = 0.f, ledFiltHP = 0.f;
static volatile bool  ledFreeze = false, ledOverFb = false;
static double         blinkPh[2]  = {0.0, 0.0}; // beat phase (samples)
static double         blinkAge[2] = {0.0, 0.0}; // samples since the last blink
static double         blinkHold[2] = {0.0, 0.0}; // remaining on-time (samples)

// -----------------------------------------------------------------------------
//  Audio callback
// -----------------------------------------------------------------------------
static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    hw.ProcessAllControls();
    hw.tap.Debounce();

    // ---- clock: FSU (gate) and button
    if(sinceEdge < 0x7FFFFFFFu)
        sinceEdge += size;
    if(sinceEdge > (uint32_t)MAX_DELAY)
        edgeCount = 0; // too long since the last pulse: start counting again

    bool g = hw.Gate();
    if(g && !lastGate)
        OnClockEdge();
    lastGate = g;

    // FSU button: each press toggles freeze
    if(hw.tap.RisingEdge())
    {
        freezeOn      = !freezeOn;
        longPressDone = false;
    }
    // long press (1.5 s): undo the freeze toggle and return to free-running time
    if(hw.tap.Pressed() && hw.tap.TimeHeldMs() > 1500.f && !longPressDone)
    {
        freezeOn      = !freezeOn;
        clocked       = false;
        edgeCount     = 0;
        longPressDone = true;
    }

    // ---- read controls
    const float kTime   = hw.GetKnobValue(K_TIME);
    const float kFb     = hw.GetKnobValue(K_FEEDBACK);
    const float kMix    = hw.GetKnobValue(K_MIX);
    const float kFilt   = hw.GetKnobValue(K_FILTER);
    const float kRange  = hw.GetKnobValue(K_RANGE);
    const float kSkew   = hw.GetKnobValue(K_SKEW);
    const float kRev    = hw.GetKnobValue(K_REVERSE);

    // ABC: A = ping-pong, B (centre) = stereo, C = cross
    const int routing = hw.sw[SW_ROUTING].Read();
    // XYZ: X = repitch (tape), Y (centre) = crossfade, Z = hard jump
    const int swTime  = hw.sw[SW_TIME].Read();
    const int tmode   = (swTime == Switch3::POS_UP)     ? TM_REPITCH
                        : (swTime == Switch3::POS_DOWN) ? TM_JUMP
                                                        : TM_CROSSFADE;

    // ---- delay time
    // base time (the longer side) and bipolar skew:
    // knob left = right shorter, centre = equal, knob right = left shorter
    float base, ratioL = 1.f, ratioR = 1.f;
    if(clocked)
    {
        int idx = stTime.Process(kTime, kNumRatios);
        base    = clockPeriod * kRatios[idx];
        while(base > MAX_DELAY)
            base *= 0.5f;
        int sk = stSkew.Process(kSkew, kNumSkewSteps) - 4; // -4..+4
        if(sk < 0)
            ratioR = kSkewRatios[-sk];
        else if(sk > 0)
            ratioL = kSkewRatios[sk];
    }
    else
    {
        const Zone& z = kZones[stZone.Process(kRange, kNumZones)];
        base          = z.lo * powf(z.hi / z.lo, kTime) * SR;
        if(kSkew < 0.46f)
            ratioR = 1.f - 0.5f * (0.46f - kSkew) / 0.46f;
        else if(kSkew > 0.54f)
            ratioL = 1.f - 0.5f * (kSkew - 0.54f) / 0.46f;
    }
    base         = clampf(base, MIN_DELAY, MAX_DELAY);
    float delayL = clampf(base * ratioL, MIN_DELAY, MAX_DELAY);
    float delayR = clampf(base * ratioR, MIN_DELAY, MAX_DELAY);

    uint32_t revL = (uint32_t)clampf(delayL, (float)REV_MIN, (float)REV_MAX);
    uint32_t revR = (uint32_t)clampf(delayR, (float)REV_MIN, (float)REV_MAX);

    // ---- feedback (above 1 = self-oscillation held by the saturation)
    const float fb = kFb * 1.1f;

    // ---- mix equal power
    const float dryG = cosf(kMix * HALF_PI);
    const float wetG = sinf(kMix * HALF_PI);

    // ---- DJ filter: left LP (20 kHz -> 500 Hz), centre off,
    //      right HP (20 Hz -> 1.2 kHz). Slightly finer curve near the centre.
    int   fmode = 0;
    float fc    = 1000.f, depth = 0.f;
    if(kFilt < 0.46f)
    {
        depth   = (0.46f - kFilt) / 0.46f;
        fmode   = 1;
        float d = powf(depth, 1.25f);
        fc      = 20000.f * powf(500.f / 20000.f, d);
    }
    else if(kFilt > 0.54f)
    {
        depth   = (kFilt - 0.54f) / 0.46f;
        fmode   = 2;
        float d = powf(depth, 1.25f);
        fc      = 20.f * powf(1200.f / 20.f, d);
    }
    lineL.filt.Set(fmode, fc);
    lineR.filt.Set(fmode, fc);

    // ---- freeze: latch the loop length when entering freeze
    const bool freeze = freezeOn;
    if(freeze && lineL.freezeMix <= 0.f)
    {
        lineL.loopLen = (uint32_t)delayL;
        lineR.loopLen = (uint32_t)delayR;
    }

    for(size_t i = 0; i < size; i++)
    {
        const float inL  = in[0][i];
        const float inR  = in[1][i];
        const float mono = 0.5f * (inL + inR);

        Line*       ln[2]  = {&lineL, &lineR};
        const float dly[2] = {delayL, delayR};
        const uint32_t rl[2] = {revL, revR};
        float          fw[2];

        // read + filter
        for(int c = 0; c < 2; c++)
        {
            Line& l   = *ln[c];
            float fwd = l.ReadForward(wpos, dly[c], tmode);
            float rev = l.ReadReverse(wpos, rl[c], kRev);
            l.revGain += (l.revTarget - l.revGain) * kRevSlew;
            float wet = fwd + (rev - fwd) * l.revGain;
            fw[c]     = l.filt.Process(wet);
        }

        // feedback routing
        float fbL = lineL.DcBlock(fw[0]) * fb;
        float fbR = lineR.DcBlock(fw[1]) * fb;
        float nL, nR;
        switch(routing)
        {
            case Switch3::POS_UP: // ping-pong: mono input on the left, cross feedback
                nL = mono + fbR;
                nR = fbL;
                break;
            case Switch3::POS_DOWN: // cross: stereo input, cross feedback
                nL = inL + fbR;
                nR = inR + fbL;
                break;
            default: // stereo: two independent lines
                nL = inL + fbL;
                nR = inR + fbR;
                break;
        }
        nL = SoftClip(nL);
        nR = SoftClip(nR);

        // freeze: the buffer recirculates itself with period loopLen
        float nv[2] = {nL, nR};
        for(int c = 0; c < 2; c++)
        {
            Line& l = *ln[c];
            l.freezeMix
                = clampf(l.freezeMix + (freeze ? kFreezeStep : -kFreezeStep),
                         0.f,
                         1.f);
            float x = nv[c];
            if(l.freezeMix > 0.f)
            {
                float recirc = l.At(wpos, l.loopLen);
                x            = x + (recirc - x) * l.freezeMix;
            }
            l.buf[wpos] = x;
        }
        wpos = (wpos + 1) & BUF_MASK;

        out[0][i] = OutLimit(dryG * inL + wetG * fw[0]);
        out[1][i] = OutLimit(dryG * inR + wetG * fw[1]);
    }

    // ---- LEDs: beat of each side, locked to the clock
    {
        const double D[2]  = {delayL, delayR};
        Line*        ln[2] = {&lineL, &lineR};
        for(int c = 0; c < 2; c++)
        {
            bool blink = false;
            if(clocked && resync)
            {
                // expected phase at the edge: grid anchored to the first locked edge
                double ph = fmod((double)lockIdx * (double)clockPeriod, D[c]);
                blink     = (ph < (double)size) || (D[c] - ph < (double)size);
                blinkPh[c] = ph;
            }
            else
            {
                blinkPh[c] += (double)size;
                if(blinkPh[c] >= D[c])
                {
                    blinkPh[c] = fmod(blinkPh[c], D[c]); // keep the remainder: no drift
                    blink      = true;
                }
            }
            blinkAge[c] += (double)size;
            // avoid double blinks when a resync coincides with a beat
            if(blink && blinkAge[c] > 0.5 * D[c])
            {
                blinkAge[c] = 0.0;
                // sharp pulse: 50 ms, or shorter if repeats are closer together
                double hold    = 0.05 * SR;
                blinkHold[c]   = (hold < 0.4 * D[c]) ? hold : 0.4 * D[c];
                ledBlinkRev[c] = ln[c]->revTarget;
            }
            else
            {
                blinkHold[c] -= (double)size;
            }
            ledBlink[c] = (blinkHold[c] > 0.0) ? 1.f : 0.f;
        }
        resync = false;
    }
    ledFiltLP = (fmode == 1) ? depth : 0.f;
    ledFiltHP = (fmode == 2) ? depth : 0.f;
    ledFreeze = freezeOn;
    ledOverFb = (kFb * 1.1f) > 1.0f;
}

// -----------------------------------------------------------------------------
//  main
// -----------------------------------------------------------------------------
int main(void)
{
    hw.Init(true); // 480 MHz
    hw.SetAudioBlockSize(BLOCK);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    // the SDRAM is not cleared at startup
    memset(bufL, 0, sizeof(bufL));
    memset(bufR, 0, sizeof(bufR));

    lineL.Init(bufL);
    lineR.Init(bufR);
    // the reverse heads of the two channels start out of phase: more stereo movement
    lineR.h[0].pos = 1200;
    lineR.h[1].pos = 3600;

    hw.StartAdc();
    hw.StartAudio(AudioCallback);

    // The LEDs use software PWM: they must be updated often and regularly,
    // otherwise the PWM becomes slow and visible (broken-up blinks).
    // Here: 4000 updates per second -> PWM at about 480 Hz.
    uint32_t tLed = System::GetUs();
    while(1)
    {
        // LEDs 1 and 2: left / right repeats
        //   white = forward (free-running), cyan = forward (clocked), violet = reverse
        for(int c = 0; c < 2; c++)
        {
            float b = ledBlink[c];
            if(ledBlinkRev[c] > 0.5f)
                hw.SetLed(c, 0.7f * b, 0.f, b);
            else if(clocked)
                hw.SetLed(c, 0.f, b, b);
            else
                hw.SetLed(c, b, b, b);
        }

        // LED 3: filter (orange = low pass, light blue = high pass, off = neutral)
        float lp = ledFiltLP, hp = ledFiltHP;
        hw.SetLed(2, lp, 0.45f * lp + 0.6f * hp, hp);

        // LED 4: freeze = solid red; feedback above 100% = pulsing red
        if(ledFreeze)
            hw.SetLed(3, 1.f, 0.f, 0.f);
        else if(ledOverFb)
        {
            float t     = (System::GetNow() % 500) / 500.f;
            float pulse = 0.5f - 0.5f * cosf(2.f * PI_F * t);
            hw.SetLed(3, pulse, 0.f, 0.f);
        }
        else
            hw.SetLed(3, 0.f, 0.f, 0.f);

        hw.UpdateLeds();
        while((uint32_t)(System::GetUs() - tLed) < 250u) {}
        tLed += 250u;
    }
}
