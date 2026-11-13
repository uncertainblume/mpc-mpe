# MPE Synth

Version 0.2.0. A light 12-voice synthesizer for the MPC in which every note has its own
pitch bend, timbre (CC 74) and pressure. It is meant as a quick MPE-first performance
instrument, not a deep one.

On MPC 3.10.1 (measured on a Live III) it gets each note's channel through its own track:
put it on a plugin track and make the MPE controller that track's input. If a track ever
delivers every note on channel 1, use its own port, "MPE Synth MIDI In", as described in
[docs/routing-guide.md](../../docs/routing-guide.md), recipe 1.

- Voice: one PolyBLEP oscillator (sine, triangle, saw, square), an optional detuned
  second copy (Spread), a sine one octave down (Sub) and white noise, through Drive into a
  low-pass filter and an ADSR. Y and pressure move the cutoff; pressure and velocity
  share the loudness; Z Curve sets how firmly pressure must be applied.
- Stereo: each voice has a fixed place left or right, scaled by Width (0 = mono).
- Pages: PLAY (filter, expression, envelope, output: four blocks of four for the
  Q-Links), OSC, SETUP (bend range, master channel, input route, and which channels have
  arrived by each route).
- MPE: lower zone or no master channel; bend range from RPN 0 or set by hand (default
  48); sustain pedal; channel pressure or poly aftertouch for Z; smoothing for the 7-bit
  controls.
- Nine built-in programs. The last four use the 0.2.0 additions.
- Compatibility: the 0.2.0 parameters were appended with neutral defaults, so sounds and
  projects saved with the first build load unchanged. That first build was labelled
  1.0.0; 0.2.0 is the newer one.
- Tested: builds for arm32 and arm64 and passes the SDK's host simulator. Version 0.2.0
  has not been run on an MPC yet and its new programs have not been listened to.
- Not done: no XY pad or envelope display, no user patch files beyond the MPC's own
  preset save, no release velocity, no effects.
- CPU: not measured on a device. Spread adds a second oscillator per voice.

If your controller plays notes on channel 1 as well (a LinnStrument with per-note
channels 1 to 8, for example), set Master Ch to None on the SETUP page; otherwise notes
on channel 1 use the master channel's narrow bend range and bend every other note.

Built with Akai's MPC Plugin SDK (MIT). MIT licensed.
