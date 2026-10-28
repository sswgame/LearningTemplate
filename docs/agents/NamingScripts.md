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
- **A pass or feature switch picks a definition, not a branch inside a body.** A step a switch changes is a member function with one definition per case, chosen by a file-level `#if` (a switched-off case is an empty definition), so the calling body reads the same in every permutation (`surface.applyAlphaCutoff()` in `materialshadermain.hlsli`). A `#if` inside a function body is allowed only when its condition names a backend macro.

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

C++ binds globals and cbuffer members **by name** (`shaderslot::resname` / `cbname`, `PassConstantNames`, the `g_<Name>Index` registry convention). They are not renamed for style, and the container, plural and abbreviation rules do not apply. The same holds for every other string-bound name: cbuffer names, semantics, externally supplied defines (`DX11`, `SW_PASS_*`, `MATERIAL_*`), macros in `bindingslots.hlsli`, shader file names, material field names.

**Renaming a string-bound name changes C++ in the same commit**: `ShaderBindingValidator::validate` skips an unknown reflected name, so a name C++ binds that is missing from the cooked reflection goes unnoticed.

### Resource Assets

- Everything under `Content/` uses lowercase names only (`[a-z0-9_.-]+`: `inventory.anim`, `0.title.scene.xml`, `ghost.prefab.json`). The one exception is documentation `README.md`.
- **Textures**: the runtime reads only DDS. A runtime `textures/` folder holds `.dds` and data (`.sprite.xml`, `.meta`) only. Source images (PNG, JPG, TGA ...) live under `textures_raw/` at the same relative path and are imported with `App --import-textures` (the editor imports on hot reload too). Commit the DDS with `textures_raw/import.stamp`; `App --check-textures` fails when a source, its import rule or its DDS drifts. Delete a source nothing references instead of moving it.
- **Models**: glTF sources (`.glb`, `.gltf`) live under `models_raw/`; `App --import-models` writes `models/<name>.mesh` plus `models_raw/import.stamp` (`App --check-models` compares). Commit the `.mesh` with the stamp.
