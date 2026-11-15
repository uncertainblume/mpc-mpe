#!/usr/bin/env python3
"""Read an MPC project straight off the device, and make edited copies of it.

    tools/mpc_project.py show <address> "<Project name>"
        Tracks with their plugin, MIDI input/output, monitor mode and "send MPE" switch;
        warnings about risky routing; and, for each recorded clip, how many notes have no
        pressure data recorded for them.

    tools/mpc_project.py ports <address> "<Project name>" "<New name>"
        For bridge 0.3.0: switches every MPE Bridge that is in Rotate mode to "Out Ports: Per
        Voice", and points each instrument track that listens to "<bridge> MIDI Out" on one
        channel at that bridge's "Ch N Out" port instead (channel Any). Writes a copy.

    tools/mpc_project.py copy <address> "<Project name>" "<New name>" [--send-mpe PLUGIN[,PLUGIN...]]
                                                       [--input "TRACK=PORT" ...]
        Writes "<New name>.xpj" next to the original (which is never changed) and copies its
        [ProjectData] folder. --send-mpe turns the MPC's per-program "send MPE" switch on for
        tracks whose plugin name contains one of the given words. --input sets a track's MIDI
        input port by track name.

A project file (.xpj) is gzip-compressed text: five header lines, then JSON. This format is
not documented by Akai; it was worked out from a 3.10.1.12 project and may change. Open an
edited copy on the MPC before trusting it, and keep the original.

Needs developer mode (SSH as root with no password) on the MPC.
"""
import argparse, collections, gzip, json, re, shlex, subprocess, sys

PROJECTS = "/nvme/MPC Documents/Projects"
MONITOR = {0: "Off", 1: "In", 2: "Auto", 3: "Merge"}
PARAM = {128: "bend", 129: "pressure"}


def ssh(address, command, data=None):
    r = subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8", "root@" + address, command],
                       input=data, capture_output=True)
    if r.returncode != 0:
        sys.exit("ssh failed: " + r.stderr.decode(errors="replace").strip())
    return r.stdout


def load(address, name):
    raw = gzip.decompress(ssh(address, "cat " + shlex.quote("%s/%s.xpj" % (PROJECTS, name))))
    cut = raw.index(b"{")
    return raw[:cut], json.loads(raw[cut:])


def plugin_name(track):
    plugin = track.get("program", {}).get("plugin") if isinstance(track.get("program"), dict) else None
    if not isinstance(plugin, dict):
        return None, None
    m = re.search(r'"(?:name|pluginName|descriptiveName)": "([^"]+)"', json.dumps(plugin.get("plugin", {})))
    return (m.group(1) if m else "?"), plugin


def clips(data):
    def walk(o):
        if isinstance(o, dict):
            if "eventList" in o:
                ev = o["eventList"]
                ev = ev.get("events") if isinstance(ev, dict) else ev
                if ev:
                    yield o.get("name"), ev
            for v in o.values():
                yield from walk(v)
        elif isinstance(o, list):
            for v in o:
                yield from walk(v)
    return walk(data)


_B64 = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"


def juce_decode(text):
    """JUCE MemoryBlock::fromBase64Encoding: "<size>.<6 bits at a time, low bits first>"."""
    size, _, body = text.partition(".")
    acc = bits = 0
    out = bytearray()
    for ch in body:
        acc |= _B64.index(ch) << bits
        bits += 6
        while bits >= 8:
            out.append(acc & 255); acc >>= 8; bits -= 8
    return bytes(out[:int(size)])


def juce_encode(data):
    acc = bits = 0
    out = []
    for b in data:
        acc |= b << bits
        bits += 8
        while bits >= 6:
            out.append(_B64[acc & 63]); acc >>= 6; bits -= 6
    if bits:
        out.append(_B64[acc & 63])
    return "%d.%s" % (len(data), "".join(out))


def bridge_state_params(blob):
    """(offset of the first entry, count, {name: (value offset, value)}) of a bridge's saved state."""
    import struct
    i = blob.index(b"MPCP")
    count = struct.unpack_from("<I", blob, i + 8)[0]
    pos = i + 12
    found = {}
    for _ in range(count):
        n = blob[pos]; name = blob[pos + 1:pos + 1 + n].decode()
        found[name] = (pos + 1 + n, struct.unpack_from("<f", blob, pos + 1 + n)[0])
        pos += 1 + n + 8
    return i, count, pos, found


def set_bridge_ports(plugin):
    """Out Ports = Per Voice in a Rotate bridge's saved state. Returns True if changed."""
    import struct
    blob = juce_decode(plugin["state"])
    if juce_encode(blob) != plugin["state"]:
        return False
    i, count, end, found = bridge_state_params(blob)
    if found.get("mode", (0, 0))[1] != 1.0:
        return False
    if "outports" in found:
        off = found["outports"][0]
        blob = blob[:off] + struct.pack("<ff", 1.0, 1.0) + blob[off + 8:]
    else:
        entry = bytes([8]) + b"outports" + struct.pack("<ff", 1.0, 1.0)
        blob = blob[:end] + entry + blob[end:]
        blob = blob[:i + 8] + struct.pack("<I", count + 1) + blob[i + 12:]
        size = struct.unpack(">I", blob[i - 4:i])[0] + len(entry)
        blob = blob[:i - 4] + struct.pack(">I", size) + blob[i:]
    plugin["state"] = juce_encode(blob)
    plugin["presetEdited"] = True
    return True


