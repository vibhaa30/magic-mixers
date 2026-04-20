/**
 * dsp.cpp
 * Three audio effects running on Core 0.
 *
 * All sample math is done in normalised float [-1.0, 1.0].
 * Input: PCM1808 24-bit left-justified in 32-bit word → right-shift 8.
 * Output: PCM5102A full-scale 32-bit signed.
 */

#include "dsp.h"
#include <math.h>
#include <string.h>

// ─── Scaling helpers ─────────────────────────────────────────────────────────
static const float INV_24BIT = 1.0f / (float)(1 << 23);  // 24-bit full scale
static const float SCALE_32  = (float)0x7FFFFFFF;

// ─── Delay effect state ───────────────────────────────────────────────────────
#define DELAY_MAX_SAMPLES  44100  // 1 s @ 44100 Hz
static float delayBuf[DELAY_MAX_SAMPLES];
static int   delayWritePtr = 0;

// ─── Distortion: persistent 1-pole tone filter state ─────────────────────────
static float toneState = 0.0f;

// ─────────────────────────────────────────────────────────────────────────────
void dspReset() {
    memset(delayBuf, 0, sizeof(delayBuf));
    delayWritePtr = 0;
    toneState     = 0.0f;
}

// ─── Diode clipping helpers ───────────────────────────────────────────────────

// Symmetrical step-function clipping (mimics hard diode pair, e.g. DS-1 style).
// Models three regions: linear → quadratic transition → hard clip.
static float diodeClipStep(float in) {
    float x = fabsf(in);
    float out;
    if      (x <= (1.0f / 3.0f)) out = 2.0f * x;
    else if (x <= (2.0f / 3.0f)) out = (-3.0f * x * x) + (4.0f * x) - (1.0f / 3.0f);
    else                          out = 1.0f;
    return copysignf(out, in);   // restore original sign
}

// Exponential clipping (models real diode I/V curve — smooth asymptotic saturation).
static float diodeClipExp(float in) {
    if      (in > 0.0f) return  1.0f - expf(-in);
    else if (in < 0.0f) return -1.0f + expf( in);
    else                return  0.0f;
}

// ─── Distortion ───────────────────────────────────────────────────────────────
// param1 = drive   (0–1 → 1x–41x pre-gain)
// param2 = mix     (dry/wet)
// param3 = shape   (0 = pure step/hard clip, 1 = pure exponential/smooth)
static float processSampleDistortion(float in, const EffectParams& p) {
    float drive  = 1.0f + p.param1 * 40.0f;
    float wetMix = p.param2;
    float shape  = p.param3;   // blend between step and exponential

    // Apply pre-gain then normalise into [-1, 1] for the clipping functions
    float x = in * drive;
    if      (x >  1.0f) x =  1.0f;
    else if (x < -1.0f) x = -1.0f;

    // Blend step and exponential clipping
    float stepOut = diodeClipStep(x);
    float expOut  = diodeClipExp(x);
    float clipped = stepOut * (1.0f - shape) + expOut * shape;

    // 1-pole low-pass to tame the harshest high-frequency content post-clip
    toneState += 0.35f * (clipped - toneState);

    return in * (1.0f - wetMix) + toneState * wetMix;
}

// ─── Delay ────────────────────────────────────────────────────────────────────
// Circular buffer echo with feedback.
static float processSampleDelay(float in, const EffectParams& p) {
    float timeMs   = p.param1;   // 1–1000 ms
    float feedback = p.param2;   // 0.0–0.95
    float wetMix   = p.param3;   // mix

    int delaySamples = (int)(timeMs * SAMPLE_RATE / 1000.0f);
    if (delaySamples >= DELAY_MAX_SAMPLES)
        delaySamples = DELAY_MAX_SAMPLES - 1;
    if (delaySamples < 1) delaySamples = 1;

    int readPtr = (delayWritePtr - delaySamples + DELAY_MAX_SAMPLES) % DELAY_MAX_SAMPLES;
    float echo = delayBuf[readPtr];

    delayBuf[delayWritePtr] = in + echo * feedback;
    delayWritePtr = (delayWritePtr + 1) % DELAY_MAX_SAMPLES;

    return in * (1.0f - wetMix) + echo * wetMix;
}

// ─── Public block processor ───────────────────────────────────────────────────
void dspProcessBlock(const int32_t* monoIn,
                     int32_t*       stereoOut,
                     int            n,
                     EffectID       effect,
                     bool           enabled,
                     const EffectParams& p)
{
    for (int i = 0; i < n; i++) {
        // PCM1808 sends 24-bit left-justified in 32-bit → right-shift 8
        float sample = (float)(monoIn[i] >> 8) * INV_24BIT;

        if (enabled) {
            switch (effect) {
                case EFFECT_DISTORTION: sample = processSampleDistortion(sample, p); break;
                case EFFECT_DELAY:      sample = processSampleDelay(sample, p);      break;
                default: break;
            }
        }

        // Clamp to [-1, 1]
        if (sample >  1.0f) sample =  1.0f;
        if (sample < -1.0f) sample = -1.0f;

        // Scale to 32-bit signed for PCM5102A, duplicate to L+R
        int32_t out = (int32_t)(sample * SCALE_32);
        stereoOut[i * 2]     = out;  // left
        stereoOut[i * 2 + 1] = out;  // right
    }
}
