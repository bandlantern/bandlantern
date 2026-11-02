#!/usr/bin/env python3
"""tt: tiny time tracker for Bandlantern.

Log:    progress/timelog.csv  (gitignored: private, local only; one row per work block)
Timer:  progress/.tt-active   (gitignored; the running session)
Phase:  progress/current-phase (gitignored; one line, e.g. "0")

Usage:
  tt start [category] [start note...]  start a timer (stops nothing; errors if one is running)
  tt stop [note...]               stop the timer and log it (note becomes the end note)
  tt status                       show the running timer and today's/this week's totals
  tt cancel                       discard the running timer
  tt add <dur> [category] [note...] [--date YYYY-MM-DD]
                                  log time after the fact; dur like 90m, 1.5h, 1h30m
  tt log [n]                      show the last n entries (default 10)
  tt report [week|lastweek|month|phase|all]
                                  totals by day, category and phase (default: week)
  tt phase [N]                    show or set the current phase (0-9)

Categories (default: build; custom: cat:<name>):
  build    writing engine/UI code, features, refactors, bug fixes
  test     writing/running tests, golden vectors, fuzzing, CI failures
  design   architecture, ADRs, protocol/schema and UI/style design
  docs     README, plan, progress notes, user/dev documentation
  admin    licensing, SBOM/notices, repo housekeeping, planning
  learn    reading specs, datasheets, papers; tutorials and research
  hw       radio/antenna setup, bench work, capturing real IQ
  env      toolchain, IDE, vcpkg/CMake setup, dev machine fixes
"""
import csv
import json
import re
import sys
from collections import defaultdict
from datetime import date, datetime, timedelta
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROG = ROOT / "progress"
LOG = PROG / "timelog.csv"
ACTIVE = PROG / ".tt-active"
PHASE = PROG / "current-phase"
FIELDS = ["date", "start", "end", "minutes", "phase", "category", "start_note", "end_note"]
WEEKLY_TARGET_H = 20
CATEGORIES = {"build", "test", "design", "docs", "admin", "learn", "hw", "env"}

# Plan §20.0: FTE weeks per phase (x40 h). Half-time-with-AI calendar weeks for reference.
PHASE_FTE_WEEKS = {0: (3, 5), 1: (10, 14), 2: (10, 13), 3: (10, 14), 4: (8, 12),
                   5: (8, 10), 6: (6, 8), 7: (8, 12), 8: (8, 10), 9: (8, 10)}


def die(msg):
    print(f"tt: {msg}", file=sys.stderr)
    sys.exit(1)


def current_phase():
    try:
        return PHASE.read_text().strip() or "0"
    except FileNotFoundError:
        return "0"


def read_log():
    if not LOG.exists():
        return []
    with LOG.open(newline="") as f:
        return list(csv.DictReader(f))


