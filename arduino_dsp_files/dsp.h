#pragma once
#include "shared_state.h"

/**
 * Process one block of mono audio samples through the active effect.
 *
 * @param monoIn   Input samples  (24-bit values in int32_t, left-justified)
 * @param stereoOut Output stereo interleaved int32_t for PCM5102A (32-bit full scale)
 * @param n        Number of mono samples (= AUDIO_BLOCK_SAMPLES)
 * @param effect   Which effect to apply
 * @param enabled  If false, passes audio through dry
 * @param p        Effect parameters for the active effect
 */
void dspProcessBlock(const int32_t* monoIn,
                     int32_t*       stereoOut,
                     int            n,
                     EffectID       effect,
                     bool           enabled,
                     const EffectParams& p);

/** Reset all effect internal state (delay lines, LFO phase, filter state). */
void dspReset();
