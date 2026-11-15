#!/usr/bin/env python3
"""Audits a 360 Link profile against the live plug-ins and SSL's meaning of each slot. Read-only.

  PlugHost dump VST3 /Library/Audio/Plug-Ins/VST3/COHERENCE.vst3 > coherence.txt
  PlugHost dump VST3 "/Library/Audio/Plug-Ins/VST3/SSL 360 Link.vst3" > link.txt
  python3 audit_profile.py coherence.txt link.txt [profile.user] [SSL360Link folder]

Exit code 0 = no defects. A mapping can resolve perfectly and still be wrong, so this also checks what each slot means.
make_profile.py uses the same rules and refuses to write a profile that breaks them.
"""
import collections
import glob
import os
import re
import sys
import xml.etree.ElementTree as ET

PROFILES = "/Library/Application Support/SSL/SSLPlugins/SSL360Link"

# THE RULES. A mapping has to be listed here, so changing one is always a deliberate edit.
#
# Slots that SSL defines as a specific function, with the parameter IDs allowed on them. EQ Output on Output Trim is the
# owner's decision (RELEASE-PLAN.md). Output Trim doesn't appear in the UC1's Extended menu, so EQ Output is also on
# Saturation Amount, the slot SSL's own strips use for an extra control there (Mic, THD, Drive): the owner's decision of
# 2026-11-07. COHERENCE has no saturation in this release, so nothing else competes for it.
NAMED_FUNCTION = {"CompMix": {"comp_mix"}, "InputTrim": {"input"}, "OutputTrim": {"output", "eq_output"},
                  "LinkableFaderLevel": {"output"}, "SaturationAmount": {"eq_output"}, "SaturationIn": set()}
# The only parameter that may be on two slots. One slot is normally enough, and no SSL profile does this; both slots were
# tested moving it through 360 Link (verify_in_link.py).
ON_TWO_SLOTS = {"eq_output"}
EXACT = {"EqIn": "eq_on", "DynamicsIn": "dyn_on", "CompRatio": "comp_ratio", "CompThreshold": "comp_threshold",
         "LowEqGain": "focus_lf_gain", "LowEqFreq": "focus_lf_freq",
         "LowMidEqGain": "focus_lmf_gain", "LowMidEqFreq": "focus_lmf_freq", "LowMidEqQ": "focus_lmf_q",
         "HighMidEqGain": "focus_hmf_gain", "HighMidEqFreq": "focus_hmf_freq", "HighMidEqQ": "focus_hmf_q",
         "HighEqGain": "focus_hf_gain", "HighEqFreq": "focus_hf_freq"}
# Slots whose own function COHERENCE doesn't have, or that the owner's v0.3 profile placed elsewhere. Slot: (parameter, why).
REPURPOSED = {"CompRelease": ("comp_attack", "owner v0.3"), "GateRange": ("comp_release", "owner v0.3"),
              "GateThreshold": ("comp_knee", "owner v0.3"), "GateRelease": ("comp_sc_hpf", "owner v0.3"),
              "CompFastAttack": ("comp_auto_release", "owner v0.3"), "HighPassFreq": ("focus_lf_q", "owner v0.3"),
              "LowPassFreq": ("focus_hf_q", "owner v0.3"), "GateHold": ("comp_makeup", "owner, 2026-11-06"),
              "GateExpander": ("ab_select", "owner, 2026-11-07"),
              "EqE": ("eq_ms", "passed UC1"), "HighEqBell": ("eq_channel", "passed UC1"),
              "CompPeak": ("comp_auto_makeup", "unconfirmed on UC1")}


def rule_problem(slot, pid):
    """Why this parameter can't go on this slot, or None."""
    if slot in NAMED_FUNCTION:
        if pid not in NAMED_FUNCTION[slot]:
            allowed = ", ".join(sorted(NAMED_FUNCTION[slot])) or "nothing in this release"
            return f"{slot} is a named SSL function and can't carry {pid} (allowed: {allowed})"
    elif slot in EXACT:
        if pid != EXACT[slot]:
            return f"{slot} must carry {EXACT[slot]}, not {pid}"
    elif slot in REPURPOSED:
        if pid != REPURPOSED[slot][0]:
            return f"{slot} must carry {REPURPOSED[slot][0]}, not {pid}; change the rule first if this is deliberate"
    else:
        return f"{slot} has no rule, so {pid} can't go on it until one is added"
    return None


