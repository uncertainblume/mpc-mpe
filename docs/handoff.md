# Where the MPE tools stand, and what to pick up next

Written 2026-11-14 for the next working session. Evidence and detail are in
[research.md](research.md); this is the short list.

## On the MPC now (Live III, 3.10.1.12, internal drive)

- MPE Bridge 0.2.1 and MPE Synth 0.2.1, in `/nvme/VST2 Plugins`.
- Template project `Claude_MPEClean` in `/nvme/MPC Documents/Projects`, made from the
  user's `Mpetestproject` by `tools/mpc_project.py`-style editing.
- SurgeXXL comes from its own repository and session, not this one.

Local commits run to `46fc828` plus this file. Nothing has been pushed.

## Start here next session

1. The user wants the work put into git first. Everything is already committed on the
   local branch `claude/stoic-volta-193u51`; what is missing is the push to GitHub.
   Confirm with the user, then push.
2. Ask for the result of the user's own test of concern 8 (Rotate with one instrument
   copy per channel). The earlier slide glitches came from single copies listening on
   all channels, so they were not a test of Rotate.
3. Then the remaining concerns below.

## Open concerns, most useful first

1. **`Claude_MPEClean` has never been opened.** First check: it loads; Fabric XL and Hype
   show "MPE Bridge 2 MIDI Out" as their input (that route was set by name only, with no
   saved example to copy); recording works in it. Set the second bridge's mode by hand
   (Single for one stock instrument): plugin settings inside a project could not be
   edited.
2. **Ordinary recording on an instrument track fed by "MPE Bridge MIDI Out" records
   nothing; Retrospective Record on the same track works.** Not diagnosed. Recording on
   the bridge's own track works, but stores the controller's raw stream, so Thin does not
   reduce what is recorded there.
3. **The recorder's event limits** (about 512 and about 4,096 events) are inferred from
   three clips, not measured. Which recording method has which limit is unknown. The MPC
   has a `RetrospectiveRecordSize` setting that is not set in `MPC.settings`; untried.
4. **"Send MPE"**: every plugin track stores `sendMPE: false`, and the MPC has an MPE
   settings dialog the user has not found. `tools/mpc_project.py copy --send-mpe` can
   switch it on in a copy of a project; that command has never been run. What the switch
   does is unknown (it may only concern the MPCe pads).
5. **Loop points.** The bridge now ends a note that is re-sent while held, and Note Start
   = Clean ignores stale bend and Y. Untested on the device, and how the MPC treats a
   note held across the loop point (cut, or re-sent) has not been observed. The values
   the MPC inserts at a clip's start (bend centre, a CC 74 value, pressure 0 on one
   channel) still reach a held note; Z Min only keeps that from silencing it.
6. **Never seen on the device:** the bridge's SHAPE page, the release of notes when a
   MIDI device disappears, and release on plugin reset. The user will send a screenshot of
   SHAPE and of Vintage Pro's MPE page, whose per-target depth controls the bridge's
   Z Depth / Y Depth only approximate.
7. **The LinnStrument drops off USB** (`endpoint halt`, twice in two days). A hardware or
   cable matter, and the cause of the earlier stuck notes. Worth trying another cable or
   port. It shares the onboard hub that logged an error in the separate boot-hang
   investigation (`local/boot-hang/`).
8. **Stock instruments still glitch on slides in different directions** through Rotate
   (first report, 2026-11-12). Never diagnosed. Capture "MPE Bridge MIDI Out" with
   `arecordmidi` while reproducing it to see which side is at fault.
9. **Small things.** Bridges saved with program 0 selected may show the new name "Thru
   Thin 2" (their settings are saved separately and should be unchanged; unverified).
   MPE Synth: set Master Ch to None for a LinnStrument that plays notes on channel 1; an
   XY pad and envelope display are not done. The README's instruction to install from a
   USB drive or SD card is untested (removable drives are mounted `noexec`).

## Working rules that proved necessary

- Never copy a new build over a plugin that is loaded in the open project; check
  `/proc/<MPC pid>/maps` first, or wait for the user to close the project.
- A saved project is a gzip file of five header lines plus JSON. In an event list,
  `length` is a maximum (int64 max), not a count: leave it alone.
- The saved `.xpj` is only as new as the user's last save; the `_[AutoSave]` copy is
  often newer.
- Restarting the MPC app discards the unsaved project. Ask first.
