# Where the MPE tools stand, and what to pick up next

Written 2026-11-14, last updated that evening, for the next working session. Evidence and detail are in
[research.md](research.md); this is the short list.

## On the MPC now (Live III, 3.10.1.12, internal drive)

- MPE Bridge 0.2.2 and MPE Synth 0.2.2, in `/nvme/VST2 Plugins` (installed 2026-11-14;
  0.2.2 only relabels the choice controls, and the user has not yet looked at it). Build with
  `tools/build.sh`, test with `tools/run_tests.sh`, install with
  `python3 third_party/mpc-plugin-builder-sdk/tools/mpc_remote.py 192.168.1.20 --drive 1 update plugins/<Name> --yes`.
- Template project `Claude_MPEClean` in `/nvme/MPC Documents/Projects`, made from the
  user's `Mpetestproject` and since modified by the user on the device.
- SurgeXXL comes from its own repository and session, not this one.

Everything up to this file is on GitHub (`uncertainblume/mpc-mpe`), on `main` and on the
branch `claude/stoic-volta-193u51`, which point at the same commit. The next revision
counts as a new version.

## Start here next session

This file is written so that a fresh session (any model) can continue without the earlier
conversation. Read it, then [research.md](research.md) for evidence when a claim matters.

1. Run `git status` and `git log origin/main..HEAD`. `main` and the branch
   `claude/stoic-volta-193u51` were pushed in step on 2026-11-14. The next change to a
   plugin is a new version: bump `i.version` in the `.cpp` and `version` in `plugin.json`.
2. Look at what the user has now: `python3 tools/mpc_project.py show 192.168.1.20
   "Claude_MPEClean"` (add `_[AutoSave]/Claude_MPEClean` to the name for the newer
   autosave). The MPC is at 192.168.1.20, SSH as root, no password, developer mode.
3. Ask the user what they want next. Their stated direction: keep testing the routing,
   which they still find confusing; CPU headroom is the coming limit.

## The user's working setup (autosave of 2026-11-14 19:06)

```
LinnStrument
   |-- MIDI track "Surge midi send" (monitor Auto) -- out: MPE Bridge MIDI In
   |       bridge 1 = track "Plugin 004", program Thru, edited
   |       "MPE Bridge MIDI Out" --> "Plugin 001"   SurgeXXL (Overdriven Guitar), all channels, Merge
   |                             --> "Plugin 001-1" SurgeXXL (Dark Cello),        all channels, Merge
   |
   |-- MIDI track "MIDI 001" (monitor Merge) -- out: MPE Bridge 2 MIDI In
           bridge 2 = track "Plugin 005", program "8 Voices +-12", edited (Rotate)
           "MPE Bridge 2 MIDI Out" --> "Plugin 003", "-1", "-2", "-3": Fabric XL "Jazz Club",
                                       input channels 2, 3, 4, 5, monitor In
```

Both bridge tracks also have the LinnStrument as their own track input (monitor In).

How to read it, for explaining to the user:

- A **MIDI track** is where a take is recorded and what feeds a bridge: its output is the
  bridge's "MIDI In" port, channel Any.
- A **bridge** is only a converter between its In port and its Out port. With its Input
  setting on Port (the default) it ignores the track it sits on, so that track's own
  input and monitor setting do nothing.
- An **instrument track** listens to a bridge's "MIDI Out" port: all channels for an MPE
  instrument (Thru), one channel per copy for a stock instrument (Rotate).
- Monitor **Auto** on a MIDI track passes live playing only while that track is selected
  or armed; **Merge** always passes it. That is the switch for "which instrument am I
  playing now".

Things in this setup worth raising with the user (none verified on the device):

- Bridge 2 was started from "8 Voices +-12" but only four Fabric XL copies exist, on
  channels 2 to 5. If its Voices is still 8, every second note goes to a channel nothing
  listens to. It should be Voices 4, First Ch 2. The setting is inside the plugin's saved
  state and cannot be read from the project file; ask the user or have them look.
