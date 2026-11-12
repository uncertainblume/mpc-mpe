# mpc-mpe

Full MPE on an Akai MPC, using the plugin support that arrived in MPC OS 3.10.1.

**Status: written and desk-tested, not yet run on an MPC.** The routing logic has passing
tests. The two plugins compile cleanly against Akai's SDK headers but have not been built
for ARM or loaded on a device from this repository yet. Start with the checklist at the
end of [docs/research.md](docs/research.md).

## The problem

The MPC sequencer records and plays MIDI channels per event (since 3.6), so a track can
hold an MPE performance. But a plugin sitting on a track receives its note-ons on channel
1 whatever channel they were played on, and anything a plugin sends back as MIDI is
discarded. MPE depends on knowing each note's channel, so neither an MPE instrument nor an
MPE splitter works the obvious way.

## The way round it

A plugin can open its own MIDI ports at operating-system level. The MPC lists them beside
the hardware ports, in both directions, and they keep their channels. Two plugins are
built on that:

| Plugin | What it is | Use it for |
|---|---|---|
| **MPE Synth** | A 12-voice synth with per-note pitch bend, timbre and pressure, listening on its own port "MPE Synth MIDI In" | Full MPE from one recorded track |
| **MPE Bridge** | A silent router with "MPE Bridge MIDI In" and "MPE Bridge MIDI Out" | Playing **any** MPC instrument with MPE: it deals notes onto one channel per copy of the instrument and converts bend range, Y and Z on the way |

```
LinnStrument ─► MIDI track (in: LinnStrument, ch All · out: MPE Bridge MIDI In, ch Any)
                      │
                 MPE Bridge  ── MPE Bridge MIDI Out ──► track, in ch 2 ─ instrument copy 1
                                                    ├─► track, in ch 3 ─ instrument copy 2
                                                    └─► track, in ch 4 ─ instrument copy 3 …
```

There is also a recipe that needs no plugins, only one track per channel.

## Read

- [docs/routing-guide.md](docs/routing-guide.md): step-by-step setup for the LinnStrument
  and the MPC, three recipes, troubleshooting.
- [docs/research.md](docs/research.md): what the MPC does with MIDI and plugins, with the
  evidence for each claim, the designs, limits, and what to verify on a device.
- [docs/linnstrument.md](docs/linnstrument.md): LinnStrument MPE behaviour and the NRPNs
  that configure it, from the firmware source.
- [docs/sources.md](docs/sources.md): every document gathered, with links and pinned
  versions.

## Build

Needs Python 3.9+ and git. The first build downloads Akai's SDK and, through it, the Zig
compiler.

```
tools/build.sh
```

Each plugin's folder appears under `plugins/<Name>/build/`, named
`MPE Tools - VST - MPE Synth` and `MPE Tools - VST - MPE Bridge`. Copy those folders into
`VST2 Plugins` at the top of a USB drive or SD card. On the MPC (OS 3.10.1 or later):
Preferences → Enhancements (Beta) → **Enable Enhancements** and **External Plugin
Support**.

The SDK's build also runs each plugin through its host simulator. Treat any FAIL there as
a reason not to put the plugin on a device: plugins run inside the MPC application with
no crash protection.

```
tools/run_tests.sh      # routing logic tests; needs only a C++ compiler
tools/fetch_docs.sh     # download the reference documentation into third_party/
```

## Layout

```
shared/mpe/alsa_seq.h     a plugin's own MIDI ports (libasound loaded at run time)
shared/mpe/mpe_zone.h     MPE receive state: per-channel bend / pressure / timbre, RPN 0 and 6
shared/mpe/mpe_router.h   Thru / Rotate / Single routing and conversion (pure logic)
shared/mpe/mpe_entry.h    plugin entry that answers the host's "MPE" capability question
plugins/MPEBridge/        the router plugin
plugins/MPESynth/         the MPE synth
tests/test_router.cpp     tests with LinnStrument-shaped input
tools/                    build, test and fetch scripts
```

Inside each plugin, `src/mpe` is a symbolic link to `shared/mpe`. On Windows, clone with
symlinks enabled (`git clone -c core.symlinks=true`) or copy the folder in.

## Good to know

- The manufacturer name "MPE Tools" and the plugin ids `MpBr` / `MpSy` are part of how the
  MPC finds a plugin in saved projects. Change them before first use if you want your own;
  not after.
- The port technique is an operating-system feature that the community established on MPC
  OS 3.9.1. Akai's SDK does not document it. It modifies nothing on the MPC, but a later
  OS could stop listing such ports; the plugins then show "No port".
- MIDI through a port is timed to the audio block: about 3 ms at the default 128 frames.

MIT licensed. Not affiliated with Akai Professional, inMusic or Roger Linn Design.
