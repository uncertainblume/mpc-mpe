# Setting it up: LinnStrument + MPC

Three recipes, from "full MPE, one instrument" to "no plugins at all". Menu names are from
the MPC 3 user guide; they may sit in slightly different places on your model. None of
this has been run on a device yet: see "Open questions" in [research.md](research.md) and
check the status page of each plugin as you go.

## Once: the MPC

1. MPC OS 3.10.1 or later. Preferences → Enhancements (Beta): turn on **Enable
   Enhancements** and **External Plugin Support**.
2. Copy `MPE Tools - VST - MPE Synth` and/or `MPE Tools - VST - MPE Bridge` into a folder
   named `VST2 Plugins` at the top of a USB drive or SD card. They appear in the plugin
   browser under **External Plugins**.
3. Preferences → MIDI / Sync → Input Ports, the LinnStrument's row: **Track** on,
   **Global** off. (Global would also send it to whichever track is selected, which
   doubles notes.)
4. Preferences: **Global Pitch Bend** off, so each instrument keeps its own bend range.

## Once: the LinnStrument

Per-Split Settings for the split you play:

| Setting | Value | Why |
|---|---|---|
| MIDI Mode | **ChPerNote** | This is MPE. |
| Main channel | 1 | The MPE master channel. |
| Per-Note channels | 2–16 for MPE Synth; exactly as many as you have instrument copies for recipe 3 | One note per channel. |
| Bend Range | **48** for recipes 1 and 2; the instrument's own range for recipe 3 | 48 is the MPE standard and gives the smoothest slides. |
| Pitch/X | On | |
| Timbre/Y | On, **CC 74** | |
| Loudness/Z | On, **Channel Pressure** | Poly Pressure also works; the plugins accept both. |

Holding the *ChPerNote* button switches the split to the firmware's MPE setup in one go
(left split: Main 1 and seven Per-Note channels, 2–8, with standard MPE expression); add
more Per-Note channels afterwards if you want more than seven voices, and check the Bend
Range reads 48. The same settings can be sent from a computer as NRPNs; see
[linnstrument.md](linnstrument.md).

## Recipe 1: MPE Synth (full MPE, one track)

1. Add a **plugin track** and load **MPE Synth**. On its MPE page, *Port* should read OK.
2. Add a **MIDI track**, call it "MPE":
   - MIDI Input Port: the LinnStrument. MIDI Input Channel: **All**.
   - MIDI Output Port: **MPE Synth MIDI In**. Output Channel: **Any**.
   - Monitor: **Auto** (or Merge). Record-arm it.
3. Play. Each finger bends, brightens and swells on its own.
4. Record on the "MPE" track. Every event is stored with its channel; the list editor
   shows it. Playback goes back out to the synth exactly as played.

On the synth: *Bend Range* Auto follows what the LinnStrument announces and otherwise
assumes 48. *Y>Cutoff* and *Z>Cutoff* set how far the filter moves; *Z>Level* sets how
much pressure, rather than velocity, controls loudness.

If *Port Chs* shows only "1" while you play live but the recorded take plays back
correctly, the MPC is flattening channels on live monitoring: use recipe 3 for live
playing, or record first.

## Recipe 2: any MPC instrument, through MPE Bridge

Example: four voices of a keygroup or plugin instrument.

1. Add a **plugin track**, load **MPE Bridge**. Leave it on the first program
   ("8 Voices +-12") and set **Voices** to 4, **First Ch** 2.
2. Add the "MPE" **MIDI track** as in recipe 1, but with MIDI Output Port **MPE Bridge
   MIDI In**, Output Channel **Any**.
3. Add a track with the instrument you want. Set:
   - MIDI Input Port: **MPE Bridge MIDI Out**. MIDI Input Channel: **2**.
   - Monitor: **In**.
   - The instrument's pitch bend range to the bridge's **Out Bend** (12 by default).
   - In the instrument, route aftertouch (and CC 74 or mod wheel) to what you want
     pressure and Y to do, for example filter cutoff.
4. Duplicate that track three times. Change only the MIDI Input Channel: **3**, **4**,
   **5**.
5. Play. Record on the "MPE" track only; the instrument tracks stay empty and just listen.

Bridge settings:

| Control | What it does |
|---|---|
| Mode | *Thru*: pass everything unchanged. *Rotate*: one note per output channel. *Single*: everything on one channel. |
| Voices / First Ch | How many output channels, starting where. Match your instrument tracks. |
| Master Ch | The controller's master channel: 1, 16, or None (LinnStrument with Main switched off). |
| In Bend | The controller's bend range. *Auto* follows its RPN 0 announcement, else 48. |
| Out Bend | The instruments' bend range. Slides wider than this are clamped. |
| Y Dest / Z Dest | What CC 74 and pressure become: Off, channel pressure, poly aftertouch, CC 1, CC 2, CC 11, CC 74. |
| Input | *Port* (default). *Track* / *Both* also listen to the bridge's own track; only useful if the MPC turns out to deliver channels there. |
| Panic | Releases everything and sends All Notes Off on all channels. |

Variations:

- **Instrument only understands mod wheel**: Y Dest = CC1 Mod.
- **Instrument with a ±2 bend**: Out Bend 2. Slides beyond a whole tone stop at the limit.
- **One copy is all the CPU allows**: Mode *Single*, one instrument track listening on
  First Ch. You keep per-note pressure if the instrument responds to poly aftertouch;
  bend and Y follow the last note pressed.
- **Thru**: lets several tracks each pick one channel out of a recorded MPE track without
  changing anything, for example to send different channels to different instruments.

## Recipe 3: no plugins

1. LinnStrument: Per-Note channels 2–5 only (for four voices); Bend Range = the
   instrument's range; Y and Z set to messages the instrument understands.
2. Four tracks with the same instrument, MIDI Input Port = LinnStrument, MIDI Input
   Channel 2, 3, 4, 5. Monitor **Merge**.
3. Preferences → Rec Arm Behavior **Multi**; arm all four. Each track records its own
   channel.

This needs nothing from 3.10.1 and is the fallback if the ports do not appear.

## If something is wrong

| Symptom | Look at |
|---|---|
| "MPE Synth MIDI In" is not in the output port list | *Port* on the status page. "No port": the MPC did not let the plugin open a MIDI port; use recipe 3. "OK" but not listed: Preferences → MIDI / Sync → Output Ports, turn **Track** on for it. |
| All notes bend together | *Port Chs* shows "1": the channel was flattened before the port. Check Input Channel **All** and Output Channel **Any** on the MPE track. |
| Every note plays twice | The LinnStrument port is still *Global*, or the instrument track also listens to the LinnStrument directly. |
| Slides are out of tune | Bend ranges disagree: LinnStrument ↔ *In Bend*, and *Out Bend* ↔ the instrument. |
| Notes hang after changing settings | *Panic* on the bridge. |
| Runaway notes | A feedback loop: something is sending "MPE Bridge MIDI Out" back into "MPE Bridge MIDI In". Set the MPE track's input to the LinnStrument, never All Ports. |
