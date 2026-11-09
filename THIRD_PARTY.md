# Third-party code

| Part of this repository | Derived from | License | License text |
|---|---|---|---|
| `src/d3d12_x`, `src/MMDevAPI` | [WinDurango](https://github.com/WinDurango/WinDurango) (D3D12X, MMDevAPI) | LGPL-3.0 | `licenses/WinDurango-LGPL-3.0.txt` |
| `src/kernelx`, `src/combase`, `src/EtwPlus` | SlimEra by XWine1 (ERA kernelx, incl. `MapTitleEsramPages`) | MIT, Copyright (c) 2025 XWine1 | `licenses/SlimEra-MIT.txt` |

Everything else (launcher, GCN→HLSL translator, tessellation bypass, scripts) was written for this project and is distributed under the repository license (GPL-3.0, `LICENSE`). LGPL-3.0 code may be redistributed under GPL-3.0 (LGPL-3.0 section 2).

No file of the game ONRUSH, or of the Xbox One system software, is included. The source contains only what is needed to interoperate with them: function/structure layouts, enum mappings and shader hashes.
