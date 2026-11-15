#!/usr/bin/env python3
"""Measures the plug-in's Classic panel against the approved design, region by region. Needs Pillow.

  build/CoherenceTests_artefacts/Release/CoherenceTests --approved-snapshot /tmp/classic.png classic
  python3 Tools/gui-preview/compare_classic.py /tmp/classic.png

The approved image shows sample meter levels and a sample version, so those regions always differ. The others must be 0.00.
Exit code 0 = the fixed regions match.
"""
import sys
from pathlib import Path
from PIL import Image, ImageChops

APPROVED = Path(__file__).resolve().parents[2] / "mac-results/gui-preview/COHERENCE-preview-A-default.png"
FIXED = {"EQ column": (0, 0, 360, 1268), "header": (360, 0, 1080, 78), "centre top block": (540, 80, 900, 410),
         "upper compressor": (1080, 0, 1440, 410), "lower compressor": (1080, 610, 1440, 940), "plate and order screen": (1080, 940, 1440, 1268)}
LIVE = {"centre strip (M/S meter)": (540, 414, 900, 1268), "gain reduction ladder": (1270, 410, 1440, 610), "meter columns": (360, 78, 540, 1268), "bottom bar (version)": (0, 1268, 1440, 1331)}
a = Image.open(APPROVED).convert("RGB")
b = Image.open(sys.argv[1]).convert("RGB")


def mean(box):
    h = ImageChops.difference(a.crop(box), b.crop(box)).convert("L").histogram()
    return sum(i * c for i, c in enumerate(h)) / sum(h)


failed = False
for name, box in FIXED.items():
    m = mean(box)
    failed = failed or m > 0.005
    print(f"  {'ok  ' if m <= 0.005 else 'FAIL'} {name:26} mean difference {m:5.2f}/255")
for name, box in LIVE.items():
    print(f"  live {name:26} mean difference {mean(box):5.2f}/255 (shows sample values in the approved image)")
sys.exit(1 if failed else 0)
