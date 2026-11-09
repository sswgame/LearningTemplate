> 질문: CMake · Python · HLSL · 리소스 에셋의 이름과 모양은?
> 독자: CMake, 파이썬, 셰이더를 쓰거나 리소스 에셋을 더하는 사람.
> 결론: CMake 옵션은 `SW_*`, 함수는 `sw_camelCase`. 파이썬은 `camelCase` 함수에 타입 힌트 필수. HLSL 은 C++ 규칙을 따르되 공유 헤더 함수는 구조체 멤버. 리소스 이름은 소문자.

## Naming — CMake · Python · HLSL · Resource Assets

### CMake

| Thing | Form |
| --- | --- |
| Feature options (`option()`) | `SW_*` (`SW_ENABLE_PCH`) |
| Functions / macros | `sw_camelCase` (`sw_configurePch`) |
| Target properties, compile definitions | `SW_SCREAMING_CASE` |
| Product targets | `PascalCase` |

### Python

| Thing | Form |
| --- | --- |
| Public functions | `camelCase` |
| Private helpers | `camelCaseInternal` |
| Module constants | `kPascalCase` or `_kPascalCase` |
| Module / file names | `PascalCase.py` |
| JSON keys of Python-owned contracts (`Config/Engine/CookContract.json`, hand-read import configs) | `snake_case` |
| JSON keys read through reflection | the C++ member name (`_width`) |

Shape (examples in `docs/04_CodingGuidelines.md` 8). The code must run on Python 3.10.

- A module docstring, then `from __future__ import annotations`.
- Every function annotated (`__init__` → `-> None`). `X | None` and built-in generics, never `typing.Optional` / `List` / `Dict`.
- Text I/O names `encoding="utf-8"`.
- `pathlib.Path`; `os.path` only for `normpath`, `relpath`, `commonpath`, `commonprefix`, `expandvars`, `splitext`.
- `sys.exit(…)`, never `raise SystemExit(…)`. The `__main__` guard is the single line `sys.exit(main())` (tests: `unittest.main()`).
- Under `Scripts/`, `main( argv )` returns `ExitCode` values. `argparse.ArgumentParser` gets a `description=`.
- Console output is plain `print`. Catch the exceptions you expect; `except Exception` only at a boundary that must keep going and reports what it swallowed (a setup step, a session hook).

### HLSL

Shaders follow the C++ rules wherever HLSL can express them.

#### Functions

- `camelCase`, no `sw` prefix anywhere. FXC (DX11) has no namespaces, so a function in a shared header (`.hlsli`) belongs to a struct.
- A function whose first argument was a struct is that struct's member function (`instance.computeTint()`, `cursor.next( outLight )`; a member function may change fields).
- A function that makes a struct or needs no instance is a `static` member (`InstanceData::load( slot )`, `PixelContext::make( … )`, `View::getViewProjection()`).
- A shared header declares the struct with its member functions and defines them outside it; the definition repeats `static` (`static float4 Bindless::sampleIndex( … ) { … }`; FXC rejects a mismatch). `public` / `private` are reserved and `const` member functions do not compile.
- A helper used in one `.hlsl` is a free function (`isVisible`, `hashSeed`).
- C++ function-name rules apply (`Bindless::loadRwTexture2D`, `Tex` → `Texture`; a computed value is `compute…`). GPU verbs: `load` / `store` for buffer and RW-texture elements, `sample` / `gather` for texture reads.
- Entry points stay `VSMain` / `PSMain` / `CSMain` (the compiler and pipeline XML name them as strings).
- **Branch shapes OpenGL miscompiles.** The GL backend feeds DXC's SPIR-V straight to the driver (ARB_gl_spirv), and NVIDIA GL gets two shapes wrong that Vulkan runs correctly from the same SPIR-V: an early `return` in a function that must stay one expression (`MorphPool::computeElement` in `binding.hlsli`), and an `else if` that asks again for a `&&` term of an earlier branch (`if ( a && b ) { … } else if ( a ) { … }` took the `a` branch where `a` was false — commit 46c0d6ad17). Nest instead: `if ( a ) { if ( b ) { … } else { … } }`. The second shape is blocked by `py -3 -m Scripts lint shader-branch-shape` (`ShaderBranchShape.py`, run by the commit hook).
- **A pass or feature switch picks a definition, not a branch inside a body.** A step a switch changes is a member function with one definition per case, chosen by a file-level `#if` (a switched-off case is an empty definition), so the calling body reads the same in every permutation (`surface.applyAlphaCutoff()` in `material_shader_main.hlsli`). A `#if` inside a function body is allowed only when its condition names a backend macro.

