#!/bin/bash
# Run a long job and freeze it whenever the laptop gets hot (one working fan)
# or runs on battery (the GPU is capped at 30 W then, and a drained battery
# would take unsaved work with it).
# Usage: heatwatch.sh LOGFILE -- command args...
# Pauses the job (SIGSTOP) when the CPU passes 85 C or the GPU 82 C at once, or
# 82 C / 80 C on two readings in a row (a single-reading spike on a laptop CPU
# can be 20 degrees and gone in seconds), or when no charger is connected;
# resumes it (SIGCONT) once the CPU is below 75 C, the GPU below 72 C and the
# charger is back. Checks every 2 s (the CPU can climb 10 C in a second), logs every change and the total paused
# time. Temperature comes from ActivityScope's live file (one second fresh),
# with lm-sensors as a fallback.
# Never SIGSTOP/SIGCONT the job or its unit from outside (systemctl kill -s STOP): the guard keeps
# its own paused flag, and an outside SIGCONT leaves the job running hot with the guard believing
# it is paused, so it never pauses it again (happened 2026-10-19). Stop the unit instead; it resumes
# from the last checkpoint.
set -u
log=$1
shift
[ "${1:-}" = "--" ] && shift
F=${XDG_RUNTIME_DIR:-/run/user/$UID}/activityscope/conky.txt

cpu_temp() {
  local t
  t=$(tr '=' ' ' < "$F" 2>/dev/null | awk '$1=="cpu_temp"{printf "%d", $2; exit}')
  if [ -n "$t" ]; then echo "$t"; return; fi
  sensors 2>/dev/null | awk '/Package id 0|Tctl/{for(i=1;i<=NF;i++) if($i ~ /^\+[0-9.]+°C/){gsub(/[+°C]/,"",$i); printf "%d", $i; exit}}'
}
gpu_temp() { nvidia-smi --query-gpu=temperature.gpu --format=csv,noheader 2>/dev/null | awk '{printf "%d", $1}'; }
on_battery() {  # true when the machine has a charger socket and nothing is plugged into it
  local f any=0
  for f in /sys/class/power_supply/*/online; do
    [ -r "$f" ] || continue
    any=1
    [ "$(cat "$f")" = "1" ] && return 1
  done
  [ "$any" = 1 ]
}

"$@" &
pid=$!
paused=0
pause_s=0
t_pause=0
hot=0
echo "$(date +%T) start pid $pid: $*" >> "$log"
while kill -0 "$pid" 2>/dev/null; do
  c=$(cpu_temp); g=$(gpu_temp); c=${c:-0}; g=${g:-0}
  reason=""
  if [ "$paused" = 0 ]; then
    if on_battery; then
      reason="on battery"
    else
      if [ "$c" -gt 82 ] || [ "$g" -gt 80 ]; then hot=$((hot + 1)); else hot=0; fi
      if [ "$c" -gt 85 ] || [ "$g" -gt 82 ] || [ "$hot" -ge 2 ]; then reason="cpu ${c} C gpu ${g} C"; fi
    fi
    if [ -n "$reason" ]; then
      kill -STOP "$pid"; paused=1; hot=0; t_pause=$(date +%s)
      echo "$(date +%T) PAUSE $reason" >> "$log"
    fi
  elif ! on_battery && [ "$c" -lt 75 ] && [ "$g" -lt 72 ]; then
    kill -CONT "$pid"; paused=0; pause_s=$((pause_s + $(date +%s) - t_pause))
    echo "$(date +%T) resume cpu ${c} C gpu ${g} C (paused ${pause_s}s so far)" >> "$log"
  fi
  sleep 2
done
wait "$pid"
rc=$?
echo "$(date +%T) end rc $rc, total paused ${pause_s}s" >> "$log"
exit $rc
