
declare name "phasemod";
declare description "phase modulation synth";
declare author "Albert Graef";
declare version "2.0";

import("stdfaust.lib");

// master volume and pan
vol = hslider("/v:[1]/vol [midi:ctrl 2]", 0.3, 0, 1, 0.01);
pan = hslider("/v:[1]/pan [midi:ctrl 10]", 0.5, 0, 1, 0.01);

// ADSR envelop
attack	= hslider("/v:[3]/[1] attack", 0.01, 0, 1, 0.001);
decay	= hslider("/v:[3]/[2] decay", 0.3, 0, 1, 0.001);
sustain = hslider("/v:[3]/[3] sustain", 0.5, 0, 1, 0.01);
release = hslider("/v:[3]/[4] release", 0.2, 0, 1, 0.001);

// voice parameters
freq(i)	= nentry("/freq%i[voice:freq]", 440, 20, 20000, 1);
gain(i)	= nentry("/gain%i[voice:gain]", 1, 0, 10, 0.01);
gate(i)	= button("/gate%i[voice:gate]");

// generic table-driven oscillator with phase modulation

// n	= the size of the table, must be a power of 2
// f	= the wave function, must be defined on the range [0,2*PI]
// freq	= the desired frequency in Hz
// mod	= the phase modulation signal, in radians

tblosc(n,f,freq,mod)	= (1-d)*rdtable(n,wave,i&(n-1)) +
			  d*rdtable(n,wave,(i+1)&(n-1))
with {
	wave	 	= ba.time*(2.0*ma.PI)/n : f;
	phase		= freq/ma.SR : (+ : ma.decimal) ~ _;
	modphase	= ma.decimal(phase+mod/(2*ma.PI))*n;
	i		= int(floor(modphase));
	d		= ma.decimal(modphase);
};

// phase modulation synth (sine modulated by another sine)

voice(i) = tblosc(1<<16, sin, freq(i), mod) * env * gain(i) with {
  env = gate(i) : en.adsr(attack, decay, sustain, release);
  mod = 2*ma.PI*tblosc(1<<16, sin, freq(i), 0)*env;
};

n = 8;
process	= sum(i, n, voice(i))
  :  (*(vol:si.smooth(0.99)) : sp.panner(pan:si.smooth(0.99)));
