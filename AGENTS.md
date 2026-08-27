# SW Engine Project Instructions

These instructions apply to all work in this repository. Preserve existing
project conventions unless the user explicitly requests otherwise.

## Project architecture

- This is a C++ game and editor engine template using CMake, Ninja, and Clang.
- **Dev** builds include editor tooling and support `LiveReload` through dynamic
  DLL loading. **Shipping** builds exclude editor code and statically link into
  a single executable.
- `Source/Core` is the foundational utility static library, shared with Engine
  through object compilation.
- `Source/Engine` contains objects, RHI/graphics, physics, scenes, and other
  engine facilities. It is a shared DLL in Dev builds.
- `Source/RuntimeAPI` is a header-only `INTERFACE` library defining the pure
  C-ABI contract between App and Editor/Game modules. Do not put implementations
  in it.
- `Source/App` is a thin executable launcher. It links only Engine and
  RuntimeAPI; it uses `EngineLoop` for the main loop and `ModuleHost` to manage
  hot-reload state serialization and async-task fencing.
- `Source/Editor` is Dev-only. `Source/GameFramework` contains genre-common
  frameworks/kits. `Source/Games` contains concrete games; `SW_ACTIVE_GAME`
  selects the game to build.
- **DLL Export / Import (API) Macros**:
  - `SW_API`: Used to export/import symbols from **Engine.dll**. Responds to the `SW_EXPORTS` definition.
  - `SW_MODULE_API`: A generic C-ABI entry point macro used across **all dynamically loaded plugin modules** (Editor.dll, Demo.dll, RHI backends, etc.). Responds to `SW_MODULE_EXPORTS`.
  - `SW_GAMESERVICE_API`: Used to export/import symbols from **GameFramework.dll**, which is a `SHARED` library linked by other game modules. Responds to `SW_GF_EXPORTS`.

## Build workflow

```powershell
py -3 Scripts/setup/SetupEnvironment.py
py -3 Scripts/setup/SetupVcpkg.py --install
cmake --preset Ninja-Debug
cmake --build --preset Ninja-Debug
```

- Prefer `-DSW_USE_SCCACHE=ON` when sccache is available.
- Build outputs are in `build/Ninja-Debug/Bin`.
- The compilation database is `build/Ninja-Debug/compile_commands.json`.

## Naming

### CMake

- Feature options: `SW_*`, declared with `option()` (for example,
  `SW_ENABLE_PCH`).
- Functions/macros: `sw_camelCase`.
- Target properties and compile definitions: `SW_SCREAMING_CASE`.
- Product target names: `PascalCase`.

### C++

- Namespaces: `sw` and nested namespaces (for example, `sw::editor`).
- Classes, structs, enums: `PascalCase`; interfaces: `I` + `PascalCase`.
- Functions: `camelCase`; members: `_camelCase`; locals: `camelCase`.
- Constants: `kPascalCase`; globals: `gv_camelCase`.
- Static variables: `s_camelCase`; private statics: `_s_camelCase`.
- Macros: `SW_SCREAMING_CASE`.
- Raw pointers use a `p` prefix (`pObject`, `_pObject`); double pointers use
  `pp`. Do not introduce triple pointers without reconsidering the design.
- Associative containers use a `map` prefix; fixed arrays use `arr`; variable
  arrays/lists use a `list` suffix; sets use a `unique` prefix.
- Use descriptive names; do not use opaque abbreviations or loop counters such
  as `i`, `j`, or `k` (use at least `index`).

### Python

- Public functions use `camelCase`; private helpers use `camelCaseInternal`.
- Module constants use `kPascalCase` or `_kPascalCase`.
- Module/file names use `PascalCase.py`.
- JSON configuration keys use `snake_case`.

## C++ structure and includes

- In headers, prefer forward declarations. Include only when a forward
  declaration is not possible; never include ThirdParty headers directly from a
  project header.
- Class declaration order: public member variables; constructors/destructor;
  `initialize`/`shutdown`; `process`; getters/setters; private functions in a
  separate access section; private member variables last.
- In `.cpp` files, include `"pch.h"` first, followed by a blank line and the
  matching header. Group project headers by relative source scope, separated by
  blank lines. System, OS-specific, project-global, and ThirdParty headers use
  angle brackets and are separately grouped.
- Sort includes within a group where doing so does not undermine a required
  ordering. Prefer `Core/Common/StdHeaders.h` and
  `Core/Common/PlatformOsHeaders.h` over scattered system/OS includes.
- Use the project type aliases from `Types.h`.
- Prefer Core and Engine facilities over STL or direct system facilities when
  they meet the need.

## C++ style

- Follow `.clang-format`.
- When a constructor exists, initialize fields in the constructor (not in the
  header), in declaration order. Use brace initialization. Put one initializer
  per line, with subsequent lines beginning with `,`.
- Arrange fields to minimize byte padding; use bit packing where appropriate.
- Do not compare booleans through negation: write explicit comparisons such as
  `if (_bValid == false)`. Explicitly compare pointers to `nullptr` when needed.
- Bitfield flags (e.g. `uint8 _bFlag : 1;`) must be compared and assigned using `SW_TRUE` (1) and `SW_FALSE` (0) instead of `true`/`false`: write `if (_bFlag == SW_TRUE)` / `if (_bFlag == SW_FALSE)` and `_bFlag = SW_TRUE;`.
- Omit braces for a single-line `if` body. If an `else`/`else if` is present,
  use braces for any multi-line blocks.
- Do not use `if` initializers. For unclear conditions or conditions with three
  or more parts, name the condition in a local variable first.
- Use `auto` only for iterators, structured bindings, or similarly complex
  types.
- Avoid lambdas unless they offer a performance benefit.
- Apply `const` wherever it is appropriate unless doing so harms performance.
- For range comparisons, place the variable in the middle (between lower and upper bounds) to reflect mathematical range notation: write `kMin <= value && value <= kMax` instead of `value >= kMin && value <= kMax`.
