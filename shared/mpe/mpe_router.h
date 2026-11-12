// mpe_router.h - turns an MPE stream into something non-MPE instruments can play.
//
// SPDX-License-Identifier: MIT
//
// Three modes:
//
//   Thru    Every message passes unchanged, channel included. With the bridge plugin's two
//           ports this is a virtual MIDI loop-back cable inside the MPC: one recorded MPE
//           track goes in, and any number of tracks can listen to it, each on one channel.
//
//   Rotate  Notes are re-dealt onto N consecutive output channels, one note per channel,
//           so N copies of an ordinary mono-timbral instrument (one per track, each
//           listening to one channel) together behave like one MPE instrument. The
//           per-note controls are translated on the way: pitch bend is rescaled from the
//           controller's range to the instrument's, CC 74 (Y) and pressure (Z) are sent
//           as whatever message the instrument understands.
//
//   Single  Everything lands on one channel for a single ordinary instrument. Pressure
//           stays per note (polyphonic aftertouch). Pitch bend and Y cannot be per note on
//           one channel, so they follow the most recently pressed note that is still held.
//
// Pure logic: no I/O, no allocation, no system calls. Feed it channel voice messages with
// handle(); collect what it wants sent from output()/outputCount() and call clearOutput().

#ifndef MPE_ROUTER_H
#define MPE_ROUTER_H

#include <cmath>
#include <cstdint>
#include <cstring>

#include "mpe_zone.h"

namespace mpe {

enum class Mode { Thru = 0, Rotate = 1, Single = 2 };

// Where a per-note control is sent on the output side.
enum class Dest { Off = 0, ChannelPressure, PolyPressure, CC1, CC2, CC11, CC74, kCount };

inline int destController(Dest d)
{
    switch (d) { case Dest::CC1: return 1; case Dest::CC2: return 2; case Dest::CC11: return 11; case Dest::CC74: return 74; default: return -1; }
}

struct RouterConfig
{
    Mode mode = Mode::Rotate;
    int  master = 0;          // input master channel 0..15, or kNoMaster
    int  voices = 8;          // Rotate: number of output channels (1..16)
    int  firstChannel = 1;    // first output channel, 0..15 (default: channel 2)
    int  inBendRange = 0;     // semitones of the controller's member-channel bend; 0 = follow RPN 0, else 48
    int  outBendRange = 12;   // semitones the receiving instruments bend at full pitch bend
    Dest yDest = Dest::CC74;
    Dest zDest = Dest::ChannelPressure;

    bool operator==(const RouterConfig& o) const
    {
        return mode == o.mode && master == o.master && voices == o.voices && firstChannel == o.firstChannel
            && inBendRange == o.inBendRange && outBendRange == o.outBendRange && yDest == o.yDest && zDest == o.zDest;
    }
};

struct Msg { uint8_t d[3]; uint8_t len; };

class Router
{
public:
    enum { kMaxSlots = 16, kMaxOut = 2048 };

    Router() { hardReset(); }

    // Changing the configuration while notes sound releases them first (their note-offs are
    // queued in the output) so nothing hangs on a channel that is about to stop being used.
    void configure(const RouterConfig& c)
    {
        RouterConfig n = c;
        n.voices = clampInt(n.voices, 1, kMaxSlots);
        n.firstChannel = clampInt(n.firstChannel, 0, 15);
        if (n.mode == Mode::Rotate && n.firstChannel + n.voices > 16) n.voices = 16 - n.firstChannel;
        if (n.outBendRange < 1) n.outBendRange = 1;
        if (n == cfg_) return;
        const bool remap = n.mode != cfg_.mode || n.voices != cfg_.voices || n.firstChannel != cfg_.firstChannel
                        || n.master != cfg_.master;
        if (remap) releaseAll(true);
        cfg_ = n;
        zone_.setMaster(n.master);
        zone_.setForcedMemberRange(n.inBendRange);
        if (!remap) refreshAllExpression();
    }
    const RouterConfig& config() const { return cfg_; }

