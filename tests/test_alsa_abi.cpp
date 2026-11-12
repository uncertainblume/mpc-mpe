// test_alsa_abi.cpp - checks shared/mpe/alsa_seq.h against the real ALSA headers.
//
// SPDX-License-Identifier: MIT
//
// alsa_seq.h describes the sequencer event struct and constants by hand so the plugins
// need no ALSA headers to build. This file fails to compile if that description ever
// disagrees with the real thing. Compiled by tools/run_tests.sh when ALSA's development
// headers are installed (Debian/Ubuntu: libasound2-dev); skipped otherwise.

#include <cstddef>
#include <cstdio>

#include "mpe/alsa_seq.h"

#if __has_include(<alsa/asoundlib.h>)
#include <alsa/asoundlib.h>

#define SAME_OFFSET(mine, theirs) static_assert(offsetof(mpe::SeqEvent, mine) == offsetof(snd_seq_event_t, theirs), #mine)

static_assert(sizeof(mpe::SeqEvent) == sizeof(snd_seq_event_t), "event size");
SAME_OFFSET(type, type);
SAME_OFFSET(flags, flags);
SAME_OFFSET(queue, queue);
SAME_OFFSET(time, time);
SAME_OFFSET(source, source);
SAME_OFFSET(dest, dest);
SAME_OFFSET(data, data);
SAME_OFFSET(data.note.channel, data.note.channel);
SAME_OFFSET(data.note.note, data.note.note);
SAME_OFFSET(data.note.velocity, data.note.velocity);
SAME_OFFSET(data.control.channel, data.control.channel);
SAME_OFFSET(data.control.param, data.control.param);
SAME_OFFSET(data.control.value, data.control.value);

static_assert((int)mpe::kSeqNoteOn == (int)SND_SEQ_EVENT_NOTEON && (int)mpe::kSeqNoteOff == (int)SND_SEQ_EVENT_NOTEOFF, "note types");
static_assert((int)mpe::kSeqKeyPress == (int)SND_SEQ_EVENT_KEYPRESS && (int)mpe::kSeqController == (int)SND_SEQ_EVENT_CONTROLLER, "types");
static_assert((int)mpe::kSeqPgmChange == (int)SND_SEQ_EVENT_PGMCHANGE && (int)mpe::kSeqChanPress == (int)SND_SEQ_EVENT_CHANPRESS, "types");
static_assert((int)mpe::kSeqPitchBend == (int)SND_SEQ_EVENT_PITCHBEND, "types");
static_assert((int)mpe::kSeqQueueDirect == (int)SND_SEQ_QUEUE_DIRECT, "direct queue");
static_assert((int)mpe::kSeqAddrSubscribers == (int)SND_SEQ_ADDRESS_SUBSCRIBERS && (int)mpe::kSeqAddrUnknown == (int)SND_SEQ_ADDRESS_UNKNOWN, "addresses");
static_assert((int)mpe::kSeqCapRead == (int)SND_SEQ_PORT_CAP_READ && (int)mpe::kSeqCapWrite == (int)SND_SEQ_PORT_CAP_WRITE, "caps");
static_assert((int)mpe::kSeqCapSubsRead == (int)SND_SEQ_PORT_CAP_SUBS_READ && (int)mpe::kSeqCapSubsWrite == (int)SND_SEQ_PORT_CAP_SUBS_WRITE, "caps");
static_assert((int)mpe::kSeqTypeMidiGeneric == (int)SND_SEQ_PORT_TYPE_MIDI_GENERIC && (int)mpe::kSeqTypeApplication == (int)SND_SEQ_PORT_TYPE_APPLICATION, "port types");
static_assert((int)mpe::kSeqOpenDuplex == (int)SND_SEQ_OPEN_DUPLEX && (int)mpe::kSeqNonblock == (int)SND_SEQ_NONBLOCK, "open flags");

int main() { std::printf("ALSA ABI check: matches the installed headers\n"); return 0; }
#else
int main() { std::printf("ALSA ABI check: skipped (ALSA development headers not installed)\n"); return 0; }
#endif
