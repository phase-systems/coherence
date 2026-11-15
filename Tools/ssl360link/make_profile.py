#!/usr/bin/env python3
"""Writes the SSL 360 Link profile for COHERENCE, and checks it against the plug-in.

  PlugHost dump VST3 <COHERENCE.vst3> > dump.txt
  python3 make_profile.py dump.txt COHERENCE.user

It starts from the v0.3 profile (COHERENCE-v0.3.user):
  - the 12 EQ knobs move to the focus parameters, so that they follow the Mid/Side selector;
  - Gate Hold moves from Mix to Makeup, because Mix belongs on Comp Mix;
  - the additions use free slots (mac-results/v0.5.1/extended-functions-audit.md, mac-results/v0.5.2/).
Hosted indices and names come from the plug-in's own parameter list, and the IDs from JUCE's hash of the parameter ID.
Nothing is written if any of them doesn't match, or if a mapping breaks the rules in audit_profile.py.
"""
import re
import sys
from pathlib import Path

from audit_profile import juce_hash, parameter_ids, problems

HERE = Path(__file__).parent

# Link slot: the band control it moves to. Index, name and legend stay as they were.
FOCUS = {"LowEqGain": "lf_gain", "LowEqFreq": "lf_freq", "HighPassFreq": "lf_q",
         "LowMidEqGain": "lmf_gain", "LowMidEqFreq": "lmf_freq", "LowMidEqQ": "lmf_q",
         "HighMidEqGain": "hmf_gain", "HighMidEqFreq": "hmf_freq", "HighMidEqQ": "hmf_q",
         "HighEqGain": "hf_gain", "HighEqFreq": "hf_freq", "LowPassFreq": "hf_q"}
# Link slot from the v0.3 profile that now carries something else: (parameter ID, parameter name, legend).
MOVED = {"GateHold": ("comp_makeup", "Makeup", "Makeup")}
# Link slot: (parameter ID, parameter name, LinkParamIndex, LinkParamName, legend). Indices as in SSL's profiles of this format.
ADDED = {"OutputTrim": ("eq_output", "EQ Output", 37, "Output Trim", "EQ Output"),
         "SaturationAmount": ("eq_output", "EQ Output", 44, "Saturation Amount", "EQ Output"),
         "CompMix": ("comp_mix", "Mix", 23, "Comp Mix", "Mix"),
         "GateExpander": ("ab_select", "A/B", 33, "Gate Expander", "A/B"),
         "EqE": ("eq_ms", "EQ M/S", 14, "EQ Type", "M/S"),
         "HighEqBell": ("eq_channel", "EQ Channel", 8, "HF Type", "Mid/Side"),
         "CompPeak": ("comp_auto_makeup", "Auto Makeup", 25, "Comp Peak", "Auto Makeup")}


def parameter_names(dump):
    return {m.group(2): int(m.group(1)) for m in re.finditer(r"^\[(\d+)\] (.+?) \| current", dump, re.M)}


def entry(slot, index, name, legend, inverted, hosted_index, hosted_id, hosted_name):
    return (f'  <VALUE name="{slot}">\n'
            f'    <LINK_PARAM LinkParamIndex="{index}" LinkParamId="{slot}" LinkParamName="{name}"\n'
            f'                LinkParamLegend="{legend}" LinkInverted="{inverted}">\n'
            f'      <HOSTED_PARAM HostedParamIndex="{hosted_index}" HostedParamId="{hosted_id}" HostedParamName="{hosted_name}"/>\n'
            f'    </LINK_PARAM>\n'
            f'  </VALUE>\n')


def main():
    names = parameter_names(Path(sys.argv[1]).read_text(errors="replace"))
    old = (HERE / "COHERENCE-v0.3.user").read_text()
    head = old[:old.index('  <VALUE name="HighMidEqGain">')]
    meter = re.search(r'  <VALUE name="Compression Meter">.*?</VALUE>\n', old, re.S).group(0)

    ids = {juce_hash(p): p for p in parameter_ids()}
    out = [head]
    assignments = []
    count = 0
    for m in re.finditer(r'<VALUE name="([^"]+)">\s*<LINK_PARAM LinkParamIndex="(\d+)" LinkParamId="[^"]+" LinkParamName="([^"]*)"\s+'
                         r'LinkParamLegend="([^"]*)" LinkInverted="(\d)">\s*<HOSTED_PARAM HostedParamIndex="(\d+)" HostedParamId="(\d+)" HostedParamName="([^"]+)"',
                         old):
        slot, index, name, legend, inverted, hosted_index, hosted_id, hosted_name = m.groups()
        if slot in FOCUS:
            pid = "focus_" + FOCUS[slot]
            hosted_name = "Focus " + hosted_name
            hosted_id = juce_hash(pid)
        elif slot in MOVED:
            pid, hosted_name, legend = MOVED[slot]
            hosted_id = juce_hash(pid)
        if names.get(hosted_name) is None:
            sys.exit(f"{slot}: the plug-in has no parameter called {hosted_name}")
        if slot not in FOCUS and slot not in MOVED and names[hosted_name] != int(hosted_index):
            sys.exit(f"{slot}: {hosted_name} moved from index {hosted_index} to {names[hosted_name]}")
        if int(hosted_id) not in ids:
            sys.exit(f"{slot}: ID {hosted_id} isn't the hash of any parameter ID")
        assignments.append((slot, ids[int(hosted_id)]))
        out.append(entry(slot, index, name, legend, inverted, names[hosted_name], hosted_id, hosted_name))
        count += 1
    if count != 22:
        sys.exit(f"expected the 22 existing mappings, found {count}")

    for slot, (pid, hosted_name, index, name, legend) in ADDED.items():
        if hosted_name not in names:
            sys.exit(f"{slot}: the plug-in has no parameter called {hosted_name}")
        assignments.append((slot, pid))
        out.append(entry(slot, index, name, legend, 0, names[hosted_name], juce_hash(pid), hosted_name))

    breaches = problems(assignments)
    if breaches:
        sys.exit("Profile not written. Mappings that break the rules in audit_profile.py:\n  " + "\n  ".join(breaches))

    out.append(meter)
    out.append("</PROPERTIES>\n")
    Path(sys.argv[2]).write_text("".join(out))
    print(f"{sys.argv[2]}: {len(assignments)} mappings: 22 from v0.3 (12 moved to focus parameters, {len(MOVED)} to another parameter) + {len(ADDED)} added")


if __name__ == "__main__":
    main()