def problems(assignments):
    """All the rule breaches in a list of (slot, parameter ID)."""
    found = [p for p in (rule_problem(s, pid) for s, pid in assignments) if p]
    for label, values in (("slot", [s for s, _ in assignments]), ("parameter", [pid for _, pid in assignments])):
        found += [f"{label} {v} is mapped {n} times" for v, n in collections.Counter(values).items()
                  if n > 1 and not (label == "parameter" and v in ON_TWO_SLOTS and n == 2)]
    return found


def parameter_ids():
    ids = ["input", "eq_on", "dyn_on", "comp_threshold", "comp_ratio", "comp_attack", "comp_release", "comp_auto_release",
           "comp_knee", "comp_makeup", "comp_auto_makeup", "comp_mix", "comp_sc_hpf", "order", "mid", "side", "ms_link",
           "balance", "mono", "output", "bypass", "eq_mode", "gr_meter", "eq_output", "eq_ms", "eq_channel", "ab_select"]
    for band in ("lf", "lmf", "hmf", "hf"):
        for what in ("gain", "freq", "q"):
            ids += [f"{band}_{what}", f"side_{band}_{what}", f"focus_{band}_{what}"]
    for band in ("lf", "hf"):
        ids += [f"{band}_type", f"side_{band}_type"]
    return ids


def juce_hash(s):
    h = 0
    for c in s:
        h = (h * 31 + ord(c)) & 0xFFFFFFFFFFFFFFFF
    return h & 0x7FFFFFFF


def parameters(path):
    return {m.group(2): int(m.group(1))
            for m in re.finditer(r"^\[(\d+)\] (.+?) \| current", open(path, errors="replace").read(), re.M)}


def factory_uses(folder):
    uses = collections.defaultdict(list)
    for f in glob.glob(os.path.join(folder, "*.factory")):
        for lp in ET.parse(f).getroot().iter("LINK_PARAM"):
            uses[lp.get("LinkParamId")].append(lp.get("LinkParamLegend"))
    return uses


def main():
    hosted, link = parameters(sys.argv[1]), parameters(sys.argv[2])
    profile = sys.argv[3] if len(sys.argv) > 3 else os.path.join(PROFILES, "COHERENCE.user")
    uses = factory_uses(sys.argv[4] if len(sys.argv) > 4 else PROFILES)
    ids = {juce_hash(p): p for p in parameter_ids()}
    print(f"COHERENCE: {len(hosted)} parameters, {len(ids)} distinct IDs (of {len(parameter_ids())}). 360 Link: {len(link)} slots.")

    invalid, assignments = [], []
    rows = list(ET.parse(profile).getroot().iter("LINK_PARAM"))
    for lp in rows:
        slot, name, index = lp.get("LinkParamId"), lp.get("LinkParamName"), int(lp.get("LinkParamIndex"))
        hp = lp.find("HOSTED_PARAM")
        hosted_name, hosted_index, hosted_id = hp.get("HostedParamName"), int(hp.get("HostedParamIndex")), int(hp.get("HostedParamId"))
        pid = ids.get(hosted_id, "?")
        assignments.append((slot, pid))
        if link.get(name) != index:
            invalid.append(f"{slot}: 360 Link has {name!r} at {link.get(name)}, the profile says {index}")
        if hosted.get(hosted_name) != hosted_index or pid == "?":
            invalid.append(f"{slot}: {hosted_name!r} index {hosted_index}, ID {hosted_id} doesn't match COHERENCE")
        verdict = "WRONG" if rule_problem(slot, pid) else REPURPOSED[slot][1] if slot in REPURPOSED else "ok"
        print(f"  {name:18} #{index:<2} -> [{hosted_index:>2}] {hosted_name:15} {pid:18} {verdict}")

    wrong = problems(assignments)
    print(f"\nMappings: {len(rows)}. Don't resolve: {len(invalid)}. Break the rules: {len(wrong)}.")
    for line in invalid:
        print("  INVALID", line)
    for line in wrong:
        slot = line.split()[0]
        used = sorted(set(uses.get(slot, [])))[:8]
        print("  WRONG", line + (f"; SSL's factory profiles use it for: {', '.join(used)}" if used else ""))
    sys.exit(1 if invalid or wrong else 0)


if __name__ == "__main__":
    main()