def ports(address, name, new):
    header, doc = load(address, name)
    changed = []
    rotate_bridges = 0
    for t in doc["data"]["tracks"]:
        pname, plugin = plugin_name(t)
        if pname == "MPE Bridge" and set_bridge_ports(t["program"]["plugin"]["plugin"]):
            rotate_bridges += 1
            changed.append("%s: Out Ports set to Per Voice" % t["name"])
    for t in doc["data"]["tracks"]:
        pname, plugin = plugin_name(t)
        route = t["midiInputRoute"]
        dev = route["inputPort"]["deviceName"]
        if pname is None or "Bridge" in pname or not dev.startswith("MPE Bridge") or not dev.endswith(" MIDI Out"):
            continue
        if route["inputChannel"] == 0:
            continue                                  # listens to every channel: not a Rotate copy
        client = dev[:-len(" MIDI Out")]
        route["inputPort"]["deviceName"] = "%s Ch %d Out" % (client, route["inputChannel"])
        route["inputPort"]["deviceId"] = ""
        changed.append("%s: input %s ch %d -> %s" % (t["name"], dev, route["inputChannel"], route["inputPort"]["deviceName"]))
        route["inputChannel"] = 0
    if not rotate_bridges:
        sys.exit("No bridge in Rotate mode found; no copy written.")
    write_copy(address, name, new, header, doc)
    print('Wrote "%s" on the MPC:' % new)
    for c in changed:
        print("  " + c)


def show(address, name):
    _, doc = load(address, name)
    data = doc["data"]
    print("Tracks with plugins")
    bridge_ports = False
    rows = []
    for t in data["tracks"]:
        pname, plugin = plugin_name(t)
        if pname is None:
            continue
        port = t["midiInputRoute"]["inputPort"]["deviceName"]
        ch = t["midiInputRoute"]["inputChannel"]
        rows.append((t["name"], pname, port, "All" if ch == 0 else str(ch),
                     MONITOR.get(t["midiMonitorable"]["state"], "?"), "on" if plugin.get("mpe", {}).get("sendMPE") else "off"))
        bridge_ports = bridge_ports or "Bridge" in pname
    for r in rows:
        print("  %-14s %-12s in: %-26s ch %-3s monitor %-5s send MPE %s" % r)
    for r in rows:
        if r[2] == "All Ports" and bridge_ports:
            print("  WARNING  %s listens to All Ports, which includes the bridge's own output." % r[0])
    print("\nClips with notes")
    for cname, ev in clips(data):
        notes = [e for e in ev if e["type"] == 3]
        if not notes:
            continue
        press = collections.defaultdict(list)
        for e in ev:
            if e["type"] == 1 and e["automation"]["parameter"] == 129:
                press[e["channel"]].append(e["time"])
        has_press = bool(press)
        bare = sum(1 for n in notes
                   if not any(n["time"] < t <= n["time"] + n["note"]["length"] for t in press[n["channel"]]))
        channels = sorted({n["channel"] + 1 for n in notes})
        if len(channels) == 1 and not has_press:
            continue                                  # an ordinary one-channel clip
        print("  %-28s %5d events, %3d notes on channels %s, %d notes with no pressure data"
              % (cname, len(ev), len(notes), channels, bare if has_press else len(notes)))


def write_copy(address, name, new, header, doc):
    target = "%s/%s.xpj" % (PROJECTS, new)
    if ssh(address, "test -e %s && echo yes || true" % shlex.quote(target)).strip():
        sys.exit('"%s" already exists on the MPC; choose another name.' % new)
    body = header + json.dumps(doc, indent=0, ensure_ascii=False).encode()
    ssh(address, "cat > " + shlex.quote(target), gzip.compress(body))
    src = "%s/%s_[ProjectData]" % (PROJECTS, name)
    dst = "%s/%s_[ProjectData]" % (PROJECTS, new)
    ssh(address, "if [ -d %s ]; then cp -r %s %s; fi" % (shlex.quote(src), shlex.quote(src), shlex.quote(dst)))


def copy(address, name, new, send_mpe, inputs):
    header, doc = load(address, name)
    changed = []
    for t in doc["data"]["tracks"]:
        pname, plugin = plugin_name(t)
        if pname is None:
            continue
        if any(w.lower() in pname.lower() for w in send_mpe) and "mpe" in plugin:
            plugin["mpe"]["sendMPE"] = True
            changed.append("send MPE on: %s (%s)" % (t["name"], pname))
        for track, port in inputs:
            if t["name"] == track:
                t["midiInputRoute"]["inputPort"]["deviceName"] = port
                t["midiInputRoute"]["inputPort"]["deviceId"] = ""
                changed.append("input of %s set to %s" % (track, port))
    if not changed:
        sys.exit("Nothing matched; no copy written.")
    write_copy(address, name, new, header, doc)
    print('Wrote "%s" on the MPC:' % new)
    for c in changed:
        print("  " + c)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("show"); s.add_argument("address"); s.add_argument("project")
    c = sub.add_parser("copy"); c.add_argument("address"); c.add_argument("project"); c.add_argument("new")
    c.add_argument("--send-mpe", default=""); c.add_argument("--input", action="append", default=[])
    q = sub.add_parser("ports"); q.add_argument("address"); q.add_argument("project"); q.add_argument("new")
    a = ap.parse_args()
    if a.cmd == "ports":
        ports(a.address, a.project, a.new)
    elif a.cmd == "show":
        show(a.address, a.project)
    else:
        copy(a.address, a.project, a.new, [w for w in a.send_mpe.split(",") if w],
             [tuple(i.split("=", 1)) for i in a.input])


if __name__ == "__main__":
    main()
