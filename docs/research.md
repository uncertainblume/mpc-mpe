# What the MPC does with MPE, and how to get around it

Findings from the documentation gathered on 2026-11-12 (list and links in
[sources.md](sources.md)). Each claim says where it comes from. "Verified" below always
means *verified by the cited source*, never by this project: nothing here has been run on
an MPC yet. The last section lists what to check first on a device.

## The short version

- The MPC sequencer has recorded and played back **per-event MIDI channels since MPC 3.6**.
  One track can hold a complete MPE performance.
- A **plugin on a track does not get those channels**. Through the plugin interface,
  note-ons arrive on channel 1. A plugin's MIDI *output* through the plugin interface is
  discarded. So neither "an MPE synth on a track" nor "a plugin that splits MPE for other
  tracks" works the obvious way.
- A plugin can open **its own MIDI ports** at operating-system level (ALSA sequencer). The
  MPC lists them with the hardware ports, in both directions, and they keep channels.
- That gives two working designs, both built in this repository:
  1. an MPE instrument that listens on its own port (**MPE Synth**), fed by one track;
  2. a router with an input and an output port (**MPE Bridge**) that deals an MPE stream
     onto one channel per copy of *any* MPC instrument.
- Without any plugin at all, N tracks that each listen to one MIDI channel already give
  N-voice MPE with any instrument; the cost is recording onto N tracks.

## What Akai documents

### Plugins (MPC OS 3.10.1, official SDK)

Source: `inmusic-software/mpc-plugin-builder-sdk`, `docs/host-contract.md`,
`docs/overview.md`, `docs/faq-and-legal.md`.

- Third-party plugins are Linux shared libraries with the VST 2.4 binary interface, loaded
  in-process from a `VST2 Plugins` folder on any drive. Needs MPC OS 3.10.1 and
  Preferences → Enhancements (Beta) → *Enable Enhancements* + *External Plugin Support*.
- "**Instruments** receive note on/off, controllers, pitch bend, aftertouch, program change
  and sysex from the track." "**Effects receive no MIDI at all.**"
- "MIDI output from a plugin is accepted by the host but then discarded."
- The host asks each plugin four capability questions: `receiveVstMidiEvent`,
  `sendVstMidiEvent`, `bypass` and **`MPE`**. The SDK framework does not answer the last
  one, and nothing documents what the MPC does when a plugin says yes.
- No sandbox: "A crash in your plugin crashes the MPC."
- Audio: 44.1 kHz, float, blocks of 32 to 512 frames (128 by default).

### Sequencer and track routing

Source: *MPC Standalone OS User Guide v3.4* and the *MPC 3.6 release notes* (Akai PDFs).

