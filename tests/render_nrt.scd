// Non-real-time render through SuperCollider's own server (no audio hardware needed).
//
// Mono mode (default): left channel is SuperBand audio, right channel is the
// reported latency / 2048 as a DC value.
// Stereo mode (SB_STEREO=1): two independent per-channel stretchers, one
// frequency per channel, both pitch shifted.
//
// Parameters come from the environment so the same patch can be rendered for
// several settings:
//
//   SB_RATIO=1 SB_PITCH=2 SB_FREQ=440 SB_OUT=/tmp/x.wav sclang tests/render_nrt.scd
//
(
var options, score, def;
var bundled = "/Applications/SuperCollider.app/Contents/Resources/scsynth";
var ratio = ("SB_RATIO".getenv ? "1.0").asFloat;
var pitch = ("SB_PITCH".getenv ? "1.0").asFloat;
var freq = ("SB_FREQ".getenv ? "440.0").asFloat;
var freq2 = ("SB_FREQ2".getenv ? "660.0").asFloat;
var stereo = ("SB_STEREO".getenv ? "0").asFloat > 0;
var outPath = "SB_OUT".getenv ? "/tmp/superband_nrt.wav";
var seconds = ("SB_SECONDS".getenv ? "2.5").asFloat;

// Headless use: scsynth is normally found through PATH, which the macOS app
// bundle does not provide.
if(File.exists(bundled)) { Server.program = bundled };

options = ServerOptions.new;
options.numOutputBusChannels = 2;
options.sampleRate = 48000;

def = if(stereo) {
	// Multichannel expansion: one stretcher instance per channel.
	SynthDef(\sbTest, { |ratio = 1.0, pitch = 1.0, freq = 440, freq2 = 660|
		var sb = SuperBand.ar([SinOsc.ar(freq, 0, 0.5), SinOsc.ar(freq2, 0, 0.5)], ratio, pitch, 0);
		Out.ar(0, sb);
	});
} {
	SynthDef(\sbTest, { |ratio = 1.0, pitch = 1.0, freq = 440|
		var sb = SuperBand.arWithLatency(SinOsc.ar(freq, 0, 0.5), ratio, pitch, 0);
		Out.ar(0, [sb[0], sb[1] / 2048.0]);
	});
};

score = Score.new;
score.add([0.0, [\d_recv, def.asBytes]]);
score.add([0.02, [\s_new, \sbTest, 1000, 0, 0, \ratio, ratio, \pitch, pitch,
	\freq, freq, \freq2, freq2]]);
score.add([0.02 + seconds, [\n_free, 1000]]);
score.recordNRT(
	outputFilePath: outPath,
	oscFilePath: outPath ++ ".osc.txt",
	sampleRate: 48000,
	headerFormat: "WAV",
	sampleFormat: "float",
	options: options,
	duration: seconds + 0.5,
	action: { |exitCode|
		("rendered ratio % pitch % freq % to % (scsynth exit %)")
			.format(ratio, pitch, freq, outPath, exitCode).postln;
		// headless: quit once the render is on disk
		0.exit;
	}
);
)