    // Forget everything without sending anything (transport reset, state load).
    void hardReset()
    {
        zone_.reset();
        zone_.setMaster(cfg_.master);
        zone_.setForcedMemberRange(cfg_.inBendRange);
        for (Slot& s : slots_) s = Slot();
        for (int c = 0; c < 16; ++c) clearCache(c);
        clock_ = 0; numOut_ = 0; dropped_ = 0;
    }

    // Release every held note and centre the output channels. Queues the messages.
    void panic()
    {
        releaseAll(true);
        for (int c = 0; c < 16; ++c) { emit3(0xB0 | c, kCcSustain, 0); emit3(0xB0 | c, kCcAllNotesOff, 0); }
    }

    // ---- input ----------------------------------------------------------------
    void handle(const uint8_t* m, int len)
    {
        if (len < 2 || m[0] < 0x80 || m[0] >= 0xF0) return;
        const int status = m[0] & 0xF0, ch = m[0] & 15;
        const int d1 = m[1] & 127, d2 = len > 2 ? (m[2] & 127) : 0;
        if (status == 0x90 && d2 > 0) noteChannelMask_ |= (uint16_t)(1u << ch);

        if (cfg_.mode == Mode::Thru) { emit(m, len); return; }

        switch (status)
        {
            case 0x90: if (d2 > 0) noteOn(ch, d1, d2); else noteOff(ch, d1, 64); break;
            case 0x80: noteOff(ch, d1, d2); break;
            case 0xA0: polyPressure(ch, d1, d2); break;
            case 0xB0: controller(ch, d1, d2); break;
            case 0xC0: if (zone_.isMaster(ch) || cfg_.master == kNoMaster) broadcast2(0xC0, d1); break;
            case 0xD0: channelPressure(ch, d1); break;
            case 0xE0: pitchBend(ch, (d1 | (d2 << 7)) - 8192); break;
            default: break;
        }
    }

    // ---- output ---------------------------------------------------------------
    int outputCount() const { return numOut_; }
    const Msg& output(int i) const { return out_[i]; }
    void clearOutput() { numOut_ = 0; }
    int droppedCount() const { return dropped_; }       // messages lost because the output buffer was full

    // ---- status ---------------------------------------------------------------
    uint16_t noteChannelMask() const { return noteChannelMask_; }   // input channels that have carried a note-on
    void clearStats() { noteChannelMask_ = 0; }
    int heldCount() const { int n = 0; for (const Slot& s : slots_) n += s.held ? 1 : 0; return n; }
    const Zone& zone() const { return zone_; }

private:
    struct Slot
    {
        bool held = false;
        int inCh = 0, note = 0;
        int z = 0;                  // current pressure of this note, 0..127
        int sentPoly = -1;          // last polyphonic pressure sent for this note
        uint32_t onStamp = 0, offStamp = 0;
    };

    int slotCount() const { return cfg_.mode == Mode::Rotate ? cfg_.voices : (int)kMaxSlots; }
    int outChannel(int slot) const { return cfg_.mode == Mode::Rotate ? cfg_.firstChannel + slot : cfg_.firstChannel; }

    // The note that owns the shared pitch bend and Y in Single mode.
    int focusSlot() const
    {
        int best = -1;
        for (int i = 0; i < kMaxSlots; ++i)
            if (slots_[i].held && (best < 0 || slots_[i].onStamp > slots_[best].onStamp)) best = i;
        return best;
    }
    bool ownsChannelControls(int slot) const { return cfg_.mode == Mode::Rotate || slot == focusSlot(); }

