# What the MPC does with MPE, and how to get around it

Findings from the documentation gathered on 2026-11-12 (list and links in
[sources.md](sources.md)). Each claim says where it comes from. "Verified" below always
means *verified by the cited source*, not by this project. The one exception is the section
[Device findings, 2026-11-12](#device-findings-2026-11-12), which records what was measured
on an MPC Live III; it settles the port question and leaves the track-routing questions
open.

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
makes the open questions below checkable directly on the device. `aconnect` and `aseqdump`
are not installed on 3.10.1; use `cat /proc/asound/seq/clients` for ports and connections,
and `arecordmidi -p <port> file.mid` / `aplaymidi -p <port> file.mid` to capture and send
with channels. The *Advanced audio settings* enhancement exposes the block size, which sets
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

## Device findings, 2026-11-12

Device: MPC Live III, MPC 3.10.1.12 (firmware 9a27d579), aarch64, kernel 6.18.44-rt,
developer mode on, reached over SSH. Both plugins were built with `tools/build.sh` and
installed to the internal drive (`/nvme/VST2 Plugins`) with the SDK's `mpc_remote.py`.

**How it was tested, and what that does not cover.** Nobody was at the touchscreen, so
the plugins were never loaded on a track by the MPC itself. Instead the installed arm64
binaries were loaded on the device by a small stand-in VST2 host run over SSH (a separate
process calling `processReplacing` every 128 frames in real time), with test MIDI sent
and captured by `aplaymidi` / `arecordmidi`. That exercises the real plugin code, the
device's `libasound` and kernel, and the MPC application's reaction to the ports. It does
not exercise the MPC's own plugin loader or any MIDI that passes *through an MPC track*.

Measured:

- **The plugins' ports open on the device.** `dlopen("libasound.so.2")` succeeds
  (`/usr/lib/libasound.so.2.0.0`), *Port* reads OK, and the sequencer shows client
  `"MPE Bridge"` with `MIDI In` (write) and `MIDI Out` (read), and client `"MPE Synth"`
  with `MIDI In`. Both plugins answer 1 to `effCanDo("MPE")`.
- **MPC 3.10.1 hot-detects them, in both directions.** Within 4 seconds the application's
  own client `"MPC 3"` had created matching ports and subscribed them:

  ```
  Client 129 : "MPC 3"
    Port  14 : "MPE Bridge MIDI Out" (-we-)   Connected From: 130:1
    Port  15 : "MPE Bridge MIDI In"  (r-e-)   Connecting To: 130:0
  Client 130 : "MPE Bridge"
    Port   0 : "MIDI In"  (-We-)              Connected From: 129:15
    Port   1 : "MIDI Out" (R-e-)              Connecting To: 129:14
  ```

  and likewise `"MPE Synth MIDI In"` → `MPE Synth:0`. The names are the ones the guides
  use. When the plugin closes, the MPC removes its ports again; the MPC application kept
  running throughout (same process id before and after).
- **Channels survive the ports and the plugin code.** A 40-event file (8 notes on channels
  2 to 9, each with pitch bend, channel pressure and CC 74, then 8 note-offs) was played
  into `MPE Bridge:0` and captured from `MPE Bridge:1`:
  - *Thru*: the capture is identical to what was sent, event for event. *Port Chs* read
    "2-9", *Track Chs* "none".
  - *Rotate* (defaults: 8 voices from channel 2, In Bend Auto, Out Bend 12): each note
    stays on its own channel, preceded by a bend / CC 74 / pressure reset, with bends
    scaled 48 → 12 semitones (for example `e5 74 45` became `e5 50 57`) and pressure and
    CC 74 unchanged.
  - *MPE Synth*: the same file into `MPE Synth:0` gave *Port Chs* "2-9" and audio out of
    the plugin (peak about 0.21) while the notes were held, silence after.

**On a track, loaded by the MPC itself (same day, reported from the touchscreen).** MPE
Synth was loaded on a track and played from a LinnStrument:

- The MPC's loader runs the plugin and its port opens: the sequencer showed client
  `"MPE Synth"` with the MPC's own "MPE Synth MIDI In" port subscribed to it.
- **The track delivered note-ons on their own channels.** *Track Chs* read "1-8" and
  *Port Chs* "none", so nothing went through the port, and per-note pitch bend and
  pressure were heard working. This contradicts the 3.9.1 community trace (every note-on
  on channel 1). Not yet separated: whether MPC 3.10.1 changed this for all plugins, or
  whether it happens because the plugin answers yes to `MPE`.
- Not recorded: the exact track routing used (assumed: LinnStrument as the plugin
  track's own input) and the LinnStrument's channel settings.

What this changes: for an MPE *instrument plugin* on 3.10.1 the port detour looks
unnecessary; design 1 can be a single plugin track. It does not help stock MPC
instruments, so the bridge and its ports are still the route for those. The statements
earlier in this document that a plugin on a track gets note-ons on channel 1 describe
3.9.1 and are not true of this setup.

**MPE Bridge on a track (same day, reported from the touchscreen; settings not
recorded).** Tried with Fabric (sampled) and OPx-4 (modelled) as the instruments:

- MPE data reaches the stock instruments through the bridge.
- Bend range gives odd results. Not yet narrowed down: the bridge's In Bend / Out Bend
  against the LinnStrument's range and each instrument's own Pitch Bend Range setting.
- **Polyphonic slides glitch when notes bend in different directions**, on both
  instruments. Not diagnosed. A guess to check first, not a finding: bends are not being
  kept apart per copy, which would happen if the instrument tracks are not each limited
  to a single MIDI input channel, or if the MPC applies pitch bend from the bridge's port
  to more than the track whose channel matches. Capturing "MPE Bridge MIDI Out" with
  `arecordmidi` while reproducing it would show whether the bridge's output is already
  wrong or the fault is downstream.
- **Retrospective Record fills in about ten seconds** of MPE playing. Its buffer was
  described as 4096 long; the dense stream of bend, pressure and CC 74 messages is the
  likely cause. Thinning that stream (in the LinnStrument's settings or in the bridge)
  is untested.
- CPU cost of the per-voice instrument copies and MIDI tracks: not measured.

Also seen on this device:

- No ALSA sequencer client for a LinnStrument or any other USB controller was present, so
  nothing live was played.
- Removable drives are mounted `noexec` (the SDK's remote tool warns that plugins "may
  not load from here"); the internal drive is not. Whether the MPC loads plugins from a
  removable drive anyway was not tested. This matters for the README's "copy to a USB
  drive or SD card" instruction.
- The MPC application also owns a client `"JUCE"` with *Virtual MIDI Input/Output 1–2*
  ports wired to itself. Not investigated.

## Recording and playback findings, 2026-11-14

From the user's report after an evening of recording through MPE Bridge into SurgeXXL and
Vintage Pro, plus the device's log and the saved test project (`.xpj` files are a short
text header followed by gzip-compressed JSON, and can be read off the device).

Reported by the user:

- Played live, MPE works. Recorded straight onto an instrument track, takes come back
  "flattened". Recorded on the bridge's track and played through it, more expression
  survives, but some notes play back very quiet, unpredictably, and not according to the
  velocity shown in the piano roll.
- Now and then the MPC shows its loading display for a moment; audio glitches and some
  notes stay stuck until played again.

Found:

- **The stuck notes line up with the LinnStrument dropping off USB.** The kernel log has
  it failing with `endpoint halt` and disconnecting twice (Nov 13 02:07 and 21:41),
  returning seconds to minutes later. Notes held at that moment never get a note-off.
  That is a USB-level fault (cable, port or power), separate from MPE. Two software gaps
  made it worse and are fixed in 0.2.0 / 0.2.1: the bridge forgot sounding notes without
  releasing them when the MPC suspended and resumed it, and nothing reacted to a device
  disappearing.
- **The MPC stores MPE settings per plugin program**: `sendMPE` (false on every plugin
  track in the project), `mpeXEnabled`, `mpeYEnabled`, `mpeAftertouchEnabled`,
  `mpeChannelMode`, `mpeFirstNoteChannel`, `mpeLastNoteChannel`. Notes also carry
  per-note modifier slots. Akai's documentation says nothing about these that a web
  search could find. Whether switching "send MPE" on (wherever the MPC shows it) makes
  recorded takes play back as MPE into a plugin is the most promising untested lead for
  the flattening.
- **Recorded expression does keep its channel.** The one clip in the saved project holds
  pitch bend (parameter 128), CC 74 and channel pressure (129) as automation events with
  per-event channels, on two channels. It holds exactly 512 events and no notes; whether
  512 is a limit, and where the notes went, is not known.
- **The MPC adds "invented" events at the start of a clip**: pitch bend centre, a CC 74
  value and pressure 0 on a channel.
- **Several tracks listened to "All Ports"**, including a second MPE Bridge on Monitor
  Merge. All Ports includes "MPE Bridge MIDI Out", so those tracks receive the controller
  and the bridge's re-dealt copy of it at once, on different channels. For an MPE
  instrument that mixes one note's pressure into another's. This is a likely contributor
  to notes at the wrong level; it has not been isolated.

**Recorded takes lose expression part-way while notes carry on** (from the autosaved
project, read with `tools/mpc_project.py`):

- A two-bar clip on the bridge's track holds 502 events. From about beat 3 of bar 1 to
  the bar line there is no bend, CC 74 or pressure at all, though four notes start in
  that stretch; expression resumes exactly on the bar line. Read as a loop that started
  recording at bar 2 and wrapped, that is expression stopping once roughly 500 events
  were stored, with notes still being recorded. Those four notes have no pressure data.
- Another clip holds exactly 512 events, all expression and no notes; a third holds 3887
  events with 20 notes. So there appear to be event-count limits near 512 and near 4096
  (the user knows the latter as Retrospective Record's size); which recording method
  gives which is not established. The MPC binary has a `RetrospectiveRecordSize`
  setting; it is not set in the settings file.
- A note with no recorded pressure played through bridge 1.0.0 started from its
  channel's last pressure, usually 0: silent or very quiet in any sound where pressure
  drives level, whatever its velocity. This matches the reported quiet notes.

So the quiet notes are largely explained: the take ran out of room for expression. Two
of the 0.2.0 changes address it directly: Z Start = Velocity gives such notes their
struck level, and Thin makes the room last several times longer. Still not determined:
whether the All Ports mix-up above also plays a part.

Superseded by the above: "Not determined: why specific notes go quiet on playback." The candidates are the mix-up
above, pressure data that the sequencer does not play back for some notes, and the
bridge's old habit of starting each note from whatever pressure its channel last had.
0.2.0 removes the last one and makes the bridge tolerant of the second (Z Start =
Velocity). The decisive test is to capture "MPE Bridge MIDI Out" with `arecordmidi` once
while playing live and once while the take plays back, and compare.

Changes made in response (bridge 0.2.0, synth 0.2.1):

- Z Start (Last / Velocity / Zero, default Velocity): what pressure a note has before any
  arrives for it. With Velocity, a note that gets no pressure on playback is as loud as
  it was struck, and the piano roll's velocity means something again.
- Z Depth, Z Min and Y Depth: scale how far pressure and Y move their destinations, and
  give pressure a floor. This is the bridge's version of the per-target depth controls on
  Vintage Pro's MPE page, which could not be examined (no public documentation found;
  the SDK gives no access to Akai's own plugins).
- Thin: skip small changes of bend, Y and Z, to send and record less data.
- A finished note's pressure is no longer inherited by the next note on its channel.
- Notes are released, not forgotten, when the MPC resets the plugin; and both plugins
  release everything when a hardware MIDI device disappears.

None of this has run on the device yet.

## Open questions: check these first on a device

Status after the findings above: question 1 is answered except for seeing the name in a
track's port list; question 3 is answered yes for live playing; questions 2, 4 and 5 are
still open, as are the follow-ups under 3. The bridge now works on a track in the sense
that data arrives, with the faults listed above still to diagnose.

Both plugins have a status page showing **Port** (OK / No port), **Port Chs** and
**Track Chs** (the channels on which note-ons have arrived by each route; turn the knob to
refresh the text, turn it up to clear).

1. **Do the ports appear under the official 3.10.1 loader?** Load MPE Synth, open a MIDI
   track's output port list and look for "MPE Synth MIDI In". *Port* should read OK.
   **Partly answered (2026-11-12):** on 3.10.1.12 the installed plugin opens its port and
   the MPC creates and subscribes a matching "MPE Synth MIDI In" port (same for both
   bridge ports) when the plugin runs in a stand-in host. Still to confirm: that the MPC's
   loader instantiates the plugin without restrictions that block the port, and that the
   name shows in the track's port list.
2. **Does a MIDI track monitor live input through to its output port with channels
   intact when the output channel is Any?** Play the LinnStrument on several channels and
   read *Port Chs*: "2-9" is right, "1" means the live path flattens channels even though
   playback does not. If so, record first and play back, or use design 3 for live playing.
3. **Does answering yes to `MPE` change what the track delivers?** Route the LinnStrument
   straight to the MPE Synth track and read *Track Chs*. If it shows "2-9", the MPC passes
   channels to MPE-declaring plugins and the port detour is unnecessary for instruments
   (set Input to Track).
   **Answered (2026-11-12): yes, live.** *Track Chs* read "1-8" with the LinnStrument
   played into the MPE Synth track. Follow-ups: does a *recorded* take play back into
   the plugin with channels intact; and does a plugin that does not answer `MPE` also
   get them (build MPE Synth with the SDK's plain entry macro and compare).
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
