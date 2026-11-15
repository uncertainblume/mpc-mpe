#!/usr/bin/env python3
"""Watch the MPC for dropped notes, unattended.

    tools/watch_midi.py <address> [--window 60] [--hours 3] [--bridge "MPE Bridge 2"] [--log FILE]

Records the LinnStrument and the bridge output in back-to-back windows (see capture_midi.py),
samples the busiest MPC thread every 3 s, and appends one line per window to the log. Quiet windows (no notes)
are logged but ignored. Exits with status 1 at the first window where a controller note did
not reach the bridge output, or where one MPC thread reached 85% of a core; otherwise runs until --hours.
Read-only on the device (writes only /tmp/watch.mid).
"""
import argparse, os, re, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from capture_midi import ssh, find_port, smf_events

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("address"); ap.add_argument("--window", type=int, default=60)
    ap.add_argument("--hours", type=float, default=3); ap.add_argument("--bridge", default="MPE Bridge 2")
    ap.add_argument("--source", default="LinnStrument MIDI"); ap.add_argument("--log", default="local/watch/log.txt")
    a = ap.parse_args(); os.makedirs(os.path.dirname(a.log), exist_ok=True)
    end = time.time() + a.hours * 3600
    def log(s):
        line = time.strftime("%H:%M:%S ") + s
        print(line, flush=True); open(a.log, "a").write(line + "\n")
    log("watching %s, %d s windows" % (a.bridge, a.window))
    while time.time() < end:
        try:
            clients = ssh(a.address, "cat /proc/asound/seq/clients").decode(errors="replace")
            src = find_port(clients, a.source); outs = [find_port(clients, b.strip(), "MIDI Out") for b in a.bridge.split(",")]
            outs = [o for o in outs if o]; out = ",".join(outs)
            if not src or not out:
                log("ports not open (project closed?); waiting"); time.sleep(20); continue
            cmd = ("rm -f /tmp/watch.mid; arecordmidi -f 25 -t 40 -p %s,%s /tmp/watch.mid >/dev/null 2>&1 & P=$!; "
                   "i=0; while [ $i -lt %d ]; do INTERVAL=3 sh /tmp/mpc_threads.sh | head -n 1; i=$((i+3)); done; "
                   "kill -INT $P; sleep 1" % (src, out, a.window))
            cpu = ssh(a.address, cmd, timeout=a.window + 40).decode(errors="replace")
            data = ssh(a.address, "cat /tmp/watch.mid")
        except Exception as e:
            log("device not reachable: %s" % e); time.sleep(20); continue
        busy = [int(m) for m in re.findall(r"(\d+)%\s", cpu)]
        maxbusy = max(busy) if busy else -1
        if not data:
            log("no file recorded"); continue
        ev = smf_events(data); inn = [e for e in ev if e[0] == 0]; got = [e for e in ev if e[0] >= 1]
        per_bridge = {a.bridge.split(",")[e[0]-1].strip() if e[0]-1 < len(outs) else "?": 0 for e in got}
        for e in got: per_bridge[a.bridge.split(",")[e[0]-1].strip()] += 1
        free = list(got); lost = []
        for t, ms, ch, n, v in inn:
            hit = next((g for g in free if g[3] == n and -50 <= g[1] - ms <= 50), None)
            if hit: free.remove(hit)
            else: lost.append((round(ms / 1000, 1), n))
        chs = {}
        for e in got: chs[e[2] + 1] = chs.get(e[2] + 1, 0) + 1
        log("notes in %d, out %d %s, missing %d, out channels %s, busiest audio thread peaked at %d%%" % (len(inn), len(got), per_bridge, len(lost), dict(sorted(chs.items())), maxbusy))
        if lost or (inn and maxbusy >= 85):
            log("PROBLEM: missing notes (s into window, note) %s" % lost[:20] if lost else "PROBLEM: an audio thread was at %d%% of a core" % maxbusy)
            sys.exit(1)
    log("finished without a problem")

if __name__ == "__main__":
    main()
