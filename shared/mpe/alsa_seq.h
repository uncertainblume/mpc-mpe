// alsa_seq.h - a plugin's own ALSA sequencer MIDI ports on the MPC.
//
// SPDX-License-Identifier: MIT
//
// Why this exists: the MPC's plugin host hands a plugin its track's MIDI with the channel
// information damaged (note-ons arrive on channel 1) and throws away any MIDI the plugin
// sends back through the plugin interface. MPE needs both to work. A plugin can, however,
// open its own ALSA sequencer client. The MPC notices new sequencer ports by itself and
// lists them next to the hardware ports:
//
//   - a WRITABLE port ("<client> MIDI In")  shows up as a track's MIDI OUTPUT port
//   - a READABLE port ("<client> MIDI Out") shows up as a track's MIDI INPUT port
//
// and data on those ports keeps its MIDI channel. See docs/research.md for the evidence.
//
// libasound is loaded with dlopen() so nothing here needs ALSA headers or link flags, and
// the same binary runs (without ports) on a computer that has no ALSA at all. The event
// struct below is the kernel's snd_seq_event (include/uapi/sound/asequencer.h), which is
// stable ABI; tests/test_alsa_abi.cpp checks it against the real headers when they exist.
//
// Threading: open()/close() on the UI thread (constructor/destructor). read()/send() on
// the audio thread: both are non-blocking system calls, no allocation, no locks.

#ifndef MPE_ALSA_SEQ_H
#define MPE_ALSA_SEQ_H

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#if defined(__linux__)
  #include <dlfcn.h>
  #define MPE_HAVE_DLOPEN 1
#else
  #define MPE_HAVE_DLOPEN 0
#endif

