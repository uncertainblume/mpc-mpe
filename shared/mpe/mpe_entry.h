// mpe_entry.h - plugin entry point that also tells the host "yes, I understand MPE".
//
// SPDX-License-Identifier: MIT
//
// The MPC's plugin host asks every plugin the capability question "MPE" (see the SDK's
// docs/host-contract.md). The SDK framework leaves it at "don't know". Answering yes is
// the conventional way a VST2 instrument asks a host for per-channel note data, and it is
// simply true for these plugins. What the MPC does differently for a plugin that answers
// yes is not documented; both plugins show which channels actually reach them through
// the track so this can be seen on the device (docs/research.md, "Open questions").
//
// Use MPE_PLUGIN_ENTRY(Class) instead of the SDK's MPC_PLUGIN_ENTRY(Class).

#ifndef MPE_ENTRY_H
#define MPE_ENTRY_H

#include <cstring>

#include "mpc_plugin.h"

namespace mpe {

inline VstIntPtr mpeDispatcher(AEffect* e, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt)
{
    if (opcode == effCanDo && ptr != nullptr && std::strcmp(static_cast<const char*>(ptr), "MPE") == 0) return 1;
    return mpc::detail::dispatcher(e, opcode, index, value, ptr, opt);
}

inline AEffect* createMpeEffect(mpc::Plugin* plugin, audioMasterCallback host)
{
    AEffect* e = mpc::detail::createEffect(plugin, host);
    if (e != nullptr) e->dispatcher = &mpeDispatcher;
    return e;
}

// "2-9" / "1" / "1-3,7" / "none": the channels set in a 16-bit mask, 1-based, for status text.
inline void formatChannelMask(unsigned mask, char* out, size_t size)
{
    if (size == 0) return;
    out[0] = 0;
    if ((mask & 0xFFFF) == 0) { std::snprintf(out, size, "none"); return; }
    size_t pos = 0;
    for (int c = 0; c < 16;)
    {
        if (!(mask & (1u << c))) { ++c; continue; }
        int end = c;
        while (end + 1 < 16 && (mask & (1u << (end + 1)))) ++end;
        char part[16];
        if (end > c) std::snprintf(part, sizeof(part), "%s%d-%d", pos ? "," : "", c + 1, end + 1);
        else         std::snprintf(part, sizeof(part), "%s%d", pos ? "," : "", c + 1);
        const size_t len = std::strlen(part);
        if (pos + len + 1 > size) break;
        std::memcpy(out + pos, part, len + 1);
        pos += len;
        c = end + 1;
    }
}

} // namespace mpe

#define MPE_PLUGIN_ENTRY(PluginClass)                                                        \
    extern "C" MPC_EXPORT AEffect* VSTPluginMain(audioMasterCallback host)                   \
    {                                                                                        \
        if (host != nullptr && host(nullptr, audioMasterVersion, 0, 0, nullptr, 0.0f) == 0)  \
            return nullptr;                                                                  \
        return mpe::createMpeEffect(new PluginClass(), host);                                \
    }

#endif // MPE_ENTRY_H
