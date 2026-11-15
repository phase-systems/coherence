#!/usr/bin/env python3
"""What the UC1's Expand (Gate EXP) button does, through the real SSL 360 Link, without the hardware.

  python3 Tools/ssl360link/expand_check.py <PlugHost> <work folder>

Hosts the installed COHERENCE in 360 Link, moves slots as the UC1 would, and reads every setting from COHERENCE's own
saved state before and after each press. Exit code 0 = Expand changes nothing except the selected slot, unless the two
slots really hold different settings. Stop if Ableton Live is open: this loads 360 Link.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from verify_in_link import LINK, changed, coherence_values, scripted, with_coherence_hosted  # noqa: E402
import subprocess  # noqa: E402

PRESS = "Gate Expander"
failures = 0


def check(ok, what):
    global failures
    failures += not ok
    print(f"  {'ok  ' if ok else 'FAIL'} {what}")


def values(work, run, name):
    return coherence_values((work / run / f"{name}.state").read_bytes())


def moved(a, b):
    return [k for k in changed(a, b) if k != "abSlot"]


def main():
    host, work = sys.argv[1], Path(sys.argv[2])
    work.mkdir(parents=True, exist_ok=True)
    subprocess.run([host, "link-walk", "VST3", LINK, str(work / "default")], capture_output=True, timeout=120, check=True)
    state = work / "coherence.component"
    state.write_bytes(with_coherence_hosted((work / "default/base.state").read_bytes()))

    print("1. A new instance, nothing touched: press Expand.")
    scripted(host, work, "untouched", state, [("save", "a"), ("set", PRESS, 1), ("save", "b"), ("set", PRESS, 0), ("save", "a2")])
    a, b, a2 = (values(work, "untouched", n) for n in ("a", "b", "a2"))
    check(b["abSlot"] == "1" and a2["abSlot"] == "0", f"the slot goes A, B, A (read {a['abSlot']}, {b['abSlot']}, {a2['abSlot']})")
    check(moved(a, b) == [], f"to B, other settings that moved: {moved(a, b) or 'none'}")
    check(moved(b, a2) == [], f"back to A, other settings that moved: {moved(b, a2) or 'none'}")

    print("\n2. As in use: on A, turn the compressor on, select M/S and move Threshold on the UC1, then press Expand.")
    set_up = [("set", "Dynamics In", 1), ("set", "EQ Type", 1), ("set", "Comp Threshold", 0.25), ("set", "HF Gain", 0.75)]
    scripted(host, work, "in-use", state, [*set_up, ("save", "a"), ("set", PRESS, 1), ("save", "b"), ("set", PRESS, 0), ("save", "a2")])
    a, b, a2 = (values(work, "in-use", n) for n in ("a", "b", "a2"))
    check(all(k in changed(values(work, "untouched", "a"), a) for k in ("dyn_on", "eq_ms", "comp_threshold", "hf_gain")), "the set-up moved Dyn In, M/S, Threshold and HF Gain")
    check(b["abSlot"] == "1", "pressing Expand selects B")
    check(moved(a, b) == [], f"compressor, M/S and Threshold unchanged by the press; settings that moved: {moved(a, b) or 'none'}")
    check(moved(b, a2) == [] and a2["abSlot"] == "0", f"pressing again selects A; settings that moved: {moved(b, a2) or 'none'}")

    print("\n3. Independent settings: make B different, and each press brings back that slot exactly.")
    scripted(host, work, "independent", state, [
        *set_up, ("save", "a"), ("set", PRESS, 1),
        ("set", "Dynamics In", 0), ("set", "EQ Type", 0), ("set", "Comp Threshold", 0.75), ("set", "HF Gain", 0.25), ("save", "b"),
        ("set", PRESS, 0), ("save", "a2"), ("set", PRESS, 1), ("save", "b2")])
    a, b, a2, b2 = (values(work, "independent", n) for n in ("a", "b", "a2", "b2"))
    want = ["comp_threshold", "dyn_on", "eq_ms", "hf_gain"]
    check(moved(a, b) == want, f"B differs from A only where it was set to: {moved(a, b)}")
    check(moved(a, a2) == [] and a2["abSlot"] == "0", f"back on A, it is exactly as it was; differences: {moved(a, a2) or 'none'}")
    check(moved(b, b2) == [] and b2["abSlot"] == "1", f"back on B, it is exactly as it was; differences: {moved(b, b2) or 'none'}")

    print(f"\nFailures: {failures}.")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