namespace mpe {

// ---- kernel ABI ------------------------------------------------------------
struct SeqAddr { uint8_t client, port; };
struct SeqEvNote { uint8_t channel, note, velocity, offVelocity; uint32_t duration; };
struct SeqEvCtrl { uint8_t channel, unused[3]; uint32_t param; int32_t value; };
struct SeqEvent
{
    uint8_t type, flags, tag, queue;
    uint32_t time[2];
    SeqAddr source, dest;
    union { SeqEvNote note; SeqEvCtrl control; uint8_t raw[12]; } data;
};
static_assert(sizeof(SeqEvent) == 28, "snd_seq_event must be 28 bytes");

enum : uint8_t
{
    kSeqNoteOn = 6, kSeqNoteOff = 7, kSeqKeyPress = 8, kSeqController = 10, kSeqPgmChange = 11,
    kSeqChanPress = 12, kSeqPitchBend = 13,
    kSeqQueueDirect = 253, kSeqAddrUnknown = 253, kSeqAddrSubscribers = 254
};
enum : unsigned
{
    kSeqCapRead = 1u << 0, kSeqCapWrite = 1u << 1, kSeqCapSubsRead = 1u << 5, kSeqCapSubsWrite = 1u << 6,
    kSeqTypeMidiGeneric = 1u << 1, kSeqTypeApplication = 1u << 20,
    kSeqOpenDuplex = 3, kSeqNonblock = 1
};

// ---- conversion between sequencer events and MIDI bytes --------------------
// Returns the message length (2 or 3), or 0 for events that are not channel voice messages.
inline int seqEventToMidi(const SeqEvent& ev, uint8_t m[3])
{
    const uint8_t nch = ev.data.note.channel & 15, cch = ev.data.control.channel & 15;
    switch (ev.type)
    {
        case kSeqNoteOn:   m[0] = 0x90 | nch; m[1] = ev.data.note.note & 127; m[2] = ev.data.note.velocity & 127; return 3;
        case kSeqNoteOff:  m[0] = 0x80 | nch; m[1] = ev.data.note.note & 127; m[2] = ev.data.note.velocity & 127; return 3;
        case kSeqKeyPress: m[0] = 0xA0 | nch; m[1] = ev.data.note.note & 127; m[2] = ev.data.note.velocity & 127; return 3;
        case kSeqController:
            m[0] = 0xB0 | cch; m[1] = (uint8_t)(ev.data.control.param & 127); m[2] = (uint8_t)(ev.data.control.value & 127); return 3;
        case kSeqPgmChange: m[0] = 0xC0 | cch; m[1] = (uint8_t)(ev.data.control.value & 127); m[2] = 0; return 2;
        case kSeqChanPress: m[0] = 0xD0 | cch; m[1] = (uint8_t)(ev.data.control.value & 127); m[2] = 0; return 2;
        case kSeqPitchBend:
        {
            int v = ev.data.control.value + 8192;
            if (v < 0) v = 0; else if (v > 16383) v = 16383;
            m[0] = 0xE0 | cch; m[1] = (uint8_t)(v & 127); m[2] = (uint8_t)(v >> 7); return 3;
        }
        default: return 0;
    }
}

// Fills ev from a channel voice message. Returns false for anything else.
inline bool midiToSeqEvent(const uint8_t* m, SeqEvent& ev)
{
    std::memset(&ev, 0, sizeof(ev));
    const uint8_t ch = m[0] & 15;
    switch (m[0] & 0xF0)
    {
        case 0x90: ev.type = kSeqNoteOn;   ev.data.note.channel = ch; ev.data.note.note = m[1]; ev.data.note.velocity = m[2]; return true;
        case 0x80: ev.type = kSeqNoteOff;  ev.data.note.channel = ch; ev.data.note.note = m[1]; ev.data.note.velocity = m[2]; return true;
        case 0xA0: ev.type = kSeqKeyPress; ev.data.note.channel = ch; ev.data.note.note = m[1]; ev.data.note.velocity = m[2]; return true;
        case 0xB0: ev.type = kSeqController; ev.data.control.channel = ch; ev.data.control.param = m[1]; ev.data.control.value = m[2]; return true;
        case 0xC0: ev.type = kSeqPgmChange;  ev.data.control.channel = ch; ev.data.control.value = m[1]; return true;
        case 0xD0: ev.type = kSeqChanPress;  ev.data.control.channel = ch; ev.data.control.value = m[1]; return true;
        case 0xE0: ev.type = kSeqPitchBend;  ev.data.control.channel = ch; ev.data.control.value = (m[1] | (m[2] << 7)) - 8192; return true;
        default: return false;
    }
}

// ---- the port ---------------------------------------------------------------
class AlsaMidiPort
{
public:
    AlsaMidiPort() = default;
    ~AlsaMidiPort() { close(); }
    AlsaMidiPort(const AlsaMidiPort&) = delete;
    AlsaMidiPort& operator=(const AlsaMidiPort&) = delete;

    // Opens a sequencer client called `clientName` (a number is appended for the second and
    // later instances of the same plugin) with a "MIDI In" port and/or a "MIDI Out" port.
    // Returns false, leaving everything closed, when ALSA is not available.
    bool open(const char* clientName, bool wantInput, bool wantOutput)
    {
        close();
#if MPE_HAVE_DLOPEN
        if (!api().load()) return false;
        void* seq = nullptr;
        if (api().open(&seq, "default", (int)kSeqOpenDuplex, (int)kSeqNonblock) < 0 || seq == nullptr) return false;

        const int n = instanceCounter().fetch_add(1, std::memory_order_relaxed) + 1;
        if (n > 1) std::snprintf(name_, sizeof(name_), "%s %d", clientName, n);
        else       std::snprintf(name_, sizeof(name_), "%s", clientName);
        api().setName(seq, name_);

        const unsigned type = kSeqTypeMidiGeneric | kSeqTypeApplication;
        if (wantInput)  inPort_  = api().createPort(seq, "MIDI In",  kSeqCapWrite | kSeqCapSubsWrite, type);
        if (wantOutput) outPort_ = api().createPort(seq, "MIDI Out", kSeqCapRead | kSeqCapSubsRead, type);
        if ((wantInput && inPort_ < 0) || (wantOutput && outPort_ < 0))
        {
            api().close(seq); inPort_ = outPort_ = -1; name_[0] = 0; return false;
        }
        seq_ = seq;
        return true;
#else
        (void)clientName; (void)wantInput; (void)wantOutput;
        return false;
#endif
    }