    // ---- notes ----
    void noteOn(int ch, int note, int vel)
    {
        const int n = slotCount();
        int pick = -1;
        for (int i = 0; i < n; ++i)                     // the free slot that has been silent longest,
            if (!slots_[i].held && (pick < 0 || slots_[i].offStamp < slots_[pick].offStamp)) pick = i;   // so release tails survive
        if (pick < 0)
        {
            for (int i = 0; i < n; ++i)                 // all busy: steal the oldest note
                if (pick < 0 || slots_[i].onStamp < slots_[pick].onStamp) pick = i;
            emit3(0x80 | outChannel(pick), slots_[pick].note, 64);
        }
        Slot& s = slots_[pick];
        s.held = true; s.inCh = ch; s.note = note; s.onStamp = ++clock_;
        s.z = zone_.channel(ch).pressure; s.sentPoly = -1;

        // Per-note controls first, then the note, so it starts at the right pitch and timbre.
        sendBend(pick); sendY(pick); sendZ(pick);
        emit3(0x90 | outChannel(pick), note, vel);
    }

    void noteOff(int ch, int note, int vel)
    {
        for (int i = 0; i < kMaxSlots; ++i)
        {
            Slot& s = slots_[i];
            if (!s.held || s.inCh != ch || s.note != note) continue;
            const bool wasFocus = cfg_.mode == Mode::Single && i == focusSlot();
            s.held = false; s.offStamp = ++clock_;
            emit3(0x80 | outChannel(i), note, vel);
            if (wasFocus) { const int f = focusSlot(); if (f >= 0) { sendBend(f); sendY(f); } }
            return;
        }
    }

    void releaseAll(bool centre)
    {
        for (int i = 0; i < kMaxSlots; ++i)
            if (slots_[i].held) { slots_[i].held = false; slots_[i].offStamp = ++clock_; emit3(0x80 | outChannel(i), slots_[i].note, 64); }
        if (!centre) return;
        for (int c = 0; c < 16; ++c)
        {
            if (sentBend_[c] != 0 && sentBend_[c] != kUnset) emit3(0xE0 | c, 0, 64);
            clearCache(c);
        }
    }

    // ---- per-note controls ----
    void pitchBend(int ch, int value)
    {
        zone_.channel(ch).bend = value;
        const bool all = zone_.isMaster(ch);
        for (int i = 0; i < kMaxSlots; ++i)
            if (slots_[i].held && (all || slots_[i].inCh == ch)) sendBend(i);
    }

    void channelPressure(int ch, int value)
    {
        zone_.channel(ch).pressure = value;
        for (int i = 0; i < kMaxSlots; ++i)
            if (slots_[i].held && slots_[i].inCh == ch) { slots_[i].z = value; sendZ(i); }
    }

    void polyPressure(int ch, int note, int value)
    {
        for (int i = 0; i < kMaxSlots; ++i)
            if (slots_[i].held && slots_[i].inCh == ch && slots_[i].note == note) { slots_[i].z = value; sendZ(i); }
    }

    void controller(int ch, int cc, int value)
    {
        if (zone_.handleRpnController(ch, cc, value))
        {
            if (cc == kCcDataMsb) refreshAllExpression();   // a bend range may have changed
            return;
        }
        const bool master = zone_.isMaster(ch);
        if (cc == kCcAllNotesOff || cc == kCcAllSoundOff)
        {
            if (master || cfg_.master == kNoMaster) { releaseAll(false); broadcast3(0xB0, cc, value); }
            return;
        }
        if (cc == kCcTimbre && !master)
        {
            zone_.channel(ch).timbre = value;
            for (int i = 0; i < kMaxSlots; ++i)
                if (slots_[i].held && slots_[i].inCh == ch) sendY(i);
            return;
        }
        if (master) { broadcast3(0xB0, cc, value); return; }
        if (cfg_.master == kNoMaster && cc >= 64 && cc <= 69)
        {
            // No master channel: the controller repeats pedals on every member channel.
            // Send each change once to every output channel.
            int& last = sentPedal_[cc - 64];
            if (last != value) { last = value; broadcast3(0xB0, cc, value); }
            return;
        }
        // Any other controller on a member channel follows that channel's notes.
        for (int i = 0; i < kMaxSlots; ++i)
            if (slots_[i].held && slots_[i].inCh == ch && ownsChannelControls(i)) emit3(0xB0 | outChannel(i), cc, value);
    }

