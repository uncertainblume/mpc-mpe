// MPESynth.cpp - MPE Synth: a 12-voice synthesizer for the MPC that is MPE from the ground up.
//
// SPDX-License-Identifier: MIT
//
// Every note has its own pitch bend (X), its own timbre (Y, CC 74) and its own pressure (Z),
// taken from the MIDI channel the note arrived on.
//
// Signal path per voice:
//   PolyBLEP oscillator (pitch = note + that note's bend)
//     + optional second, detuned copy (Spread) + sine one octave down (Sub) + white noise
//     -> Drive (tanh saturation)
//     -> low-pass SVF (cutoff + Y amount + Z amount + key tracking)
//     -> amp ADSR * (velocity blended with that note's pressure, through Z Curve)
//     -> a fixed place in the stereo field, scaled by Width
//   all voices summed -> master volume -> soft clip -> left and right.
//
// Where the MIDI comes from:
//   the plugin's track   On MPC 3.10.1 (measured on a Live III) note-ons arrive on their own
//                        channels, so MPE works with the controller as the track's input.
//                        On MPC OS 3.9.1 they arrived on channel 1, which breaks MPE.
//   "MPE Synth MIDI In"  the plugin's own MIDI port, for when the track loses channels:
//                        choose it as the MIDI OUTPUT port of the track that holds the MPE
//                        performance, with the output channel set to Any.
//   Input = Auto uses the port as soon as anything arrives there, and the track until then.
//   The SETUP page shows which channels came in by each route.
//
// Version 0.2.0 appended Sub, Spread, Noise, Drive, Z Curve and Width. Their defaults are
// neutral, so sounds saved with the first version are unchanged.