- If either bridge has Input set to Track or Both, it receives the LinnStrument twice
  (through its own track and through the MIDI track's port). With Port it does not.
  Simplest: Input = Port, and the bridge tracks' monitor Off.
- Both SurgeXXL tracks listen to the same bridge on all channels with Merge, so they
  always sound together. That is a layer, and it doubles Surge's CPU.
- "MIDI 001" is on Merge, so the Fabric XL copies sound whenever the LinnStrument is
  played, whichever track is selected. Auto would make it selectable.
- The drum and audio tracks still listen to All Ports. Harmless unless one is selected
  while playing.
- SurgeXXL names its own MIDI client by a counter that rises on every load ("SurgeXXL 2",
  "SurgeXXL 3" in one session), so a route to SurgeXXL's own port by name will not survive
  a reload. The bridges' names are stable while they load in the same order. This belongs
  to the SurgeXXL repository.

## Why the bridge is a plugin track, and how to need fewer tracks

Asked by the user on 2026-11-14; keep for when the routing guide is rewritten.

- The MPC cannot put a plugin on a MIDI track. Its plugin system has instruments (which
  get MIDI, on a plugin track) and effects (which get none). Anything that processes MIDI
  has to be an instrument plugin on a plugin track. That is a platform limit.
- The separate MIDI track feeding the bridge's port is a leftover of MPC OS 3.9.1, where a
  plugin's track delivered every note on channel 1. On 3.10.1 the track delivers each
  note's channel, so the bridge can take its input from its own track: track input =
  LinnStrument, all channels, monitor Merge; bridge Input = Track; record takes on the
  bridge's track; instruments listen to the bridge's Out port as before. One track fewer
  per bridge. Believed to be what the user did when recording "in the bridge track"
  worked, but the Input setting cannot be read from the project file: unconfirmed.
- Either way the recorded take is the controller's raw stream, before the bridge, so Thin
  does not reduce what is stored. And the bridge's track must not listen to All Ports.
- Rejected: running the bridge as a background program on the MPC instead of a plugin.
  The ports would appear, but it would have no screen or Q-Links, would not be saved with
  the project, and would mean installing into the system.
- Possible later feature, not started: one bridge instance serving several instruments
  in different modes at once (several output ports or channel ranges), so that a project
  does not need one bridge per mode.

The user plans to live with the current setup for a few days before deciding whether more
work is worth it. Do not assume further development is wanted; ask.

## Results since the last notes (user's report, 2026-11-14 evening)

- **Rotate works**: slides in different directions are clean with one Fabric XL copy per
  channel. The earlier glitch report came from single copies on all channels. Closed.
- **`Claude_MPEClean` opens** and the user has built on it (above). Closed.
- "The workarounds seem to be working quite well" (the user's words; no detail).
- **CPU: over 50% with two SurgeXXL instances** and very heavy LinnStrument polyphony,
  nothing else playing. No dropouts were reported and the system log shows no audio
  errors, USB disconnects or crashes between 14:28 and 19:10 UTC.

## Open concerns, most useful first

1. **CPU headroom.** Not measured beyond the figure above. Cheap levers, in order: do not
   layer two Surges unless wanted; Surge's own Poly Limit (PATCH page); fewer voices in
   Rotate; a larger audio block (Preferences). A real measurement would run each
   instrument in the stand-in host over SSH (see research.md, "How it was tested") or
   read the MPC's CPU meter per configuration. Surge's cost belongs to the SurgeXXL
   repository.
1a. **The brief loading display with an audio glitch is unexplained. An earlier claim in
   these notes and in research.md, that it was the LinnStrument dropping off USB, is
   withdrawn.** The user's correction (2026-11-14): unplugging the LinnStrument does not
   show that display; the display looks like the one shown when creating a project; it
   has happened since with no possible change of connection; and the last couple of
   times no note hung. The two USB dropouts in the log were real, but matching them to
   the display was an assumption.
   - The user's hypothesis is the autosave. Supporting fact: `MPC.settings` has
     `AutoSaveEnabled = 1` and `AutoSaveTimeout = 300` (seconds). Nothing in the system
     log marks an autosave, so the log alone cannot confirm it.
   - A mechanism worth testing, not established: saving asks every plugin for its state
     while audio runs. For the two MPE Tools plugins that is a few hundred bytes. For
     SurgeXXL it is 50 to 210 KB of text per instance (its README), which could stall
     audio or MIDI for a moment.
   - **First capture, 2026-11-14 20:28 UTC, within about a minute of the user seeing
     it:** the autosave file `Claude_MPEClean_[AutoSave]/Claude_MPEClean.xpj` had been
     written at 20:27:18, 45 seconds earlier. The system log had no entry of any kind in
     the previous 12 minutes; the last USB event was an hour before; the LinnStrument's
     MIDI client was present; the MPC app had not restarted; all four plugin MIDI clients
     were present. One observation, but it fits autosave and rules out USB for this
     occurrence. Loaded at the time: two SurgeXXL (one restores 65,795 bytes of state)
     and two bridges. Raw capture: `local/blip/` (not in git).
   - **Second capture, 2026-11-14 20:37:42 UTC**, after another occurrence: the autosave
     file had been written at 20:37:19, 23 seconds earlier. The previous write was at
     20:27:18, so writes are 601 s apart, consistent with a fixed 300 s timer (a write
     near 20:32 was not caught). Again nothing in the system log for 10 minutes, no MIDI
     client lost, no app restart, no USB event since 19:17. Two for two, but the exact
     moment the user saw the glitch is not known, so this is consistent with autosave
     rather than proof. The decisive test is still autosave off for a session.
   - How to find out: have the user note the clock time when it happens, then compare
     with the modification time of `<project>_[AutoSave]/` on the device; switch autosave
     off in Preferences for a session and see whether it stops; see whether it happens
     in a project with no SurgeXXL loaded.
   - What to watch when it happens: whether MIDI input routing changes or drops, whether
     notes hang, and whether the bridges are suspended and resumed (0.2.0 and later
     release their notes in that case).
   - The goal is that it does not disturb playing. If autosave is the cause, the options
     are a longer interval, saving only while stopped (if the MPC offers it), or making
     the plugin's state cheaper to produce.
2. **Routing is still confusing to the user.** The explanation above is the one that
   landed in conversation; `docs/routing-guide.md` predates everything learned on the
   device and should be rewritten around it, with the template as the worked example.
3. **Ordinary recording on an instrument track fed by a bridge's Out port records
   nothing; Retrospective Record there works.** Not diagnosed. The user now records on
   MIDI tracks that feed the bridges, which sidesteps it and is the cleaner design; that
   records the controller's raw stream, so Thin does not reduce what is stored.
4. **The recorder's event limits** (about 512 and about 4,096 events) are inferred from
   three clips, not measured. The MPC has a `RetrospectiveRecordSize` setting that is not
   set in `/data/Settings/MPC/MPC.settings`; untried.
5. **"Send MPE"**: every plugin track stores `sendMPE: false`; the MPC has an MPE settings
   dialog the user has not found. `tools/mpc_project.py copy --send-mpe` can switch it on
   in a copy of a project; never run. What the switch does is unknown.
6. **Loop points.** Repeated note-ons and stale bend / Y are handled in the bridge
   (tests pass); how the MPC treats a note held across the loop point has not been
   observed, and the values it inserts at a clip's start still reach a held note.
7. **Not yet seen by me on the device:** the bridge's SHAPE page; release of notes when a
   MIDI device disappears; release on plugin reset. The user offered screenshots of SHAPE
   and of Vintage Pro's MPE page (per-target depth controls, which Z Depth / Y Depth
   only approximate).
8. **The LinnStrument dropped off USB twice** on Nov 13 (`endpoint halt`). Hardware or
   cable; it shares the onboard hub that logged an error in the separate boot-hang
   investigation (`local/boot-hang/`, not in git). This is NOT the explanation for the
   brief "loading" display: see concern 1a.
9. **Small things.** MPE Synth: Master Ch should be None for a LinnStrument that plays
   notes on channel 1; no XY pad or envelope display. The README's "install from a USB
   drive or SD card" is untested (removable drives are mounted `noexec`). The template
   kept the old audio recordings in its data folder (about 28 MB).

## Working rules that proved necessary

- Never copy a new build over a plugin that is loaded in the open project; check
  `/proc/<MPC pid>/maps` first, or wait for the user to close the project.
- A saved project is a gzip file of five header lines plus JSON. In an event list,
  `length` is a maximum (int64 max), not a count: leave it alone.
- The saved `.xpj` is only as new as the user's last save; the `_[AutoSave]` copy is
  often newer.
- Restarting the MPC app discards the unsaved project. Ask first.

## Rapid-play check of Rotate, 2026-11-15

Saved template (`MPETemplate`, 02:52 UTC): bridge 2 is Rotate, 4 voices, first channel 2, Input Port; instrument copies listen on channels 2 to 5. That matches. A capture with `tools/capture_midi.py` (LinnStrument port vs "MPE Bridge 2" MIDI Out) while the user played fast: 82 of 82 controller notes appeared at the bridge output within 50 ms, spread over channels 2 to 5. So the MPC hop and the bridge lost nothing in that run; if keys still do not sound, look downstream (instrument copies, voice stealing beyond 4 notes, CPU). Not measured: the bridge's input port cannot be recorded (write-only), and CPU was sampled once mid-run at under 5% MPC, with the idle-looking load, so it says little about the heavy two-SurgeXXL case.

### Findings after the rapid-play report, 2026-11-15 evening (supersedes the CPU remark above)

- The earlier single-shot `top` CPU reading was unreliable; whole-system idle says little about one audio thread. `tools/mpc_threads.sh` reads per-thread CPU from `/proc`: with 4 Fabric XL and SurgeXXL loaded and the user playing, the busiest MPC thread peaked at 9 to 11% of a core. Not a CPU problem.
- `tools/watch_midi.py` (background watcher): across many windows every LinnStrument note reached "MPE Bridge 2 MIDI Out", evenly over channels 2 to 5, while the user heard notes missing. The bridge sends bends (403), Y and pressure on channels 2 to 5 as well. ALSA counters for the MPC's input queue: 0 allocation failures, peak 10 of 200.
- Playing the LinnStrument straight into one Fabric track (no bridge) is clean (no MPE).
- An app restart and project reload changed nothing.
- Turning monitoring off on the Surge send track and the bridge-1 plugin track made all fast notes sound again, but Fabric bends then played "super glitchy". The user also hears cross-talk between slides (a held note moves when another slides) and says Fabric's legato setting changes the symptom.
- Reading: loss and cross-talk happen between the MPC's port input and the Fabric tracks, not in the bridge. Suspected: the MPC's per-track channel filter does not isolate bends and pressure (cross-talk), and Fabric legato can swallow overlapping notes.
- Untested ideas: `MPETemplate_SendMPE` (copy of the template with Send MPE on for the Fabric tracks); a bridge option giving each Rotate voice its own ALSA output port so the MPC routes by port, not channel.

### Bridge 0.3.0 (per-voice output ports), 2026-11-15

- New setting Out Ports (Single / Per Voice; Rotate only). Per Voice makes one ALSA port per output channel, named "Ch 2 Out", "Ch 3 Out", and so on (the MPC lists them as "MPE Bridge 2 Ch 3 Out"), so each Fabric copy routes by port, not by the MPC's channel filter. Aim: stop bend cross-talk between copies. Unit-tested (channel mapping) and built; checked on a device only after the push recorded below.
- `tools/mpc_project.py ports <addr> "<project>" "<new>"` makes a copy with Rotate bridges set to Per Voice and Fabric-style tracks pointed at their own port (channel Any). Written for the MPC: `MPETemplate_Ports` and `Claude_MPEClean_Ports` (originals untouched).
- If it does not help: the next suspects are Fabric XL's legato/voice settings and the MPC's own handling; see the findings above.
