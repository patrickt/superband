// SuperBand examples — run in SuperCollider after installing the plugin.

// 1. Pitch shift up an octave, same duration.
{ SuperBand.ar(SinOsc.ar(220) * 0.2, pitch: 2) }.play;

// 2. Time stretch 2x with latency compensation.
(
{
    var src = SoundIn.ar(0);
    var (wet, lat) = SuperBand.arWithLatency(src, ratio: 2, pitch: 1);
    wet * 0.5
}.play;
)

// 3. Reset on a trigger.
(
{
    var trig = Impulse.kr(0.5);
    var src = SinOsc.ar(440) * EnvGen.kr(Env.perc, trig);
    SuperBand.ar(src, ratio: 1.5, reset: trig)
}.play;
)

// 4. Stereo per-channel independent stretchers.
{ SuperBand.ar(SinOsc.ar([440, 660]) * 0.2, pitch: 1.5) }.play;