    void refreshAllExpression()
    {
        for (int i = 0; i < kMaxSlots; ++i)
            if (slots_[i].held) { sendBend(i); sendY(i); sendZ(i); }
    }

    void sendBend(int slot)
    {
        if (!ownsChannelControls(slot)) return;
        const int oc = outChannel(slot);
        const float semis = zone_.bendSemitones(slots_[slot].inCh);
        const int v = clampInt((int)std::lround(semis * 8192.0f / (float)cfg_.outBendRange), -8192, 8191);
        if (v == sentBend_[oc]) return;
        sentBend_[oc] = v;
        emit3(0xE0 | oc, (v + 8192) & 127, (v + 8192) >> 7);
    }

    void sendY(int slot)
    {
        Dest d = cfg_.yDest;
        if (cfg_.mode == Mode::Single && d == Dest::ChannelPressure) d = Dest::PolyPressure;
        sendControl(slot, d, zone_.channel(slots_[slot].inCh).timbre);
    }

    void sendZ(int slot)
    {
        Dest d = cfg_.zDest;
        if (cfg_.mode == Mode::Single && d == Dest::ChannelPressure) d = Dest::PolyPressure;   // keep it per note
        sendControl(slot, d, slots_[slot].z);
    }

    void sendControl(int slot, Dest d, int value)
    {
        if (d == Dest::Off) return;
        const int oc = outChannel(slot);
        if (d == Dest::PolyPressure)
        {
            if (slots_[slot].sentPoly == value) return;
            slots_[slot].sentPoly = value;
            emit3(0xA0 | oc, slots_[slot].note, value);
            return;
        }
        if (!ownsChannelControls(slot)) return;
        int& last = sentCtl_[oc][(int)d];
        if (last == value) return;
        last = value;
        if (d == Dest::ChannelPressure) emit2(0xD0 | oc, value);
        else emit3(0xB0 | oc, destController(d), value);
    }

    // ---- sending ----
    void broadcast3(int status, int d1, int d2)
    {
        const int n = cfg_.mode == Mode::Rotate ? cfg_.voices : 1;
        for (int i = 0; i < n; ++i) emit3(status | (cfg_.firstChannel + i), d1, d2);
    }
    void broadcast2(int status, int d1)
    {
        const int n = cfg_.mode == Mode::Rotate ? cfg_.voices : 1;
        for (int i = 0; i < n; ++i) emit2(status | (cfg_.firstChannel + i), d1);
    }
    void emit3(int s, int d1, int d2) { const uint8_t m[3] = {(uint8_t)s, (uint8_t)d1, (uint8_t)d2}; emit(m, 3); }
    void emit2(int s, int d1) { const uint8_t m[3] = {(uint8_t)s, (uint8_t)d1, 0}; emit(m, 2); }
    void emit(const uint8_t* m, int len)
    {
        if (numOut_ >= kMaxOut) { ++dropped_; return; }
        Msg& o = out_[numOut_++];
        o.d[0] = m[0]; o.d[1] = m[1]; o.d[2] = len > 2 ? m[2] : 0; o.len = (uint8_t)len;
    }

    void clearCache(int c)
    {
        sentBend_[c] = kUnset;
        for (int& v : sentCtl_[c]) v = -1;
        if (c == 0) for (int& v : sentPedal_) v = -1;
    }

    enum { kUnset = 0x7FFFFFFF };

    RouterConfig cfg_;
    Zone zone_;
    Slot slots_[kMaxSlots];
    int sentBend_[16];
    int sentCtl_[16][(int)Dest::kCount];
    int sentPedal_[6];
    uint32_t clock_ = 0;
    uint16_t noteChannelMask_ = 0;
    Msg out_[kMaxOut];
    int numOut_ = 0;
    int dropped_ = 0;
};

} // namespace mpe

#endif // MPE_ROUTER_H
