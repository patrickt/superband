# SuperBand

A SuperCollider (scsynth) plugin that wraps [RubberBand](https://breakfastquay.com/rubberband/) in real-time mode: a streaming time-stretcher / pitch-shifter as a UGen. One `SuperBand` instance processes one channel; multichannel expansion gives independent stretchers per channel.

## Installation / Build

Dependencies:

- CMake ≥ 3.16
- C++17 compiler (clang++ recommended on macOS)
- pkg-config + librubberband ≥ 3.0.0 (`brew install rubberband` on macOS)
- SuperCollider plugin headers (auto-fetched from the 3.14.1 release if not installed)

Build:

```bash
cmake -S . -B build
cmake --build build
```

The plugin lands in `build/plugins/` (`SuperBand.scx` on macOS, `SuperBand.so` elsewhere). Tests:

```bash
ctest --test-dir build
```

### macOS note

Apple clang 15 + SDK 27 breaks libc++. Use the Homebrew LLVM clang and the macOS 26.5 SDK:

```bash
CXX=/opt/homebrew/opt/llvm/bin/clang++ cmake -S . -B build \
  -DCMAKE_OSX_SYSROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX26.5.sdk
cmake --build build
```

Install into your SuperCollider extensions path (plugin + class):

```bash
cmake --install build   # plugin -> lib/SuperCollider/plugins, class -> share/SuperCollider/Extensions
```

Or copy manually: `build/plugins/SuperBand.scx` → `~/Library/Application Support/SuperCollider/Extensions/SuperBand/`, and `sc/classes/SuperBand.sc` → the same `Extensions/SuperBand/` directory, then recompile the class library (sclang: `Language → Recompile Class Library`).

## UGen API

```supercollider
SuperBand.ar(in, ratio = 1.0, pitch = 1.0, reset = 0.0)           // audio only
SuperBand.arWithLatency(in, ratio = 1.0, pitch = 1.0, reset = 0.0) // [audio, latency]
```

Arguments:

- `in` — audio input, mono per instance. Multichannel expansion gives one independent stretcher per channel (no cross-channel coherence).
- `ratio` — k-rate. Output duration / input duration. Clamped to `[0.05, 20]`. Non-finite or non-positive values fall back to `1.0`.
- `pitch` — k-rate. Pitch factor (2.0 = octave up). Clamped to `[0.05, 20]`. Non-finite or non-positive values fall back to `1.0`.
- `reset` — k-rate. Restarts the stretcher on a rising edge. Non-finite input also triggers a reset and is replaced with silence.

Outputs:

- Output 0 — audio.
- Output 1 — latency in frames (constant within a block), equal to `RubberBandStretcher::getStartDelay()` for the current settings.

Verified latency (48 kHz): 1024 frames at ratio 1 / pitch 1; 512 at pitch 2; 2048 at pitch 0.5.

## Latency compensation

SuperBand reports its latency on output 1 so you can compensate downstream. The reported value equals RubberBand's `getStartDelay()` and matches inside scsynth renders. Two common patterns:

1. **Delay the dry/reference path** by the reported latency so wet and dry align:

   ```supercollider
   var (wet, lat) = SuperBand.arWithLatency(src, ratio, pitch);
   var dry = DelayN.ar(src, 1, lat / SampleRate.ir);
   ```

2. **Trim a fixed number of frames** at the start of an offline render using the reported latency.

Startup behaviour: the first material frame appears at output frame 0 (the recommended start pad is fed and the start delay is trimmed), but the first ~31 blocks (~41 ms at 48 kHz / block size 64) are silence while RubberBand fills its first internal chunk. Compensate with a fade-in or a fixed pre-delay if needed.

## Known limits

- `ratio != 1` on an unbounded live stream cannot be sustained: once the output FIFO is full the UGen drops input blocks rather than growing without bound (≈47 % dropped after 4 s at ratio 2). For real stretching, feed finite material or use `ratio = 1` for pure pitch shifting.
- Real-time mode leaves a constant input/output frame-count offset of about one start pad (+1024 frames at ratio 2, −512 at 0.5). This is not a ratio error.
- Per-channel audio relies on multichannel expansion: no `ChannelsTogether` stereo coherence.
- Startup emits ~31 blocks of silence (see above).

## Examples

Pitch shift up an octave, same duration:

```supercollider
{ SuperBand.ar(SinOsc.ar(220) * 0.2, pitch: 2) }.play;
```

Time stretch 2× with latency compensation:

```supercollider
(
{
    var src = SoundIn.ar(0);
    var (wet, lat) = SuperBand.arWithLatency(src, ratio: 2, pitch: 1);
    wet * 0.5
}.play;
)
```

Reset on a trigger:

```supercollider
(
{
    var trig = Impulse.kr(0.5);
    var src = SinOsc.ar(440) * EnvGen.kr(Env.perc, trig);
    SuperBand.ar(src, ratio: 1.5, reset: trig)
}.play;
)
```

Stereo (per-channel independent stretchers via multichannel expansion):

```supercollider
{ SuperBand.ar(SinOsc.ar([440, 660]) * 0.2, pitch: 1.5) }.play;
```
