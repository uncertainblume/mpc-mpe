# MPE Bridge

A silent MIDI router for the MPC with its own ports, "MPE Bridge MIDI In" and "MPE Bridge
MIDI Out". It plays ordinary MPC instruments with MPE by dealing notes onto one channel
per copy of the instrument, converting bend range, Y (CC 74) and Z (pressure) on the way.

Setup and controls: [docs/routing-guide.md](../../docs/routing-guide.md), recipe 2.

- Works: Thru, Rotate and Single modes; bend-range conversion with clamping; Y / Z
  destinations; master-channel messages to all voices; RPN 0 / RPN 6 from the controller;
  panic; echo guard for Track input. The routing logic is covered by `tests/test_router.cpp`.
- Not done: nothing has been run on an MPC yet. MIDI timing is audio-block resolution.
  MPE upper zones (master on 16 with members counting down) are routed like a lower zone
  when Master Ch is 16, but two zones at once are not separated.
- CPU: negligible. The cost is in the instrument copies it feeds.

Built with Akai's MPC Plugin SDK (MIT). MIT licensed.

## Version 0.2.0

- SHAPE page: Z Start (the pressure a note has before any arrives: Last, Velocity or
  Zero), Z Depth, Z Min (a floor for pressure), Y Depth, and Thin (send fewer bend, Y and
  Z messages).
- Notes are released when the MPC resets the plugin or a MIDI device disappears, so
  they no longer hang.
- A finished note's pressure is not passed on to the next note on its channel.
- Thin also works in Thru mode, and about ten times a second the true current values
  are sent, so a thinned control always ends where the player left it.
- Note Start = Clean (default): a new note ignores bend and Y left on its channel more
  than 100 ms earlier. This protects notes whose own starting values were not recorded,
  and notes that follow a note held across a loop point. Use As Sent for an ordinary
  keyboard whose wheel you hold while playing new notes.
- The same note arriving again while still held (a loop wrapping under a held note) ends
  the first one, so it cannot hang.
- New settings default to the old behaviour except Z Start (now Velocity) and Note
  Start (Clean).
- Routing: do not give any track that listens to the bridge, or the bridge's own track,
  the input "All Ports"; pick the one port it should hear.
- Not yet run on an MPC. The first build was labelled 1.0.0; 0.2.0 is newer.

## Version 0.2.1

- A new instance starts as "Thru Thin 2" (mode Thru, Thin 2). The old starting
  program, "8 Voices +-12", is now the last program. A project saved with program 0
  selected keeps its own saved settings, but shows the new name.

## Version 0.2.2

- Every choice control is now the MPC's name-and-value bar with a menu, so each one
  shows its name (Mode, Master Ch and the others were unlabelled segment buttons).
  No change to sound or MIDI handling.
