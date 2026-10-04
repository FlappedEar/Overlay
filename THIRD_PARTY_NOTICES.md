# Third-Party Notices

This file is an engineering inventory, not final legal advice. Distribution requires a complete
license review, particularly if future builds bundle FFmpeg/HEVC components or map data.

| Project/package | Purpose | License | Upstream |
| --- | --- | --- | --- |
| zlib 1.3.2 | Bounded native RCZ ZIP decompression; pinned upstream source, statically linked | zlib | https://zlib.net/zlib_license.html |
| NSIS 3.12 | Windows installer compiler and installer stub; zlib compression selected | zlib/libpng for core and selected compression module | https://nsis.sourceforge.io/License |
| Sora (Sora Project Authors) | Editor interface font, bundled in `native/resources/fonts` | SIL Open Font License 1.1 (`OFL-Sora.txt`) | https://github.com/sora-xor/sora-font |
| JetBrains Mono (JetBrains Mono Project Authors) | Editor monospaced font for times, bundled in `native/resources/fonts` | SIL Open Font License 1.1 (`OFL-JetBrainsMono.txt`) | https://github.com/JetBrains/JetBrainsMono |
| Qt 6 | Native UI, multimedia, application, and test framework | LGPL-3.0/GPL/commercial, depending on distribution terms | https://www.qt.io/licensing |

The source tree does not vendor Electron, React, Node packages, MapLibre, external FFmpeg command-line binaries, or an HEVC encoder. Export discovers a user-installed FFmpeg at runtime and may use its `libx265` encoder when a
working hardware HEVC encoder is unavailable. The x265 project publishes GPL-2.0-or-later licensing;
this is not a legal conclusion about a particular FFmpeg build or redistribution. Before distributing
or bundling FFmpeg/x265, review the exact binary configuration, license obligations, attribution,
patent considerations, and redistribution terms and update this inventory.

No user GoPro, VBO, RCZ, project, map credential, or API key is redistributed by this repository.

## Internal candidate artifacts

Release CI uses Qt deployment tools to copy shared Qt libraries, QML plugins and runtime dependencies from the Qt 6.8.3 SDK. This can include SDK multimedia codec libraries, separately from user-installed FFmpeg command-line tools. `candidate-manifest.json` lists every packaged regular file and hash, and symlink targets. These manifests are engineering inventories, not complete redistribution license notices. Exact component notices, corresponding source access and other distribution obligations must be verified against those bytes before a release is approved. No legal compliance or public redistribution approval is asserted by generating an internal test archive.
