> 질문: CMake · Python · HLSL · 리소스 에셋의 이름과 모양은?
> 독자: CMake, 파이썬, 셰이더를 쓰거나 리소스 에셋을 더하는 사람.
> 결론: CMake 옵션은 `SW_*`, 함수는 `sw_camelCase` 다. 파이썬 · HLSL · 리소스 에셋은 아래 절의 규칙을 따른다(리소스 이름은 소문자).

## Naming — CMake · Python · HLSL · Resource Assets

### CMake

- Feature options: `SW_*`, declared with `option()` (for example,
  `SW_ENABLE_PCH`, `sw_configurePch`).
- Functions/macros: `sw_camelCase`.
- Target properties and compile definitions: `SW_SCREAMING_CASE`.
- Product target names: `PascalCase`.

### Python

- Public functions use `camelCase`; private helpers use `camelCaseInternal`.
- Module constants use `kPascalCase` or `_kPascalCase`.
- Module/file names use `PascalCase.py`.
- JSON keys of Python-owned contracts (`Config/Engine/CookContract.json`, hand-read import configs) use `snake_case`. Configs read through reflection use the C++ member name as the key (`_width`).

Shape (examples in `docs/04_CodingGuidelines.md` 8): a module docstring and `from __future__ import annotations`;
every function annotated (`__init__` → `-> None`); `X | None` and built-in generics, never `typing.Optional` / `List` / `Dict`; text I/O names
`encoding="utf-8"`; `pathlib.Path` (`os.path` only for `normpath`, `relpath`, `commonpath`, `commonprefix`, `expandvars`, `splitext`);
`sys.exit(…)` never `raise SystemExit(…)`, with the `__main__` guard the single line `sys.exit(main())` (tests: `unittest.main()`); under `Scripts/`,
`main( argv )` returns `ExitCode` values; `argparse.ArgumentParser` gets a `description=`. The code must run on Python 3.10.

- Not checked by a gate: console output is plain `print`; catch the exceptions you expect, and `except Exception` only at a boundary that must keep going and reports what it swallowed (a setup step, a session hook).

### HLSL

Shaders follow the C++ rules wherever HLSL can express them.

- **Functions** are `camelCase`. A function in a shared header (`.hlsli`) belongs to a struct — FXC (DX11) has
  no namespaces, so a struct stands in for one, and there is no `sw` prefix anywhere. A function whose first
  argument was a struct is that struct's member function (`instance.computeTint()`,
  `light.computeIncidence( … )`, `cursor.next( outLight )` — a member function may change the fields). A
  function that makes a struct or needs no instance is a `static` member function (`InstanceData::load( slot )`,
  `PixelContext::make( … )`, `Bindless::sampleIndex( index, uv )`, `View::getViewProjection()`).
  A shared header declares the struct with its member functions and defines them outside it
  (`static float4 Bindless::sampleIndex( … ) { … }` — the definition repeats `static`, FXC rejects a mismatch).
  `public` / `private` are reserved words and `const` member functions do not compile. A helper that lives in one `.hlsl` is a free
  function (`isVisible`, `hashSeed`).
  The C++ function-name rules apply (`Bindless::loadRwTexture2D`, `Tex` → `Texture`; a value worked out from inputs is `compute…`, not an `…Of` suffix).
  GPU access verbs: `load` / `store` for buffer and RW-texture elements, `sample` / `gather` for texture reads.
  Entry points stay `VSMain` / `PSMain` / `CSMain` (the compiler and pipeline XML name them as strings).
- **A pass or feature switch picks a definition, not a branch inside a body.** A step that a switch changes is a
  member function with one definition per case, chosen by a file-level `#if` (a switched-off case is an empty
  definition), so the calling body reads the same in every permutation (`surface.applyAlphaCutoff()`,
  `context.storeSurface( surface )` in `materialshadermain.hlsli`).
  A `#if` inside a function body is allowed only when its condition names a backend macro.
