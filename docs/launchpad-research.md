# Launching MPC clips from another device: research

Collected 2026-11-16, offline (manuals, SDK, community notes, web search). Nothing here was
checked on a device. MPC OS 3.10.1, MPC Live III, Clip Workflow on (Pro Pack).

## The goal

Launch Clip Matrix clips and rows from a separate controller (first idea: an old Novation
Launchpad Mk2) **without going to the Clip Matrix screen and without taking over the MPC's
own pads**, so the pads stay free for playing. Once on the Clip Matrix screen the MPC's
pads already launch clips (Pads: Clip Launch / Row Launch, 8×8 on MPCe pads), so a
solution that needs that screen, or that borrows the pads, gains nothing.

Conclusion so far: **no route found that meets this.** The one lead that might is the
controller-script engine Akai uses for the MPK mini IV (below). Work moved to the web
remote, which needs the same entry point.

## Ruled out

| Route | Why it fails | Evidence |
|---|---|---|
| Plugin SDK | A plugin sees transport (tempo, position, playing/recording) and nothing else of the host. No clip, row, scene or sequence call. MIDI a plugin sends to the host is discarded. | SDK `docs/host-contract.md` ("accepted by the host but then discarded"), `framework/mpc_plugin.h` |
| MIDI Learn | Targets are mixer (volume, pan, mute, solo), drum pad, track and insert-effect parameters only. No clip, row or transport targets. The 3.10 chapter is word for word the 3.4 one. | User Guide v3.10 p. 317–318 |
| Program Change in | Preferences › MIDI/Sync › Program Change switches the current *Sequence* or *Track*. Nothing says it launches clips or rows with Clip Workflow on. Not tried on a device. | User Guide v3.10 p. 60 |
| MMC in | Transport only (play, stop, record). | User Guide v3.10 p. 60, Appendix › MMC |
| Clip Matrix chapter | Launching is described only from the touchscreen and the MPC's own pads. No external control. | User Guide v3.10 p. 485–490 |
| Injecting presses into the MPC's own pad stream | Works technically (community add-ins hook `snd_rawmidi_read`/`write` on the panel's "…Private" port; TheKikGen's IamForce ran a Launchpad Pro MK3 as a Force's clip grid). But it only works in Clip Launch pad mode, so the pads can't play and launch at once. Also needs an `LD_PRELOAD` add-in, which on 3.10 must be re-applied after every boot. | `third_party/mpc-vst-plugins/docs/NOTES.md` (2026-10-09, 2026-10-10), `docs/ADDINS.md`; TheKikGen links below |
| Injecting touchscreen taps | Needs the Clip Matrix screen showing. Each tap needs about a 300 ms hold, so no fast or simultaneous launches, and there is no LED feedback. | `NOTES.md` (tap injection, 2026-10-09) |

## Leads still open

1. **Controller-script engine (MPK mini IV).** MPC 3.10 added "dedicated scripted
   controller integration" for the MPK mini IV and MPK mini Plus II. In MPC 3 Beats the MPK's
   Clip Launch pad mode launches clips 1–16 of the selected track from the MPK's own pads,
   which is the separate-controller model wanted here. Community string analysis of the MPC
   binary found Lua 5.4.7, a JavaScript registrar, `--script` / `--scriptLibs` /
   `--scriptOutputPath` flags and a `ScriptableTrackPool` of 73 track operations. An
   Elektronauts post (could not be read from here) reportedly describes a Lua engine with a
   clip-matrix window up to 16×16 and scene launch. Unknown: where the stock scripts live,
   how the MPC recognises the MPK (port name, USB ID or identity SysEx), and whether user
   scripts are loaded from anywhere writable. Read-only check in Developer mode's shell:
   ```
   find / -xdev \( -iname "*.lua" -o -iname "*mpk*" \) 2>/dev/null | head -50
   ```
2. **`az0x-webserver` object API (TCP 80).** `/api/v1/object-meta`, `object-properties`,
   `object-method`, `subtree`, pairing-gated (`notPaired` → `pending` with `pairPin` →
   `paired`). The MPC application has not been seen registering objects with it. If clips
   or rows ever appear there, a web remote could launch them with no add-in.
   (`MPC_OS_REFERENCE.md`, unverified.)
3. **Other listeners, undecoded:** `DeviceControlServer.bin` on 51000 ("Network MIDI AZ01
   Server"), `az01-script-runner` on UDP 127.0.0.1:8080, MPC on TCP 41401, and SMEX (MPC's
   remote screen/touch/file protocol, magic `2c 9e b4 f2`).

## Sources

- MPC Standalone OS User Guide v3.10:
  https://cdn.inmusicbrands.com/Software/M310PCFIN26OCT/MPC%20Standalone%20OS%20-%20User%20Guide%20-%20v3.10.pdf
- MPC 3.10 and 3.10.1 release notes (see `sources.md`).
- MPK mini IV – MPC 3 Beats Setup Guide:
  https://cdn.inmusicbrands.com/akai/AD41/MPK%20mini%20IV%20-%20MPC%203%20Beats%20Setup%20Guide%20-%20v1.0.pdf
- MPK mini IV – Ableton Live Setup Guide (4×4 movable clip focus):
  https://cdn.inmusicbrands.com/akai/mpk-mini-4/MPK%20mini%20IV%20-%20Ableton%20Live%20Setup%20Guide%20-%20v1.0.pdf
- MPC Plugin SDK at the pinned commit: `docs/host-contract.md`.
- open-mpc/mpc-vst-plugins at the pinned commit: `docs/NOTES.md`, `docs/ADDINS.md`,
  `docs/MPC_OS_REFERENCE.md`.
- TheKikGen: https://github.com/TheKikGen/MPC-LiveXplore/wiki/Iamforce-:-a-MPC-Live-like-a-Force-proof-of-concept ,
  https://github.com/TheKikGen/MPCLiveXplore-libs
- Elektronauts, unread (host not reachable from the research session):
  https://www.elektronauts.com/t/the-all-things-mpc-thread-part-4/240914/9966
