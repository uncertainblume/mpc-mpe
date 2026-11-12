// MPESynth.cpp - MPE Synth: a 12-voice synthesizer for the MPC that is MPE from the ground up.
//
// SPDX-License-Identifier: MIT
//
// Every note has its own pitch bend (X), its own timbre (Y, CC 74) and its own pressure (Z),
// taken from the MIDI channel the note arrived on.
//
// Signal path per voice:
//   PolyBLEP oscillator (pitch = note + that note's bend)
//     -> low-pass SVF (cutoff + Y amount + Z amount + key tracking)
//     -> amp ADSR * (velocity blended with that note's pressure)
//   all voices summed -> master volume -> both outputs.
//
// Where the MIDI comes from (the important part on an MPC):
//   "MPE Synth MIDI In"  the plugin's own MIDI port. Choose it as the MIDI OUTPUT port of the
//                        track that holds the MPE performance, with the output channel set to
//                        Any. Channels arrive intact. This is the route that works.
//   the plugin's track   also listened to. On MPC OS 3.9.1 note-ons arrived on channel 1
//                        whatever channel they were played on, which breaks MPE; whether
//                        3.10.1 changes that for a plugin that declares MPE is unknown. The
//                        STATUS page shows which channels came in by each route.
//   Input = Auto uses the port as soon as anything arrives there, and the track until then.

#include "mpc_plugin.h"
#include "dsp/math.h"
#include "dsp/oscillator.h"
#include "dsp/svf.h"
#include "dsp/adsr.h"
#include "dsp/smoother.h"
#include "mpe/alsa_seq.h"
#include "mpe/mpe_entry.h"
#include "mpe/mpe_zone.h"

using namespace mpc::dsp;

