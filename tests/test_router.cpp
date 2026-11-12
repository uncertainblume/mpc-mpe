// test_router.cpp - desktop tests for the MPE routing logic (no MPC, no ALSA needed).
//
// SPDX-License-Identifier: MIT
//
// Build and run:  tools/run_tests.sh
//
// The input streams are shaped like what a LinnStrument sends in Channel Per Note mode
// (ls_handleTouches.ino, sendNewNote): on the note's channel, pitch bend 0, then Y, then
// pressure 0, then the note-on; while the finger is down, a stream of pitch bend / CC 74 /
// channel pressure; then the note-off.

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "mpe/alsa_seq.h"
#include "mpe/mpe_router.h"

using namespace mpe;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

struct Rig
{
    Router r;
    std::vector<Msg> out;

    void send(int status, int ch, int d1, int d2 = 0)
    {
        const uint8_t m[3] = {(uint8_t)(status | ch), (uint8_t)d1, (uint8_t)d2};
        r.handle(m, (status == 0xC0 || status == 0xD0) ? 2 : 3);
        collect();
    }
    void bend(int ch, int v) { send(0xE0, ch, (v + 8192) & 127, (v + 8192) >> 7); }
    void collect() { for (int i = 0; i < r.outputCount(); ++i) out.push_back(r.output(i)); r.clearOutput(); }
    void clear() { out.clear(); }

    // LinnStrument-style new touch
    void touch(int ch, int note, int vel, int y = 64)
    {
        bend(ch, 0); send(0xB0, ch, 74, y); send(0xD0, ch, 0); send(0x90, ch, note, vel);
    }

    int count(int status, int ch) const
    {
        int n = 0;
        for (const Msg& m : out) if (m.d[0] == (status | ch)) ++n;
        return n;
    }
    // last message of a kind on a channel, or nullptr
    const Msg* last(int status, int ch) const
    {
        for (size_t i = out.size(); i-- > 0;) if (out[i].d[0] == (status | ch)) return &out[i];
        return nullptr;
    }
    int lastBend(int ch) const { const Msg* m = last(0xE0, ch); return m ? (m->d[1] | (m->d[2] << 7)) - 8192 : 99999; }
};

static void testThru()
{
    Rig g; RouterConfig c; c.mode = Mode::Thru; g.r.configure(c);
    g.touch(3, 60, 100);
    g.bend(3, 1234);
    CHECK(g.out.size() == 5);
    CHECK(g.count(0x90, 3) == 1);
    CHECK(g.lastBend(3) == 1234);                 // channel and value untouched
    CHECK(g.r.noteChannelMask() == (1u << 3));
}

static void testRotateBasics()
{
    Rig g; RouterConfig c; c.mode = Mode::Rotate; c.voices = 4; c.firstChannel = 1; c.outBendRange = 12; g.r.configure(c);

    // Three notes on LinnStrument channels 2, 3, 4 (indices 1, 2, 3)
    g.touch(1, 60, 100); g.touch(2, 64, 90); g.touch(3, 67, 80);
    CHECK(g.count(0x90, 1) == 1 && g.count(0x90, 2) == 1 && g.count(0x90, 3) == 1);

    // The per-note controls are sent before the note-on
    size_t firstBend = 0, firstOn = 0;
    for (size_t i = 0; i < g.out.size(); ++i) { if (g.out[i].d[0] == 0xE1 && !firstBend) firstBend = i + 1; if (g.out[i].d[0] == 0x91 && !firstOn) firstOn = i + 1; }
    CHECK(firstBend != 0 && firstBend < firstOn);

    // Bend only the middle note by one semitone at the LinnStrument's 48-semitone range:
    // 8192/48 = 170.67 units in, and at a 12-semitone instrument range that is 682.67 out.
    g.clear();
    g.bend(2, 171);
    CHECK(g.out.size() == 1);
    CHECK(g.lastBend(2) >= 682 && g.lastBend(2) <= 685);
    CHECK(g.count(0xE0, 1) == 0 && g.count(0xE0, 3) == 0);   // the other notes did not move

    // A bend wider than the instrument can follow is clamped, not wrapped
    g.clear(); g.bend(2, 8191);
    CHECK(g.lastBend(2) == 8191);

    // Pressure and Y stay per note
    g.clear();
    g.send(0xD0, 1, 99); g.send(0xB0, 3, 74, 20);
    CHECK(g.last(0xD0, 1) && g.last(0xD0, 1)->d[1] == 99);
    CHECK(g.last(0xB0, 3) && g.last(0xB0, 3)->d[1] == 74 && g.last(0xB0, 3)->d[2] == 20);
    CHECK(g.count(0xD0, 2) == 0 && g.count(0xB0, 1) == 0);

    // Note-offs go to the right output channel
    g.clear();
    g.send(0x80, 2, 64, 0);
    CHECK(g.count(0x80, 2) == 1 && g.out.size() == 1);

    // Expression on a channel whose note has ended moves nothing (the tail keeps its pitch)
    g.clear(); g.bend(2, 0);
    CHECK(g.out.empty());
    CHECK(g.r.heldCount() == 2);
}

