# COHERENCE changelog

Measurements behind each entry are in `VALIDATION.md`, and the reasons are in `DESIGN.md`. Backups of earlier versions sit next to the installer: `Coherence-v0.1.tar.gz` and `Coherence-v0.2.0.tar.gz`.

## v0.6.2 (2026-11-14): release. Three undo fixes, a guard on saved sets; no audio change

From a technical review of 0.6.1 before release, on branch `rc/1.0`. Three candidates, all version 0.6.2: `v0.6.2-rc1` (the undo fixes), `v0.6.2-rc2` (the same, plus the saved-set guard, at the owner's request) and `v0.6.2-rc3` (rc2's plug-in with a new 360 Link profile, withdrawn). **The release `v0.6.2` is rc2:** the same source and the same 360 Link profile (sha256 `c1f93b6f…`). Records: `mac-results/v0.6.2/README.md` `mac-results/v0.6.2/rc2/README.md` and `mac-results/v0.6.2/rc3/README.md`.

- **The owner's acceptance test of rc2 passed (2026-11-14):** the export of an existing Live project nulls completely against 0.6.1, stepped gains on the UC1 and the rest of the checklist work, and UF1 Focus Mode works.
- **360 Link profile: unchanged from 0.6.1 (third candidate withdrawn).** rc3 put Mid Level and Side Level on Quick Access 1 and 2 (the UC1's Extended rows Quick Ctrl 1 and 2), at the owner's request. Through 360 Link they stepped in 0.5 dB, but on the UC1 those rows are buttons: turning them only jumps between -12 and +12 dB (owner's test, 2026-11-14), so the change was reverted. The Extended rows Plugin On/Off, Pan, Width, Solo Safe and A/B are 360 Link's own and can't be assigned or removed; their effect on audio was measured (`mac-results/v0.6.2/extended-functions/`).

- **Undo history under host automation:** values a host writes are recorded into the open undo step, and with two or more parameters moving they can't be merged. The step grew for as long as the automation played, with the panel open or closed: 85 entries a second for two parameters, about 27 MB an hour for each instance, never released, and one press of UNDO then rewound the automated controls. A step with more than 2048 entries now starts the history again, so the history is bounded and UNDO reaches back about 24 s at most while two parameters are automated. UNDO can still take an automated control back within that step. One automated parameter was always a single entry, and still is.
- **First move in a new instance:** since v0.5.0 the first move of 36 of the 53 controls wasn't recorded in an instance that hadn't loaded a set, and UNDO took back an earlier move of another control. The constructor now gives every parameter's entry its value.
- **UNDO straight after a change:** a change made less than about half a second earlier could be passed over. `undoStep` records pending values first.
- **Saved sets with values that aren't numbers (second candidate):** a set holding NaN left 44 of the 53 parameters NaN, the output silent and the readouts saying "nan", and every save wrote it back; an infinity put every control at one end of its range. Such a set comes from a host writing NaN to a parameter, or from a damaged file. A value that isn't a finite number now loads that parameter at its default, in the settings in use and in the other A/B slot, as a set from before the parameter existed does. A window scale that isn't finite counts as not chosen. Finite values load exactly as before, including the nearest end for one beyond its range. DESIGN.md section 21.
- **Not changed:** `Source/dsp`, the parameters, the state format, the GUI, the 360 Link profile. The audio is bit-identical to 0.6.1: 15.1 million samples with 17,207 automation writes across every parameter, A/B and recall; hashes 22/22; all 88 Weiss renders identical.
- **README** rewritten for this version (it described v0.2.1).
- **Tests added:** 19 suite checks: 10 on the undo history (6 fail against the 0.6.1 processor, as they should) and 9 on saved sets (7 fail against the first candidate; the other 2 test what must not change).
- **Checks** (`mac-results/v0.6.2/`): suite 271 PASS + 1 LIMIT, hashes 22/22, Weiss comparison identical, no heap calls in the audio callback, 3970 damaged states with none left non-finite, 24 instances at once, threaded stress, rates 22.05 to 384 kHz. `auval -strict` and the AU/VST3 format check (54 PASS) on a test-identity build of the first candidate; not rerun on the second, because the owner asked for no installation. Under the production identity, the installer's auval and format check (54 PASS) passed when the owner installed rc2 and rc3 (2026-11-14).
- **Owner's hardware test:** Live, UC1 and UF1 passed on rc2 (above).

## v0.6.1 (2026-11-07): NaN/Inf recovery, Expand only switches A/B, Weiss as the default (hardware test pending)

- **NaN and Inf input:** one bad sample no longer breaks the output. `Engine::process` takes NaN, Inf and anything beyond +120 dBFS as 0 before any processing, and resets itself if its output is ever non-finite. After a bad sample the output is bit-identical to a run that got 0 there. Finite audio is untouched: hashes 22/22, Weiss comparison unchanged. DESIGN.md section 21.
- **Expand (Gate EXP):** a press switches A and B and nothing else. The mapping was already right. B used to hold the factory defaults until first used, so the first switch reset the compressor, M/S and Threshold to them. B now starts as a copy of A. Sets from earlier builds keep their B as saved. DESIGN.md section 19.
- **Extended Functions:** Pan, Width, Solo Safe and A/B are SSL 360 Link's own rows. The profile can't remove them, so nothing changed.
- **Weiss is the default** for new instances, reporting 15 samples from creation. Saved sets keep their mode, and sets from before v0.2.5 open in COHERENCE mode. DESIGN.md section 22.
- **Not changed:** the EQ designs, `Equaliser`, compressor, latency, oversampling, parameter IDs, names and ranges, the 360 Link profile, the GUI.
- **Tests added:** 31 suite checks, `PlugHost nan-check`, `Tools/ssl360link/expand_check.py`. The format check now covers both EQ modes.
- **Checks** (`mac-results/v0.6.1/`): suite 252 PASS + 1 LIMIT, hashes 22/22, Weiss comparison identical, references 76/76, auval, format check 54 PASS, real-host NaN, A/B and focus checks, sets from 15 earlier builds, all 29 mappings and Expand through the real 360 Link.

## Release audit of 0.6.0 (2026-11-07): no code change

Verdict: not ready for public release. Findings and evidence: `mac-results/release-audit-0.6.0/REPORT.md`.

## v0.6.0 (2026-11-07): the final panel, with three themes (hardware test pending)

- **The final panel:** the approved preview A is now the plug-in's panel. EQ on the left, channel and levels in the centre, compression on the right. Base window size 900 x 832 (was 980 x 740), with the same 60 to 200 % range and a fixed shape.
- **Three themes:** Classic (the approved design, and the default), Warm Graphite and Champagne, under SETUP > APPEARANCE > Theme. A change applies at once in every open instance. The choice belongs to the user, in `COHERENCE.settings`, so sets and presets don't carry it.
- **A theme is one palette:** one function and one table entry in `Source/gui/Theme.cpp`. Controls, layout, parameters and audio don't know which is in use.
- **What changed in use:**
  - Values appear in the bottom display on hover or drag, not on each knob.
  - Hover a level meter for its peak, and click it to reset. Hover the gain reduction ladder for its number.
  - Click a knob's caption to type a value. Double-click the Mix and EQ Output readouts to type. Alt-click resets.
  - HF and LF type are one key that cycles BELL, SHELF and CUT.
  - No gate section or labels: the compressor reads as one section.
- **Font:** macOS's Helvetica Neue, so Inter is no longer built in (its files stay in `Assets/`).
- **Not changed:** `Source/dsp`, `Source/Parameters.*`, `Source/PluginProcessor.*`, `Source/EqFocus.*`, the 360 Link profile and JUCE (`git diff v0.5.2` is empty for all of them). Parameter IDs, names and ranges, automation and recall are as in 0.5.2.
- **Sessions:** a state saved by the 0.5.2 VST3 opens in 0.6.0 with all 67 readouts identical. A saved window scale still applies, to the new base size.
- **Tests added:** 19 suite checks (themes and reach of every control), `--approved-snapshot`, `Tools/gui-preview/compare_classic.py`.
- **Checks** (`mac-results/v0.6.0/`): suite 221 PASS + 1 LIMIT, hashes 22/22, Weiss comparison identical, references 76/76, auval, format check 54 PASS, all 29 mappings correct through the real 360 Link. Classic differs from the approved image by 0.00/255 in every region without sample meter values.
- **Not tested:** the UC1 and UF1, real mouse use and the window inside Live.

## v0.5.2 (2026-11-07): A/B on the UC1 and panel, EQ Output in Extended Functions (hardware test pending)

- **A/B on the UC1:** the Gate EXP switch toggles A and B and lights on B. The UC1's own A/B can't reach a hosted plug-in, so this is a new parameter, `ab_select`. It's last in the list, isn't automatable or saved, and no existing index or ID changes.
- **A/B on the panel:** clicking A or B always switches to the other. The lit button is the slot in use. The panel and the switch stay in step, including against late VST3 echoes.
- **EQ Output in Extended Functions:** also on the Saturation Amount slot, since Output Trim isn't listed. Whether the row appears needs the UC1.
- **Stepping:** no change needed. EQ Output steps 0.5 dB through 360 Link, on the same grid as the EQ gains.
- **Can't be done, SSL's limits:** Plugin On/Off and Pan can't be removed from the Extended menu. They're 360 Link's own.
- **Not changed:** `Source/dsp`, the EQ, the compressor, and every existing parameter ID and mapping.
- **Sessions:** a state saved by the 0.5.1 VST3 opens in 0.5.2 with all 66 readouts identical. Lanes on 360 Link's own Saturation Amount slot now move EQ Output.
- **Tests added:** 23 suite checks, `PlugHost ab-check`, `state-save` and `state-load`, and A/B and stepping in `verify_in_link.py`.
- **Checks** (`mac-results/v0.5.2/`): suite 202 PASS + 1 LIMIT, hashes 22/22, Weiss comparison identical, references 76/76, auval, format check 54 PASS, all 29 mappings correct through the real 360 Link.

## 360 Link profile corrected (2026-11-06; no plug-in change)

- **Owner's UC1 test of v0.5.1:** Mid/Side EQ, its settings preservation and 0.5 dB stepping passed. Extended Functions appeared, with some controls on the wrong parameters: Comp Mix controlled Side Level.
- **Root cause:** the profile assigned them that way. Stage 3 used named SSL functions as spare knobs. No parameter shifted. Audit: `mac-results/v0.5.1/extended-functions-audit.md`.
- **Profile:** Comp Mix = Mix, Gate Hold = Makeup, Saturation Amount and Input Trim empty. Mid Level and Side Level left the UC1.
- **Automation on 360 Link's own slots follows the slot:** a lane on Gate Hold now moves Makeup instead of Mix. Automation on COHERENCE's own parameters isn't affected.
- **Generator:** `make_profile.py` refuses a profile that breaks the rules in `audit_profile.py`. `test_profile_rules.py` tests this. `verify_in_link.py` moves each slot through the real 360 Link.

## v0.5.1 (2026-11-06): test build for the UC1, not the first release (branch `release/1.0`)

- **auval fixed:** the first install of this code failed auval, because moving a focus parameter moves another parameter. The focus parameters now declare the AU meta flag. AU only, no DSP change.
- **Installer:** the package is no longer v0.3.0. The installer starts its working folder empty, checks that the installed files are identical to the build, installs the 360 Link profile, keeps and removes a stale system-wide AU, keeps the previous profile and format check report, and reports failure when auval or the format check fails.
- **Not in this build:** a stepping fix, Weiss as the default, the owner's listening verdict.

### Everything else since v0.3.0

- **SSL 292 removed from the release path.** `release/1.0` starts from `main`. All saturation work is preserved: `../SSL292 archive/`, branch `ssl292-saturation`, tag `archive/ssl292-final`. Sets saved by v0.4.x load; their saturation settings are ignored (new check in the suite).
- **Installer:** the VST3 now goes to `/Library/Audio/Plug-Ins/VST3` when that folder is writable, because SSL 360 Link scans only that folder. Exactly one copy is installed, and every copy replaced is kept in `Installed builds`.
- **Measured, no plug-in change:** COHERENCE's automatic gain options against Pro-C 3 Clean's Auto Gain (`mac-results/v0.5.0/autogain-compare.md`, `Tools/proc3/autogain_compare.py`). `--render-file` gained an `autogain` key.
- **Plan:** `RELEASE-PLAN.md`. No DSP change so far: COHERENCE mode 22/22 hashes OK.
- **Global AUTO GAIN removed** (owner's decision): the `auto_gain` parameter, its measurement and correction, the panel button and readout. Old sets load and ignore it.
- **Panel:** M/S button and MID | SIDE selector in the EQ row; the Side channel's controls in the same places as the main ones, with amber arcs; EQ Output where AUTO GAIN was. No other layout changes.
- **The UC1 follows the Mid/Side selector:** twelve `focus_*` parameters stand for the EQ's gain, frequency and Q on the selected channel (DESIGN.md section 18). New profile `Tools/ssl360link/COHERENCE.user`, not installed yet: all 22 existing mappings keep their function, with EQ Output, Mid, Side, Makeup, M/S, the selector and AUTO makeup added.
- **M/S EQ, signal path and parameters:** `eq_ms` and `eq_channel` plus 14 Side parameters (`side_lf_gain` and so on), all after the meter. M/S encodes, runs the existing EQ on Mid and on a second instance for Side, and decodes. The stereo path is unchanged. 
- **EQ Output added** (`eq_output`, ±15 dB, default 0 dB): after the compressor in both orders, so it never changes what the compressor hears, and it follows EQ IN. A set from before it existed loads it at 0 dB.
- **AUTO makeup redesigned and on by default:** it now restores 80 % of the level the compressor measurably takes away, adds nothing below the threshold, and is independent of Pro-C 3 by the owner's decision. Over 63 stem cases: -0.66 LU median, -2.50 to -0.08 LU, never louder than the input (`mac-results/v0.5.0/automakeup.md`, DESIGN.md section 16). With AUTO off, renders are bit-identical to before. Saved sets keep their own AUTO setting; those saved with it on change level.

## v0.3.0 (2026-11-04): the compressor, modelled on FabFilter Pro-C 3 (Clean)

- **New compressor:** a model of Pro-C 3's default Clean algorithm, measured headlessly from the plug-in (DESIGN.md section 3, COMPRESSOR-TESTING.md 1.4):
  - instantaneous linked peak detector;
  - Pro-C 3's measured static curve and knee;
  - distance-dependent attack;
  - program-dependent release (fast path plus two slow stores);
  - long attacks shorten the release;
  - AUTO release as Pro-C 3's Auto Release.
- **Against Pro-C 3 at identical settings:**
  - the owner's 7 stems × 8 settings (0.5–6 dB GR): GR_avg within ±0.11 dB, envelope rms 0.075 dB median, null −47 dB median;
  - DC steps: median error 0.047 dB;
  - sines within 0.13 dB.
  - Report: `mac-results/proc3-clean/compare-v0.3.0.md`.
- **Knee:** shown and typed in dB, now 0–72 dB (the stored value is unchanged; 0.4 is still 8 dB).
- **360 Link / UC1 meter:** the Gain Reduction parameter now carries the actual reduction per block, without the 30 dB/s meter release. `Tools/ssl360link/COHERENCE.user` is a profile with the meter assigned.
- **Sound changes from v0.2.3** (intended):
  - for the same knob settings, sines and dense material get less reduction than before;
  - the release is program-dependent;
  - low tones with short times show Pro-C 3-like gain ripple (THD −39 dB at 50 Hz, 10/150 ms, 6 dB GR; v0.2.3 was at the floor).
- **Unchanged:** the EQ (22/22 COHERENCE-mode hashes, the Weiss comparison identical), all parameter IDs and ranges, state and recall.
- **Tools:** `Tools/plughost` (values centred in the display resolution, "sec"/"kHz" units, current values in dumps) and `Tools/proc3/` (characterisation, fitting, knee table, stems, comparison).
- **Suite:** the compressor checks are now against Pro-C 3 measurements; 120 checks pass, plus the documented LIMIT.

## Unreleased (2026-11-04): compressor testing protocol (documentation and test tooling only)

- **`COMPRESSOR-TESTING.md`**: the compressor testing protocol, with FabFilter Pro-C 3 (v3.00, Clean style) as the reference. Every Pro-C 3 control was verified on the plug-in; the static curve, knee and timing were measured (`mac-results/proc3-probe/`).
- **Removed** the Metric Halo MIO capture instructions (`Tools/compressor kit/HOW TO CAPTURE.md`). The C0–C4 test signals (`Tools/comp_signals.py`) are kept and reused.
- **`Tools/plughost`**: a headless AU/VST3 host for dumping parameters and rendering Pro-C 3. It's a separate CMake project and not part of the plug-in.
- No change to the plug-in, the EQ or any DSP; v0.2.6 is untouched.

## v0.2.6 (2026-11-04): fixes found on the Mac

- **AU readouts of the 10 dB controls are no longer empty** (bug since v0.2.4). JUCE's AU wrapper asks for parameter text with a maximum length of 0, and the stepped gain controls truncated their text to it. VST3 was unaffected. A new headless check confirms `getText (value, 0)` is the full text for every parameter.
- **Format-check harness:** the "same value for every parameter" comparison now skips the read-only Gain Reduction meter, as the recall check already did. It's an output and differs between test instances.
- **Footer:** shows the real latency (0 samples in COHERENCE mode, 15 in WEISS) and updates when the EQ mode changes.
- **SETUP menu:** shows "·" instead of "Â·".
- **No DSP change.** COHERENCE mode is bit-identical to v0.2.5 on all 22 reference renders (Mac hashes, `references-coh-normal.mac.sha256`); WEISS mode's 88 comparison renders are bit-identical to v0.2.5's and the Weiss report is unchanged.
- **Development** moved to a git repository (`coherence-dev`, Claude Code on the Mac). The first commit is v0.2.5 exactly as delivered.

## v0.2.5 (2026-11-03): Weiss mode

- **New EQ mode: WEISS** (SETUP > EQ > EQ mode). It reproduces Weiss EQ MP, measured from your renders.
  - The EQ runs at 2× with Weiss's own half-band resampling, which was identified from the renders.
  - Bells use the classic Q. Shelves use Weiss's frequency (the pole frequency) and its Q behaviour, so the Q is adjustable as on Weiss.
  - **Against all 28 Weiss renders at 48 kHz:** level ≤ 0.0018 dB to 20 kHz, phase ≤ 0.013°, impulse null −80 to −85 dB, and the same pre-ringing.
  - It adds **15 samples (0.3 ms) of latency**, which is reported to the host and compensated by Ableton.
  - It costs about 0.6 % more of one core.
- **COHERENCE mode (zero latency) is unchanged:** bit-identical to v0.2.4 on all 22 reference renders.
- **The EQ mode is saved with the set** and isn't automatable. Sets from earlier versions open in COHERENCE mode.
  - Switching mode fades out and back in over 5 ms.
  - In Weiss mode, bypass and EQ IN stay in time with the processed signal.
- **Not measured against Weiss:** 96 kHz, shelf gains beyond ±12 dB, and the cut filters. The cuts equal MDWEQ6's.
- **Tests:** 119 checks pass (9 new), plus the documented high-cut LIMIT. The format-check harness passes 54/54.
- **New tools:** `CoherenceTests --render-mode` and `Tools/weiss_compare.py`.
- **No change to the compressor**, the gain stepping or the UC1 meter.

## v0.2.4 (2026-11-03): stepping on every control path, UC1 gain-reduction meter

- **Gain stepping now applies however a control is moved:** panel, UC1/UF1 via 360 Link, Ableton automation, other controllers and recall (DESIGN.md section 12).
  - Values from outside play and display on the mode's step: in Standard, 1.37 dB plays 1.5.
  - The stored value is never changed, so there is no feedback to the host, no automation written, and the UC1 reads back its own value.
  - Shift fine adjustment and typed values on the panel stay exact. A set plays as it did when saved; sets from v0.2.3 and earlier play exactly as stored.
  - **Behaviour change:** automation and controller values off the grid now play on it. Ramps play in steps of the mode, so choose Precision for continuous automated sweeps.
- **New read-only parameter "Gain Reduction"** for control-surface meters, in SSL's 360 Link format (0 = none, Link reads 20·log10(1 − value)). It uses the panel meter's ballistics, isn't automatable, and isn't saved. Not yet tried with the UC1.
- **Tests:** 110 checks pass (17 new), plus the documented high-cut LIMIT. The format-check harness passes 54/54; its recall check leaves out the meter, which is an output.
- **No change to the EQ or compressor DSP.** One metering line was added in the engine.

## v0.2.3 (2026-11-03): the compressor

- **New detector and gain smoothing:** a held peak (ripple-free for steady tones down to 25 Hz), a gain computer in dB with a soft knee centred on the threshold, and attack/release on the gain reduction in dB. Details in DESIGN.md section 3.
- **Measured:**
  - static curve exact;
  - THD on steady 50 Hz–1 kHz tones from up to 1.6 % to the numerical floor;
  - IMD from −47 to −84 dB;
  - attack time independent of level;
  - identical at 44.1, 48 and 96 kHz;
  - the GR meter equals the applied reduction.
- **Behaviour change for old sessions:** the knee is now centred on the threshold, and the attack and release times are now exact time constants on the gain. Compressor settings from earlier versions sound somewhat different. Parameters are unchanged, so sessions load.
- **No MIO modelling** (decision); the MIO capture kit is no longer needed.
- **Every control tested:** SC HPF, MAKEUP, ∞ ratio, AUTO makeup, AUTO release, MIX and DYN IN switching. 93 checks pass; the EQ is unchanged.

## v0.2.2 (2026-11-03)

**Fixes from the code audit** (each has a test in `VALIDATION.md`, "Host edge cases"):
- **Crash risk when saving.** A host saving the state from a background thread while A/B was switched (or undo used) on the UI thread could crash. JUCE's `copyState()` writes pending values into the shared ValueTree through the UndoManager on the caller's thread.
  - State is now built from the parameter values directly, with the same layout.
  - The A/B slot is guarded by a lock, and the window size is stored atomically.
  - Found by a new stress test: 400 A/B switches during continuous saving. It crashed before the fix and passes after.
- **Mono tracks:** MID did nothing on a mono (one-channel) track, because the Mid/Side stage was skipped. MID now acts as a level control on mono (the signal is all Mid); SIDE and BALANCE have nothing to act on. Stereo is unchanged.
- **Recall without an A/B slot** (older or host presets) kept the previous B settings. B now starts as a copy of the recalled A.
- **Tail length:** 2.0 s is now reported to the host (it was 0). The worst-case filter ring-out is about 1.6 s. Hosts that pause idle plug-ins (Logic) no longer cut off low-frequency ring-outs. Ableton processes continuously, so nothing changes there.
- **Compatibility:** v0.2.1 sessions load (test: parameters, window size and B slot are restored), and the AU/VST3 format check passes 54/54 (Linux harness).

**New: SETUP menu** (the key in the header that showed the window size):
- **DISPLAY: Window size.** Presets Small (80 %), Medium (100 %), Large (120 %) and XL (140 %), plus the current custom size when you have dragged to another one.
- **CONTROLS: Gain stepping.** Fine (0.1 dB, default: the previous behaviour), Standard (0.5 dB), Precision (0.01 dB).
  - Applies to every dB control: the four EQ gains, IN TRIM, MID, SIDE, THRESHOLD, MAKEUP and the OUTPUT fader.
  - Covers mouse drag and wheel. Shift (or Cmd) always gives 0.01 dB. Typed values are exact in every mode.
  - **Host automation and hardware controllers (UC1 via 360 Link) are never quantised.** They reach the plug-in as host parameter changes and can't be told apart from automation (DESIGN.md section 11).
  - A user preference, saved in `~/Library/Application Support/Phase Systems/COHERENCE.settings`. It persists between sessions and applies to every instance at once.

**New: resizable window.**
- Drag the host window or the corner grip, from 60 % to 200 %. The aspect ratio is locked, and the whole panel scales as one (knobs, type, meters, spacing).
- Each set keeps its own size, as before.
- New instances open at the size last chosen in the SETUP menu.

**Mid/Side verified:** MID and SIDE are independent (bit-exact), automation is smooth (−94 dB above 4 kHz), and LINK keeps the offset. A Mid/Side *EQ* doesn't exist; this is now documented.

**No change to the EQ or compressor DSP.**

## v0.2.1 (2026-11-03)

**Sound: only at 88.2 kHz and above.** Sessions at 44.1/48 kHz sound exactly as in v0.2.0.
- **High-rate EQ table (B) integrated**, for 88.2 kHz and above:
  - bells go from 0.40 to 0.12 dB worst case at 88.2 kHz and from 0.38 to 0.04 dB at 96 kHz;
  - resonant high shelves go from 1.07 to 0.26 / 0.16 dB;
  - the Q error of bells at 96 kHz goes from 4.5% to 2.3%;
  - centre gain stays exact.

**Measurements added to the test suite:**
- **Phase** against the analog filters, and the **same settings across 44.1–192 kHz**.
- **Cut filter slopes**, and **four random bands together** against analog.
- **Latency:** 0 samples, reported and measured.
- **Distortion:**
  - the EQ adds none, with THD+N at the float-signal floor (−149 dB);
  - the compressor's harmonic distortion is measured, and its stereo link is checked.
- **Null tests:** bypass, compressor below threshold, order switch with DYN out, and plug-in against engine all null completely.
- **Host behaviour:** buffer sizes 1–4096, sample-rate changes and several instances all give identical output.
- **One documented limitation, measured and explained:** the high cut's phase leads the analog filter (DESIGN.md section 8).

**Comparison with MDWEQ6:**
- Phase and null depth were added.
- MDWEQ6 was found to process at 2× internally with linear-phase resampling, which explains its near-analog phase.
- Two proposals for your decision (DESIGN.md section 8):
  - A: a phase-matched high cut;
  - B: an optional 2× "analog phase" mode.

**Kits and tools:**
- EQ reference renders round 2 (`REFERENCES/EQ renders v2`): shelves, cuts, more bells, multi-band, 96 kHz, Weiss EQ MP.
- MIO compressor capture kit (`REFERENCES/Compressor capture kit`), with its analysis validated on COHERENCE's own compressor.
- `Tools/make_validation.sh` regenerates the full `VALIDATION.md` with one command.
- `eq_optimize3.py` can resume an interrupted run.

**Fixes:** the format check's report printed stray quotes after "every parameter found by name".

**Verified on the Mac:**
- auval passes;
- the AU-vs-VST3 format check passes 54/54 at 44.1, 48 and 96 kHz;
- v0.2.1 loads in Ableton Live 12 (smoke test).

## v0.2.0 (2026-11-03)

**Behaviour changes:**
- **Q definition** now matches MDWEQ6 (the classic analog Q of the boost), measured from your renders. v0.1 sessions with bell boosts or cuts sound wider at the same Q numbers.
- **New EQ table (44.1/48 kHz)** with exact gain at the set frequency (v0.1 could be 0.08 dB off on wide bells).
- At 88.2 kHz and above, the matched design was used until the high-rate table was finished (done in v0.2.1).

**Behaviour, without changing the sound:**
- **Readouts equal the DSP:** values snap to what the display shows.
- **Fixed:** typing a negative value with the true minus sign, and "−∞".
- **Mid/Side in 0.1 dB steps**, 0.01 dB with Shift, double-click to 0.0. The same stepping applies to the other dB knobs.

**Look:** light warm-grey identity with charcoal controls, dark displays and blue indicators.

**Tests:** the EQ technical review suite, and the AU vs VST3 format check, which the installer runs on the Mac.

## v0.1.0

First prototype:
- four-band EQ, MIOComp-style compressor, Mid/Side architecture stage, input/output with metering, auto gain;
- AU and VST3 for macOS.