class MPESynth : public mpc::Plugin
{
public:
    // Parameter order is permanent once projects exist. Append new ones at the end.
    enum Params
    {
        kVolume,
        kWave, kCutoff, kReso, kKeyTrack,
        kYCutoff, kZCutoff, kZLevel, kVelLevel,
        kAttack, kDecay, kSustain, kRelease,
        kBendRange, kMasterCh, kSmooth, kInput,
        kPortStatus, kPortChs, kTrackChs,
        kNumParams
    };
    enum InputSource { kInAuto = 0, kInTrack = 1, kInPort = 2 };

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "MPE Synth";
        i.vendor = "MPE Tools";
        i.uniqueId = MPC_FOURCC('M', 'p', 'S', 'y');   // must match "uid" in plugin.json
        i.version = 1000;
        i.isInstrument = true;
        i.numInputs = 0;
        i.numOutputs = 2;
        return i;
    }

    MPESynth() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        auto readOnly = [](Param p) { p.automatable = false; return p; };
        addParam(floatParam ("vol",      "Volume", -40.0f, 6.0f, -10.0f, "dB", 1));
        addParam(choiceParam("wave",     "Wave", {"Sine", "Triangle", "Saw", "Square"}, 2));   // == dsp::Wave order
        addParam(logParam   ("cutoff",   "Cutoff", 40.0f, 18000.0f, 700.0f, "Hz", 0));
        addParam(floatParam ("reso",     "Resonance", 0.0f, 100.0f, 25.0f, "%", 0));
        addParam(floatParam ("keytrk",   "Key Track", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam ("ycut",     "Y>Cutoff", -4.0f, 4.0f, 2.5f, "oct", 1));   // CC 74 swing around its centre
        addParam(floatParam ("zcut",     "Z>Cutoff", 0.0f, 5.0f, 2.0f, "oct", 1));    // pressure opens the filter
        addParam(floatParam ("zlvl",     "Z>Level", 0.0f, 100.0f, 70.0f, "%", 0));    // how much pressure sets loudness
        addParam(floatParam ("vellvl",   "Vel>Level", 0.0f, 100.0f, 60.0f, "%", 0));
        addParam(logParam   ("atk",      "Attack", 1.0f, 5000.0f, 4.0f, "ms", 0));
        addParam(logParam   ("dec",      "Decay", 1.0f, 5000.0f, 400.0f, "ms", 0));
        addParam(floatParam ("sus",      "Sustain", 0.0f, 100.0f, 85.0f, "%", 0));
        addParam(logParam   ("rel",      "Release", 1.0f, 5000.0f, 250.0f, "ms", 0));
        addParam(intParam   ("bend",     "Bend Range", 0, 96, 0, "st"));               // 0 = Auto (RPN 0, else 48)
        addParam(choiceParam("masterch", "Master Ch", {"1", "16", "None"}, 0));
        addParam(floatParam ("smooth",   "Smoothing", 0.0f, 50.0f, 6.0f, "ms", 0));    // de-steps 7-bit Y and Z
        addParam(choiceParam("input",    "Input", {"Auto", "Track", "Port"}, kInAuto));
        addParam(readOnly(intParam("portstat", "Port", 0, 1, 0)));
        addParam(readOnly(intParam("portchs",  "Port Chs", 0, 1, 0)));
        addParam(readOnly(intParam("trackchs", "Track Chs", 0, 1, 0)));
        if (numParams() != kNumParams) mpc::log("MPESynth: parameter count mismatch");

        auto program = [this](const char* name, std::initializer_list<std::pair<int, float>> overrides)
        {
            std::vector<float> v((size_t)numParams());
            for (int i = 0; i < numParams(); ++i) v[(size_t)i] = param(i).def;
            for (const auto& o : overrides) v[(size_t)o.first] = o.second;
            addProgram(name, std::move(v));
        };
        program("Expressive Saw", {});
        program("Bowed Square",   {{kWave, 3}, {kCutoff, 400}, {kReso, 35}, {kZLevel, 100}, {kVelLevel, 20}, {kZCutoff, 3.0f},
                                   {kAttack, 30}, {kRelease, 400}});
        program("Glass Sine",     {{kWave, 0}, {kCutoff, 6000}, {kReso, 5}, {kYCutoff, 0.5f}, {kZCutoff, 0.5f}, {kZLevel, 85},
                                   {kRelease, 900}});
        program("Pluck (vel)",    {{kWave, 2}, {kCutoff, 1200}, {kZLevel, 0}, {kVelLevel, 100}, {kZCutoff, 3.5f},
                                   {kDecay, 350}, {kSustain, 20}, {kRelease, 300}});
        program("Soft Triangle",  {{kWave, 1}, {kCutoff, 2500}, {kReso, 10}, {kZLevel, 60}, {kAttack, 60}, {kRelease, 700},
                                   {kVolume, -8}});

        port_.open("MPE Synth", true, false);
    }

    // ----- lifecycle --------------------------------------------------------
    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        for (Voice& v : voices_)
        {
            v.osc.setSampleRate(sampleRate); v.filter.setSampleRate(sampleRate); v.env.setSampleRate(sampleRate);
        }
        volume_.setTime(10.0f, sampleRate);
        volume_.snap(dbToGain(get(kVolume)));
    }

    void reset() override
    {
        for (Voice& v : voices_) { v.env.reset(); v.filter.reset(); v.held = v.pedalHeld = false; }
        zone_.reset();
        sustain_ = false;
        portSeen_ = false;
        volume_.snap(dbToGain(get(kVolume)));
    }

    // ----- audio ------------------------------------------------------------
    void process(mpc::ProcessContext& ctx) override
    {
        updateBlockSettings(ctx);
        const int input = getInt(kInput);

        // MIDI from our own port: block resolution (it is read once per block).
        uint8_t m[3];
        for (int n = 0, len; n < kMaxPortEventsPerBlock && (len = port_.read(m)) > 0; ++n)
        {
            if ((m[0] & 0xF0) == 0x90 && m[2] > 0) portMask_.fetch_or((unsigned)(1u << (m[0] & 15)), std::memory_order_relaxed);
            if (input == kInTrack) continue;
            if (!portSeen_) { portSeen_ = true; if (input == kInAuto) allNotesOff(); }   // hand over from the track cleanly
            handleMidi(m, len);
        }

        // MIDI from the track: sample accurate.
        const bool useTrack = input == kInTrack || (input == kInAuto && !portSeen_);
        int pos = 0;
        for (int e = 0; e < ctx.numMidiEvents; ++e)
        {
            const mpc::MidiEvent& ev = ctx.midi[e];
            if (ev.size < 2 || ev.data[0] >= 0xF0) continue;
            if (ev.isNoteOn()) trackMask_.fetch_or((unsigned)(1u << ev.channel()), std::memory_order_relaxed);
            if (!useTrack) continue;
            if (ev.sampleOffset > pos) { render(ctx, pos, ev.sampleOffset); pos = ev.sampleOffset; }
            handleMidi(ev.data, ev.size);
        }
        render(ctx, pos, ctx.numFrames);

        float* L = ctx.outputs[0];
        float* R = ctx.numOutputs > 1 ? ctx.outputs[1] : nullptr;
        for (int i = 0; i < ctx.numFrames; ++i)
        {
            L[i] = softClip(L[i] * volume_.process());
            if (R) R[i] = L[i];
        }
    }

    void parameterChanged(int index, float plain) override
    {
        if (index == kPortChs && plain >= 0.5f) portMask_.store(0, std::memory_order_relaxed);
        if (index == kTrackChs && plain >= 0.5f) trackMask_.store(0, std::memory_order_relaxed);
    }

    bool formatValue(int index, float plain, std::string& out) override
    {
        char buf[48];
        switch (index)
        {
            case kPortStatus: out = port_.isOpen() ? "OK" : "No port"; return true;
            case kBendRange:  if (plain < 0.5f) { out = "Auto"; return true; } return false;
            case kPortChs:    mpe::formatChannelMask(portMask_.load(std::memory_order_relaxed), buf, sizeof(buf)); out = buf; return true;
            case kTrackChs:   mpe::formatChannelMask(trackMask_.load(std::memory_order_relaxed), buf, sizeof(buf)); out = buf; return true;
            default: return false;
        }
    }

