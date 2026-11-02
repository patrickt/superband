#include "SC_PlugIn.hpp"

static InterfaceTable *ft;

struct PassThrough : public Unit {
    // no state
};

extern "C" void PassThrough_Ctor(PassThrough *unit) {
    // nothing to init
}

extern "C" void PassThrough_next(PassThrough *unit, int inNumSamples) {
    float *in = IN(0);
    float *out = OUT(0);
    for (int i = 0; i < inNumSamples; ++i) {
        out[i] = in[i];
    }
}

PluginLoad(PassThrough) {
    ft = inTable;
    DefineSimpleUnit(PassThrough);
}
