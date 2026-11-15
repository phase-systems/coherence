#!/usr/bin/env python3
"""Tests the 360 Link profile rules and generator. Loads no plug-in. Exit code 0 = all pass.

  python3 Tools/ssl360link/test_profile_rules.py [parameter dump]
"""
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).parent
sys.path.insert(0, str(HERE))
from audit_profile import problems, rule_problem

DUMP = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE.parent.parent / "mac-results/v0.5.2/vst3-parameters.txt"
failures = 0


def check(what, ok):
    global failures
    failures += not ok
    print(("PASS " if ok else "FAIL ") + what)


def generate(setup=""):
    """Runs the generator with a changed table. Returns (exit code, the file it wrote or None, its message)."""
    with tempfile.TemporaryDirectory() as d:
        out = Path(d) / "out.user"
        code = (f"import sys; sys.path.insert(0, {str(HERE)!r}); import make_profile as m\n{setup}\n"
                f"sys.argv = ['make_profile.py', {str(DUMP)!r}, {str(out)!r}]; m.main()")
        r = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True)
        return r.returncode, out.read_bytes() if out.exists() else None, r.stderr + r.stdout


# The three mappings the owner's UC1 test exposed, and the rest of the rules.
for slot, pid in (("CompMix", "side"), ("InputTrim", "mid"), ("SaturationAmount", "comp_makeup"), ("CompMix", "mid"),
                  ("SaturationIn", "comp_mix"), ("OutputTrim", "side"), ("LinkableFaderLevel", "side"),
                  ("GateExpander", "comp_mix"), ("GateFastAttack", "ab_select")):
    check(f"rules refuse {slot} -> {pid}", rule_problem(slot, pid) is not None)
for slot, pid in (("CompMix", "comp_mix"), ("InputTrim", "input"), ("OutputTrim", "eq_output"), ("OutputTrim", "output"),
                  ("GateHold", "comp_makeup"), ("HighEqBell", "eq_channel"), ("SaturationAmount", "eq_output"),
                  ("GateExpander", "ab_select")):
    check(f"rules allow {slot} -> {pid}", rule_problem(slot, pid) is None)
check("rules refuse a slot with no rule", rule_problem("LowEqBell", "lf_type") is not None)
check("rules refuse changing an existing mapping unreviewed", rule_problem("CompRatio", "comp_knee") is not None)
check("rules refuse one parameter on two slots", any("mapped 2 times" in p for p in problems([("CompMix", "comp_mix"), ("OutputTrim", "comp_mix")])))
check("rules allow EQ Output on its two slots", not problems([("OutputTrim", "eq_output"), ("SaturationAmount", "eq_output")]))

# The generator itself, with a bad table: it must fail, say why and write nothing.
for label, setup in (("Comp Mix -> Side Level", 'm.ADDED["CompMix"] = ("side", "Side Level", 23, "Comp Mix", "Side")'),
                     ("Input Trim -> Mid Level", 'm.ADDED["InputTrim"] = ("mid", "Mid Level", 4, "Input Trim", "Mid")'),
                     ("Saturation Amount -> Makeup", 'm.ADDED["SaturationAmount"] = ("comp_makeup", "Makeup", 44, "Saturation Amount", "Makeup")'),
                     ("Mix left on Gate Hold as well", 'm.MOVED.clear()')):
    code, written, message = generate(setup)
    check(f"generator refuses {label}, writes nothing", code != 0 and written is None and "Profile not written" in message)

# What is committed is exactly what the generator produces.
code, written, message = generate()
check("generator accepts the current table", code == 0 and written is not None)
check("COHERENCE.user is exactly what the generator produces", written == (HERE / "COHERENCE.user").read_bytes())

print(f"\n{failures} failed" if failures else "\nAll passed")
sys.exit(1 if failures else 0)
