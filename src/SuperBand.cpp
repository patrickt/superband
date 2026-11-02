/*
 * SuperBand — RubberBand-powered time/pitch stretcher UGen for SuperCollider.
 *
 * SuperCollider interface:
 *   SuperBand.ar(in, ratio, pitch, reset)  ->  [audio, latency]
 *
 *   in     audio   input signal (mono per instance; multichannel expansion
 *                  gives one independent stretcher per channel)
 *   ratio  k-rate  output duration / input duration, 1.0 = no time stretch
 *   pitch  k-rate  pitch scale, 1.0 = unchanged (2.0 = one octave up)
 *   reset  k-rate  rising edge restarts the stretcher (use it after splices)
 *
 *   output 0  audio    stretched / pitch-shifted signal
 *   output 1  audio    latency in frames as reported by RubberBand
 *                      (RubberBandStretcher::getStartDelay()) at the current
 *                      settings, constant within a block
 */

#include "SC_PlugIn.hpp"

#include "SuperBandCore.hpp"

static InterfaceTable *ft;

struct SuperBand : public Unit {
    superband::Core *mCore;
    float mPrevReset;
};

extern "C" void SuperBand_next(SuperBand *unit, int inNumSamples);

static inline float sanitizedRatio(float v) {
    return (std::isfinite(v) && v > 0.0f) ? v : 1.0f;
}

extern "C" void SuperBand_Ctor(SuperBand *unit) {
    unit->mPrevReset = 0.0f;
    unit->mCore = nullptr;

    // Sample-rate aware initialisation: the stretcher is built for the rate of
    // the server graph this instance lives in, at the block size scsynth uses.
    const double sampleRate = SAMPLERATE;
    const int blockSize = static_cast<int>(BUFLENGTH);

    try {
        unit->mCore = new superband::Core(sampleRate, blockSize, sanitizedRatio(IN0(1)),
                                         sanitizedRatio(IN0(2)));
    } catch (...) {
        unit->mCore = nullptr;
    }

    if (unit->mCore == nullptr) {
        Print("SuperBand: could not allocate the stretcher, unit is silent\n");
        SETCALC(ClearUnitOutputs);
        ClearUnitOutputs(unit, 1);
        return;
    }

    SETCALC(SuperBand_next);
    ClearUnitOutputs(unit, 1);
    SuperBand_next(unit, 1);
}

extern "C" void SuperBand_Dtor(SuperBand *unit) {
    delete unit->mCore;
    unit->mCore = nullptr;
}

extern "C" void SuperBand_next(SuperBand *unit, int inNumSamples) {
    superband::Core *core = unit->mCore;
    if (core == nullptr || inNumSamples <= 0) return;

    float *out = OUT(0);
    // Unconnected inputs are wired to a zero buffer by scsynth, never to null.
    const float *in = IN(0);

    // Control-rate settings are block constants, so read index 0 only.
    const float reset = IN0(3);
    if (reset > 0.0f && unit->mPrevReset <= 0.0f) core->reset();
    unit->mPrevReset = reset;

    core->setRatio(sanitizedRatio(IN0(1)));
    core->setPitch(sanitizedRatio(IN0(2)));

    core->processBlock(in, out, inNumSamples);

    if (unit->mNumOutputs > 1) {
        const float latency = static_cast<float>(core->latency());
        float *lat = OUT(1);
        for (int i = 0; i < inNumSamples; ++i) lat[i] = latency;
    }
}

PluginLoad(SuperBand) {
    ft = inTable;
    DefineDtorCantAliasUnit(SuperBand);
}
