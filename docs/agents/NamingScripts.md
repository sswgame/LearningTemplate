# NamingScripts

> 결론: CMake 는 `SW_*` · `sw_snake_case`(매크로 대신 함수), 파이썬은 PEP 8 + 타입 힌트, HLSL 은 C++ 규칙 + 공유 헤더 함수는 구조체 멤버, 리소스 이름은 소문자.

## Naming — CMake · Python · HLSL · Resource Assets

### CMake

| Thing | Form |
| --- | --- |
| Options, cache and global variables, target properties, compile definitions | `SW_SCREAMING_CASE` (`SW_ENABLE_PCH`) |
| Functions | `sw_snake_case`, acronyms lowercase too (`sw_configure_pch`); private helpers end in `_internal` |
| Function-local and file-temporary variables | lowercase `snake_case`, no `sw` prefix |
| `cmake_parse_arguments` prefix | `ARG` |
| Product targets | `PascalCase`; internal `INTERFACE` flag targets `sw_snake_case` |
| File names | `PascalCase.cmake`; a `cmake -P` test driver is `Run<What>.cmake` |

Lint `naming-words` enforces the function form.

- **No `macro()`** — it leaks every local into the caller. Use `function()` + `set(<name> … PARENT_SCOPE)` (we require 3.24, so no `return(PROPAGATE)`). A body that must run in the caller's scope stays a macro with a one-line reason.
- Renames go through a pair table: `py -3 -m Scripts rename-symbol --pairs Config/Rules/Renames/<table>.json --reapply` (`"scope": "cmake"` for locals, `"ban": false` to keep them off `_bannedNames`).

### Python

PEP 8. Python names do not mirror C++ names; a JSON key or C++ name read as data stays a string. Lint `naming-words` enforces the shapes below.

| Thing | Form |
| --- | --- |
| Modules, packages (files and folders) | `snake_case`; never a standard-library module name |
| Functions, methods, variables, parameters, attributes | `snake_case`; a flag drops the C++ `b` prefix (`dry_run`) |
| Private helpers | leading `_` — not an `Internal` suffix |
| Classes, exceptions, `TypeVar`s | `PascalCase` |
| Module and class constants, enum members | `UPPER_CASE`; a function-local constant is a `snake_case` local |
| Overrides of an outside API | the outside spelling (`setUp`, `do_POST`), listed in `NamingWords.json` `_keep` |
| CLI subcommands and lint rule ids | `kebab-case` |
| JSON keys of Python-owned contracts (`CookContract.json`, hand-read import configs) | `snake_case`; keys read through reflection keep the C++ member name (`_width`) |
| Shell scripts and functions | `snake_case` |

Shape (Python 3.10):

- A module docstring, then `from __future__ import annotations`.
- Every function annotated (`__init__` → `-> None`). `X | None` and built-in generics, never `typing.Optional` / `List` / `Dict`.
- Text I/O names `encoding="utf-8"` (bytes: `"rb"` · `read_bytes()`).
- `pathlib.Path`; `os.path` only for `normpath`, `relpath`, `commonpath`, `commonprefix`, `expandvars`, `splitext`.
- `sys.exit(main())` as the single `__main__` line, never `raise SystemExit(…)` (tests: `unittest.main()`).
- Under `Scripts/`, `main( argv )` returns `ExitCode` values; `argparse.ArgumentParser` gets a `description=`.
- Output is plain `print`. Catch only expected exceptions; `except Exception` only at a boundary that must keep going and reports what it swallowed. A missing required setting stops with `sys.exit`, never `os.environ.get( "X", "<default>" )`.

### HLSL

Shaders follow the C++ rules wherever HLSL can express them.

#### Functions

- `camelCase`, no `sw` prefix. FXC has no namespaces, so a function in a shared header (`.hlsli`) belongs to a struct.
- A function whose first argument was a struct is that struct's member (`instance.computeTint()`); one that makes a struct or needs no instance is a `static` member (`InstanceData::load( slot )`).
- A shared header declares the struct with its members and defines them outside it, repeating `static` (FXC rejects a mismatch). `public` / `private` are reserved; `const` members do not compile.
- A helper used in one `.hlsl` is a free function.
- C++ function-name rules apply (`Bindless::loadRwTexture2D`, not `SW_LoadRWTex2D`). GPU verbs: `load` / `store` for buffer and RW-texture elements, `sample` / `gather` for texture reads.
- Entry points stay `VSMain` / `PSMain` / `CSMain` (named as strings by the compiler and pipeline XML).
- **Branch shapes NVIDIA OpenGL miscompiles** (DXC SPIR-V through ARB_gl_spirv): an early `return` in a function that must stay one expression, and an `else if` repeating a `&&` term of an earlier branch. Nest instead: `if ( a ) { if ( b ) { … } else { … } }`. Lint `shader-branch-shape` blocks the second.
- **A switch picks a definition, not a branch inside a body.** A step a pass or feature switch changes is a member function with one definition per case, chosen by a file-level `#if` (off = empty definition). A `#if` inside a body only for a backend macro.