#include "mpc_plugin.h"
#include "dsp/math.h"
#include "dsp/oscillator.h"
#include "dsp/noise.h"
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
        kSub, kSpread, kNoise, kDrive, kZCurve, kWidth,     // added in 0.2.0
        kNumParams
    };
    enum InputSource { kInAuto = 0, kInTrack = 1, kInPort = 2 };

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "MPE Synth";
        i.vendor = "MPE Tools";
        i.uniqueId = MPC_FOURCC('M', 'p', 'S', 'y');   // must match "uid" in plugin.json
        i.version = 201;            // 0.2.1
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
        addParam(floatParam ("sub",      "Sub", 0.0f, 100.0f, 0.0f, "%", 0));          // sine one octave down
        addParam(floatParam ("spread",   "Spread", 0.0f, 50.0f, 0.0f, "ct", 0));       // detune between two copies; 0 = one oscillator
        addParam(floatParam ("noise",    "Noise", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam ("drive",    "Drive", 0.0f, 24.0f, 0.0f, "dB", 1));        // saturation before the filter
        addParam(choiceParam("zcurve",   "Z Curve", {"Soft", "Linear", "Hard"}, 1));   // how pressure maps to its effect
        addParam(floatParam ("width",    "Width", 0.0f, 100.0f, 0.0f, "%", 0));        // 0 = mono
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
        // Added in 0.2.0. New programs go at the end: projects store the program number.
        program("Wide Unison",    {{kSpread, 14}, {kWidth, 80}, {kSub, 25}, {kCutoff, 900}, {kZCutoff, 2.5f}, {kRelease, 500}});
        program("Driven Reed",    {{kWave, 3}, {kDrive, 14}, {kCutoff, 500}, {kReso, 30}, {kZLevel, 100}, {kVelLevel, 10},
                                   {kZCurve, 2}, {kZCutoff, 3.5f}, {kAttack, 15}, {kVolume, -14}});
        program("Breath Flute",   {{kWave, 1}, {kNoise, 18}, {kCutoff, 1800}, {kReso, 15}, {kZLevel, 100}, {kVelLevel, 0},
                                   {kZCurve, 0}, {kYCutoff, 1.5f}, {kAttack, 40}, {kRelease, 350}, {kWidth, 40}});
        program("Sub Bass",       {{kWave, 2}, {kSub, 90}, {kCutoff, 300}, {kReso, 20}, {kKeyTrack, 30}, {kDrive, 6},
                                   {kZCutoff, 3.0f}, {kZLevel, 40}, {kVolume, -12}});

        port_.open("MPE Synth", true, false);
    }

    // ----- lifecycle --------------------------------------------------------
    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        for (Voice& v : voices_)
        {
            v.osc.setSampleRate(sampleRate); v.osc2.setSampleRate(sampleRate); v.sub.setSampleRate(sampleRate);
            v.filter.setSampleRate(sampleRate); v.env.setSampleRate(sampleRate);
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

        // A hardware MIDI device vanished (unplugged, or dropped off USB): release its notes.
        if (port_.takeDeviceGone()) allNotesOff();

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
            const float g = volume_.process();
            L[i] = softClip(L[i] * g);
            if (R) R[i] = softClip(R[i] * g);
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
        PolyBlepOsc osc, osc2;      // osc2 only runs when Spread is above zero
        WavetableSine sub;
        WhiteNoise noise;
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
        float pan = 0.0f;                           // fixed place of this voice, -1..1, scaled by Width
        float gainL = 1.0f, gainR = 1.0f;
        uint32_t stamp = 0;
        int ctrl = 0;
    };

    struct Settings
    {
        float cutoff, reso, keyTrack, yCut, zCut, zLevel, velLevel, ctlCoeff, ampCoeff;
        float sub, noise, driveGain, width, spreadUp, spreadDown, oscGain;
        int zCurve;
        bool spreadOn, driveOn;
    } s_{};

    // Pressure response: Soft rises quickly then flattens, Hard needs a firm press.
    static float shapeZ(float z, int curve) { return curve == 0 ? z * (2.0f - z) : (curve == 2 ? z * z : z); }

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
        for (Voice& v : voices_) { v.env.set(a, d, su, r); v.osc.setWave(wave); v.osc2.setWave(wave); }

        s_.sub = get(kSub) * 0.01f; s_.noise = get(kNoise) * 0.01f * 0.5f; s_.width = get(kWidth) * 0.01f;
        const float cents = get(kSpread);
        s_.spreadOn = cents > 0.05f;
        s_.spreadUp = centsToRatio(cents * 0.5f); s_.spreadDown = centsToRatio(-cents * 0.5f);
        s_.oscGain = s_.spreadOn ? 0.7f : 1.0f;      // two copies sum to about the level of one
        const float driveDb = get(kDrive);
        s_.driveOn = driveDb > 0.05f;
        s_.driveGain = dbToGain(driveDb);
        s_.zCurve = getInt(kZCurve);
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
        if (fresh) { v.osc.resetPhase(); v.osc2.resetPhase(0.37f); v.sub.resetPhase(); v.filter.reset(); v.levelZ = v.z; }
        // Voices sit at fixed places left and right, further out in turn: -1/3, +1/3, -2/3, +2/3, -1, +1.
        v.pan = ((pick & 1) ? 1.0f : -1.0f) * (float)((pick / 2) % 3 + 1) * (1.0f / 3.0f);
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
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : nullptr;
        const int n = to - from;
        if (n <= 0) return;
        for (Voice& v : voices_)
            if (v.env.isActive()) renderVoice(v, outL + from, outR ? outR + from : nullptr, n);
    }

    void renderVoice(Voice& v, float* outL, float* outR, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            if ((v.ctrl++ % kCtrlInterval) == 0)
            {
                v.bend += s_.ctlCoeff * (v.bendTarget - v.bend);
                v.y    += s_.ctlCoeff * (v.yTarget - v.y);
                v.z    += s_.ctlCoeff * (v.zTarget - v.z);
                const float pitch = (float)v.note + v.bend;
                const float hz = midiToHz(pitch);
                if (s_.spreadOn) { v.osc.setFrequency(hz * s_.spreadUp); v.osc2.setFrequency(hz * s_.spreadDown); }
                else v.osc.setFrequency(hz);
                v.sub.setFrequency(hz * 0.5f);
                const float octaves = s_.yCut * v.y + s_.zCut * shapeZ(v.z, s_.zCurve) + s_.keyTrack * (pitch - 60.0f) / 12.0f;
                v.filter.set(s_.cutoff * std::pow(2.0f, mpc::clamp(octaves, -10.0f, 10.0f)), s_.reso);
                // sqrt(1 -+ p): both sides are 1 in the middle, so Width 0 is the old mono level.
                const float p = v.pan * s_.width;
                v.gainL = std::sqrt(1.0f - p); v.gainR = std::sqrt(1.0f + p);
            }
            v.levelZ += s_.ampCoeff * (v.zTarget - v.levelZ);
            const float env = v.env.process();
            // Loudness: velocity sets the base level; Z>Level crossfades towards pressure.
            const float vel = 1.0f - s_.velLevel + s_.velLevel * v.velGain;
            const float gain = vel * ((1.0f - s_.zLevel) + s_.zLevel * shapeZ(v.levelZ, s_.zCurve));
            float x = v.osc.process();
            if (s_.spreadOn) x += v.osc2.process();
            x *= s_.oscGain;
            if (s_.sub > 0.0f) x += s_.sub * v.sub.process();
            if (s_.noise > 0.0f) x += s_.noise * v.noise.process();
            if (s_.driveOn) x = fastTanh(x * s_.driveGain);
            const float y = v.filter.process(x, SVF::Mode::Lowpass) * env * gain * 0.35f;
            if (outR) { outL[i] += y * v.gainL; outR[i] += y * v.gainR; }
            else outL[i] += y;
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
