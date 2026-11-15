#!/bin/sh
# Per-thread CPU of the MPC app over INTERVAL seconds (default 2), busiest first. Runs on the MPC.
# usage: INTERVAL=3 ssh root@<addr> 'sh -s' < tools/mpc_threads.sh   (set INTERVAL on the remote side)
P=$(pidof MPC | cut -d' ' -f1); I=${INTERVAL:-2}
snap() { for t in /proc/$P/task/*; do [ -r $t/stat ] && echo "${t##*/} $(tr ' ' _ < $t/comm 2>/dev/null) $(awk '{print $14+$15}' $t/stat 2>/dev/null)"; done; }
snap > /tmp/th1; sleep $I; snap > /tmp/th2
awk -v i=$I 'NR==FNR{a[$1]=$3; next} ($1 in a){d=$3-a[$1]; if (d>0) printf "%4.0f%%  %s\n", d/i, $2}' /tmp/th1 /tmp/th2 | sort -rn | head -n 8
