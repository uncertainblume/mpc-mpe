# LinnStrument MIDI / MPE reference (for this project)

Condensed from the open-source firmware, `rogerlinndesign/linnstrument-firmware`
(Apache 2.0): `midi.md`, `ls_midi.ino`, `ls_handleTouches.ino`, `ls_settings.ino`, at the
commit listed in [sources.md](sources.md). The full NRPN table is in the firmware's
`midi.md`; only what matters for MPE on an MPC is here.

## The three MIDI modes (per split)

| Mode | Notes go to | Per-note X / Y / Z |
|---|---|---|
| One Channel | the Main channel | No (Z can still be per note as Poly Aftertouch) |
| **Channel Per Note** | one of the enabled Per-Note channels each | **Yes. This is MPE.** |
| Channel Per Row | a channel per row | Per row |

## What a touch sends in Channel Per Note mode

On the note's own channel, in this order (`handleXYZupdate()` and `sendNewNote()`):

1. Pitch bend: the touch's starting offset, if Send X is on
2. Y: CC 74 (or the CC you chose), if Send Y is on
3. Z: pressure 0
4. **Note On**
5. While held: pitch bend (X), Y, Z as they change
6. **Note Off** with release velocity

So a receiver always knows a note's starting pitch, timbre and pressure before the note
starts. Both plugins here rely on that and apply those values at note-on without gliding.

After release the firmware does not reset pitch bend unless "Pitch Reset On Release" is
on; the next touch on that channel sends its own bend first.

## Pitch bend scaling

Bend values are scaled so that the configured **Bend Range** in semitones is full scale
(`scalePitch()`); internally the surface is measured at 48 semitones full scale. Sender
and receiver must agree on the range. Options on the panel are 2, 3, 12 and a custom value
from 1 to 96 (default 24). The MPE standard is 48.

With MPE enabled the firmware announces the range to the receiver
(`midiSendMpePitchBendRange()`): RPN 0 with the range on the Main channel and on each
Per-Note channel, after RPN 6 (MPE Configuration) on the Main channel with the number of
Per-Note channels (`midiSendMpeState()`). The plugins here read both, so with *In Bend* /
*Bend Range* on Auto they follow the LinnStrument. An MPC track that was not recording at
that moment will not have those messages, which is why Auto falls back to 48.

RPN message shape, as sent: CC 101 (MSB), CC 100 (LSB), CC 6 (value MSB), CC 38 (value
LSB), then CC 101 = 127 and CC 100 = 127 to close.

## Y and Z options

| Axis | Choices |
|---|---|
| Y (timbre) | CC 74 (default), CC 1, any CC, Poly Aftertouch, Channel Aftertouch; absolute or relative; min / max values |
| Z (loudness) | Poly Aftertouch, Channel Aftertouch, any CC (CC 11 suggested); optional 14-bit CC |

Because the LinnStrument can already send Y and Z as almost anything, recipe 3 in
[routing-guide.md](routing-guide.md) needs no translation at all. The bridge exists for
everything else: recording once in standard MPE and re-aiming it later, fewer instrument
copies than fingers, and bend ranges the instruments do not have.

## No master channel

Switching the Main channel off makes every enabled channel a Per-Note channel; messages
meant for all notes (sustain, volume) are then repeated on every one of them. Set
*Master Ch* to **None** in the plugins for this layout; the bridge sends each pedal change
once per output channel.

## Setting it from the MPC or a computer (NRPN)

NRPNs are received on any channel. Send CC 99 (number MSB), CC 98 (number LSB), CC 6
(value MSB), CC 38 (value LSB), then CC 101 = 127, CC 100 = 127. Left split shown; add 100
for the right split.

| NRPN | Value | Meaning |
|---|---|---|
| 0 | 1 | MIDI Mode = Channel Per Note |
| 1 | 1–16 | Main channel |
| 2–17 | 0 / 1 | Per-Note channel 1–16 off / on |
| 19 | 1–96 | Bend Range |
| 20 | 0 / 1 | Send X |
| 23 | 0 / 1 | Pitch Reset On Release |
| 24 | 0 / 1 | Send Y |
| 25 | 0–127 | CC for Y (74 recommended) |
| 26 | 0 / 1 | Relative Y |
| 27 | 0 / 1 | Send Z |
| 28 | 0 / 1 / 2 | Z as Poly Aftertouch / Channel Aftertouch / CC |
| 29 | 0–127 | CC for Z |
| 39 | 0 / 1 / 2 | Y as Poly Aftertouch / Channel Aftertouch / CC |
| 299 | NRPN number | Ask for the current value of that setting |

Example, "four voices on channels 2–5, bend 12" for recipe 3: NRPN 0 = 1, NRPN 1 = 1,
NRPN 2 = 0, NRPN 3–6 = 1, NRPN 7–17 = 0, NRPN 19 = 12.

The LinnStrument also understands the standard MPE switch-on message itself
(`receivedRpn()`): RPN 6 with the number of member channels, sent on channel 1, enables
MPE on the left split; sent on channel 16, on the right split; a count of 0 switches it
off. RPN 0 sets the bend range of both splits.

## Also in the firmware repository

- `user_firmware_mode.md`: raw X / Y / Z per cell for custom hosts.
- `examples/browser/`: a settings editor and surface designer that run in a browser over
  Web MIDI (hosted at uwyn.com/linnstrument).
- A "Virtual LinnStrument" that runs the firmware on a computer behind a virtual MIDI
  port, useful for generating real MPE streams without the instrument.
