SuperBand : MultiOutUGen {
	// RubberBand time/pitch stretcher.
	//
	//	SinOsc.ar(220) |> SuperBand.ar(ratio: 2)	// twice as long, same pitch
	//	SinOsc.ar(220) |> SuperBand.ar(pitch: 2)	// octave up, same duration
	//
	// `in` is mono per instance: SuperCollider's multichannel expansion gives
	// one independent stretcher per channel, so stereo input needs no special
	// handling. `reset` restarts the stretcher on a rising edge (use it after a
	// splice or a discontinuous source change).
	//
	// The UGen has two outputs: audio, and the latency in frames as reported by
	// RubberBand (RubberBandStretcher::getStartDelay). .ar returns the audio
	// only; .arWithLatency returns both, e.g. [audio, latency].

	*ar { |in, ratio = 1.0, pitch = 1.0, reset = 0.0|
		var ugen = this.multiNew('audio', in, ratio, pitch, reset);
		^if(in.isArray) { ugen.collect { |u| u.at(0) } } { ugen.at(0) }
	}

	*arWithLatency { |in, ratio = 1.0, pitch = 1.0, reset = 0.0|
		^this.multiNew('audio', in, ratio, pitch, reset)
	}

	init { |... theInputs|
		inputs = theInputs;
		channels = [
			OutputProxy(rate, this, 0),
			OutputProxy(rate, this, 1)
		];
		^channels
	}
}
