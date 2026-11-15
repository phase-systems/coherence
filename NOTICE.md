# COHERENCE: copyright and licences

COHERENCE, a channel strip plug-in for macOS (AU and VST3).
Copyright (C) 2026 Phase Systems.

This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License as published by the Free Software Foundation, version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details. The full text is in `LICENSE`.

If you received COHERENCE as an installer, its complete source is at the project page where you downloaded it. The installed plug-ins carry these licences in `Contents/Resources/Licenses`.

## Third-party components

| Component | Where | Licence |
|---|---|---|
| JUCE 9.0.4 | `deps/JUCE` | AGPLv3 (used under the AGPLv3, not the commercial JUCE licence); `licenses/JUCE-LICENSE.md` |
| Steinberg VST3 SDK (inside JUCE) | `deps/JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK` | MIT; its `LICENSE.txt` |
| Apple Audio Unit SDK (inside JUCE) | `deps/JUCE/modules/juce_audio_plugin_client/AU/AudioUnitSDK` | Apache 2.0; its `LICENSE.txt` |
| Other libraries bundled with JUCE (zlib, libpng and others) | `deps/JUCE/modules` | Their own licences, next to each one |
| Inter typeface (embedded in the plug-in) | `Assets/Fonts` | SIL Open Font License 1.1; `licenses/Inter-OFL.txt` |
| Avid AAX SDK, Steinberg ASIO SDK, Oboe (inside JUCE) | `deps/JUCE/modules` | GPLv3 or their own licences, next to each one. Part of the unmodified JUCE source; COHERENCE doesn't build AAX, ASIO or Android, so none of them is in the plug-in |

VST is a registered trademark of Steinberg Media Technologies GmbH. Audio Units is a trademark of Apple Inc. SSL, UC1, UF1 and 360 Link are trademarks of Solid State Logic. Weiss and EQ MP belong to Weiss Engineering; the Weiss EQ MP plug-in is published by Softube. FabFilter and Pro-C belong to FabFilter; MDWEQ6 to Metric Halo; Harrison and 32Classic to Harrison Audio. These names are used only to describe compatibility and what COHERENCE was measured against. COHERENCE is an independent project: it is not affiliated with, endorsed by or derived from the software of any of these companies, and contains none of their code, graphics or audio.
