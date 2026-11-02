/*
 * SuperBand — RubberBand-powered time/pitch stretcher for SuperCollider.
 *
 * SuperBandCore.hpp: the streaming stretcher engine, deliberately free of any
 * SuperCollider dependency so it can be driven by the offline test harness as
 * well as by the UGen wrapper.
 *
 * Streaming model
 * ---------------
 * RubberBandStretcher in real-time mode is a block-based stream processor: you
 * push frames with process() and pull frames with retrieve(), and the number of
 * output frames only matches the input frame count when the time ratio is 1.
 * A SuperCollider UGen, by contrast, must produce exactly one output block per
 * input block. This class bridges the two with an output FIFO:
 *
 *   writeInput()  -> RubberBandStretcher::process()
 *   drain()       -> RubberBandStretcher::retrieve() -> FIFO
 *   readOutput()  -> FIFO -> caller
 *
 * Start-up follows the procedure recommended by the RubberBand manual for
 * real-time mode: getPreferredStartPad() frames of silence are pushed before
 * any material, and getStartDelay() frames of output are trimmed, which keeps
 * the output time-aligned with the input instead of losing the first frames.
 *
 * Ratios other than 1 cannot be sustained forever on an unbounded live stream
 * (stretching means consuming input slower than it arrives). When the FIFO
 * cannot absorb more output, processBlock() drops input frames and counts them
 * in droppedInputFrames(); the pull-style API instead reports 0 accepted frames
 * and lets the caller drain before retrying, so no data is lost.
 */

#pragma once

#include <rubberband/RubberBandStretcher.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace superband {

class Core {
public:
    // RubberBand is documented for sample rates of 8000..192000 Hz; ratios far
    // outside this range are not musically useful and stress the engine.
    static constexpr double kMinSampleRate = 8000.0;
    static constexpr double kMaxSampleRate = 192000.0;
    static constexpr double kMinRatio = 0.05;
    static constexpr double kMaxRatio = 20.0;

    Core(double sampleRate, int maxBlockSize, double ratio = 1.0, double pitch = 1.0);
    ~Core();

    Core(const Core &) = delete;
    Core &operator=(const Core &) = delete;

    // ---- parameters -------------------------------------------------------
    double ratio() const { return mRatio; }
    double pitch() const { return mPitch; }
    void setRatio(double ratio);
    void setPitch(double pitch);

    // Latency reported by RubberBand for the current settings, in frames
    // (RubberBandStretcher::getStartDelay(), the successor of getLatency()).
    double latency() const { return static_cast<double>(mStartDelay); }
    // Silence frames pushed ahead of the material at start-up, in frames.
    double startPad() const { return static_cast<double>(mStartPad); }

    // ---- streaming --------------------------------------------------------
    // Restart as if freshly constructed, keeping the current ratio and pitch.
    // Discards buffered output and re-applies the start pad.
    void reset();

    // Push style. Returns the number of frames accepted, which is 0 when the
    // output FIFO cannot absorb another block yet (drain, then retry).
    std::size_t writeInput(const float *src, std::size_t n);

    // Pull style: frames currently buffered and ready to be read.
    std::size_t availableOutput() const { return mFifoCount; }
    // Copies up to maxFrames buffered frames; returns how many were copied.
    // Does not pad: a short read simply means less is buffered.
    std::size_t readOutput(float *dst, std::size_t maxFrames);

    // Fixed-block path used by the UGen: exactly n frames in, exactly n frames
    // out, zero-filled while the pipeline is still filling.
    void processBlock(const float *in, float *out, int n);

    // Signal end of input so the stretcher emits its tail (finite use only).
    void finish();

    // ---- diagnostics ------------------------------------------------------
    std::uint64_t droppedInputFrames() const { return mDroppedInput; }
    std::uint64_t starvedOutputFrames() const { return mStarvedOutput; }
    std::uint64_t droppedOutputFrames() const { return mDroppedOutput; }
    std::size_t bufferedFrames() const { return mFifoCount; }
    std::size_t fifoCapacity() const { return mFifo.size(); }
    double sampleRate() const { return mSampleRate; }
    int blockSize() const { return mBlockSize; }

private:
    void resizeFifo();
    void prime();
    void drain();
    void processChunk(const float *in, float *out, int n);
    double clampRatio(double v) const;

    std::unique_ptr<RubberBand::RubberBandStretcher> mStretcher;

    double mSampleRate;
    int mBlockSize;
    double mRatio;
    double mPitch;

    std::size_t mStartPad = 0;
    std::size_t mStartDelay = 0;
    std::size_t mSkipRemaining = 0;

    std::vector<float> mFifo;
    std::size_t mFifoRead = 0;
    std::size_t mFifoCount = 0;

    std::vector<float> mPad;
    std::vector<float> mScratch;
    std::vector<float> mIn;

    std::uint64_t mDroppedInput = 0;
    std::uint64_t mStarvedOutput = 0;
    std::uint64_t mDroppedOutput = 0;
};

} // namespace superband
