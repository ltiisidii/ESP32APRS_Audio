#!/bin/sh
# Hardware test (needs the station on the network): bursts of parallel web requests.
# Before the fixes in lib/AsyncTCP and src/webapi.cpp, 5 bursts of 18 restarted an ESP32.
# Bursts of parallel heavy requests (what a browser does when it opens a few tabs at once).
# Usage: web_burst.sh <rounds> <parallel>. Prints the uptime before/after: a drop means the station restarted.
H=http://192.168.1.89; A="-u admin:admin"; R=${1:-5}; P=${2:-18}
up() { curl -s --max-time 8 $A $H/api/info | grep -o '"uptime":[0-9]*' | cut -d: -f2; }
u0=$(up); echo "uptime before: $u0"
for r in $(seq 1 $R); do
  for i in $(seq 1 $P); do
    case $((i % 3)) in 0) p=/api/config;; 1) p=/api/meta;; 2) p=/forms.js;; esac
    curl -s -o /dev/null --max-time 15 --compressed $A $H$p &
  done
  wait; sleep 2
done
sleep 8; u1=$(up); echo "uptime after: ${u1:-no answer}"
[ -n "$u1" ] && [ "$u1" -gt "$u0" ] && echo "RESULT: survived" || echo "RESULT: RESTARTED"
