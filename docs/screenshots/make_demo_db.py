#!/usr/bin/env python3
"""Populate an Artilody Watts Trainer database with realistic demo rides.

Used to produce the documentation screenshots in this folder. Only the Python 3
standard library is needed. Output is deterministic (fixed random seed); ride
dates are relative to today.

Usage:
    # 1. Let the app create the schema and seed profile "Lone" + built-in workouts
    #    (screenshot mode runs headless and quits by itself):
    QT_QPA_PLATFORM=offscreen WATT_DB_PATH=/tmp/watt-demo.sqlite \
        WATT_SCREENSHOT_DIR=/tmp/wshot-init WATT_SCREENSHOT_STEP_MS=700 \
        "<build>/src/app/Artilody Watts Trainer.app/Contents/MacOS/Artilody Watts Trainer"

    # 2. Insert 16 rides + 7 rows over the last 30 days, three rowing workouts,
    #    rowing personal bests and a training plan (calendar) around today
    #    (replaces existing rides/plan/records of "Lone"):
    python3 docs/screenshots/make_demo_db.py /tmp/watt-demo.sqlite

    # 3. Take screenshots on a copy (the app saves its own ride on exit):
    cp /tmp/watt-demo.sqlite /tmp/run.sqlite
    QT_QPA_PLATFORM=offscreen WATT_DB_PATH=/tmp/run.sqlite WATT_SCREENSHOT_DIR=/tmp/shots \
        WATT_SCREENSHOT_STEP_MS=25000 "<app binary>"

Summary values mirror src/core/RideAnalysis.cpp: NP = 30 s rolling mean, 4th
power; IF = NP/FTP; TSS = dur*NP*IF/(FTP*3600)*100; kJ = sum(W)/1000; zones =
seconds per power zone with bounds [0.55, 0.75, 0.87, 0.94, 1.05] x FTP.
Rows use the rowing FTP and the Concept2 pace formula (W = 2.80 / (s/m)^3).
Needs a schema v2 database (app version 1.1.0 or newer).
"""
import datetime as dt
import json
import random
import sqlite3
import sys

PROFILE_NAME = "Lone"
ZONE_UPPER = [0.55, 0.75, 0.87, 0.94, 1.05]

# (days ago, start hh:mm, workout name, ERG mode, ramp-test failure watts or None)
# The last entry is the most recent ride -> selected by default in History.
PLAN = [
    (29, "18:10", "Endurance Base", True, None),
    (27, "07:05", "Sweet Spot 3 × 12", True, None),
    (25, "18:40", "Recovery Spin", False, None),
    (23, "17:55", "Threshold 2 × 20", True, None),
    (21, "09:30", "Endurance Base", False, None),
    (19, "18:20", "Ramp FTP Test", True, 336),
    (17, "07:10", "Tempo 3 × 15", True, None),
    (15, "18:05", "VO₂ Max 5 × 3", True, None),
    (13, "10:00", "Endurance Base", True, None),
    (12, "18:30", "Recovery Spin", False, None),
    (10, "17:45", "Over-Unders 4 × 8", True, None),
    (8, "07:00", "Sweet Spot 3 × 12", True, None),
    (6, "09:15", "Endurance Base", False, None),
    (5, "18:15", "Threshold 2 × 20", True, None),
    (3, "07:20", "Tempo 3 × 15", True, None),
    (1, "18:25", "Sweet Spot 3 × 12", True, None),
]
FAVORITES = ["Sweet Spot 3 × 12", "Threshold 2 × 20"]
# Uses before the 30-day window (drives the "Most popular" sort).
EXTRA_USES = {"Sweet Spot 3 × 12": 9, "Threshold 2 × 20": 5, "Endurance Base": 4, "Tempo 3 × 15": 3,
              "Ramp FTP Test": 2, "VO₂ Max 5 × 3": 2, "Over-Unders 4 × 8": 1, "Recovery Spin": 1}


ROWING_FTP = 205

def block(name, kind, sec, start, end=None):
    return {"name": name, "type": kind, "duration": sec, "start": start, "end": start if end is None else end}

