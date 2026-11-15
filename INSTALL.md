# Installing COHERENCE

COHERENCE is free and isn't signed by Apple (that needs a paid Apple Developer membership). macOS therefore can't check the installer, and asks you to approve it once. After that it installs like any other plug-in.

**Needs:** macOS 11 or later, on an Apple Silicon or Intel Mac, and an AU or VST3 host such as Ableton Live, Logic Pro, Reaper or Bitwig.

## 1. Download

From the Releases page, download `COHERENCE-0.6.2.pkg`.

To check that the download is intact (optional), open Terminal and run the line below, then compare the result with `COHERENCE-0.6.2.pkg.sha256` on the Releases page:

```bash
shasum -a 256 ~/Downloads/COHERENCE-0.6.2.pkg
```

## 2. Approve the installer (once)

Quit your DAW and SSL 360 first.

**macOS 15 (Sequoia) and later:**

1. Double-click `COHERENCE-0.6.2.pkg`. macOS says it can't verify the file. Click **Done**, not Move to Trash.
2. Open **System Settings > Privacy & Security** and scroll down to **Security**. There's a line saying `COHERENCE-0.6.2.pkg` was blocked. Click **Open Anyway**.
3. Enter your Mac's password. If macOS asks once more, click **Open Anyway**.

**macOS 11 to 14:** hold Control, click `COHERENCE-0.6.2.pkg`, choose **Open**, then click **Open** in the dialog.

The exact wording changes between macOS versions, but the steps stay the same.

**Or, in Terminal:** this removes the "downloaded from the internet" mark from the installer, so it opens with a normal double-click:

```bash
xattr -d com.apple.quarantine ~/Downloads/COHERENCE-0.6.2.pkg
```

## 3. Install

The macOS installer opens. Click **Continue**, read and accept the licence, and click **Install**. It asks for your password, because plug-ins go in the system's plug-in folders.

**Customize** (optional) lets you choose what to install:

| Item | Installs | Default |
|---|---|---|
| Audio Unit (AU) | `/Library/Audio/Plug-Ins/Components/COHERENCE.component` | On |
| VST3 | `/Library/Audio/Plug-Ins/VST3/COHERENCE.vst3` | On |
| SSL 360 Link profile | `/Library/Application Support/SSL/SSLPlugins/SSL360Link/COHERENCE.user` | On if SSL 360 Link is installed |

## 4. Open it

- **Your DAW:** rescan plug-ins (Ableton Live: Settings > Plug-Ins > Rescan). COHERENCE is listed under Phase Systems.
- **SSL UC1 / UF1:** open SSL 360, then rescan in 360 Link.

## If macOS blocks the plug-in

Plug-ins installed by the installer aren't normally blocked. If your DAW still says COHERENCE can't be opened or isn't verified, quit the DAW, run this line in Terminal (it asks for your password), then rescan:

```bash
sudo xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/Components/COHERENCE.component /Library/Audio/Plug-Ins/VST3/COHERENCE.vst3
```

## Updating

Install the new version's package the same way. It replaces the old plug-ins. Your sets keep their settings.

## Uninstalling

Quit your DAW and SSL 360, then run these in Terminal:

```bash
sudo rm -rf /Library/Audio/Plug-Ins/Components/COHERENCE.component /Library/Audio/Plug-Ins/VST3/COHERENCE.vst3
```

```bash
sudo rm -f "/Library/Application Support/SSL/SSLPlugins/SSL360Link/COHERENCE.user"
```

```bash
for p in au vst3 profile; do sudo pkgutil --forget com.phasesystems.coherence.pkg.$p; done
```

COHERENCE's own preferences (gain stepping, theme, window size) are in `~/Library/Application Support/Phase Systems/COHERENCE.settings`, and you can delete them too.

## Built it yourself before?

`build.sh` from the source installs the AU in your user folder (`~/Library/Audio/Plug-Ins/Components`), and the package installs it in the system folder. If you have both, delete `~/Library/Audio/Plug-Ins/Components/COHERENCE.component` so your DAW doesn't see two copies.
