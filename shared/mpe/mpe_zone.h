// mpe_zone.h - the receiving side of MPE: per-channel expression state for one zone.
//
// SPDX-License-Identifier: MIT
//
// MPE in one paragraph: every sounding note gets its own MIDI channel (a "member"
// channel) for as long as it sounds, so the three per-note controls can use ordinary
// channel messages: pitch bend (X), channel pressure (Z) and CC 74 (Y, "timbre"). One
// more channel, the "master" (1 for the usual lower zone), carries things meant for all
// notes: sustain pedal, program change, a global pitch bend. Bend range is announced
// with RPN 0 per channel (default 48 semitones on member channels, 2 on the master) and
// the zone layout with RPN 6 on the master channel.
//
// This header only keeps that state; it makes no sound and sends nothing. MpeRouter and
// the MPE Synth plugin are built on it. No allocation, no system calls.

#ifndef MPE_ZONE_H
#define MPE_ZONE_H

#include <cstdint>

namespace mpe {

enum { kNumChannels = 16, kNoMaster = -1 };
enum { kCcTimbre = 74, kCcDataMsb = 6, kCcDataLsb = 38, kCcRpnLsb = 100, kCcRpnMsb = 101,
       kCcNrpnLsb = 98, kCcNrpnMsb = 99, kCcSustain = 64, kCcAllSoundOff = 120, kCcResetAll = 121, kCcAllNotesOff = 123 };

inline int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct ChannelState
{
    int bend = 0;            // -8192..8191
    int pressure = 0;        // 0..127 (channel pressure)
    int timbre = 64;         // 0..127 (CC 74); 64 is the MPE resting value
    int bendRange = 0;       // semitones announced with RPN 0 on this channel, 0 = never announced
    int rpn = 0x3FFF;        // selected RPN (0x3FFF = none)
    bool rpnIsNrpn = false;
    int dataMsb = 0;
};

class Zone
{
public:
    // master: 0..15, or kNoMaster when the controller has no master channel (every channel
    // then carries notes; LinnStrument does this when its Main channel is switched off).
    void setMaster(int master) { master_ = master; }
    int master() const { return master_; }
    bool isMaster(int ch) const { return ch == master_; }

    // Fallback ranges used until the controller announces its own with RPN 0.
    // forcedMemberRange > 0 overrides whatever the controller announces.
    void setForcedMemberRange(int semitones) { forcedMemberRange_ = semitones; }

    void reset()
    {
        for (ChannelState& c : ch_) c = ChannelState();
        memberCount_ = 0;
    }

    ChannelState& channel(int ch) { return ch_[ch & 15]; }
    const ChannelState& channel(int ch) const { return ch_[ch & 15]; }
    int announcedMemberCount() const { return memberCount_; }   // from RPN 6, 0 = never announced

    int memberBendRange(int ch) const
    {
        if (forcedMemberRange_ > 0) return forcedMemberRange_;
        const int r = ch_[ch & 15].bendRange;
        return r > 0 ? r : 48;
    }
    int masterBendRange() const
    {
        if (master_ < 0) return 0;
        const int r = ch_[master_].bendRange;
        return r > 0 ? r : 2;
    }

    // Total pitch offset in semitones for a note that lives on channel ch.
    float bendSemitones(int ch) const
    {
        float s = 0.0f;
        if (master_ >= 0) s += (float)ch_[master_].bend * (1.0f / 8192.0f) * (float)masterBendRange();
        if (ch != master_) s += (float)ch_[ch & 15].bend * (1.0f / 8192.0f) * (float)memberBendRange(ch);
        return s;
    }

    // Feeds one controller message through the RPN state machine. Returns true when the
    // message was part of an RPN/NRPN conversation (and so should not be treated as an
    // ordinary controller).
    bool handleRpnController(int ch, int cc, int value)
    {
        ChannelState& c = ch_[ch & 15];
        switch (cc)
        {
            case kCcRpnMsb:  c.rpn = (c.rpn & 0x7F) | (value << 7); c.rpnIsNrpn = false; return true;
            case kCcRpnLsb:  c.rpn = (c.rpn & 0x3F80) | value;      c.rpnIsNrpn = false; return true;
            case kCcNrpnMsb: c.rpn = (c.rpn & 0x7F) | (value << 7); c.rpnIsNrpn = true;  return true;
            case kCcNrpnLsb: c.rpn = (c.rpn & 0x3F80) | value;      c.rpnIsNrpn = true;  return true;
            case kCcDataMsb:
                if (c.rpn == 0x3FFF) return false;
                c.dataMsb = value;
                if (!c.rpnIsNrpn)
                {
                    if (c.rpn == 0) c.bendRange = clampInt(value, 0, 96);          // pitch bend sensitivity
                    else if (c.rpn == 6 && ch == master_) memberCount_ = clampInt(value, 0, 15);   // MPE configuration
                }
                return true;
            case kCcDataLsb:
                return c.rpn != 0x3FFF;    // cents of bend range and other LSBs are not needed
            default: return false;
        }
    }

private:
    ChannelState ch_[kNumChannels];
    int master_ = 0;
    int forcedMemberRange_ = 0;
    int memberCount_ = 0;
};

} // namespace mpe

#endif // MPE_ZONE_H