#### Types, fields, locals, constants

| Thing | Form |
| --- | --- |
| Types | `PascalCase`, no `_t` suffix, no `Sw` prefix (`InstanceData`, `PSInput`). A struct mirroring a C++ struct takes its name (`RHIDrawIndirectCommand`) |
| Struct fields | `_camelCase`, same name as the C++ member it mirrors (`_startVertexLocation`); a member function reads its own fields bare |
| Locals, parameters | `camelCase`, descriptive: no single letters, no `i` / `j` / `k`, no `idx` / `inst` / `ao` / `dtid` |
| `out` / `inout` parameters | start with `out` / `inout` (`outPosition`) |
| Fixed-size local array | `arr` + singular noun (`arrOffset[4]`) |
| `groupshared` | `s_` (`s_arrKey[]` for an array) |
| Local `const` | may be `kPascalCase` |
| File-scope `static const` | `kPascalCase` (`kInvalidIndex`, `kPi`) |
| `#define` | `SW_` + `SCREAMING_CASE`; never aliases a resource, function or type (a per-pass value is a function with per-pass definitions) |
| Include guards | `SW_<DOMAIN>_<FILE>_HLSLI` (`SW_ENGINE_POSTBLOOM_HLSLI`) |
| Globals, cbuffer members | `g_` + `PascalCase` (`g_ViewProj`) |

- No opaque abbreviations: `_position`, `_normal`, `_color`, `_worldPosition`, `_vertexId`, not `_pos`, `_nrm`, `_col`, `_wpos`, `_vid`.
- Never use an HLSL keyword as a name (`sample`, `point`, `line`, `linear`, `texture`, `sampler`, `vector`, `matrix` ...).
- Material struct fields (`SW_MATERIAL_BEGIN`) follow the field rule and are string-bound: a `.material` property name and a C++ parameter name are the field name with its `_`.
- `clang-format` formats shaders as C++ (`py -3 -m Scripts format`). A compute entry takes `SW_NUMTHREADS( x, y, z )` (`common.hlsli`), not `[numthreads(…)]`.

#### String-bound names

C++ binds globals and cbuffer members **by name** (`shaderslot::resname` / `cbname`, `PassConstantNames`, the `g_<Name>Index` registry convention). They are not renamed for style, and the container, plural and abbreviation rules do not apply. The same holds for every other string-bound name: cbuffer names, semantics, externally supplied defines (`DX11`, `SW_PASS_*`, `MATERIAL_*`), macros in `binding_slots.hlsli`, shader file names, material field names.

**Engine texture indices come from one C++ table.** A `g_<Name>Index` engine texture is added only to `shaderslot::emulated::kEngineTextureNames` (`ShaderBindingSlots.h`): the shader compiler injects `SW_ENGINE_TEXTURE_INDEX_<n>` from it and `binding.hlsli` expands the PassCB fields and the emulated backends' `Bindless::emulatedSlot` from those macros (`#error` when one is missing or the count changes). Never hand-write a new PassCB index field — a second hand list let DX11 · GL draw the magenta "unmapped" colour silently (Unreal shader parameter structs: one definition, both sides).

**Renaming a string-bound name changes C++ in the same commit**: `ShaderBindingValidator::validate` skips an unknown reflected name, so a name C++ binds that is missing from the cooked reflection goes unnoticed.

### Resource Assets

