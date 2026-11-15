#!/usr/bin/env python3
"""Tests the installed 360 Link profile through the real SSL 360 Link, without the hardware.

  python3 Tools/ssl360link/verify_in_link.py <PlugHost> <work folder> [<a saved 360 Link state to start from>]

Hosts COHERENCE inside 360 Link, moves each slot in turn as the UC1 would, and reads from COHERENCE's own saved state
which parameter changed. Exit code 0 = every slot moves exactly what the profile says. Also reports whether 360 Link's
saved state contains the mapping. Loading 360 Link re-saves its favourites file, and SSL 360 Core sees it for a moment.
"""
import re
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

LINK = "/Library/Audio/Plug-Ins/VST3/SSL 360 Link.vst3"
PROFILE = "/Library/Application Support/SSL/SSLPlugins/SSL360Link/COHERENCE.user"
SCANNED = "/Library/Application Support/SSL/SSLPlugins/SSL360Link/SSL 360 Link.scanned"
JUCE64 = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"


def from_juce_base64(text):
    """juce::MemoryBlock::fromBase64Encoding: '<size>.<characters>', 6 bits per character, least significant first."""
    size, body = text.strip().split(".", 1)
    bits, count, out = 0, 0, bytearray()
    for c in body:
        bits |= JUCE64.index(c) << count
        count += 6
        while count >= 8:
            out.append(bits & 255)
            bits >>= 8
            count -= 8
    return bytes(out[:int(size)])


def link_xml(raw):
    start = raw.index(b"<?xml")
    return raw[start:raw.index(b"</SSL_PLUGIN_STATE>") + len(b"</SSL_PLUGIN_STATE>")].decode()


def with_coherence_hosted(raw):
    """A 360 Link state hosting COHERENCE, with default settings: what choosing it in 360 Link gives."""
    xml = link_xml(raw)
    record = re.search(r'<PLUGIN name="COHERENCE".*?/>', Path(SCANNED).read_text(errors="replace"), re.S).group(0)
    new, n = re.subn(r'(<NODE nodeIsIO="0">)<PLUGIN .*?/>(?:<HOSTEDSTATE>.*?</HOSTEDSTATE>)?', lambda m: m.group(1) + record, xml, flags=re.S)
    if n != 1:
        sys.exit("360 Link's default state has no hosted plug-in to replace; set a default hosted plug-in in 360 Link")
    new = new.encode()
    # VstW and CcnK headers, then the chunk: 'VC2!', the XML's size, the XML, and JUCE's private data.
    start, end = raw.index(b"<?xml"), raw.index(b"</SSL_PLUGIN_STATE>") + len(b"</SSL_PLUGIN_STATE>")
    out = bytearray(raw[:start] + new + raw[end:])
    struct.pack_into(">I", out, 20, len(out) - 24)   # CcnK size
    struct.pack_into(">I", out, 172, len(out) - 176)  # chunk size
    struct.pack_into("<I", out, 180, len(new))        # XML size
    return bytes(out)


def coherence_values(raw):
    node = ET.fromstring(link_xml(raw)).find(".//NODE[@nodeIsIO='0']")
    hosted = from_juce_base64(node.find("HOSTEDSTATE/IComponent").text)
    xml = hosted[hosted.index(b"<?xml"):]
    root = ET.fromstring(xml[:xml.rindex(b">") + 1].decode())
    values = {p.get("id"): p.get("value") for p in root.findall("PARAM")}    # not OTHER_SLOT's, the other A/B slot
    values["abSlot"] = root.get("abSlot", "0")
    return values


def changed(base, values):
    """The settings that really moved. Writing a value back can leave a rounding difference in the last digits."""
    return sorted(k for k in base if abs(float(values.get(k, base[k])) - float(base[k])) > 0.02)


def expected(pid):
    if pid == "ab_select":
        return "abSlot"                                                # the switch selects a slot; it holds no setting
    return pid[len("focus_"):] if pid.startswith("focus_") else pid    # a focus move lands on the channel's parameter


def scripted(host, work, name, state, steps):
    """Runs steps in 360 Link; returns [(slot, readout, value)] for each 'show'."""
    script = work / f"{name}.txt"
    script.write_text("\n".join("\t".join(str(x) for x in s) for s in steps) + "\n")
    r = subprocess.run([host, "link-walk", "VST3", LINK, str(work / name), str(state), str(script)],
                       capture_output=True, text=True, errors="replace", timeout=300)
    if r.returncode:
        sys.exit(f"The host stopped: {r.returncode}")
    return [tuple(l.split("\t")) for l in r.stdout.splitlines() if l.count("\t") == 2]


def grid(text, step=0.5):
    number = float(text.replace("\u2212", "-").replace("+", ""))
    return abs(number / step - round(number / step)) < 1e-6