static void testRotateReallocation()
{
    // A controller using 7 channels into 3 instrument tracks.
    Rig g; RouterConfig c; c.mode = Mode::Rotate; c.voices = 3; c.firstChannel = 4; g.r.configure(c);
    g.touch(1, 60, 100); g.touch(2, 62, 100); g.touch(3, 64, 100);
    CHECK(g.count(0x90, 4) == 1 && g.count(0x90, 5) == 1 && g.count(0x90, 6) == 1);

    // Release the first; the next note takes the channel that has been silent longest
    g.send(0x80, 1, 60, 0);
    g.clear();
    g.touch(5, 70, 100);
    CHECK(g.count(0x90, 4) == 1);

    // All busy: the oldest note (62 on output channel 5) is stolen and gets a note-off first
    g.clear();
    g.touch(6, 72, 100);
    CHECK(g.count(0x80, 5) == 1 && g.count(0x90, 5) == 1);
    CHECK(g.r.heldCount() == 3);

    // The stolen note's own note-off later is ignored
    g.clear();
    g.send(0x80, 2, 62, 0);
    CHECK(g.out.empty());

    // Voices never spill past channel 16
    RouterConfig d; d.mode = Mode::Rotate; d.voices = 16; d.firstChannel = 12; g.r.configure(d);
    CHECK(g.r.config().voices == 4);
}

static void testDestinationsAndRanges()
{
    Rig g; RouterConfig c; c.mode = Mode::Rotate; c.voices = 2; c.firstChannel = 0;
    c.yDest = Dest::CC1; c.zDest = Dest::PolyPressure; c.outBendRange = 2; g.r.configure(c);
    g.touch(1, 60, 100, 30);
    CHECK(g.last(0xB0, 0) && g.last(0xB0, 0)->d[1] == 1 && g.last(0xB0, 0)->d[2] == 30);   // Y arrived as mod wheel
    g.clear(); g.send(0xD0, 1, 77);
    CHECK(g.last(0xA0, 0) && g.last(0xA0, 0)->d[1] == 60 && g.last(0xA0, 0)->d[2] == 77);  // Z as poly aftertouch
    g.clear(); g.send(0xD0, 1, 77);
    CHECK(g.out.empty());                                                                   // repeats are not re-sent

    // The controller announces a 24-semitone member range with RPN 0 (as LinnStrument does
    // when MPE is switched on). One semitone is then 8192/24 = 341 in, and 4096 out at +-2.
    g.send(0xB0, 1, 101, 0); g.send(0xB0, 1, 100, 0); g.send(0xB0, 1, 6, 24); g.send(0xB0, 1, 38, 0);
    g.send(0xB0, 1, 101, 127); g.send(0xB0, 1, 100, 127);
    g.clear(); g.bend(1, 341);
    CHECK(g.lastBend(0) >= 4090 && g.lastBend(0) <= 4096);
    CHECK(g.count(0xB0, 0) == 0);                       // the RPN itself was not passed on as controllers

    // A fixed input range overrides the announcement
    c.inBendRange = 48; g.r.configure(c);
    g.clear(); g.bend(1, 342);
    CHECK(g.lastBend(0) >= 8180);                       // 2 semitones at 48 -> full scale at +-2
}

static void testMasterChannel()
{
    Rig g; RouterConfig c; c.mode = Mode::Rotate; c.voices = 3; c.firstChannel = 1; c.outBendRange = 12; g.r.configure(c);
    g.touch(1, 60, 100); g.touch(2, 64, 100);
    g.clear();
    g.send(0xB0, 0, 64, 127);                           // sustain pedal on the master channel
    CHECK(g.count(0xB0, 1) == 1 && g.count(0xB0, 2) == 1 && g.count(0xB0, 3) == 1);

    g.clear();
    g.bend(0, 8191);                                    // master bend: +2 semitones for every note
    CHECK(g.lastBend(1) >= 1364 && g.lastBend(1) <= 1366 && g.lastBend(2) == g.lastBend(1));

    g.clear();
    g.send(0xB0, 0, 123, 0);                            // all notes off
    CHECK(g.count(0x80, 1) == 1 && g.count(0x80, 2) == 1 && g.r.heldCount() == 0);

    // No master channel (LinnStrument with Main switched off): channel 1 is an ordinary
    // member, and a pedal repeated on every member channel goes out once per output channel.
    RouterConfig n = c; n.master = kNoMaster; g.r.configure(n);
    g.touch(0, 50, 100); g.touch(1, 52, 100);
    g.clear();
    for (int ch = 0; ch < 8; ++ch) g.send(0xB0, ch, 64, 127);
    CHECK(g.count(0xB0, 1) == 1 && g.count(0xB0, 2) == 1 && g.count(0xB0, 3) == 1);
    g.clear(); g.bend(0, 4096);
    CHECK(g.count(0xE0, 1) + g.count(0xE0, 2) + g.count(0xE0, 3) == 1);   // only its own note
}