def append_log(row):
    new = not LOG.exists()
    with LOG.open("a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS)
        if new:
            w.writeheader()
        w.writerow(row)


def fmt(minutes):
    minutes = int(round(minutes))
    return f"{minutes // 60}h{minutes % 60:02d}m"


def parse_duration(s):
    s = s.strip().lower()
    if re.fullmatch(r"\d+(\.\d+)?", s):  # bare number = hours
        return float(s) * 60
    m = re.fullmatch(r"(?:(\d+(?:\.\d+)?)h)?(?:(\d+)m)?", s)
    if not m or not any(m.groups()):
        die(f"can't parse duration '{s}' (try 90m, 1.5h, 1h30m)")
    return float(m.group(1) or 0) * 60 + int(m.group(2) or 0)


def split_category(args):
    """First arg is the category if it is a known one (or written cat:<name>); the rest is the note."""
    if args and args[0] in CATEGORIES:
        return args[0], " ".join(args[1:])
    if args and args[0].startswith("cat:"):
        return args[0][4:], " ".join(args[1:])
    return "build", " ".join(args)


def format_notes(start_note, end_note):
    return " -> ".join(n for n in (start_note, end_note) if n)


def load_active():
    if not ACTIVE.exists():
        return None
    return json.loads(ACTIVE.read_text())


def cmd_start(args):
    if (a := load_active()):
        die(f"timer already running since {a['start'][11:16]} ({a['category']}). `tt stop` or `tt cancel` first.")
    cat, note = split_category(args)
    now = datetime.now().replace(microsecond=0)
    ACTIVE.write_text(json.dumps({"start": now.isoformat(), "category": cat,
                                  "start_note": note, "phase": current_phase()}))
    print(f"started {cat} @ {now:%H:%M} (phase {current_phase()}){': ' + note if note else ''}")


def cmd_stop(args):
    a = load_active()
    if not a:
        die("no timer running")
    start = datetime.fromisoformat(a["start"])
    end = datetime.now().replace(microsecond=0)
    minutes = (end - start).total_seconds() / 60
    end_note = " ".join(args)
    append_log({"date": start.date().isoformat(), "start": f"{start:%H:%M}", "end": f"{end:%H:%M}",
                "minutes": round(minutes), "phase": a["phase"], "category": a["category"],
                "start_note": a["start_note"], "end_note": end_note})
    ACTIVE.unlink()
    print(f"logged {fmt(minutes)} of {a['category']}{': ' + format_notes(a['start_note'], end_note) if a['start_note'] or end_note else ''}")
    print(f"today {fmt(total_since(date.today()))} | week {fmt(total_since(week_start()))} / {WEEKLY_TARGET_H}h")


def cmd_cancel(_):
    if not ACTIVE.exists():
        die("no timer running")
    ACTIVE.unlink()
    print("timer discarded")


def cmd_add(args):
    d = date.today()
    if "--date" in args:
        i = args.index("--date")
        try:
            d = date.fromisoformat(args[i + 1])
        except (IndexError, ValueError):
            die("--date needs YYYY-MM-DD")
        args = args[:i] + args[i + 2:]
    if not args:
        die("usage: tt add <dur> [category] [note...]")
    minutes = parse_duration(args[0])
    cat, note = split_category(args[1:])
    append_log({"date": d.isoformat(), "start": "", "end": "", "minutes": round(minutes),
                "phase": current_phase(), "category": cat, "start_note": note, "end_note": ""})
    print(f"logged {fmt(minutes)} of {cat} on {d}")


def week_start(d=None):
    d = d or date.today()
    return d - timedelta(days=d.weekday())  # Monday


def total_since(start, end=None):
    total = 0
    for r in read_log():
        rd = date.fromisoformat(r["date"])
        if rd >= start and (end is None or rd < end):
            total += int(r["minutes"])
    return total


def cmd_status(_):
    a = load_active()
    if a:
        start = datetime.fromisoformat(a["start"])
        running = (datetime.now() - start).total_seconds() / 60
        print(f"RUNNING {a['category']} since {start:%H:%M} ({fmt(running)}){': ' + a['start_note'] if a['start_note'] else ''}")
    else:
        running = 0
        print("no timer running")
    wk = total_since(week_start()) + running
    print(f"phase {current_phase()} | today {fmt(total_since(date.today()) + running)} | "
          f"week {fmt(wk)} / {WEEKLY_TARGET_H}h ({wk / 60 / WEEKLY_TARGET_H:.0%})")


def cmd_log(args):
    n = int(args[0]) if args else 10
    for r in read_log()[-n:]:
        span = f"{r['start']}-{r['end']}" if r["start"] else "  (added)  "
        print(f"{r['date']} {span:11} {fmt(int(r['minutes'])):>7}  P{r['phase']} {r['category']:<7} {format_notes(r['start_note'], r['end_note'])}")


def cmd_report(args):
    scope = args[0] if args else "week"
    rows = read_log()
    today = date.today()
    if scope == "week":
        lo, hi = week_start(), None
    elif scope == "lastweek":
        lo, hi = week_start() - timedelta(days=7), week_start()
    elif scope == "month":
        lo, hi = today.replace(day=1), None
    elif scope == "phase":
        rows = [r for r in rows if r["phase"] == current_phase()]
        lo, hi = date.min, None
    elif scope == "all":
        lo, hi = date.min, None
    else:
        die("scope must be week, lastweek, month, phase or all")
    rows = [r for r in rows if date.fromisoformat(r["date"]) >= lo
            and (hi is None or date.fromisoformat(r["date"]) < hi)]
    if not rows:
        print(f"no entries ({scope})")
        return

    by_day, by_cat, by_phase = defaultdict(int), defaultdict(int), defaultdict(int)
    for r in rows:
        m = int(r["minutes"])
        by_day[r["date"]] += m
        by_cat[r["category"]] += m
        by_phase[r["phase"]] += m
    total = sum(by_day.values())

    print(f"== {scope} ==")
    if scope in ("week", "lastweek", "month"):
        for d in sorted(by_day):
            print(f"  {d} {date.fromisoformat(d):%a}  {fmt(by_day[d]):>7}  {'#' * (by_day[d] // 30)}")
    print("  by category: " + ", ".join(f"{c} {fmt(m)}" for c, m in sorted(by_cat.items(), key=lambda x: -x[1])))
    if len(by_phase) > 1 or scope in ("phase", "all"):
        print("  by phase:    " + ", ".join(f"P{p} {fmt(m)}" for p, m in sorted(by_phase.items())))
    print(f"  total: {fmt(total)}")
    if scope in ("week", "lastweek"):
        print(f"  target: {WEEKLY_TARGET_H}h ({total / 60 / WEEKLY_TARGET_H:.0%})")
    if scope in ("phase", "all"):
        for p, m in sorted(by_phase.items()):
            if p.isdigit() and int(p) in PHASE_FTE_WEEKS:
                lo_w, hi_w = PHASE_FTE_WEEKS[int(p)]
                print(f"  P{p} budget (plan §20.0): {lo_w * 40}-{hi_w * 40}h FTE; spent {m / 60:.1f}h "
                      f"({m / 60 / (lo_w * 40):.0%} of low estimate)")
        days = sorted({r["date"] for r in rows})
        weeks = max(1, (date.fromisoformat(days[-1]) - date.fromisoformat(days[0])).days / 7)
        print(f"  avg/week since {days[0]}: {total / 60 / weeks:.1f}h")


def cmd_phase(args):
    if not args:
        print(current_phase())
        return
    if not re.fullmatch(r"\d", args[0]):
        die("phase must be 0-9")
    PHASE.write_text(args[0] + "\n")
    print(f"phase set to {args[0]} (remember to update CURRENT PHASE in CLAUDE.md and progress/PROGRESS.md)")


COMMANDS = {"start": cmd_start, "stop": cmd_stop, "cancel": cmd_cancel, "add": cmd_add,
            "status": cmd_status, "log": cmd_log, "report": cmd_report, "phase": cmd_phase}

if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help", "help"):
        print(__doc__)
        sys.exit(0)
    if sys.argv[1] not in COMMANDS:
        die(f"unknown command '{sys.argv[1]}' (see tt --help)")
    PROG.mkdir(exist_ok=True)
    COMMANDS[sys.argv[1]](sys.argv[2:])