private:
    enum { kNumVoices = 12, kCtrlInterval = 16, kMaxPortEventsPerBlock = 1024 };

    struct Voice
    {
        PolyBlepOsc osc;
        SVF filter;
        ADSR env;
        int ch = 0, note = 60;
        bool held = false;          // key is down
        bool pedalHeld = false;     // key is up, sustain pedal keeps it sounding
        float velGain = 1.0f;
        float bendTarget = 0.0f, bend = 0.0f;       // semitones
        float yTarget = 0.0f, y = 0.0f;             // -1..1 around the CC 74 centre
        float zTarget = 0.0f, z = 0.0f;             // 0..1
        float levelZ = 0.0f;                        // z smoothed per sample for the amplifier
        uint32_t stamp = 0;
        int ctrl = 0;
    };

    struct Settings { float cutoff, reso, keyTrack, yCut, zCut, zLevel, velLevel, ctlCoeff, ampCoeff; } s_{};

    void updateBlockSettings(mpc::ProcessContext& ctx)
    {
        s_.cutoff = get(kCutoff); s_.reso = get(kReso) * 0.0095f; s_.keyTrack = get(kKeyTrack) * 0.01f;
        s_.yCut = get(kYCutoff); s_.zCut = get(kZCutoff);
        s_.zLevel = get(kZLevel) * 0.01f; s_.velLevel = get(kVelLevel) * 0.01f;
        // One-pole smoothing of the per-note controls: once per control tick for pitch and
        // filter, once per sample for the amplifier (so pressure swells do not zipper).
        const float ms = get(kSmooth);
        s_.ctlCoeff = ms <= 0.01f ? 1.0f : 1.0f - std::exp(-(float)kCtrlInterval / (ms * 0.001f * (float)ctx.sampleRate));
        s_.ampCoeff = 1.0f - std::exp(-1.0f / ((ms < 1.0f ? 1.0f : ms) * 0.001f * (float)ctx.sampleRate));

        const int master = getInt(kMasterCh);
        zone_.setMaster(master == 0 ? 0 : (master == 1 ? 15 : (int)mpe::kNoMaster));
        zone_.setForcedMemberRange(getInt(kBendRange));

        volume_.setTarget(dbToGain(get(kVolume)));
        const int wave = getInt(kWave);
        const float a = get(kAttack), d = get(kDecay), su = get(kSustain) * 0.01f, r = get(kRelease);
        for (Voice& v : voices_) { v.env.set(a, d, su, r); v.osc.setWave(wave); }
    }

    // ----- MIDI -------------------------------------------------------------
    void handleMidi(const uint8_t* m, int len)
    {
        const int status = m[0] & 0xF0, ch = m[0] & 15;
        const int d1 = m[1] & 127, d2 = len > 2 ? (m[2] & 127) : 0;
        switch (status)
        {
            case 0x90: if (d2 > 0) noteOn(ch, d1, d2); else noteOff(ch, d1); break;
            case 0x80: noteOff(ch, d1); break;
            case 0xA0:                                   // polyphonic aftertouch: pressure for one note
                for (Voice& v : voices_) if (v.held && v.ch == ch && v.note == d1) v.zTarget = (float)d2 / 127.0f;
                break;
            case 0xD0:
                zone_.channel(ch).pressure = d1;
                for (Voice& v : voices_) if (v.held && v.ch == ch) v.zTarget = (float)d1 / 127.0f;
                break;
            case 0xE0:
                zone_.channel(ch).bend = (d1 | (d2 << 7)) - 8192;
                updateBends(zone_.isMaster(ch) ? -1 : ch);
                break;
            case 0xB0: controller(ch, d1, d2); break;
            default: break;
        }
    }

    void controller(int ch, int cc, int value)
    {
        if (zone_.handleRpnController(ch, cc, value)) { if (cc == mpe::kCcDataMsb) updateBends(-1); return; }
        const bool global = zone_.isMaster(ch) || zone_.master() == mpe::kNoMaster;
        if (cc == mpe::kCcTimbre)
        {
            zone_.channel(ch).timbre = value;
            for (Voice& v : voices_) if (v.held && v.ch == ch) v.yTarget = timbreToY(value);
        }
        else if (cc == mpe::kCcSustain && global)
        {
            sustain_ = value >= 64;
            if (!sustain_) for (Voice& v : voices_) if (v.pedalHeld) { v.pedalHeld = false; v.env.noteOff(); }
        }
        else if ((cc == mpe::kCcAllNotesOff || cc == mpe::kCcAllSoundOff) && global) allNotesOff();
    }

    static float timbreToY(int cc74) { return mpc::clamp(((float)cc74 - 64.0f) / 63.0f, -1.0f, 1.0f); }

    // ch < 0: every held voice (master bend or a bend-range change).
    void updateBends(int ch)
    {
        for (Voice& v : voices_)
            if (v.held && (ch < 0 || v.ch == ch)) v.bendTarget = zone_.bendSemitones(v.ch);
    }

    void noteOn(int ch, int note, int vel)
    {
        // Free voice first, else the one released longest ago, else the oldest held one.
        int pick = -1;
        for (int i = 0; i < kNumVoices && pick < 0; ++i) if (!voices_[i].env.isActive()) pick = i;
        if (pick < 0)
            for (int i = 0; i < kNumVoices; ++i)
                if (!voices_[i].held && (pick < 0 || voices_[i].stamp < voices_[pick].stamp)) pick = i;
        if (pick < 0)
            for (int i = 0; i < kNumVoices; ++i)
                if (pick < 0 || voices_[i].stamp < voices_[pick].stamp) pick = i;

        Voice& v = voices_[pick];
        const bool fresh = !v.env.isActive();
        v.ch = ch; v.note = note; v.held = true; v.pedalHeld = false; v.stamp = ++clock_;
        v.velGain = (float)vel / 127.0f;
        // Start from the values the controller sent just before the note-on, without gliding.
        v.bendTarget = v.bend = zone_.bendSemitones(ch);
        v.yTarget = v.y = timbreToY(zone_.channel(ch).timbre);
        v.zTarget = v.z = (float)zone_.channel(ch).pressure / 127.0f;
        if (fresh) { v.osc.resetPhase(); v.filter.reset(); v.levelZ = v.z; }
        v.ctrl = 0;
        v.env.noteOn();
    }

    void noteOff(int ch, int note)
    {
        Voice* hit = nullptr;
        for (Voice& v : voices_) if (v.held && v.ch == ch && v.note == note) { hit = &v; break; }
        // Fallback for hosts that change the channel of note-ons but not of note-offs (traced
        // on MPC OS 3.9.1): end the note whatever channel it came in on, so nothing hangs.
        if (hit == nullptr) for (Voice& v : voices_) if (v.held && v.note == note) { hit = &v; break; }
        if (hit == nullptr) return;
        hit->held = false;
        if (sustain_) hit->pedalHeld = true; else hit->env.noteOff();
    }

    void allNotesOff()
    {
        for (Voice& v : voices_) { v.held = v.pedalHeld = false; v.env.noteOff(); }
        sustain_ = false;
    }

    // ----- rendering --------------------------------------------------------
    void render(mpc::ProcessContext& ctx, int from, int to)
    {
        float* out = ctx.outputs[0];
        const int n = to - from;
        if (n <= 0) return;
        for (Voice& v : voices_)
            if (v.env.isActive()) renderVoice(v, out + from, n);
    }

    void renderVoice(Voice& v, float* out, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            if ((v.ctrl++ % kCtrlInterval) == 0)
            {
                v.bend += s_.ctlCoeff * (v.bendTarget - v.bend);
                v.y    += s_.ctlCoeff * (v.yTarget - v.y);
                v.z    += s_.ctlCoeff * (v.zTarget - v.z);
                const float pitch = (float)v.note + v.bend;
                v.osc.setFrequency(midiToHz(pitch));
                const float octaves = s_.yCut * v.y + s_.zCut * v.z + s_.keyTrack * (pitch - 60.0f) / 12.0f;
                v.filter.set(s_.cutoff * std::pow(2.0f, mpc::clamp(octaves, -10.0f, 10.0f)), s_.reso);
            }
            v.levelZ += s_.ampCoeff * (v.zTarget - v.levelZ);
            const float env = v.env.process();
            // Loudness: velocity sets the base level; Z>Level crossfades towards pressure.
            const float vel = 1.0f - s_.velLevel + s_.velLevel * v.velGain;
            const float gain = vel * ((1.0f - s_.zLevel) + s_.zLevel * v.levelZ);
            out[i] += v.filter.process(v.osc.process(), SVF::Mode::Lowpass) * env * gain * 0.35f;
        }
    }

    Voice voices_[kNumVoices];
    mpe::Zone zone_;
    mpe::AlsaMidiPort port_;
    Smoother volume_;
    uint32_t clock_ = 0;
    bool sustain_ = false;
    bool portSeen_ = false;
    std::atomic<unsigned> portMask_{0}, trackMask_{0};
};

MPE_PLUGIN_ENTRY(MPESynth)