ROWING_WORKOUTS = [
    ("Rowing Sweet Spot 3 × 4", "Intervals", "Sweet Spot", "Three 4-minute sweet spot pieces on the erg.",
     [block("Warm-up", "warmup", 300, 0.50, 0.65)]
     + [b for _ in range(3) for b in (block("Sweet Spot", "interval", 240, 0.90), block("Easy", "recovery", 120, 0.60))]
     + [block("Cool-down", "cooldown", 180, 0.60, 0.45)]),
    ("Row Pyramid 1-2-3-2-1", "Intervals", "VO₂ Max", "Short-to-long-to-short pyramid above threshold.",
     [block("Warm-up", "warmup", 300, 0.50, 0.65), block("1 min", "interval", 60, 1.12), block("Easy", "recovery", 60, 0.60),
      block("2 min", "interval", 120, 1.06), block("Easy", "recovery", 90, 0.60), block("3 min", "interval", 180, 1.00),
      block("Easy", "recovery", 90, 0.60), block("2 min", "interval", 120, 1.06), block("Easy", "recovery", 60, 0.60),
      block("1 min", "interval", 60, 1.12), block("Cool-down", "cooldown", 300, 0.60, 0.45)]),
    ("Steady State Row 30", "Endurance", "Aerobic", "Long, steady UT2 row at a conversational rate.",
     [block("Warm-up", "warmup", 300, 0.50, 0.65), block("Steady", "steady", 1500, 0.72), block("Cool-down", "cooldown", 180, 0.60, 0.45)]),
]

# (days ago, start hh:mm, workout name or ("dist", metres, watts), personal-best distance or None)
ROWS = [
    (26, "07:30", "Steady State Row 30", None),
    (20, "18:00", ("dist", 1000, 172), 1000),
    (16, "17:40", "Row Pyramid 1-2-3-2-1", None),
    (11, "09:10", ("dist", 2000, 168), 2000),
    (7, "18:10", ("dist", 1000, 180), 1000),
    (4, "07:15", "Rowing Sweet Spot 3 × 4", None),
    (2, "17:50", "Steady State Row 30", None),
]
# Older personal bests without a ride (distance m, time s, days ago)
OLD_RECORDS = [(500, 104.6, 48), (5000, 1338.0, 41), (6000, 1622.5, 60)]

# Training plan relative to today: (day offset, workout name, notes). Past entries
# are marked completed and linked to the ride of that day when there is one.
PLAN_ENTRIES = [
    (-21, "Endurance Base", ""), (-20, "Row Pyramid 1-2-3-2-1", "Swapped for a 1k race"),
    (-17, "Tempo 3 × 15", ""), (-15, "VO₂ Max 5 × 3", ""), (-13, "Endurance Base", ""),
    (-10, "Over-Unders 4 × 8", ""), (-8, "Sweet Spot 3 × 12", ""), (-6, "Endurance Base", "Outdoor if dry"),
    (-5, "Threshold 2 × 20", ""), (-4, "Rowing Sweet Spot 3 × 4", ""), (-3, "Tempo 3 × 15", ""),
    (-2, "Steady State Row 30", ""), (-1, "Sweet Spot 3 × 12", ""),
    (0, "Endurance Base", ""), (0, "Row Pyramid 1-2-3-2-1", "Easy if legs are heavy"),
    (1, "Recovery Spin", ""), (2, "Steady State Row 30", ""), (3, "Threshold 2 × 20", "Key session"),
    (5, "Sweet Spot 3 × 12", ""), (5, "Rowing Sweet Spot 3 × 4", ""), (6, "Endurance Base", "Long ride"),
    (8, "Steady State Row 30", ""), (9, "VO₂ Max 5 × 3", ""), (10, "Recovery Spin", ""),
    (12, "Over-Unders 4 × 8", ""), (13, "Row Pyramid 1-2-3-2-1", ""), (15, "Ramp FTP Test", "Retest FTP"),
    (17, "Tempo 3 × 15", ""),
]


def watts_to_speed(p):
    return 500.0 / (500.0 * (2.80 / p) ** (1 / 3)) if p > 1 else 0.0


