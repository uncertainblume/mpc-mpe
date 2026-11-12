// MPEBridge.cpp - MPE Bridge: lets ordinary MPC instruments be played with full MPE.
//
// SPDX-License-Identifier: MIT
//
// This plugin makes no sound. It is a MIDI router that lives on a plugin track and owns
// two MIDI ports of its own, which the MPC lists next to the hardware ports:
//
//     "MPE Bridge MIDI In"    pick it as the MIDI OUTPUT port of the track that carries
//                             your MPE performance (live from the controller, or recorded)
//     "MPE Bridge MIDI Out"   pick it as the MIDI INPUT port of the instrument tracks
//
//     MPE controller ──► [MPE track: in = controller, ch All; out = MPE Bridge MIDI In, ch Any]
//                                             │
//                                      ┌──────▼──────┐
//                                      │ MPE Bridge  │  Thru / Rotate / Single
//                                      └──────┬──────┘
//                                             │ MPE Bridge MIDI Out
//              ┌──────────────────┬───────────┴──────┬──────────────────┐
//        [track A: in ch 2]  [track B: in ch 3]  [track C: in ch 4]  ...   one copy of the
//         instrument copy 1   instrument copy 2   instrument copy 3        instrument each
//
// Why the detour through ports: MIDI that reaches a plugin through its own track has lost
// its channels, and MIDI a plugin sends back through the plugin interface is discarded by
// the MPC. Ports keep the channels in both directions. docs/research.md has the details.
//
// Modes (see shared/mpe/mpe_router.h): Thru passes everything unchanged; Rotate deals the
// notes onto N channels and converts bend range / Y / Z for the instruments; Single puts
// everything on one channel with pressure as polyphonic aftertouch.

#include "mpc_plugin.h"
#include "mpe/alsa_seq.h"
#include "mpe/mpe_entry.h"
#include "mpe/mpe_router.h"

class MPEBridge : public mpc::Plugin
{
public:
    // Parameter order is permanent once projects exist. Append new ones at the end.
    enum Params
    {
        kStatus,        // read-only: is the MIDI port there? (first on purpose: the host nudges parameter 0)
        kMode, kInput, kVoices, kFirstCh, kMasterCh, kInBend, kOutBend, kYDest, kZDest,
        kPanic,
        kPortChs,       // read-only: channels that have carried notes into "MIDI In"
        kTrackChs,      // read-only: channels that have carried notes in through the track
        kNumParams
    };
    enum InputSource { kInPort = 0, kInTrack = 1, kInBoth = 2 };

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "MPE Bridge";
        i.vendor = "MPE Tools";
        i.uniqueId = MPC_FOURCC('M', 'p', 'B', 'r');   // must match "uid" in plugin.json
        i.version = 1000;
        i.isInstrument = true;      // only instruments receive MIDI on the MPC
        i.numInputs = 0;
        i.numOutputs = 2;           // silent, but the MPC expects a stereo instrument
        i.sendsMidi = true;         // ignored by the MPC; used by desktop hosts when there is no ALSA port
        i.tailSamples = 1;
        return i;
    }

    MPEBridge() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        const std::vector<std::string> dests = {"Off", "Ch Press", "Poly AT", "CC1 Mod", "CC2", "CC11 Expr", "CC74"};   // == mpe::Dest
        auto readOnly = [](Param p) { p.automatable = false; return p; };
        addParam(readOnly(intParam("status", "Port", 0, 1, 0)));
        addParam(choiceParam("mode",     "Mode", {"Thru", "Rotate", "Single"}, 1));
        addParam(choiceParam("input",    "Input", {"Port", "Track", "Both"}, kInPort));
        addParam(intParam   ("voices",   "Voices", 1, 16, 8));
        addParam(intParam   ("firstch",  "First Ch", 1, 16, 2));
        addParam(choiceParam("masterch", "Master Ch", {"1", "16", "None"}, 0));
        addParam(intParam   ("inbend",   "In Bend", 0, 96, 0, "st"));
        addParam(intParam   ("outbend",  "Out Bend", 1, 96, 12, "st"));
        addParam(choiceParam("ydest",    "Y Dest", dests, (int)mpe::Dest::CC74));
        addParam(choiceParam("zdest",    "Z Dest", dests, (int)mpe::Dest::ChannelPressure));
        addParam(readOnly(boolParam("panic", "Panic", false)));
        addParam(readOnly(intParam("portchs",  "Port Chs", 0, 1, 0)));
        addParam(readOnly(intParam("trackchs", "Track Chs", 0, 1, 0)));
        if (numParams() != kNumParams) mpc::log("MPEBridge: parameter count mismatch");

        auto program = [this](const char* name, std::initializer_list<std::pair<int, float>> overrides)
        {
            std::vector<float> v((size_t)numParams());
            for (int i = 0; i < numParams(); ++i) v[(size_t)i] = param(i).def;
            for (const auto& o : overrides) v[(size_t)o.first] = o.second;
            addProgram(name, std::move(v));
        };
        program("8 Voices +-12",    {});
        program("Thru (loopback)",  {{kMode, 0}});
        program("4 Voices +-12",    {{kVoices, 4}});
        program("8 Voices +-2",     {{kOutBend, 2}});
        program("8 Voices Y>Mod",   {{kYDest, (float)mpe::Dest::CC1}});
        program("Single + Poly AT", {{kMode, 2}, {kFirstCh, 1}, {kOutBend, 48}, {kZDest, (float)mpe::Dest::PolyPressure}});

        // The port is opened here, on the UI thread, so it exists as soon as the plugin is on a track.
        port_.open("MPE Bridge", true, true);
    }

    void reset() override
    {
        router_.hardReset();
        for (Sent& s : sent_) s = Sent();
        lastPanic_ = getBool(kPanic);
    }

    void process(mpc::ProcessContext& ctx) override
    {
        ++block_;
        router_.clearOutput();
        router_.configure(readConfig());

        const bool panic = getBool(kPanic);
        if (panic && !lastPanic_) router_.panic();
        lastPanic_ = panic;

        const int input = getInt(kInput);

        // 1. What arrived at our own port since the last block (channels intact).
        if (input != kInTrack)
        {
            uint8_t m[3];
            for (int n = 0, len; n < kMaxPortEventsPerBlock && (len = port_.read(m)) > 0; ++n)
            {
                if ((m[0] & 0xF0) == 0x90 && m[2] > 0) portMask_.fetch_or((unsigned)(1u << (m[0] & 15)), std::memory_order_relaxed);
                router_.handle(m, len);
            }
        }
        else
        {
            uint8_t m[3];
            for (int n = 0; n < kMaxPortEventsPerBlock && port_.read(m) > 0; ++n) {}   // keep the queue empty
        }

        // 2. What the MPC handed us through the track.
        for (int i = 0; i < ctx.numMidiEvents; ++i)
        {
            const mpc::MidiEvent& e = ctx.midi[i];
            if (e.size < 2 || e.data[0] >= 0xF0) continue;
            if (e.isNoteOn()) trackMask_.fetch_or((unsigned)(1u << e.channel()), std::memory_order_relaxed);
            if (input == kInPort) continue;
            if (wasJustSent(e.data, e.size)) continue;   // our own output coming back in: do not loop it
            router_.handle(e.data, e.size);
        }

        // 3. Send.
        const bool remember = input != kInPort;
        for (int i = 0; i < router_.outputCount(); ++i)
        {
            const mpe::Msg& o = router_.output(i);
            if (port_.hasOutput()) port_.send(o.d);
            else
            {
                mpc::MidiEvent e; e.sampleOffset = 0; e.size = o.len;
                e.data[0] = o.d[0]; e.data[1] = o.d[1]; e.data[2] = o.d[2];
                sendMidi(e);
            }
            if (remember) rememberSent(o.d, o.len);
        }
        // Audio stays silent: the framework has already cleared the outputs.
    }

    void parameterChanged(int index, float plain) override
    {
        // Turning either status knob clears the "channels seen" display so a new test starts clean.
        if (index == kPortChs && plain >= 0.5f) portMask_.store(0, std::memory_order_relaxed);
        if (index == kTrackChs && plain >= 0.5f) trackMask_.store(0, std::memory_order_relaxed);
    }

    bool formatValue(int index, float plain, std::string& out) override
    {
        char buf[48];
        switch (index)
        {
            case kStatus:  out = port_.isOpen() ? "OK" : "No port"; return true;
            case kInBend:  if (plain < 0.5f) { out = "Auto"; return true; } return false;
            case kPortChs:  mpe::formatChannelMask(portMask_.load(std::memory_order_relaxed), buf, sizeof(buf)); out = buf; return true;
            case kTrackChs: mpe::formatChannelMask(trackMask_.load(std::memory_order_relaxed), buf, sizeof(buf)); out = buf; return true;
            default: return false;
        }
    }

