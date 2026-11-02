/*
 * Offline verification of the SuperBand engine, without SuperCollider.
 *
 *   ./superband_core_test
 *
 * Exercises the same code path the UGen uses (SuperBandCore) and checks the
 * properties the plugin promises: time-aligned start-up, duration and pitch
 * inaccuracy, latency agreement with RubberBand, reset behaviour and
 * steady-state stability of the block path.
 */

#include "SuperBandCore.hpp"

#include <rubberband/RubberBandStretcher.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int gFailures = 0;
int gChecks = 0;

void check(bool ok, const std::string &what) {
    ++gChecks;
    if (!ok) ++gFailures;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
}

bool finite(const std::vector<float> &x) {
    for (float v : x)
        if (!std::isfinite(v)) return false;
    return true;
}

double peak(const std::vector<float> &x) {
    double p = 0.0;
    for (float v : x) p = std::max(p, (double)std::fabs(v));
    return p;
}

double rms(const std::vector<float> &x, std::size_t start, std::size_t len) {
    double acc = 0.0;
    for (std::size_t i = 0; i < len; ++i) acc += (double)x[start + i] * x[start + i];
    return std::sqrt(acc / (double)len);
}

// Single-bin DFT magnitude, normalised so a unit sine of the target frequency
// reads ~1.0.
double tone(const std::vector<float> &x, std::size_t start, std::size_t len, double freq,
            double sr) {
    const double w = 2.0 * M_PI * freq / sr;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (std::size_t i = 0; i < len; ++i) {
        const double s0 = (double)x[start + i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double re = s1 - s2 * std::cos(w);
    const double im = s2 * std::sin(w);
    return 2.0 * std::sqrt(re * re + im * im) / (double)len;
}

std::vector<float> sine(double sr, double freq, std::size_t frames, double amp) {
    std::vector<float> v(frames);
    for (std::size_t i = 0; i < frames; ++i)
        v[i] = (float)(amp * std::sin(2.0 * M_PI * freq * (double)i / sr));
    return v;
}

// Push a finite signal through the engine, draining output after every block.
std::vector<float> render(superband::Core &core, const std::vector<float> &in, int block) {
    std::vector<float> out;
    std::vector<float> chunk((std::size_t)block);
    std::size_t pos = 0;
    while (pos < in.size()) {
        const std::size_t n = std::min<std::size_t>((std::size_t)block, in.size() - pos);
        // Retry until the engine accepts the block: a full FIFO is not an error
        // for a pull-style caller, it just has to be drained first.
        int guard = 0;
        while (core.writeInput(in.data() + pos, n) == 0) {
            std::size_t got;
            while ((got = core.readOutput(chunk.data(), chunk.size())) > 0)
                out.insert(out.end(), chunk.begin(), chunk.begin() + (long)got);
            if (++guard > 1000) {
                std::printf("  [FAIL] engine stopped accepting input\n");
                ++gFailures;
                return out;
            }
        }
        pos += n;
        std::size_t got;
        while ((got = core.readOutput(chunk.data(), chunk.size())) > 0)
            out.insert(out.end(), chunk.begin(), chunk.begin() + (long)got);
    }
    core.finish();
    std::size_t got;
    while ((got = core.readOutput(chunk.data(), chunk.size())) > 0)
        out.insert(out.end(), chunk.begin(), chunk.begin() + (long)got);
    return out;
}

// ---------------------------------------------------------------------------

void testReportedLatency(double sr) {
    std::printf("\n== latency reporting (sample rate %.0f) ==\n", sr);
    struct Case {
        double ratio;
        double pitch;
    };
    const Case cases[] = {{1.0, 1.0}, {0.5, 1.0}, {2.0, 1.0}, {1.0, 1.5}, {1.0, 0.5}, {3.0, 0.75}};
    for (const Case &c : cases) {
        superband::Core core(sr, 64, c.ratio, c.pitch);
        RubberBand::RubberBandStretcher rb((std::size_t)std::lround(sr), 1,
                                           RubberBand::RubberBandStretcher::OptionProcessRealTime,
                                           c.ratio, c.pitch);
        const double rbLatency = (double)rb.getStartDelay();
        char msg[160];
        std::snprintf(msg, sizeof msg,
                      "ratio=%.2f pitch=%.2f: engine latency %.0f == RubberBand getStartDelay %.0f",
                      c.ratio, c.pitch, core.latency(), rbLatency);
        check(core.latency() == rbLatency, msg);
    }
}

void testStartAlignment(double sr) {
    std::printf("\n== start-up alignment (sample rate %.0f) ==\n", sr);
    // A click at material frame 0 should appear at the very start of the output:
    // the engine's start pad and start delay are meant to cancel out.
    const std::size_t frames = (std::size_t)(sr * 0.25);
    std::vector<float> in(frames, 0.0f);
    in[0] = 0.9f;
    in[1] = -0.9f;

    superband::Core core(sr, 64, 1.0, 1.0);
    std::vector<float> out = render(core, in, 64);
    check(finite(out), "output has no NaN or inf");

    std::size_t onset = out.size();
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (std::fabs(out[i]) > 1e-3f) {
            onset = i;
            break;
        }
    }
    char msg[160];
    std::snprintf(msg, sizeof msg, "click onset at output frame %zu (startPad %.0f, latency %.0f)",
                  onset, core.startPad(), core.latency());
    check(onset < (std::size_t)(sr * 0.005), msg);
}

void testTimeStretch(double sr) {
    std::printf("\n== time stretch duration and pitch preservation (sample rate %.0f) ==\n", sr);
    // Real-time mode does not preserve the input/output frame count exactly; the
    // discrepancy is a small constant (one start pad either side), not a scaling
    // error, so measure over two seconds and allow the offset plus 1%.
    const std::size_t frames = (std::size_t)(sr * 2.0);
    std::vector<float> in = sine(sr, 440.0, frames, 0.5);

    struct Case {
        double ratio;
        double pitch;
        const char *label;
    };
    const Case cases[] = {{1.0, 1.0, "identity"}, {2.0, 1.0, "twice as long"},
                          {0.5, 1.0, "half as long"}, {1.0, 2.0, "octave up"}};

    for (const Case &c : cases) {
        superband::Core core(sr, 64, c.ratio, c.pitch);
        std::vector<float> out = render(core, in, 64);
        const double got = (double)out.size();
        const double want = (double)frames * c.ratio;
        const double offset = got - want;
        const double err = std::fabs(offset) / want;

        char msg[220];
        std::snprintf(msg, sizeof msg,
                      "%s (ratio %.2f pitch %.2f): %zu frames out, want %.0f (offset %+.0f, err %.2f%%)",
                      c.label, c.ratio, c.pitch, out.size(), want, offset, err * 100.0);
        check(err < 0.015, msg);
        check(std::fabs(offset) <= 4.0 * core.startPad(), "  offset is a constant start-up amount, not a ratio");
        check(finite(out), "  output has no NaN or inf");

        // Pitch check on the middle of the output, away from start-up and tail.
        const std::size_t len = (std::size_t)(sr * 0.1);
        if (out.size() > 2 * len + 4096) {
            const std::size_t start = out.size() / 2 - len / 2;
            const double at440 = tone(out, start, len, 440.0, sr);
            const double at880 = tone(out, start, len, 880.0, sr);
            const bool up = c.pitch > 1.5;
            const double wantMag = up ? at880 : at440;
            const double otherMag = up ? at440 : at880;
            std::snprintf(msg, sizeof msg, "  %s tone dominates: %.3f vs %.3f", up ? "880 Hz" : "440 Hz",
                          wantMag, otherMag);
            check(wantMag > 0.2 && wantMag > 4.0 * otherMag, msg);
        }
    }
}

void testResetBehaviour(double sr) {
    std::printf("\n== reset behaviour (sample rate %.0f) ==\n", sr);
    const std::size_t frames = (std::size_t)(sr * 0.3);
    std::vector<float> a = sine(sr, 220.0, frames, 0.5);
    std::vector<float> b = sine(sr, 660.0, frames, 0.5);

    superband::Core core(sr, 64, 1.0, 1.0);
    std::vector<float> first = render(core, a, 64);
    check(finite(first), "output before reset is finite");

    core.setRatio(1.5);
    core.reset();
    check(core.latency() > 0, "reset keeps a valid reported latency");

    std::vector<float> second = render(core, b, 64);
    check(finite(second), "output after reset is finite");
    check(rms(second, second.size() / 2, second.size() / 2) > 0.05,
          "stretcher produces audio again after reset");
    const double p = peak(second);
    check(p <= 2.0, "output after reset stays within a sane range");

    // Reset through the same path the UGen uses (fixed block, ratio changed).
    superband::Core core2(sr, 64, 1.0, 1.0);
    std::vector<float> out((std::size_t)64);
    std::vector<float> scratch((std::size_t)64);
    for (int blk = 0; blk < 200; ++blk) {
        const std::size_t off = (std::size_t)blk * 64;
        std::memcpy(scratch.data(), a.data() + off, 64 * sizeof(float));
        core2.processBlock(scratch.data(), out.data(), 64);
        if (blk == 100) core2.reset();
    }
    check(finite(out), "block path survives reset mid-stream");
}

void testSteadyState(double sr) {
    std::printf("\n== steady state, ratio 1 (sample rate %.0f) ==\n", sr);
    const int block = 64;
    const std::size_t seconds = 10;
    const std::size_t frames = (std::size_t)(sr * (double)seconds);
    std::vector<float> in = sine(sr, 440.0, frames, 0.5);
    std::vector<float> out((std::size_t)block), blk((std::size_t)block);

    superband::Core core(sr, block, 1.0, 1.0);
    std::vector<float> rendered;
    rendered.reserve(frames + 8192);

    bool allFinite = true;
    for (std::size_t pos = 0; pos + block <= in.size(); pos += block) {
        core.processBlock(in.data() + pos, out.data(), block);
        allFinite = allFinite && finite(out);
        rendered.insert(rendered.end(), out.begin(), out.end());
    }
    check(allFinite, "every block of a 10 second run is finite");

    char msg[200];
    std::snprintf(msg, sizeof msg, "dropped input frames: %llu, starved output frames: %llu, dropped output: %llu",
                  (unsigned long long)core.droppedInputFrames(),
                  (unsigned long long)core.starvedOutputFrames(),
                  (unsigned long long)core.droppedOutputFrames());
    std::printf("  %s\n", msg);
    check(core.droppedInputFrames() == 0, "no input is dropped at ratio 1");
    check(core.droppedOutputFrames() == 0, "no output is dropped at ratio 1");
    check(core.starvedOutputFrames() < 2560,
          "only the start-up gap (one RubberBand chunk) is starved, then the stream is continuous");

    const std::size_t tail = (std::size_t)(sr * 2.0);
    const double inRms = rms(in, in.size() - tail, tail);
    const double outRms = rms(rendered, rendered.size() - tail, tail);
    const double ratioDb = 20.0 * std::log10(outRms / inRms);
    std::snprintf(msg, sizeof msg, "level preserved: input %.4f, output %.4f (%.2f dB)", inRms, outRms,
                  ratioDb);
    check(std::fabs(ratioDb) < 1.5, msg);
    check(peak(rendered) < 1.5, "no runaway gain");
}

void testRatioTwoBlockPath(double sr) {
    std::printf("\n== ratio 2 through the fixed-block path (sample rate %.0f) ==\n", sr);
    // A live stream cannot be stretched forever: consuming input at half rate
    // must eventually fill the output FIFO. The documented policy is to drop
    // input rather than emit a time-compressed glitch or grow without bound.
    const int block = 64;
    const std::size_t frames = (std::size_t)(sr * 4.0);
    std::vector<float> in = sine(sr, 440.0, frames, 0.5);
    std::vector<float> out((std::size_t)block), all;
    all.reserve(frames);

    superband::Core core(sr, block, 2.0, 1.0);
    bool ok = true;
    for (std::size_t pos = 0; pos + block <= in.size(); pos += block) {
        core.processBlock(in.data() + pos, out.data(), block);
        ok = ok && finite(out);
        all.insert(all.end(), out.begin(), out.end());
    }
    check(ok, "output stays finite when the FIFO saturates");
    check(core.droppedInputFrames() > 0,
          "input is dropped once the FIFO is full (documented real-time limit)");
    const std::size_t processed = (frames / (std::size_t)block) * (std::size_t)block;
    check(all.size() == processed, "the UGen still emits one output block per input block");
    check(peak(all) < 1.5, "no runaway gain when saturating");
    char msg[160];
    std::snprintf(msg, sizeof msg, "  dropped %llu of %zu input frames (%.1f%%)",
                  (unsigned long long)core.droppedInputFrames(), frames,
                  100.0 * (double)core.droppedInputFrames() / (double)frames);
    std::printf("%s\n", msg);
}

void testParameterAutomation(double sr) {
    std::printf("\n== parameter automation (sample rate %.0f) ==\n", sr);
    const std::size_t frames = (std::size_t)(sr * 1.0);
    std::vector<float> in = sine(sr, 440.0, frames, 0.5);
    std::vector<float> chunk(64);

    superband::Core core(sr, 64, 1.0, 1.0);
    std::vector<float> out;
    std::size_t pos = 0;
    bool ok = true;
    int step = 0;
    while (pos < in.size()) {
        const std::size_t n = std::min<std::size_t>(64, in.size() - pos);
        // Move the controls while audio is flowing, the way a .kr signal would.
        if (step % 40 == 0) {
            const double ratio = 1.0 + 0.25 * (double)((step / 40) % 4);
            const double pitch = 1.0 + 0.125 * (double)((step / 40) % 3);
            core.setRatio(ratio);
            core.setPitch(pitch);
        }
        ++step;
        while (core.writeInput(in.data() + pos, n) == 0) {
            std::size_t got;
            while ((got = core.readOutput(chunk.data(), chunk.size())) > 0) {
                out.insert(out.end(), chunk.begin(), chunk.begin() + (long)got);
                ok = ok && finite(chunk);
            }
        }
        pos += n;
        std::size_t got;
        while ((got = core.readOutput(chunk.data(), chunk.size())) > 0) {
            out.insert(out.end(), chunk.begin(), chunk.begin() + (long)got);
            ok = ok && finite(chunk);
        }
    }
    core.finish();
    check(ok, "output stays finite while ratio and pitch move during the stream");
    check(peak(out) < 2.0, "no runaway gain during automation");

    // The reported latency must follow the stretcher's own figure for the
    // settings it ends up at.
    RubberBand::RubberBandStretcher probe((std::size_t)std::lround(sr), 1,
                                          RubberBand::RubberBandStretcher::OptionProcessRealTime,
                                          core.ratio(), core.pitch());
    check(core.latency() == (double)probe.getStartDelay(),
          "reported latency tracks the current ratio/pitch");
    char msg[160];
    std::snprintf(msg, sizeof msg, "  final ratio %.3f pitch %.3f, latency %.0f frames", core.ratio(),
                  core.pitch(), core.latency());
    std::printf("%s\n", msg);
}

void testBlockSizes(double sr) {
    std::printf("\n== block sizes (sample rate %.0f) ==\n", sr);
    for (int block : {1, 16, 64, 512}) {
        superband::Core core(sr, block, 1.0, 1.0);
        const std::size_t frames = (std::size_t)(sr * 0.2);
        std::vector<float> in = sine(sr, 440.0, frames, 0.5);
        std::vector<float> out((std::size_t)block);
        bool ok = true;
        for (std::size_t pos = 0; pos + (std::size_t)block <= in.size(); pos += (std::size_t)block) {
            core.processBlock(in.data() + pos, out.data(), block);
            ok = ok && finite(out);
        }
        char msg[120];
        std::snprintf(msg, sizeof msg, "block size %d (%.1f ms) renders finite audio", block,
                      1000.0 * block / sr);
        check(ok, msg);
    }
}

} // namespace

int main() {
    const double rates[] = {48000.0, 44100.0};
    for (double sr : rates) {
        testReportedLatency(sr);
        testStartAlignment(sr);
        testTimeStretch(sr);
        testResetBehaviour(sr);
        testSteadyState(sr);
        testRatioTwoBlockPath(sr);
        testParameterAutomation(sr);
        testBlockSizes(sr);
    }

    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
