#!/usr/bin/env python3
"""Record the LinnStrument and an MPE Bridge output port at the same time, then compare them.

    tools/capture_midi.py <address> [--seconds 20] [--bridge "MPE Bridge 2"] [--source "LinnStrument"]

Run it, then play a fast passage during the countdown. It reads the ALSA sequencer client
list on the MPC, records both ports with arecordmidi (read-only, nothing on the device is
changed), prints note counts per channel for each, and lists every note the controller sent
that has no matching note-on at the bridge output within 50 ms. It also samples CPU once.

"Missing at the output" points at the bridge or the MPC's routing; "present at the output"
means the bridge did its job and any silence is further downstream (an instrument copy, CPU).

Needs developer mode (SSH as root with no password) on the MPC. Run the plugins' project first.
"""
import argparse, re, subprocess, sys, tempfile, os

def ssh(addr, cmd, timeout=60):
    r = subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8", "root@" + addr, cmd],
                       capture_output=True, timeout=timeout)
    return r.stdout

def find_port(clients, client_name, port_name=None):
    """Return 'client:port' for the first client whose name matches exactly (then the port)."""
    cur = None
    for line in clients.splitlines():
        m = re.match(r'Client\s+(\d+) : "(.*?)"', line)
        if m:
            cur = (int(m.group(1)), m.group(2)); continue
        p = re.match(r'\s+Port\s+(\d+) : "(.*?)"', line)
        if p and cur and cur[1] == client_name and (port_name is None or p.group(2) == port_name):
            return "%d:%s" % (cur[0], p.group(1))
    return None

def smf_events(data):
    """Note-ons from a type-1 standard MIDI file: [(track, ms, channel, note, velocity)]."""
    if data[:4] != b"MThd": raise ValueError("not a MIDI file")
    ntrk = int.from_bytes(data[10:12], "big"); i = 14; out = []
    for t in range(ntrk):
        assert data[i:i+4] == b"MTrk"; ln = int.from_bytes(data[i+4:i+8], "big"); j = i + 8; end = j + ln; i = end
        now = 0; run = 0
        while j < end:
            d = 0
            while True:
                b = data[j]; j += 1; d = (d << 7) | (b & 127)
                if not b & 128: break
            now += d
            s = data[j]
            if s == 0xFF:
                l = data[j+2]; j += 3 + l; continue
            if s in (0xF0, 0xF7):
                l = 0; j += 1
                while True:
                    b = data[j]; j += 1; l = (l << 7) | (b & 127)
                    if not b & 128: break
                j += l; continue
            if s & 128: run = s; j += 1
            kind = run & 0xF0; n = 1 if kind in (0xC0, 0xD0) else 2
            args = data[j:j+n]; j += n
            if kind == 0x90 and args[1] > 0: out.append((t, now, run & 15, args[0], args[1]))
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("address"); ap.add_argument("--seconds", type=int, default=20)
    ap.add_argument("--bridge", default="MPE Bridge 2"); ap.add_argument("--source", default="LinnStrument MIDI")
    a = ap.parse_args()
    clients = ssh(a.address, "cat /proc/asound/seq/clients").decode(errors="replace")
    src = find_port(clients, a.source)
    out = find_port(clients, a.bridge, "MIDI Out")
    if not src: sys.exit("no client named %r on the MPC. Clients:\n%s" % (a.source, clients))
    if not out: sys.exit("no port %r on the MPC: load the project with the bridge first." % (a.bridge + " MIDI Out"))
    print("controller %s -> bridge output %s. Play a fast passage now: %d s." % (src, out, a.seconds))
    cmd = ("rm -f /tmp/cap.mid; arecordmidi -f 25 -t 40 -p %s,%s /tmp/cap.mid >/dev/null 2>&1 & P=$!; "
           "sleep %d; top -b -n 1 | head -n 12; sleep %d; kill -INT $P; sleep 1" % (src, out, a.seconds // 2, a.seconds - a.seconds // 2))
    cpu = ssh(a.address, cmd, timeout=a.seconds + 30).decode(errors="replace")
    data = ssh(a.address, "cat /tmp/cap.mid")
    if not data: sys.exit("nothing recorded (arecordmidi writes its file when it is stopped; try again)")
    ev = smf_events(data)
    inn = [e for e in ev if e[0] == 0]; got = [e for e in ev if e[0] == 1]
    print("\nCPU while playing:\n" + cpu)
    for label, lst in (("controller", inn), ("bridge out", got)):
        per = {}
        for _, _, ch, _, _ in lst: per[ch + 1] = per.get(ch + 1, 0) + 1
        print("%-11s %3d note-ons, by channel: %s" % (label, len(lst), dict(sorted(per.items()))))
    def unmatched(a_, b_):
        free = list(b_); lost = []
        for t, ms, ch, n, v in a_:
            hit = next((g for g in free if g[3] == n and -50 <= g[1] - ms <= 50), None)
            if hit: free.remove(hit)
            else: lost.append((ms, n, v))
        return lost, free
    for label, x, y in (("controller -> bridge output (the MPC hop and the bridge together)", inn, got),):
        lost, extra = unmatched(x, y)
        print("\n%s: %d of %d notes missing downstream, %d extra" % (label, len(lost), len(x), len(extra)))
        for ms, n, v in lost[:30]: print("  at %.2f s  note %d  velocity %d" % (ms / 1000, n, v))

if __name__ == "__main__":
    main()