def simulate_row(rest_hr, max_hr, rng, intervals=None, ftp=0, distance=0, watts=0):
    """Free row on a PM5 (no ERG): the rower roughly follows the target."""
    samples, trace = [], [(0.0, 0.0)]
    p = 0.0
    hr = rest_hr + 10.0
    dist = drift = 0.0
    tgt = targets(intervals, ftp) if intervals else None
    t = 0
    while (t < len(tgt)) if tgt else (dist < distance):
        target = tgt[t][0] if tgt else round(watts * (1.12 if dist < 150 else 1.0))
        drift += rng.gauss(0, 1.0) - drift * 0.03
        p += (target - p) * 0.25
        pw = max(0.0, p + drift * 2 + rng.gauss(0, 8))
        spm = int(18 + pw / 25 + rng.gauss(0, 0.6))
        frac = pw / ftp if ftp else pw / ROWING_FTP
        steady = rest_hr + (max_hr - rest_hr) * min(1.0, 0.2 + 0.7 * frac)
        hr += (steady - hr) / (25 if steady > hr else 40)
        dist += watts_to_speed(pw)
        samples.append((t, round(pw), target if tgt else 0, spm, round(hr + rng.gauss(0, 0.6))))
        trace.append((t + 1.0, min(dist, distance) if distance else dist))
        t += 1
    if distance:   # exact finish time: interpolate within the last second
        prev = trace[-2][1]
        frac = (distance - prev) / max(1e-6, dist - prev)
        trace[-1] = (t - 1 + frac, float(distance))
    return samples, dist, trace


def targets(intervals, ftp):
    out = []
    for iv in intervals:
        d = int(iv["duration"])
        cad = iv.get("cadence") or 0
        for i in range(d):
            f = iv["start"] + (iv["end"] - iv["start"]) * (i / max(1, d - 1))
            out.append((round(f * ftp), cad))
    return out


def simulate(intervals, ftp, rest_hr, max_hr, erg, fail_w, rng):
    samples = []
    tgt = targets(intervals, ftp)
    actual = tgt[0][0] * 0.6
    hr = rest_hr + 10.0
    fatigue = drift = 0.0
    for t, (target, cad_target) in enumerate(tgt):
        if fail_w is not None and target >= fail_w:
            # Ramp test: the rider fails – power collapses within ~10 s, ride ends.
            for k in range(10):
                actual *= 0.7
                hr -= 0.3
                samples.append((t + k, max(0, round(actual)), target, max(0, int(70 - k * 6 + rng.gauss(0, 3))),
                                round(hr)))
            break
        if erg:   # trainer follows the target with a short lag + small noise
            actual += (target - actual) * 0.35
            p = actual + rng.gauss(0, 4 + target * 0.02)
        else:     # free ride: rider roughly holds the target with a wandering offset
            drift += rng.gauss(0, 1.2) - drift * 0.02
            actual += (target - actual) * 0.08
            p = actual + drift * 2 + rng.gauss(0, 10)
        p = max(0.0, p)
        cadence = int((cad_target or 88) + rng.gauss(0, 2.5) + (2 if p > ftp else 0))
        # Heart rate: first-order lag towards a load-dependent steady state + slow drift.
        frac = p / ftp
        fatigue += 0.00004 * max(0.0, frac - 0.6)
        steady = rest_hr + (max_hr - rest_hr) * min(1.0, 0.18 + 0.72 * frac + fatigue)
        hr += (steady - hr) / (25 if steady > hr else 40)
        samples.append((t, round(p), target, cadence, round(hr + rng.gauss(0, 0.6))))
    return samples