    void close()
    {
#if MPE_HAVE_DLOPEN
        if (seq_ != nullptr) api().close(seq_);
#endif
        seq_ = nullptr; inPort_ = outPort_ = -1;
    }

    bool isOpen() const { return seq_ != nullptr; }
    bool hasInput() const { return seq_ != nullptr && inPort_ >= 0; }
    bool hasOutput() const { return seq_ != nullptr && outPort_ >= 0; }
    const char* clientName() const { return name_; }

    // Next channel voice message waiting at "MIDI In". Returns its length, or 0 if none.
    int read(uint8_t m[3])
    {
#if MPE_HAVE_DLOPEN
        if (!hasInput()) return 0;
        for (int tries = 0; tries < 64; ++tries)
        {
            SeqEvent* ev = nullptr;
            const int r = api().input(seq_, &ev);
            if (r == -28) continue;            // -ENOSPC: the kernel queue overran and was emptied
            if (r < 0 || ev == nullptr) return 0;   // -EAGAIN: nothing waiting
            const int n = seqEventToMidi(*ev, m);
            if (n > 0) return n;               // anything else (subscriptions, clock, sysex) is skipped
        }
#else
        (void)m;
#endif
        return 0;
    }

    // Sends one channel voice message to whoever is subscribed to "MIDI Out" (the MPC).
    bool send(const uint8_t* m)
    {
#if MPE_HAVE_DLOPEN
        if (!hasOutput()) return false;
        SeqEvent ev;
        if (!midiToSeqEvent(m, ev)) return false;
        ev.queue = kSeqQueueDirect;
        ev.source.port = (uint8_t)outPort_;
        ev.dest.client = kSeqAddrSubscribers;
        ev.dest.port = kSeqAddrUnknown;
        return api().outputDirect(seq_, &ev) >= 0;
#else
        (void)m;
        return false;
#endif
    }

private:
#if MPE_HAVE_DLOPEN
    struct Api
    {
        int state = 0;   // 0 = not tried, 1 = usable, -1 = not available
        int (*open)(void**, const char*, int, int) = nullptr;
        int (*setName)(void*, const char*) = nullptr;
        int (*createPort)(void*, const char*, unsigned, unsigned) = nullptr;
        int (*input)(void*, SeqEvent**) = nullptr;
        int (*outputDirect)(void*, SeqEvent*) = nullptr;
        int (*close)(void*) = nullptr;

        bool load()
        {
            if (state != 0) return state == 1;
            void* h = dlopen("libasound.so.2", RTLD_NOW);
            if (h == nullptr) { state = -1; return false; }
            *(void**)(&open)         = dlsym(h, "snd_seq_open");
            *(void**)(&setName)      = dlsym(h, "snd_seq_set_client_name");
            *(void**)(&createPort)   = dlsym(h, "snd_seq_create_simple_port");
            *(void**)(&input)        = dlsym(h, "snd_seq_event_input");
            *(void**)(&outputDirect) = dlsym(h, "snd_seq_event_output_direct");
            *(void**)(&close)        = dlsym(h, "snd_seq_close");
            state = (open && setName && createPort && input && outputDirect && close) ? 1 : -1;
            return state == 1;
        }
    };
    static Api& api() { static Api a; return a; }
#endif
    static std::atomic<int>& instanceCounter() { static std::atomic<int> c{0}; return c; }

    void* seq_ = nullptr;
    int inPort_ = -1, outPort_ = -1;
    char name_[64] = {0};
};

} // namespace mpe

#endif // MPE_ALSA_SEQ_H