static void testSingleMode()
{
    Rig g; RouterConfig c; c.mode = Mode::Single; c.firstChannel = 0; c.outBendRange = 48; g.r.configure(c);
    g.touch(1, 60, 100); g.touch(2, 64, 100);
    CHECK(g.count(0x90, 0) == 2);

    // Pressure stays per note, as polyphonic aftertouch
    g.clear();
    g.send(0xD0, 1, 50); g.send(0xD0, 2, 90);
    CHECK(g.count(0xA0, 0) == 2 && g.count(0xD0, 0) == 0);

    // Only the newest held note owns the shared pitch bend
    g.clear();
    g.bend(1, 1000); CHECK(g.out.empty());
    g.bend(2, 2000); CHECK(g.lastBend(0) == 2000);

    // When it is released, the bend hands over to the note still held
    g.clear();
    g.send(0x80, 2, 64, 0);
    CHECK(g.count(0x80, 0) == 1 && g.lastBend(0) == 1000);
}

static void testReconfigureAndPanic()
{
    Rig g; RouterConfig c; c.mode = Mode::Rotate; c.voices = 4; c.firstChannel = 1; g.r.configure(c);
    g.touch(1, 60, 100); g.bend(1, 3000);
    g.clear();
    c.firstChannel = 8; g.r.configure(c); g.collect();
    CHECK(g.count(0x80, 1) == 1);                       // the held note was released on its old channel
    CHECK(g.lastBend(1) == 0);                          // and that channel's bend was centred
    CHECK(g.r.heldCount() == 0);

    g.touch(1, 60, 100);                                // lands on channel 10: the slot that was never used
    g.clear(); g.r.panic(); g.collect();
    CHECK(g.count(0x80, 9) == 1 && g.count(0xB0, 9) == 2 && g.r.heldCount() == 0);
}

static void testDenseStream()
{
    // Ten fingers, each sending 3 controls every 8 ms for 2 seconds: the router must
    // neither drop nor invent notes, and the output must stay inside its buffer.
    Rig g; RouterConfig c; c.mode = Mode::Rotate; c.voices = 10; c.firstChannel = 1; g.r.configure(c);
    for (int f = 0; f < 10; ++f) g.touch(1 + f, 48 + f, 100);
    for (int t = 0; t < 250; ++t)
        for (int f = 0; f < 10; ++f) { g.bend(1 + f, (t * 37 + f * 100) % 8000); g.send(0xB0, 1 + f, 74, (t + f) & 127); g.send(0xD0, 1 + f, (t * 3 + f) & 127); }
    for (int f = 0; f < 10; ++f) g.send(0x80, 1 + f, 48 + f, 0);
    int on = 0, off = 0;
    for (const Msg& m : g.out) { if ((m.d[0] & 0xF0) == 0x90) ++on; if ((m.d[0] & 0xF0) == 0x80) ++off; }
    CHECK(on == 10 && off == 10 && g.r.droppedCount() == 0 && g.r.heldCount() == 0);
}

static void testSeqEventConversion()
{
    // Every channel voice message survives bytes -> sequencer event -> bytes.
    const uint8_t cases[][3] = {{0x93, 60, 100}, {0x85, 61, 12}, {0xA2, 62, 99}, {0xB7, 74, 5}, {0xCF, 9, 0},
                                {0xD1, 127, 0}, {0xE0, 0, 0}, {0xEA, 127, 127}, {0xE4, 0, 64}, {0xE4, 1, 64}};
    for (const auto& c : cases)
    {
        SeqEvent ev; uint8_t back[3] = {0, 0, 0};
        CHECK(midiToSeqEvent(c, ev));
        const int n = seqEventToMidi(ev, back);
        const int expect = ((c[0] & 0xF0) == 0xC0 || (c[0] & 0xF0) == 0xD0) ? 2 : 3;
        CHECK(n == expect && back[0] == c[0] && back[1] == c[1] && (n == 2 || back[2] == c[2]));
    }
    SeqEvent ev; const uint8_t clock[3] = {0xF8, 0, 0};
    CHECK(!midiToSeqEvent(clock, ev));
}

int main()
{
    testThru();
    testRotateBasics();
    testRotateReallocation();
    testDestinationsAndRanges();
    testMasterChannel();
    testSingleMode();
    testReconfigureAndPanic();
    testDenseStream();
    testSeqEventConversion();
    if (failures) { std::printf("%d check(s) FAILED\n", failures); return 1; }
    std::printf("router tests: all passed\n");
    return 0;
}
