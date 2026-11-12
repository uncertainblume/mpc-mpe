# Documentation gathered

Collected 2026-11-12. `tools/fetch_docs.sh` downloads everything marked *fetch* into
`third_party/` (not committed: the Akai PDFs are Akai's, and the repositories are large).

## Akai MPC

| What | Where | Notes |
|---|---|---|
| **MPC Plugin SDK** (official, MIT) | https://github.com/inmusic-software/mpc-plugin-builder-sdk at `febd2ca5bd9eb16c4a412837196e145566bc981b` (2026-11-07) | *fetch*. The whole SDK: framework headers, build tools, host simulator, examples, agent skills. |
| ↳ host contract | `docs/host-contract.md` | What the MPC host does: audio format, threads, load sequence, **MIDI**, parameters, state, capability questions. The key document. |
| ↳ overview, folder format, building, install/test, UI guide + reference, DSP cookbook, presets, troubleshooting, FAQ/legal | `docs/*.md` | |
| ↳ framework | `framework/mpc_plugin.h`, `framework/mpc_vst2_abi.h`, `framework/dsp/*.h` | |
| MPC Standalone OS User Guide v3.4 | https://cdn.inmusicbrands.com/akai/MPC3-NI/MPC%20Standalone%20OS%20-%20User%20Guide%20-%20v3.4.pdf | *fetch*. Newest full guide found. Track MIDI routing, monitor modes, preferences. |
| MPC 3.6 release notes | https://cdn.inmusicbrands.com/Software/36/MPC%203.6%20Firmware%20Update.pdf | *fetch*. Per-event MIDI channel recording, output channel "Any", MPCe pads. |
| MPC 3.7 release notes | https://cdn.inmusicbrands.com/Software/37/MPC%203.7%20Update%20Release%20Notes.pdf | *fetch*. Nothing MPE-related. |
| MPC 3 FAQ | https://support.akaipro.com/en/support/solutions/articles/69000857771-mpc3-faq | Links the guide above. |
| MPC 3.10.1 release notes | https://cdn.inmusicbrands.com/Software/MPC/MOPN4PCOPEN/MPC%203.10.1%20-%20Release%20Notes.pdf | *fetch*. Enhancements: Plugin SDK, Developer panel (shell, SSH server, external plugin management, `autorun.sh` script runner), advanced audio settings (block size). Nothing about MIDI or MPE. |
| MPC 3.10 release notes | https://cdn.inmusicbrands.com/Software/M310PCFIN26OCT/MPC%203.10%20Release%20Notes.pdf | *fetch*. MIDI fixes: notes on channels other than 1 no longer hang on stop; Note Repeat respects the input channel; input channel kept when changing track type. |
| MPC 3.9.1, 3.9, 3.8 release notes | https://cdn.inmusicbrands.com/Software/MPC/MVSU391APC/MPC%203.9.1%20-%20Release%20Notes.pdf , https://cdn.inmusicbrands.com/Software/15JM26PSBC/MPC%203.9%20Release%20Notes.pdf , https://cdn.inmusicbrands.com/Software/AM20P3C8FWUPGZL12/MPC%203.8%20Release%20Notes.pdf | *fetch*. Nothing MPE-related. |
| Cumulative release notes page | https://www.akaipro.com (Downloads & Support) | Supplied by the project owner; it carries the PDF links above. |

## Community MPC plugin work

| What | Where | Notes |
|---|---|---|
| open-mpc/mpc-vst-plugins (MIT) | https://github.com/open-mpc/mpc-vst-plugins at `a409770a3b61402877ca384c97c40934104be2fe` | *fetch*. `docs/NOTES.md` is a dated lab notebook of device traces: the channel-1 note-on finding, ALSA ports in both directions, the echo. `docs/MIDI_TIMING.md`: transport and port timing measurements. `poc/midiport.c`: minimal port example. |
| Percolator | https://github.com/saustin2010/mpc-vst-percolator | `src/midiin.h`: the reference for a plugin's own MIDI input port. |

These observations were made on MPC OS 3.9.1 with the community's loader. They are the
basis for the port approach and have not been re-checked on 3.10.1.

## LinnStrument

| What | Where | Notes |
|---|---|---|
| Firmware (Apache 2.0) | https://github.com/rogerlinndesign/linnstrument-firmware at `02261136771f647491817553eb456b793e4f6cec` (2026-11-11) | *fetch*. |
| ↳ MIDI implementation | `midi.md` | MIDI input, CC input, the complete NRPN table, colour and switch values. |
| ↳ User Firmware Mode | `user_firmware_mode.md` | Raw per-cell X / Y / Z. |
| ↳ MPE behaviour | `ls_midi.ino` (`midiSendMpeState`, `midiSendMpePitchBendRange`, `scalePitch`, `receivedRpn`), `ls_handleTouches.ino` (`sendNewNote`), `ls_settings.ino` (`enableMpe`) | What is actually sent, in what order. |
| Developers: how to add MPE | https://www.rogerlinndesign.com/support/support-developers-how-to-add-mpe | Roger Linn's guide for synth makers. |
| MIDI implementation page | https://www.rogerlinndesign.com/support/support-ls-midi-implementation | Points at `midi.md`. |

## Could not be read

- KVR "Linnstrument with Akai Force" (https://www.kvraudio.com/forum/viewtopic.php?t=573969)
  and the MPC-Forums 3.10 thread: both sites refuse automated readers. The KVR thread is
  quoted in [research.md](research.md) only from a search engine's excerpt.
- The MIDI Association's MPE specification (members' download). The summary in
  `shared/mpe/mpe_zone.h` follows the publicly described behaviour and the LinnStrument
  firmware.
