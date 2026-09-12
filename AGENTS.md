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
- `Source/App` is a thin executable launcher. In Dev it links only Engine and
  RuntimeAPI; in Shipping it also statically links `SWGame`. It uses `EngineLoop`
  for the main loop and `ModuleHost` to manage hot-reload state serialization
  and async-task fencing.
- `Source/Editor` is Dev-only. `Source/GameFramework` contains genre-common
  frameworks/kits. `Source/Games` contains concrete games; `SW_ACTIVE_GAME`
  selects the game to build.
- **DLL Export / Import (API) Macros**:
  - `SW_API`: Used to export/import symbols from **Engine.dll**. Responds to the `SW_EXPORTS` definition.
  - `SW_MODULE_API`: A generic C-ABI entry point macro used across **all dynamically loaded plugin modules** (EditorModule.dll, SWGame.dll, RHI backends, etc.). Responds to `SW_MODULE_EXPORTS`.
  - `SW_GF_API`: Used to export/import **GameFramework.dll** class symbols. Responds to `SW_GF_EXPORTS`.
  - `SW_GAMESERVICE_API`: Used by the GameService locator (`bindGameService` / `getRawService`) in RuntimeAPI. This is not the GameFramework class-export macro.

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
  `SW_ENABLE_PCH`, `sw_configurePch`).
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
  `pp` (`_ppMember`, `ppMember`). Triple pointers or higher (`ppp`, `_ppp`, `***`) are strictly forbidden as architectural flaws.
- Associative containers use a `map` prefix (`map`, `_map`); fixed arrays use `arr` (`_arr`); variable arrays/lists use a `list` prefix (`list`, `_list`); sets use a `unique` prefix (`unique`, `_unique`, e.g. `_uniqueIds`). Do not use a `List` suffix for variable arrays/lists. Except for the `unique` prefix (`_uniqueIds`, `outUniqueIds`) and raw byte buffers (`_bytes`, `outBytes`), all container and parameter names MUST use singular form (e.g. `_listActor`, `_listItem`, `_mapIdToName`, `outListItem`, `outListHandle`; plural forms like `_listActors` are strictly forbidden). Byte vectors (`vector<uint8>`, `vector<int8>`, `vector<utf8>`) whose names contain the word `byte`/`bytes` (e.g. `_bytes`, `_rawBytes`, `bytes`, `outBytes`, `pOutBytes`) omit the `list` prefix.
- Function parameters use `camelCase`. Output parameters (Out-parameters) must start with an `out` prefix (`out` + PascalCase, e.g. `outValue`, `outConfig`, `outX`) with containers following `out` in singular form (`outListItem`, `outMapData`, `outArrBuffer`; `outUniqueIds` allows plural). Exceptionally, raw pointer output parameters place the `p`/`pp` prefix before `out`: `pOut...` (pointer, e.g. `pOutBuffer`, `pOutApi`, `pOutResult`), `ppOut...` (double pointer), `pInOut...` (inout pointer, e.g. `pInOutSize`). In/Out parameters use `inout` / `pInOut` (e.g. `inoutSkeleton`, `pInOutSize`).
- Use descriptive names; do not use opaque abbreviations or loop counters such
  as `i`, `j`, or `k` (use at least `index`).
- **GPU resource verbs are a closed vocabulary.** A class that owns RHI resources derives from
  `RHIRenderResource` and names its device-lifecycle methods from this table only. Do not invent
  synonyms (`upload`, `applyToGpu`, `shutdownAllGpu`, `isUploaded`, `isReady`, `releaseGpu` were all
  renamed away for this reason):

  | Verb | Meaning |
  | --- | --- |
  | `initRhi( pDevice )` | Create this object's GPU resources on `pDevice`. Idempotent: return `true` when already resident. |
  | `updateRhi( pDevice )` | Push changed CPU data to resources that already exist. |
  | `releaseRhi( pDevice )` | `pDevice` is **still alive** — hand the resources back and clear the handles. |
  | `forgetRhi( pDevice )` | `pDevice` is **already gone** — clear the handles only; calling destroy here is use-after-free. |
  | `isRhiValid()` | Are this object's GPU resources live right now? |

  `releaseRhi` / `forgetRhi` / `initRhi` are never called in a loop from outside. `IRHIDevice` broadcasts
  them to the whole registry (`RHIRenderResource::releaseAllFor` / `forgetAllFor` / `initAllFor`), so a
  new resource class is covered the moment it derives. Per-frame buffer managers that are not assets
  (`GpuScene`) keep their own vocabulary — they are not registry members.

### Python

- Public functions use `camelCase`; private helpers use `camelCaseInternal`.
- Module constants use `kPascalCase` or `_kPascalCase`.
- Module/file names use `PascalCase.py`.
- JSON configuration keys use `snake_case`.

### Resource Assets

- All files and directories under `Resource/` MUST use strictly lowercase names (`[a-z0-9_.-]+`, e.g. `inventory.anim`, `0.title.scene.xml`, `ghost.prefab.json`). Uppercase characters are strictly prohibited (except documentation `README.md`). Enforced automatically by `CheckResourceCasing.py` and Git pre-commit hooks.

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
- Do not spell out buffer/path-size magic numbers (e.g. `char buf[64]`,
  `fixed_string<256>`, `StringBuilder<32>`). Use the sentinels in the
  `constant` namespace (`Core/Common/Defines.h`) instead: `kMaxBuffer16`
  through `kMaxBuffer8192`, and `kMaxPathSize` for filesystem paths.