#### Types, fields, locals, constants

| Thing | Form |
| --- | --- |
| Types | `PascalCase`, no `_t`, no `Sw` (`InstanceData`); a struct mirroring C++ takes its name |
| Struct fields | `_camelCase`, same name as the mirrored C++ member |
| Locals, parameters | descriptive `camelCase`: no single letters, no `idx` / `inst` / `dtid` |
| `out` / `inout` parameters | start with `out` / `inout` |
| Fixed-size local array | `arr` + singular noun |
| `groupshared` | `s_` (`s_arrKey[]`) |
| Local `const`, file-scope `static const` | `kPascalCase` |
| `#define` | `SW_` + `SCREAMING_CASE`; never aliases a resource, function or type |
| Include guards | `SW_<DOMAIN>_<FILE>_HLSLI` |
| Globals, cbuffer members | `g_` + `PascalCase` (`g_ViewProj`) |

- No opaque abbreviations (`_position`, not `_pos`); never an HLSL keyword as a name (`sample`, `point`, `texture` …).
- Material struct fields (`SW_MATERIAL_BEGIN`) are string-bound: the `.material` property and the C++ parameter are the field name with its `_`.
- `py -3 -m Scripts format` formats shaders. A compute entry takes `SW_NUMTHREADS( x, y, z )`, not `[numthreads(…)]`.

#### String-bound names

- Globals, cbuffer members and names, semantics, external defines (`DX11`, `SW_PASS_*`, `MATERIAL_*`), `binding_slots.hlsli` macros, shader file names and material fields are bound **by name** from C++. Do not rename them for style; container, plural and abbreviation rules do not apply.
- **Renaming one changes C++ in the same commit** — `ShaderBindingValidator::validate` skips an unknown reflected name, so the miss is silent.
- **Engine texture indices come from one table**: add a `g_<Name>Index` only to `shaderslot::emulated::kEngineTextureNames` (`ShaderBindingSlots.h`). Never hand-write a PassCB index field — a second list drifts and emulated backends draw magenta.

### Resource Assets

- Everything under `Content/` is lowercase (`[a-z0-9_.-]+`); the one exception is `README.md`.
- **Commit sources only.** Results and `import.stamp` are generated and git-ignored. Delete an unreferenced source instead of moving it.
  - Textures: runtime reads DDS only; sources under `textures_raw/` at the same relative path; `App --import-textures` writes, `App --check-textures` fails on drift. Import rules (`Config/Editor/TextureImportConfig.json`) must not overlap.
  - Models: glTF under `models_raw/`; `App --import-models` writes `models/<name>.mesh` (`--check-models`).
  - PBR glTF materials become `models/<name>/materials/<material>.material` from `engine/materials/pbrmaterial.material`; scenes reference that path.
- **Never reference `_shared` import results** (`<domain>/models/_shared/…`, `<domain>/textures_raw/_shared/…`) from hand-written data — reference the material slot or a clip set. A full import removes unreferenced shared files; editing one is drift.
- **Animation sets** (`*.animset.xml`, name → clip): a hand-written set derives with `_base="<set>"` and borrows with `<clip name set [source]/>`, never a `path`. Look clips up by name.
- **Domains**: `engine` · `common` · `game/<pack>` · `dlc/<name>` each cook into their own pack; an import writes only into its own domain. A game references only `common/` and `engine/`; `common/` never references a game, nor one game another. A source two domains need moves to `common/` with `Scripts/dev/move_asset.py`, with its `credits.md` line.
- **Scanned assets (Poly Haven, CC0)**: `models_raw/polyhaven/`, `textures_raw/polyhaven/<asset>/`, binaries in Git LFS, each in the pack's `credits.md`. Tiling surfaces use `pbr_lit` with `_worldTileSize`, not stretched UVs.
