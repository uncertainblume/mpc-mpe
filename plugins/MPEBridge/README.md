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