def ab_and_stepping(host, work, state, base):
    failures = 0

    def check(ok, what):
        nonlocal failures
        failures += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} {what}")

    print("\nA/B through 360 Link, on the Gate Expander slot:")
    press, mix, gain, eq = "Gate Expander", "Comp Mix", "HF Gain", "Output Trim"
    show = [("show", n) for n in (press, mix, gain, eq)]
    steps = [("set", mix, 0.2), ("set", gain, 0.75), ("set", eq, 0.25), *show,        # A's settings
             ("set", press, 1), *show,                                                # to B: a copy of A, the first time
             ("set", mix, 0.5), ("set", gain, 0.25), ("set", eq, 0.75), *show,        # B's settings
             ("set", press, 0), *show,                                                # back to A
             ("set", press, 1), *show, ("save", "onB")]                               # B again
    rows = scripted(host, work, "ab", state, steps)
    read = [{s: r for s, r, _ in rows[i:i + 4]} for i in range(0, len(rows), 4)]
    a1, b0, b1, a2, b2 = read
    check(a1[press] == "A" and b0[press] == "B", f"pressing it switches from A to B, and the slot reads {b0[press]}")
    check(b0 == {**a1, press: "B"}, f"the press moves nothing else, because B starts as a copy of A: Mix {b0[mix]}, HF Gain {b0[gain]}, EQ Output {b0[eq]}")
    check(b1[mix] != a1[mix] and b1[gain] != a1[gain] and b1[eq] != a1[eq], f"B then takes its own settings: Mix {b1[mix]}, HF Gain {b1[gain]}, EQ Output {b1[eq]}")
    check(a2 == {**a1, press: "A"}, f"back on A, everything returns: Mix {a2[mix]}, HF Gain {a2[gain]}, EQ Output {a2[eq]}")
    check(b2 == {**b1, press: "B"}, f"back on B, everything returns: Mix {b2[mix]}, HF Gain {b2[gain]}, EQ Output {b2[eq]}")
    saved = coherence_values((work / "ab/onB.state").read_bytes())
    check(saved["abSlot"] == "1", "360 Link's saved state keeps COHERENCE on B")

    print("\nStepping through 360 Link, moving 0.16 dB at a time (Standard, 0.5 dB):")
    sweep = lambda slot, span: [s for k in range(25) for s in (("set", slot, 0.5 + k * 0.16 / span), ("show", slot))]
    for slot, label, span in (("Saturation Amount", "EQ Output, Extended", 30.0), ("Output Trim", "EQ Output, Output Trim", 30.0), ("HF Gain", "HF Gain, the normal EQ knob", 30.0)):
        texts = [r for _, r, _ in scripted(host, work, "step-" + slot.replace(" ", ""), state, sweep(slot, span))]
        check(all(grid(t) for t in texts) and float(texts[-1].replace("+", "")) > 3.0, f"{label}: {' '.join(dict.fromkeys(texts))} dB")
    return failures


def main():
    host, work = sys.argv[1], Path(sys.argv[2])
    work.mkdir(parents=True, exist_ok=True)
    from audit_profile import juce_hash, parameter_ids
    ids = {juce_hash(p): p for p in parameter_ids()}
    profile = {int(lp.get("LinkParamIndex")): (lp.get("LinkParamName"), ids[int(lp.find("HOSTED_PARAM").get("HostedParamId"))])
               for lp in ET.parse(PROFILE).getroot().iter("LINK_PARAM")}

    if len(sys.argv) > 3:
        state = Path(sys.argv[3])
    else:
        subprocess.run([host, "link-walk", "VST3", LINK, str(work / "default")], capture_output=True, timeout=120, check=True)
        state = work / "coherence.component"
        state.write_bytes(with_coherence_hosted((work / "default/base.state").read_bytes()))
    run = subprocess.run([host, "link-walk", "VST3", LINK, str(work / "walk"), str(state)], capture_output=True, text=True, errors="replace", timeout=300)
    if run.returncode:
        sys.exit(f"The host stopped: {run.returncode}")

    base_raw = (work / "walk/base.state").read_bytes()
    base_xml = link_xml(base_raw)
    node = ET.fromstring(base_xml).find(".//NODE[@nodeIsIO='0']")
    print(f"360 Link hosts: {node.find('PLUGIN').get('name')}. Mapping stored in its saved state: "
          f"{'YES' if 'LINK_PARAM' in base_xml or 'HOSTED_PARAM' in base_xml else 'no'}")
    base = coherence_values(base_raw)

    failures = walked = 0
    for line in run.stdout.splitlines():
        parts = line.split("\t")
        if len(parts) != 4 or not parts[0].isdigit():
            continue
        index, name, before, after = int(parts[0]), parts[1], parts[2], parts[3]
        values = coherence_values((work / f"walk/{index}.state").read_bytes())
        moved = changed(base, values)
        want = [expected(profile[index][1])] if index in profile else []
        ok = moved == want
        failures += not ok
        walked += 1
        if index in profile or moved or name in ("Input Trim", "Saturation Amount"):
            print(f"  {'ok  ' if ok else 'FAIL'} {name:18} #{index:<2} moved {', '.join(moved) or 'nothing':20} readout {before} -> {after}")
    print(f"\n{walked} slots moved, {len(profile)} of them mapped. Unmapped slots not listed moved nothing. Failures: {failures}.")
    if walked < 42:
        sys.exit("Too few slots were walked")
    failures += ab_and_stepping(host, work, work / "walk/base.state", base)
    print(f"\nAll failures: {failures}.")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    sys.path.insert(0, str(Path(__file__).parent))
    main()