- Every track has a **MIDI Input Port** and **MIDI Input Channel** ("Select All to receive
  from all MIDI channels, or select 1–16"), a **MIDI Send** / *Send To* ("send the MIDI data
  from one track to another track"), and MIDI tracks have a **MIDI Output** port and
  channel.
- **MIDI Monitor**: *Off*; *In* ("always monitored regardless of the track's Record Arm
  state, and playback from recorded events will not be heard"); *Auto* (monitored when
  armed); *Merge* (always monitored, playback also heard).
- Several tracks can be record-armed at once (Preferences → *Rec Arm Behavior: Multi*, or
  Shift + arm).
- MPC 3.6, "Record and playback MIDI Channel with events":
  - "Incoming MIDI events now store their original channel (1–16) in the sequencer."
  - Input channel *All*: "Record events from all MIDI channels and preserve each event's
    channel information."
  - Output channel *1–16* forces a channel; "**Channel: ANY**: Send each MIDI event on its
    originally recorded channel."
  - The list editor shows each event's channel.
- Preferences → MIDI / Sync: each input port has *Global* (goes to the current track),
  *Control* and *Track* (selectable as a track input) switches; "Enable MIDI Ports When
  Discovered" turns *Track* on for new ports automatically.
- Pitch bend range is per MPC instrument, in semitones; "Pitch Bend ranges are only applied
  to MPC instruments and not to third-party plugins." A *Global Pitch Bend* preference can
  override every track's range, so leave it off for MPE work.
- MPC 3.6 also introduced "MPCe" pads on the MPC Live III that send X, Y and pressure, but
  only as modulation for drum tracks. It is not MPE and does not touch external MIDI.

Nothing in the user guide or any release notes from 3.6 to 3.10.1 mentions MPE by name.

MPC 3.10.1's Developer panel (Shift + Menu, then Developer in the mode menu) gives a
shell, switches on the SSH server, shows system logs and manages external plugins. That
makes the open questions below checkable directly on the device (`aconnect -l` lists
sequencer ports and their connections; `aseqdump -p <port>` prints what arrives, with
channels). The *Advanced audio settings* enhancement exposes the block size, which sets
the timing resolution of port MIDI.

## What the community measured

Source: `open-mpc/mpc-vst-plugins`, `docs/NOTES.md` (device traces on MPC OS 3.9.1, with
plugins loaded by the community's own loader before the official SDK existed) and
`saustin2010/mpc-vst-percolator`, `src/midiin.h`.

- "**MPC hands a plugin on its track every note-on on channel 1**, whatever channel the
  MIDI track sent it on, and its note-off on the channel it came on (traced: a MIDI track
  on channel 3, routed into the plugin's track, arrived as `90 41 7f` then `82 41 40`)."
- "**A plugin's own ALSA sequencer input port works as a MIDI track's output.**" A writable
  port is "listed in a MIDI track's MIDI OUTPUT PORT as `<client> MIDI In`, and the track's
  notes and CCs reach it on their own channels."
- A plugin's readable ALSA port is picked up as a MIDI *input*: "MPC's own seq client
  hot-detects" it, and tracks can select it. Several released community plugins drive
  other tracks this way.
- "MPC enables a plugin's new ALSA port for track input, so every note-on and note-off a
  MIDI-generating plugin sends comes back into its own track moments later, on the channel
  it was sent on." A plugin that both sends on a port and listens to its track must filter
  its own echo.
- Timing through a port is block resolution: about 2.9 ms at 128 frames, measured jitter
  0.15 to 0.5 ms.
- `libasound.so.2` ships with MPC OS.

## Why MPE needs more than the MPC gives a plugin

An MPE controller plays each note on its own channel and sends that note's pitch bend,
pressure and CC 74 on the same channel. A receiver must know which channel each note-on
came in on. With note-ons rewritten to channel 1 the link between a note and its bend is
gone, and the plugin interface offers no way to send anything onwards. Hence the ports.

## The designs

### 1. MPE instrument on its own port (MPE Synth)

```
LinnStrument ─► MIDI track "MPE"  in: LinnStrument, ch All     (records every event with its channel)
                                  out: "MPE Synth MIDI In", ch Any
                                            │
                                  plugin track: MPE Synth      (reads its port at the start of each block)
```

One track holds the whole performance. This is the only route that is fully MPE with a
single instrument instance, and it needs an instrument that reads a port, which today
means one built for it.

### 2. Any instrument, one copy per voice (MPE Bridge)

```
LinnStrument ─► MIDI track "MPE"  in: LinnStrument, ch All;  out: "MPE Bridge MIDI In", ch Any
                                            │
                                  plugin track: MPE Bridge    (Rotate: N voices from channel 2)
                                            │ "MPE Bridge MIDI Out"
                 ┌──────────────────────────┼──────────────────────────┐
        track A  in: Bridge Out, ch 2    track B  ... ch 3          track N ... ch N+1
        Monitor: In                      (each holds one copy of the same instrument)
```

Each copy plays at most one note at a time, so an ordinary channel-wide pitch bend,
aftertouch and controller *are* per note. The bridge also converts: bend range (a
controller at ±48 into instruments at ±12 or ±2), Y to any of CC 74 / CC 1 / CC 2 / CC 11
/ aftertouch, Z to channel aftertouch, poly aftertouch or a CC. Because the conversion
happens at playback, one recorded MPE take can be re-aimed at different instruments.

In *Thru* mode the bridge is just a loop-back cable: the recorded track goes in, and
tracks filtered to one channel each pick the notes out again, unchanged.

In *Single* mode everything goes to one channel for one instrument: pressure stays per
note as polyphonic aftertouch, bend and Y follow the newest held note.

### 3. No plugin: one track per channel

Set the controller to send on N channels and its bend range to the instrument's. Make N
tracks with the same instrument, MIDI Input Channel 2, 3, … N+1, Monitor *In* or *Merge*,
all armed. Each records its own channel. A KVR forum post about a LinnStrument on an MPC
Live describes exactly this ("multiple tracks, running redundant plugins, each assigned to
a different MIDI channel … up to a total of eight plugins"), and notes that on a single
channel you "lose polyphonic pitch-bend … but retain poly-aftertouch".

## What the plugins do that is not in the SDK

- Open an ALSA sequencer client with `dlopen("libasound.so.2")`. No system files are
  modified and nothing outside the plugin folder is written. This is an operating-system
  feature, not a plugin-interface feature; the SDK neither documents nor forbids it, and
  Akai's FAQ answer to "can my plugin send MIDI to MPC tracks" is simply "no".
- Answer yes to the host's `MPE` capability question.

Both could stop working in a later MPC OS. If ports stop appearing, the *Port* status
reads "No port" and the plugins fall back to the track.

## Open questions: check these first on a device

Both plugins have a status page showing **Port** (OK / No port), **Port Chs** and
**Track Chs** (the channels on which note-ons have arrived by each route; turn the knob to
refresh the text, turn it up to clear).

1. **Do the ports appear under the official 3.10.1 loader?** Load MPE Synth, open a MIDI
   track's output port list and look for "MPE Synth MIDI In". *Port* should read OK.
   (Verified by the community on 3.9.1 only.)
2. **Does a MIDI track monitor live input through to its output port with channels
   intact when the output channel is Any?** Play the LinnStrument on several channels and
   read *Port Chs*: "2-9" is right, "1" means the live path flattens channels even though
   playback does not. If so, record first and play back, or use design 3 for live playing.
3. **Does answering yes to `MPE` change what the track delivers?** Route the LinnStrument
   straight to the MPE Synth track and read *Track Chs*. If it shows "2-9", the MPC passes
   channels to MPE-declaring plugins and the port detour is unnecessary for instruments
   (set Input to Track).
4. **Does Monitor = In on the instrument tracks pass a port's input while the transport
   is stopped and the track is not selected?** The manual says yes.
5. **Feedback.** Preferences → MIDI / Sync: make sure "MPE Bridge MIDI Out" is not
   *Global*, and never set the MPE track's input to All Ports while its output is the
   bridge.

## Limits

- Port MIDI is quantised to audio blocks (about 3 ms at 128 frames) on each hop: one hop
  for MPE Synth, two for the bridge. Lower the block size in Preferences to tighten it.
- N instrument copies cost N times the CPU and memory. A 32-bit MPC is a small machine;
  four to six voices of a light instrument is a sensible start.
- Stock MPC instruments have a limited bend range (set per instrument). Slides wider than
  the range are clamped by the bridge rather than wrapping.
- Release tails: a note that has ended keeps its last pitch; the bridge reuses the
  channel that has been silent longest, so tails are only cut when all voices are busy.
- 7-bit pressure and CC 74 are stepped. MPE Synth smooths them; ordinary instruments may
  not.
