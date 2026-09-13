# Third-party software

## JUCE 9.0.2

- Source: https://github.com/juce-framework/JUCE
- Revision: `72782788ce18c2d4d760b28e0921d6ffc6431102`
- Source archive SHA-256: `28ec8ae0626a4d1c6ca8c32ad13f95868b147779d4f3df4a7868f333a38e0551`
- JUCE module licensing: AGPLv3 or the commercial JUCE licence.
- The upstream licence and dependency inventory are preserved in `Licenses/`.

The upstream SPDX inventory describes the complete JUCE source distribution;
not all those components are compiled into this VST3. Relevant bundled notices
are also copied into `Licenses/`. Original source notices remain in the pinned
source archive fetched by CMake. No JUCE commercial licence is bundled here.

## pluginval 1.0.4 (validation only)

- Source: https://github.com/Tracktion/pluginval
- Licence: GPL-3.0.
- Downloaded from the official release with a pinned SHA-256 by
  `Tools/fetch_pluginval.py`.
- A separate testing tool; it is not linked into or included in the plugin.