- **Types** are `PascalCase` with no `_t` suffix and no `Sw` prefix (`InstanceData`, `PixelContext`, `PSInput`).
  A struct that mirrors a C++ struct takes the C++ type name (`RHIDrawIndirectCommand`).
- **Struct fields** are `_camelCase`, as in a C++ plain-data struct (`PSInput::_position`, `context._worldPosition`).
  A field that mirrors a C++ member has the same name (`_startVertexLocation`), so no conversion rule exists. A
  member function reads its own fields by bare name (`_position`). No opaque abbreviations: `_position`, `_normal`,
  `_color`, `_worldPosition`, `_vertexId` — not `_pos`, `_nrm`, `_col`, `_wpos`, `_vid`. Material struct fields
  (`SW_MATERIAL_BEGIN`) follow the same rule and are string-bound: a `.material` property `name` and a C++
  parameter name are the field name with its `_`, so renaming a field changes the shader, the assets and the C++ in one commit.
- **Locals and parameters** are `camelCase` and descriptive — no single letters, no `i` / `j` / `k`, no
  `idx` / `inst` / `ao` / `dtid`. `out` / `inout` parameters start with `out` / `inout` (`outPosition`).
  A fixed-size local array takes `arr` + a singular noun (`arrOffset[4]`). `groupshared` variables take `s_`
  (`s_arrKey[]` for an array). Never use an HLSL keyword as a name (`sample`, `point`, `line`, `linear`,
  `texture`, `sampler`, `vector`, `matrix`, …). A local `const` may be `kPascalCase`, as in C++.
- **Constants and macros**: a file-scope `static const` is `kPascalCase` (`kInvalidIndex`, `kPi`). A `#define`
  is `SW_` + `SCREAMING_CASE`, and a macro never aliases a resource, a function or a type (a per-pass value is a
  function with per-pass definitions, as `View::getViewProjection()`). Include guards are
  `SW_<DOMAIN>_<FILE>_HLSLI` (`SW_ENGINE_POSTBLOOM_HLSLI`).
- **Globals and cbuffer members** are `g_` + `PascalCase` (`g_ViewProj`, `g_SwInstances`). C++ binds them
  **by name** (`shaderslot::resname` / `cbname`, `PassConstantNames`, the `g_<Name>Index` registry convention),
  so they are not renamed for style and the container, plural and abbreviation rules do not apply to them.
  The same holds for every other string-bound name: cbuffer names, semantics, externally supplied defines (`DX11`, `SW_PASS_*`, `MATERIAL_*`),
  the macros in `bindingslots.hlsli` and shader file names (a named entry with its reason in the gate, `kStringBoundName`).
- **Formatting**: `clang-format` formats shaders as it does C++ (`FormatModified.py` fixes them). A compute entry point takes
  `SW_NUMTHREADS( x, y, z )` (`common.hlsli`), not `[numthreads(…)]`.
- **Renaming a string-bound name changes C++ in the same commit** (`ShaderBindingValidator::validate` skips an unknown reflected name, so a name C++ binds that is missing from the cooked reflection goes unnoticed).

### Resource Assets

- All files and directories under `Resource/` MUST use strictly lowercase names (`[a-z0-9_.-]+`, e.g. `inventory.anim`, `0.title.scene.xml`, `ghost.prefab.json`). Uppercase characters are strictly prohibited (except documentation `README.md`)..
- The runtime reads textures only as DDS. A runtime texture folder (`textures/`) holds `.dds` and data (`.sprite.xml`,
  `.meta`) only; source images (PNG, JPG, TGA, ...) live under `textures_raw/` at the same relative path and are imported
  with `App --import-textures` (the editor imports on hot reload too). Commit the DDS together with `textures_raw/import.stamp`;
  `App --check-textures` fails when a source, its import rule or its DDS drifts. Delete a
  source nothing references instead of moving it.
- Models follow the same rule: glTF sources (`.glb`, `.gltf`) live under `models_raw/` and `App --import-models` writes
  `models/<name>.mesh` plus `models_raw/import.stamp` (`App --check-models` compares). Commit the `.mesh` with the stamp.
