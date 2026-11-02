#include "SuperBandCore.hpp"

namespace superband {

namespace {
// Largest FIFO we will ever allocate (frames of mono float audio, 4 MiB).
constexpr std::size_t kMaxFifoFrames = 1u << 20;
} // namespace

double Core::clampRatio(double v) const {
    // Non-finite or non-positive values fall back to 1.0 rather than muting.
    if (!std::isfinite(v) || v <= 0.0) return 1.0;
    return std::min(std::max(v, kMinRatio), kMaxRatio);
}

Core::Core(double sampleRate, int maxBlockSize, double ratio, double pitch)
    : mSampleRate(std::min(std::max(sampleRate, kMinSampleRate), kMaxSampleRate)),
      mBlockSize(std::max(1, maxBlockSize)),
      mRatio(clampRatio(ratio)),
      mPitch(clampRatio(pitch)) {
    if (!std::isfinite(sampleRate) || sampleRate <= 0.0) mSampleRate = 44100.0;

    mStretcher.reset(new RubberBand::RubberBandStretcher(
        static_cast<std::size_t>(std::lround(mSampleRate)), 1,
        RubberBand::RubberBandStretcher::OptionProcessRealTime, mRatio, mPitch));
    // Tell the engine our block size so it can work without reallocating.
    mStretcher->setMaxProcessSize(static_cast<std::size_t>(mBlockSize));

    const std::size_t block = static_cast<std::size_t>(mBlockSize);
    mPad.assign(block, 0.0f);
    mScratch.assign(block, 0.0f);
    mIn.assign(block, 0.0f);

    mStartPad = mStretcher->getPreferredStartPad();
    mStartDelay = mStretcher->getStartDelay();

    resizeFifo();
    prime();
}

Core::~Core() = default;

void Core::resizeFifo() {
    const std::size_t block = static_cast<std::size_t>(mBlockSize);
    std::size_t want = block * 16 + 4 * (mStartPad + mStartDelay) + 1024;
    want = std::min<std::size_t>(want, kMaxFifoFrames);
    if (mFifo.size() < want) mFifo.assign(want, 0.0f);
}

void Core::prime() {
    mFifoRead = 0;
    mFifoCount = 0;
    mSkipRemaining = mStartDelay;

    // Feed the recommended silent pre-roll so the engine has history to work
    // with when the first real sample arrives.
    std::size_t remaining = mStartPad;
    while (remaining > 0) {
        const std::size_t n = std::min(remaining, mPad.size());
        std::fill_n(mPad.begin(), n, 0.0f);
        const float *p[1] = { mPad.data() };
        mStretcher->process(p, n, false);
        remaining -= n;
    }
    drain();
}

void Core::drain() {
    // Discard the stretcher's start delay before anything reaches the FIFO.
    while (mSkipRemaining > 0) {
        const int avail = mStretcher->available();
        if (avail <= 0) return;
        std::size_t chunk = std::min<std::size_t>(static_cast<std::size_t>(avail), mSkipRemaining);
        chunk = std::min<std::size_t>(chunk, mScratch.size());
        float *p[1] = { mScratch.data() };
        const std::size_t got = mStretcher->retrieve(p, chunk);
        if (got == 0) return;
        mSkipRemaining -= got;
    }

    for (;;) {
        const int avail = mStretcher->available();
        if (avail <= 0) return;
        const std::size_t want = static_cast<std::size_t>(avail);
        const std::size_t space = mFifo.size() - mFifoCount;

        if (space == 0) {
            // FIFO full: keep the engine drained, drop the excess, count it.
            const std::size_t chunk = std::min<std::size_t>(want, mScratch.size());
            float *p[1] = { mScratch.data() };
            const std::size_t got = mStretcher->retrieve(p, chunk);
            if (got == 0) return;
            mDroppedOutput += got;
            continue;
        }

        const std::size_t writeIdx = (mFifoRead + mFifoCount) % mFifo.size();
        const std::size_t chunk = std::min(want, std::min(space, mFifo.size() - writeIdx));
        float *p[1] = { mFifo.data() + writeIdx };
        const std::size_t got = mStretcher->retrieve(p, chunk);
        if (got == 0) return;
        mFifoCount += got;
    }
}

std::size_t Core::writeInput(const float *src, std::size_t n) {
    if (src == nullptr || n == 0) return 0;
    n = std::min(n, mIn.size());

    // A strung-together splice or a misbehaving upstream UGen can put non-finite
    // values into the stream; the stretcher must never see them.
    bool corrupt = false;
    for (std::size_t i = 0; i < n; ++i) {
        const float v = src[i];
        if (std::isfinite(v)) {
            mIn[i] = v;
        } else {
            mIn[i] = 0.0f;
            corrupt = true;
        }
    }
    if (corrupt) reset();

    // Refuse input the FIFO could not absorb, so a pull-style caller can drain
    // first instead of losing frames.
    const double growth = static_cast<double>(n) * mRatio + static_cast<double>(mBlockSize);
    if (static_cast<double>(mFifoCount) + growth > static_cast<double>(mFifo.size())) return 0;

    const float *p[1] = { mIn.data() };
    mStretcher->process(p, n, false);
    drain();
    return n;
}

std::size_t Core::readOutput(float *dst, std::size_t maxFrames) {
    if (dst == nullptr || maxFrames == 0) return 0;
    const std::size_t take = std::min(maxFrames, mFifoCount);
    const std::size_t first = std::min(take, mFifo.size() - mFifoRead);
    if (first > 0) std::memcpy(dst, mFifo.data() + mFifoRead, first * sizeof(float));
    if (take > first) std::memcpy(dst + first, mFifo.data(), (take - first) * sizeof(float));

    mFifoRead = (mFifoRead + take) % mFifo.size();
    mFifoCount -= take;
    return take;
}

void Core::processBlock(const float *in, float *out, int n) {
    // Chunk defensively in case a caller hands us more than the block size the
    // engine was sized for (e.g. a server restarted with a larger block).
    int done = 0;
    while (done < n) {
        const int chunk = std::min(n - done, mBlockSize);
        processChunk(in == nullptr ? nullptr : in + done, out + done, chunk);
        done += chunk;
    }
}

void Core::processChunk(const float *in, float *out, int n) {
    if (out == nullptr || n <= 0) return;

    std::size_t got = 0;
    if (in != nullptr) {
        const std::size_t accepted = writeInput(in, static_cast<std::size_t>(n));
        if (accepted < static_cast<std::size_t>(n)) {
            // The FIFO is saturated (ratio > 1 on a live stream): drop the block
            // rather than emit time-compressed output with a discontinuity.
            mDroppedInput += static_cast<std::size_t>(n) - accepted;
        }
    }
    drain();
    got = readOutput(out, static_cast<std::size_t>(n));
    if (got < static_cast<std::size_t>(n)) {
        std::memset(out + got, 0, (static_cast<std::size_t>(n) - got) * sizeof(float));
        mStarvedOutput += static_cast<std::size_t>(n) - got;
    }
}

void Core::finish() {
    const float *p[1] = { nullptr };
    mStretcher->process(p, 0, true);
    drain();
}

void Core::setRatio(double ratio) {
    const double v = clampRatio(ratio);
    if (v == mRatio) return;
    mRatio = v;
    mStretcher->setTimeRatio(mRatio);
    mStartDelay = mStretcher->getStartDelay();
}

void Core::setPitch(double pitch) {
    const double v = clampRatio(pitch);
    if (v == mPitch) return;
    mPitch = v;
    mStretcher->setPitchScale(mPitch);
    mStartDelay = mStretcher->getStartDelay();
}

void Core::reset() {
    mStretcher->reset();
    mStretcher->setTimeRatio(mRatio);
    mStretcher->setPitchScale(mPitch);
    mStartPad = mStretcher->getPreferredStartPad();
    mStartDelay = mStretcher->getStartDelay();
    resizeFifo();
    prime();
}

} // namespace superband
