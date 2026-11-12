# MPE Synth

A 12-voice synthesizer for the MPC in which every note has its own pitch bend, timbre
(CC 74) and pressure. It receives MIDI with channels intact through its own port,
"MPE Synth MIDI In".

Setup and controls: [docs/routing-guide.md](../../docs/routing-guide.md), recipe 1.

- Voice: one PolyBLEP oscillator (sine, triangle, saw, square) into a low-pass filter and
  an ADSR. Y and pressure move the cutoff; pressure and velocity share the loudness.
- MPE: lower zone or no master channel; bend range from RPN 0 or set by hand (default
  48); sustain pedal; channel pressure or poly aftertouch for Z; smoothing for the 7-bit
  controls. Five built-in programs.
- Not done: nothing has been run on an MPC yet. Port MIDI is audio-block resolution. No
  release velocity, no per-note pan, no unison or effects.
- CPU: twelve voices of one oscillator and one filter each; expected to be light, not
  measured on a device.

Built with Akai's MPC Plugin SDK (MIT). MIT licensed.