def analyze(samples, ftp):
    power = [max(0, s[1]) for s in samples]
    n = len(power)
    hrs = [s[4] for s in samples if s[4] > 0]
    cads = [s[3] for s in samples if s[3] > 0]
    if n < 30:
        np_ = sum(power) / n
    else:
        rolling = sum(power[:30])
        acc, cnt = (rolling / 30) ** 4, 1
        for i in range(30, n):
            rolling += power[i] - power[i - 30]
            acc += (rolling / 30) ** 4
            cnt += 1
        np_ = (acc / cnt) ** 0.25
    zones = [0] * 6
    for p in power:
        zones[next((i for i, u in enumerate(ZONE_UPPER) if p / ftp < u), 5)] += 1
    if_ = np_ / ftp
    return dict(duration=n, avg_power=sum(power) / n, max_power=max(power), np=np_,
                avg_hr=sum(hrs) / len(hrs) if hrs else 0.0, max_hr=max(hrs) if hrs else 0,
                avg_cadence=sum(cads) / len(cads) if cads else 0.0, kj=sum(power) / 1000.0,
                if_value=if_, tss=n * np_ * if_ / (ftp * 3600.0) * 100.0, zones=zones)


def trace_text(trace):
    return "[" + ",".join(f"[{round(t, 1):g},{round(d, 1):g}]" for t, d in trace) + "]"


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    con = sqlite3.connect(sys.argv[1])
    con.execute("PRAGMA foreign_keys = ON")
    row = con.execute("SELECT id, ftp, max_hr, rest_hr FROM profiles WHERE name = ?", (PROFILE_NAME,)).fetchone()
    if not row:
        sys.exit("Profile 'Lone' not found – run the app once with WATT_DB_PATH first (see usage).")
    pid, ftp, max_hr, rest_hr = row
    con.execute("UPDATE profiles SET rowing_ftp = ? WHERE id = ?", (ROWING_FTP, pid))
    for name, cat, focus, desc, intervals in ROWING_WORKOUTS:
        con.execute("DELETE FROM workouts WHERE name = ?", (name,))
        con.execute("INSERT INTO workouts (name, category, focus, description, intervals, sport, created_at)"
                    " VALUES (?,?,?,?,?, 'rowing', ?)",
                    (name, cat, focus, desc, json.dumps(intervals, ensure_ascii=False),
                     int(dt.datetime.now().timestamp())))
    workouts = {name: (wid, json.loads(iv)) for wid, name, iv in con.execute("SELECT id, name, intervals FROM workouts")}

    rng = random.Random(42)
    con.execute("DELETE FROM ride_samples WHERE ride_id IN (SELECT id FROM rides WHERE profile_id = ?)", (pid,))
    con.execute("DELETE FROM rides WHERE profile_id = ?", (pid,))
    con.execute("DELETE FROM planned_workouts WHERE profile_id = ?", (pid,))
    con.execute("DELETE FROM rowing_records WHERE profile_id = ?", (pid,))
    today = dt.date.today()
    usage = {}
    ride_on = {}   # (date, workout name) -> ride id
    for days, hhmm, name, erg, fail_w in PLAN:
        wid, intervals = workouts[name]
        h, m = map(int, hhmm.split(":"))
        started = dt.datetime.combine(today - dt.timedelta(days=days), dt.time(h, m))
        samples = simulate(intervals, ftp, rest_hr, max_hr, erg, fail_w, rng)
        st = analyze(samples, ftp)
        cur = con.execute(
            "INSERT INTO rides (profile_id, workout_id, workout_name, started_at, duration, blocks, erg, ftp,"
            " avg_power, max_power, np, avg_hr, max_hr, avg_cadence, kj, if_value, tss, zones)"
            " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            (pid, wid, name, int(started.timestamp()), st["duration"], len(intervals), int(erg), ftp,
             st["avg_power"], st["max_power"], st["np"], st["avg_hr"], st["max_hr"], st["avg_cadence"],
             st["kj"], st["if_value"], st["tss"], json.dumps(st["zones"], separators=(",", ":"))))
        con.executemany("INSERT INTO ride_samples (ride_id, t, power, target, cadence, hr) VALUES (?,?,?,?,?,?)",
                        [(cur.lastrowid,) + s for s in samples])
        usage[wid] = usage.get(wid, 0) + 1
        ride_on[(started.date(), name)] = cur.lastrowid
        print(f"{started:%Y-%m-%d %H:%M}  {name:20s} {st['duration'] // 60:3d} min  avg {st['avg_power']:5.1f} W"
              f"  NP {st['np']:5.1f}  TSS {st['tss']:5.1f}  HR {st['avg_hr']:5.1f}/{st['max_hr']}")

    # --- rows (Concept2 PM5) -------------------------------------------------
    for days, hhmm, what, pb in ROWS:
        h, m = map(int, hhmm.split(":"))
        started = dt.datetime.combine(today - dt.timedelta(days=days), dt.time(h, m))
        if isinstance(what, tuple):
            _, metres, watts = what
            wid, intervals, name = None, [], f"Ghost Race {metres} m" if pb == 1000 else f"Row {metres} m"
            samples, dist, trace = simulate_row(rest_hr, max_hr, rng, distance=metres, watts=watts)
        else:
            name = what
            wid, intervals = workouts[name]
            samples, dist, trace = simulate_row(rest_hr, max_hr, rng, intervals=intervals, ftp=ROWING_FTP)
            usage[wid] = usage.get(wid, 0) + 1
        st = analyze(samples, ROWING_FTP)
        cur = con.execute(
            "INSERT INTO rides (profile_id, workout_id, workout_name, started_at, duration, blocks, erg, ftp,"
            " avg_power, max_power, np, avg_hr, max_hr, avg_cadence, kj, if_value, tss, zones, sport, distance_m)"
            " VALUES (?,?,?,?,?,?,0,?,?,?,?,?,?,?,?,?,?,?,'rowing',?)",
            (pid, wid, name, int(started.timestamp()), st["duration"], len(intervals), ROWING_FTP,
             st["avg_power"], st["max_power"], st["np"], st["avg_hr"], st["max_hr"], st["avg_cadence"],
             st["kj"], st["if_value"], st["tss"], json.dumps(st["zones"], separators=(",", ":")), round(dist, 1)))
        con.executemany("INSERT INTO ride_samples (ride_id, t, power, target, cadence, hr) VALUES (?,?,?,?,?,?)",
                        [(cur.lastrowid,) + s for s in samples])
        ride_on[(started.date(), name)] = cur.lastrowid
        if pb:
            con.execute("INSERT INTO rowing_records (profile_id, distance_m, time_sec, ride_id, created_at, trace)"
                        " VALUES (?,?,?,?,?,?)",
                        (pid, pb, round(trace[-1][0], 1), cur.lastrowid,
                         started.astimezone(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"), trace_text(trace)))
        print(f"{started:%Y-%m-%d %H:%M}  {name:24s} {st['duration'] // 60:3d} min  {dist:6.0f} m"
              f"  avg {st['avg_power']:5.1f} W  TSS {st['tss']:5.1f}" + (f"  PB {trace[-1][0]:.1f} s" if pb else ""))
    for metres, secs, days in OLD_RECORDS:
        when = dt.datetime.combine(today - dt.timedelta(days=days), dt.time(18, 0))
        con.execute("INSERT INTO rowing_records (profile_id, distance_m, time_sec, ride_id, created_at, trace)"
                    " VALUES (?,?,?,NULL,?,?)",
                    (pid, metres, secs, when.astimezone(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
                     trace_text([(0.0, 0.0), (secs, float(metres))])))

    # --- training plan (calendar) --------------------------------------------
    position = {}
    for offset, name, notes in PLAN_ENTRIES:
        day = today + dt.timedelta(days=offset)
        rid = ride_on.get((day, name))
        pos = position.get(day, 0)
        position[day] = pos + 1
        con.execute("INSERT INTO planned_workouts (profile_id, date, workout_id, position, completed, ride_id, notes)"
                    " VALUES (?,?,?,?,?,?,?)",
                    (pid, day.isoformat(), workouts[name][0], pos, int(rid is not None), rid, notes or None))

    for name, (wid, _) in workouts.items():
        con.execute("UPDATE workouts SET times_used = ?, favorite = ? WHERE id = ?",
                    (usage.get(wid, 0) + EXTRA_USES.get(name, 0), int(name in FAVORITES), wid))
    con.commit()
    con.execute("PRAGMA wal_checkpoint(TRUNCATE)")
    con.close()


if __name__ == "__main__":
    main()
