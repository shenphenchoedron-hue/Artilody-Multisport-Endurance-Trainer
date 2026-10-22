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

    # 2. Insert 16 rides over the last 30 days (replaces existing rides of "Lone"):
    python3 docs/screenshots/make_demo_db.py /tmp/watt-demo.sqlite

    # 3. Take screenshots on a copy (the app saves its own ride on exit):
    cp /tmp/watt-demo.sqlite /tmp/run.sqlite
    QT_QPA_PLATFORM=offscreen WATT_DB_PATH=/tmp/run.sqlite WATT_SCREENSHOT_DIR=/tmp/shots \
        WATT_SCREENSHOT_STEP_MS=25000 "<app binary>"

Summary values mirror src/core/RideAnalysis.cpp: NP = 30 s rolling mean, 4th
power; IF = NP/FTP; TSS = dur*NP*IF/(FTP*3600)*100; kJ = sum(W)/1000; zones =
seconds per power zone with bounds [0.55, 0.75, 0.87, 0.94, 1.05] x FTP.
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


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    con = sqlite3.connect(sys.argv[1])
    con.execute("PRAGMA foreign_keys = ON")
    row = con.execute("SELECT id, ftp, max_hr, rest_hr FROM profiles WHERE name = ?", (PROFILE_NAME,)).fetchone()
    if not row:
        sys.exit("Profile 'Lone' not found – run the app once with WATT_DB_PATH first (see usage).")
    pid, ftp, max_hr, rest_hr = row
    workouts = {name: (wid, json.loads(iv)) for wid, name, iv in con.execute("SELECT id, name, intervals FROM workouts")}

    rng = random.Random(42)
    con.execute("DELETE FROM ride_samples WHERE ride_id IN (SELECT id FROM rides WHERE profile_id = ?)", (pid,))
    con.execute("DELETE FROM rides WHERE profile_id = ?", (pid,))
    today = dt.date.today()
    usage = {}
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
        print(f"{started:%Y-%m-%d %H:%M}  {name:20s} {st['duration'] // 60:3d} min  avg {st['avg_power']:5.1f} W"
              f"  NP {st['np']:5.1f}  TSS {st['tss']:5.1f}  HR {st['avg_hr']:5.1f}/{st['max_hr']}")
    for name, (wid, _) in workouts.items():
        con.execute("UPDATE workouts SET times_used = ?, favorite = ? WHERE id = ?",
                    (usage.get(wid, 0) + EXTRA_USES.get(name, 0), int(name in FAVORITES), wid))
    con.commit()
    con.execute("PRAGMA wal_checkpoint(TRUNCATE)")
    con.close()


if __name__ == "__main__":
    main()