## Helpers: Util vs Internal

- Shared helpers used by more than one translation unit belong on a `XxxUtil`
  static struct in a header (for example `SerializerUtil`, `MaterialUtil`). Do
  not name those headers or types `Internal`.
- Helpers used only inside one `.cpp` go in a **separate** `namespace sw` block
  from the class implementation, so the two regions fold independently:

```cpp
namespace sw
{
	namespace
	{
		struct FooInternal
		{
			static void helper();
		};
	} // namespace
} // namespace sw

namespace sw
{
	void Foo::process() { FooInternal::helper(); }
} // namespace sw
```

- Locator-only `s_*` state used by bind/get APIs stays in the implementation
  block's anonymous namespace. This applies to `Source/` and
  `Tools/ReflectionParser/`.
- Name the `Internal` helper after **the translation unit, not the class**. When one
  class is split across several `.cpp` files, naming each helper after the class makes
  them collide: unity builds (`SW_ENABLE_UNITY_BUILD`, used by the `CI-*` presets) merge
  several `.cpp` files into one translation unit, and an anonymous namespace only hides a
  name *per translation unit* — so the second definition is a redefinition error. So
  `VulkanRHIResourcePipeline.cpp` uses `VulkanRHIResourcePipelineInternal`, not
  `VulkanRHIResourceInternal`. Enforced by `CheckCodeConventions.py`
  (`Naming/DuplicateInternalHelper`, full-scan only).

### One anonymous namespace per file, at the top of its scope

- A `.cpp` has **at most one** anonymous namespace, placed at the top of the namespace that
  encloses it (right after `SW_LOG_CALLER`, if present). Everything translation-unit-local —
  helper structs, constants, `s_*` state, free functions — lives in it.
- **Do not write free `static` functions.** The anonymous namespace already gives internal
  linkage, so drop the `static` keyword when you move one in.
- **Hoisting can break the build.** The block may only sit above things it does not use.
  Moving it past a type definition, a constant or an `s_*` variable that it references is a
  compile error, so build after you move one. If the dependency is itself translation-unit-local
  (a file-scope `static`), move it **into** the block instead.
- Legitimate exceptions, all of which are load-bearing:
  - Blocks in mutually exclusive preprocessor branches (`#if SW_PLATFORM_WINDOWS` /
    `#elif SW_PLATFORM_LINUX`) are one block per translation unit. Do not merge them.
  - `REFLECT`-generated types cannot move into an anonymous namespace — the generated
    `.gen.cpp` refers to them by qualified name (`sw::MockMeshComponent`).
  - `SW_GLOBAL_VARIABLE_*` declares `extern`; it is deliberately external linkage and stays out.
  - `main` and functions declared in a header stay at namespace scope.

## C++ style

- Follow `.clang-format`.
- When a constructor exists, initialize fields in the constructor (not in the
  header), in declaration order. Use brace initialization. Put one initializer
  per line, with subsequent lines beginning with `,`. An initial value has exactly
  one home: writing it in both places hides which one wins (the constructor does) and
  invites changing only one of them. Enforced by `CheckCodeConventions.py`
  (`Style/HeaderMemberInitializer`, full-scan only — it has to read the header and the
  `.cpp` together). Three cases keep their header defaults, because there the header is
  the only place the value can live: a class whose default constructor is `= default` or
  defined inline in the header, a delegating constructor (`: Self( ... )` cannot carry
  member initializers — the target carries them), and a type with no constructor at all.
- Arrange fields to minimize byte padding; use bit packing where appropriate.
- Do not compare booleans through negation: write explicit comparisons such as
  `if (_bValid == false)`. Explicitly compare pointers to `nullptr` when needed.
- Bitfield flags (e.g. `uint8 _bFlag : 1;`) must be compared and assigned using `SW_TRUE` (1) and `SW_FALSE` (0) instead of `true`/`false`: write `if (_bFlag == SW_TRUE)` / `if (_bFlag == SW_FALSE)` and `_bFlag = SW_TRUE;`.
- Omit braces for a single-line `if` body. If an `else`/`else if` is present,
  use braces for any multi-line blocks — when one branch of the chain needs
  braces, every branch keeps them. Loops (`for`/`while`/`do`) always keep their
  braces, even for a single-statement body. Enforced by `FormatBranchBraces.py`
  (clang-format's `RemoveBracesLLVM` is not used: it strips loop braces too).
- Do not use `if` initializers. For unclear conditions or conditions with three
  or more parts, name the condition in a local variable first.
- Use `auto` only for iterators, structured bindings, or similarly complex
  types.
- Avoid lambdas unless they offer a performance benefit.
- Apply `const` wherever it is appropriate unless doing so harms performance.
- For range comparisons, place the variable in the middle (between lower and upper bounds) to reflect mathematical range notation: write `kMin <= value && value <= kMax` instead of `value >= kMin && value <= kMax`.