private:
    enum { kMaxPortEventsPerBlock = 1024, kSentRing = 256, kEchoBlocks = 96 };

    mpe::RouterConfig readConfig() const
    {
        mpe::RouterConfig c;
        c.mode = (mpe::Mode)getInt(kMode);
        c.voices = getInt(kVoices);
        c.firstChannel = getInt(kFirstCh) - 1;
        const int master = getInt(kMasterCh);
        c.master = master == 0 ? 0 : (master == 1 ? 15 : (int)mpe::kNoMaster);
        c.inBendRange = getInt(kInBend);
        c.outBendRange = getInt(kOutBend);
        c.yDest = (mpe::Dest)getInt(kYDest);
        c.zDest = (mpe::Dest)getInt(kZDest);
        return c;
    }

    // ---- echo guard --------------------------------------------------------
    // The MPC enables a plugin's new port as an input for tracks, so with Input set to
    // Track or Both our own output can come straight back in through our track. Each
    // message we send is remembered for a moment and swallowed once if it returns.
    struct Sent { uint32_t key = 0, block = 0; bool pending = false; };
    static uint32_t keyOf(const uint8_t* m, int len) { return (uint32_t)m[0] | ((uint32_t)m[1] << 8) | ((uint32_t)(len > 2 ? m[2] : 0) << 16); }
    void rememberSent(const uint8_t* m, int len)
    {
        Sent& s = sent_[sentPos_++ % kSentRing];
        s.key = keyOf(m, len); s.block = block_; s.pending = true;
    }
    bool wasJustSent(const uint8_t* m, int len)
    {
        const uint32_t key = keyOf(m, len);
        for (Sent& s : sent_)
            if (s.pending && s.key == key && block_ - s.block < (uint32_t)kEchoBlocks) { s.pending = false; return true; }
        return false;
    }

    mpe::AlsaMidiPort port_;
    mpe::Router router_;
    Sent sent_[kSentRing];
    uint32_t sentPos_ = 0, block_ = 0;
    bool lastPanic_ = false;
    std::atomic<unsigned> portMask_{0}, trackMask_{0};
};

MPE_PLUGIN_ENTRY(MPEBridge)