- Everything under `Content/` uses lowercase names only (`[a-z0-9_.-]+`: `inventory.anim`, `0.title.scene.xml`, `ghost.prefab.json`). The one exception is documentation `README.md`.
- **Textures**: the runtime reads only DDS. A runtime `textures/` folder holds `.dds` and data (`.sprite.xml`) only. Source images (PNG, JPG, TGA ...) live under `textures_raw/` at the same relative path and are imported with `App --import-textures` (the editor imports on hot reload too). Commit only the source: the DDS and `textures_raw/import.stamp` are generated and ignored (`.gitignore` lists the few hand-made DDS exceptions); `App --check-textures` fails when a source, its import rule or its DDS drifts. Import rules (`Config/Editor/TextureImportConfig.json`) must not overlap — a file two rules match is an import error. Delete a source nothing references instead of moving it.
- **Models**: glTF sources (`.glb`, `.gltf`) live under `models_raw/`; `App --import-models` writes `models/<name>.mesh` plus `models_raw/import.stamp` (`App --check-models` compares). Commit only the source — `models/` and the stamp are generated and ignored.
- **PBR glTF materials**: a static glTF whose material has a base-color texture gets `models/<name>/materials/<material>.material`, filled from `engine/materials/pbrmaterial.material` (`pbr_lit.hlsl`: albedo · OpenGL normal · glTF ORM). Reference that generated path from scenes — it is a one-line slot whose `parent` is the shared values (below). Images the glTF points at under `textures_raw/` (`../../textures_raw/...` URIs) are used in place; embedded images are written to the shared source folder (below).
- **Shared import results**: an import result nobody can hand-edit is merged by content (Unreal Derived Data Cache, Unity Library keys; `ImportedAssetStore`). The key is the result bytes mixed with the settings that made them, so different settings are a different result. Layout: `<domain>/models/_shared/materials/<key>.material` (only the fields that differ from the shader's parent material, `parent` = `EngineDefaultAssets` template; the name is not in the key) · `<domain>/models/_shared/clips/<key>.animclip` · `<domain>/textures_raw/_shared/<lowercase rule name>/<key>.<ext>` (embedded images; the folder pins the import rule picked from the path beside the model, `TextureImportConfig::findMatchingRule`). A `models_raw/_shared.*` source is not allowed. Never reference `_shared` paths from hand-written data: keys change with content. Materials are referenced through their slot `models/<name>/materials/<material>.material`, clips through a set. A full import removes shared files no result references; the stamp's result hash follows the references, so deleting or editing a shared file is drift.
- **Animation sets**: `*.animset.xml` is a name → clip table (`AnimationSet`). The import writes `models/<name>/<name>.animset.xml`; a hand-written set derives with `_base="<set>"` (`DataInheritance`; the name is the key, `_op="Replace"` turns a `path` line into a `set` line) and borrows with `<clip name set [source]/>`, never a `path`. Animators (`_animationSetPath`), graphs and characters look clips up by name, and a name holding `/` is a clip file (no extension; the clip preview only).
- **Sharing across packs**: each `engine` · `common` · `game/<pack>` · `dlc/<name>` domain is cooked into its own pack, and an import writes only into its own domain — it never merges across domains, because that would grow a pack every game ships with one game's data. A game may reference `common/` and `engine/`; `common/` never references a `game/` domain, nor one game another. When two domains need the same source, move the source up to `common/` with `Scripts/dev/MoveAsset.py` and move its `credits.md` line with it (the license line goes where the file goes). There is no package without `common`: `Scripts/package/Pack.py` always stages `engine.pack` and `common.pack` next to the game pack and fails when one is missing. To ship a game independent of `common`, copy the sources into the game's own domain; its import then fills its own store.
- **Scanned assets (Poly Haven, CC0)**: sources live in `models_raw/polyhaven/` and `textures_raw/polyhaven/<asset>/` and their binaries (`.jpg` · `.png` · `.bin`) are Git LFS (`.gitattributes`). Record each one in the pack's `credits.md`. Tiling surface textures go on cubes/planes through a `pbr_lit` material with `_worldTileSize` (world-aligned UV) instead of stretching mesh UVs.
