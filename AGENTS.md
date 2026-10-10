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

- `SW_USE_SCCACHE` is `ON` by default and uses sccache when it is available.
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
- Global variables come in two kinds (`Core/GlobalVariable/GlobalVariableManager.h`); every macro takes the type first
  (`bool`, `int32`, `float32`, `sw::string` or a reflected enum). Runtime settings someone would change in the editor use
  `SW_GLOBAL_VARIABLE( type, name, default, desc )`. Bench, automation and diagnostic switches use `SW_TEST_GLOBAL_VARIABLE`: they
  stay settable with `-gv_*` but are hidden from the editor panel and presets and are **not registered in Shipping**. Use
  `SW_TEST_GLOBAL_VARIABLE_SHIPPED` only when scripts must drive the shipped executable with it
  (`gv_profileFrames`, `gv_screenshot*`, `gv_crashTest`, `gv_bench*`). An `extern` is `SW_EXTERN_GLOBAL_VARIABLE( type, name )` for
  every kind; `CheckGlobalVariableKinds.py` blocks a type that differs from the definition.
- Static variables: `s_camelCase`; private statics: `_s_camelCase`.
- Macros: `SW_SCREAMING_CASE`.
- Raw pointers use a `p` prefix (`pObject`, `_pObject`); double pointers use
  `pp` (`_ppMember`, `ppMember`). Triple pointers or higher (`ppp`, `_ppp`, `***`) are strictly forbidden as architectural flaws.
- Associative containers use a `map` prefix (`map`, `_map`); fixed arrays use `arr` (`_arr`); variable arrays/lists use a `list` prefix (`list`, `_list`); sets use a `unique` prefix (`unique`, `_unique`, e.g. `_uniqueIds`). Do not use a `List` suffix for variable arrays/lists. Except for the `unique` prefix (`_uniqueIds`, `outUniqueIds`) and raw byte buffers (`_bytes`, `outBytes`), all container and parameter names MUST use singular form (e.g. `_listActor`, `_listItem`, `_mapIdToName`, `outListItem`, `outListHandle`; plural forms like `_listActors` are strictly forbidden). Byte vectors (`vector<uint8>`, `vector<int8>`, `vector<utf8>`) whose names contain the word `byte`/`bytes` (e.g. `_bytes`, `_rawBytes`, `bytes`, `outBytes`, `pOutBytes`) omit the `list` prefix.
- Function parameters use `camelCase`. Output parameters (Out-parameters) must start with an `out` prefix (`out` + PascalCase, e.g. `outValue`, `outConfig`, `outX`) with containers following `out` in singular form (`outListItem`, `outMapData`, `outArrBuffer`; `outUniqueIds` allows plural). Exceptionally, raw pointer output parameters place the `p`/`pp` prefix before `out`: `pOut...` (pointer, e.g. `pOutBuffer`, `pOutAPI`, `pOutResult`), `ppOut...` (double pointer), `pInOut...` (inout pointer, e.g. `pInOutSize`). In/Out parameters use `inout` / `pInOut` (e.g. `inoutSkeleton`, `pInOutSize`). A bare `out` names nothing and is blocked
  in header declarations (`CheckOutParameterNames.py`).
- Use descriptive names; do not use opaque abbreviations or loop counters such
  as `i`, `j`, or `k` (use at least `index`).
- **GPU resource verbs are a closed vocabulary.** A class that owns RHI resources derives from
  `RHIRenderResource` and names its device-lifecycle methods from this table only. Do not invent
  synonyms (`upload`, `applyToGpu`, `shutdownAllGpu`, `isUploaded`, `isReady`, `releaseGpu`):

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

### Function names

A reader who knows one of these names must be able to guess the rest; that is the whole point.
`CheckFunctionVocabulary.py` enforces six checks on header declarations — acronym runs (`AcronymRun`), the banned
verbs below (`BannedVerb`), abbreviations in function names (`Abbreviation`: `Attr` → `Attribute`), `check*`
predicates (`CheckVerb`), `string_view`/`hashed_string` name pairs (`NamePair`) and bare getters (`BareGetter`).
Declarations that predate a check sit in the gate's `mapExemption` (key `<Rule>:<HeaderStem>::<function>`, the reason
"개명 예정 <new name>" or "도메인 용어"; `CheckOutParameterNames.py` does the same for bare `out`); a new declaration is never added there unless the verb is
the domain's own term (`Uuid::generate`, `SurfaceBvh::build`), and a fixed name is removed from the table — a full run
reports keys that no longer match. The `on*` and spell-it-out rules are kept by review.

- **An acronym is written in capitals, everywhere.** What counts as an acronym is one list,
  `Scripts/lint/AcronymRegistry.py` (`kAcronym`); a shortening (`Nav`, `Anim`, `Info`) is not one. Types, files, namespaces,
  enums, folders, modules and test executables: `UISystem`, `GPUScene`, `HTTPClient`, `UISystem.h`. Functions, variables and
  members: capitals in the middle or at the end (`updateUI`, `queryAABB`, `entityID`, `pUISystem`, `_pGPUScene`), all lower case
  as the first word (`uiSystem`, `_gpuScene`, `_id`). Two capital acronyms never touch (`RHIUI…` hides the boundary — spell one
  out or put a word between). Lower-case extensions and resource paths (`*.ui.xml`), `gv_` prefixes and third-party names keep
  their spelling. The tree moves one acronym at a time and only the acronyms in `kEnforced` are enforced
  (`CheckAcronymSpelling.py` — Pascal spellings and touching capitals; `AcronymRun` — a capital run in a function name must be
  one enforced acronym). An acronym not yet enforced keeps its current one-word spelling (`initRhi`, `bindComputeUav`) until
  `FormatAcronymSpelling.py` rewrites it across the tree.
- **One verb per concept.** Picking a synonym is how two names for one thing get born:

  | Concept | Verb | Never |
  | --- | --- | --- |
  | bring an object to life / take it down | `initialize` / `shutdown` | `setup`, `startup`, `cleanup`, `teardown` |
  | hand out and take back memory or a slot | `allocate` / `free` | `alloc`, `dealloc`, `dispose` |
  | build and return a new value | `create` (owning) · `make` (plain value) | `build`, `construct`, `generate` |
  | refill state that already exists | `rebuild` (from scratch) · `populate` (fill from a source) | `build` |
  | look something up | `get` (always there) · `find` (may miss, returns the value or a pointer) · `tryGet` (may miss, `bool` + out parameter — only when the value cannot be returned as "none") | `fetch`, `retrieve`, `lookup`, `obtain` |
  | get, loading on a miss, shared ownership | `acquire( path )` (the caches) | `getOrLoad` |
  | get, creating on a miss | `getOrCreate` | `findOrAdd`, `ensure` that returns something |
  | make sure something exists, return nothing | `ensure*` | — |
  | work out a value from inputs | `compute` | `calculate`, `calc` |
  | advance one frame / one fixed step / gather input | `update` · `step` · `poll` | — |

  Other spellings of get-or-create (`findOrAdd*`, an `ensure*` that returns) are aligned when their function is touched;
  the ~120 frame-advance declarations are not renamed in bulk — a new one follows the row.
- **Type suffixes say what the data is for.** `Def` — authored data definition (read from a file, shared, immutable);
  `Desc` — creation description handed to a `create*` call; `Spec` — a granted runtime instance in the Unreal GAS sense
  (`AbilitySpec` ↔ `FGameplayAbilitySpec`; the authored data is the `Def`); `Config` — preset or deployment configuration
  (project, server, build); `Settings` — user or game settings someone changes at run time; `Params` — call arguments
  bundled into one value.
- **`RT` means render thread** (profile tags `RT.Frame`, `RT.BeginFrame`). A render target is spelled `RenderTarget`.

- **A name is a `hashed_string`, and only that.** Take names as `const hashed_string&`; a string literal converts
  implicitly (`isActionDown( "Jump" )`), while pointers, `string_view` and `string` stay explicit so that interning
  dynamic text is visible at the call site (`hashed_string( text )`). Never declare a `string_view` overload next to
  the `hashed_string` one — with the same parameter count a literal call is ambiguous. A lookup that must **not**
  intern (free text that may not be a key) gets its own name: `findStringByText( string_view )`.
  `hashed_string` follows Unreal `FName`: `==` and `getHash()` ignore case, `c_str()` is the spelling you wrote
  (`isEqual( other, NameCase::CaseSensitive )` compares the spelling — use it for "did the name change?"), and there is
  no `operator<`: sort with `HashedStringLexicalLess` (stable, for output people or files see) or `HashedStringFastLess`
  (intern order, lookup only).

- **A predicate reads as a question.** Start with `is` / `has` / `was` / `can` / `should`, or use a
  third-person verb (`supportsX`, `usesX`, `requiresX`, `matchesX`, `allowsX`, `overlapsX`).
  `check*` is not a predicate — a `check*` that returns `bool` is an `is*`/`has*`, and one that returns
  `void` and asserts is an `assert*`. A getter paired with `setX()` is `getX()` / `isX()`, never bare `x()`.
- **`on*` means "this happened"** — a notification handler, never the call that registers one. Registering
  is `register*` / `unregister*` (a registration call named `onLanguageChanged` that returns a handle
  reads as the notification it registers for).
- **Spell the word out** unless one of this repo's own type names abbreviates it. `XmlNode` spells
  `attribute()`, not `attr()`, because the type beside it is `XmlAttribute`; `TagQueryExpr::…Expr` and
  `ShaderEngineCbMember`'s `…Cb…` are fine because the type carries the same short form.
- **A fallible verb returns a result you must not drop silently.** A `bool` whose name starts with a fallible verb
  (`load` · `save` · `read` · `write` · `parse` · `apply` · `remove` · `spawn` · `attach` … — the list lives in
  `CheckFallibleNodiscard.py`) is `[[nodiscard]]`, so dropping it stops the build. Dropping it on purpose is
  `(void)call(); // why that is fine` — the reason on the same line or the line above (`CheckDiscardReason.py`):
  the callee already logs the failure, the file is optional, a test asserts the state below. Other `(void)` casts
  (an unused query result, an unused parameter) need no reason.

### Python

- Public functions use `camelCase`; private helpers use `camelCaseInternal`.
- Module constants use `kPascalCase` or `_kPascalCase`.
- Module/file names use `PascalCase.py`.
- JSON keys of Python-owned contracts (`Config/Engine/CookContract.json` · `PackConfig.json` · `PackFormat.json`, hand-read import configs) use `snake_case`. Configs read through reflection use the C++ member name as the key (`_width`).

### HLSL

Shaders follow the C++ rules wherever HLSL can express them. `CheckShaderConventions.py` enforces this
section on every `.hlsl` / `.hlsli` (CI gate and pre-commit hook).

- **Functions** are `camelCase`. A helper in a shared header (`.hlsli`) starts with `sw` — FXC has no
  namespaces, so the prefix stands in for `sw::` (`swLoadInstance`, `swComputeWorldNormal`). A helper that
  lives in one `.hlsl` has no prefix (`isVisible`, `hashSeed`), so the name says where to look.
  The C++ function-name rules apply: an acronym is one word (`swLoadRwTexture2D`, never `SW_LoadRWTex2D`),
  words are spelled out (`Tex` → `Texture`, `Cmp` → `Comparison`), and the verb table applies — a value
  worked out from inputs is `compute…`, not an `…Of` suffix (`swComputeWorldPosition`, not `SwWorldPositionOf`).
  GPU access verbs: `load` / `store` for buffer and RW-texture elements, `sample` / `gather` for texture reads.
  Entry points stay `VSMain` / `PSMain` / `CSMain` (the compiler and pipeline XML name them as strings).
- **Types** are `PascalCase` with no `_t` suffix. A shared-header type starts with `Sw` (`SwInstanceData`,
  `SwMaterialData`); a type local to one `.hlsl` does not (`PSInput`, `GpuBatchInfo`). A struct that mirrors
  a C++ struct takes the C++ type name (`RHIDrawIndirectCommand`).
- **Fields** are `camelCase`. A field that mirrors a C++ member is that member without the leading `_`
  (`_startVertexLocation` → `startVertexLocation`). No opaque abbreviations: `position`, `normal`, `color`,
  `worldPosition`, `vertexId` — not `pos`, `nrm`, `col`, `wpos`, `vid`.
- **Locals and parameters** are `camelCase` and descriptive — no single letters, no `i` / `j` / `k`, no
  `idx` / `inst` / `ao` / `dtid`. `out` / `inout` parameters start with `out` / `inout` (`outPosition`).
  A fixed-size local array takes `arr` + a singular noun (`arrOffset[4]`). `groupshared` variables take `s_`
  (`s_arrKey[]` for an array). Never use an HLSL keyword as a name (`sample`, `point`, `line`, `linear`,
  `texture`, `sampler`, `vector`, `matrix`, …). A local `const` may be `kPascalCase`, as in C++.
- **Constants and macros**: a file-scope `static const` is `kPascalCase` (`kInvalidIndex`, `kPi`). A `#define`
  is `SW_` + `SCREAMING_CASE`, and a macro never aliases a resource or a function. Include guards are
  `SW_<DOMAIN>_<FILE>_HLSLI` (`SW_ENGINE_POSTBLOOM_HLSLI`).
- **Globals and cbuffer members** are `g_` + `PascalCase` (`g_ViewProj`, `g_SwInstances`). C++ binds them
  **by name** (`shaderslot::resname` / `cbname`, `PassConstantNames`, the `g_<Name>Index` registry convention),
  so they are not renamed for style and the container, plural and abbreviation rules do not apply to them.
  The same holds for every other string-bound name: cbuffer names (`PassCB`, `CullParams`, `SwRootConstants`),
  semantics, externally supplied defines (`DX11`, `SW_PASS_*`, `MATERIAL_*`), the macros in `bindingslots.hlsli`
  (C++ includes that file), material struct fields (`.material` files bind them) and shader file names.
  An exemption from the rules above is a named entry with its reason in the gate (`kStringBoundName`).
- **Renaming a string-bound name changes C++ in the same commit.** `ShaderBindingValidator::validate` skips
  a reflected name it does not know, so a one-sided rename silently switches that resource's check off;
  `ShaderBindingValidatorTest.EveryBoundNameIsInCookedReflection` fails instead when a name C++ binds is missing
  from the cooked `reflection.manifest`.

### Resource Assets

- All files and directories under `Resource/` MUST use strictly lowercase names (`[a-z0-9_.-]+`, e.g. `inventory.anim`, `0.title.scene.xml`, `ghost.prefab.json`). Uppercase characters are strictly prohibited (except documentation `README.md`). Enforced automatically by `CheckResourceCasing.py` and Git pre-commit hooks.
- The runtime reads textures only as DDS. A runtime texture folder (`textures/`) holds `.dds` and data (`.sprite.json`,
  `.meta`) only; source images (PNG, JPG, TGA, ...) live under `textures_raw/` at the same relative path and are imported
  with `App --import-textures` (the editor imports on hot reload too). Commit the DDS together with `textures_raw/import.stamp`;
  `TextureImportStampTest` (and `App --check-textures`) fails when a source, its import rule or its DDS drifts. Delete a
  source nothing references instead of moving it. Enforced by `CheckTextureFolders.py`.
- Models follow the same rule: glTF sources (`.glb`, `.gltf`) live under `models_raw/` and `App --import-models` writes
  `models/<name>.mesh` plus `models_raw/import.stamp` (`App --check-models` compares). Commit the `.mesh` with the stamp.

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
- Platform-specific includes go in **one** `#if SW_PLATFORM_WINDOWS / #elif ... / #endif` chain after the
  unconditional includes; inside a branch, project headers, a blank line, then system headers. Never open a
  second block on the same condition family for the system headers (`CheckIncludeOrder.py` rejects it).
- **Core folders are tiers** (`Common` → `Concurrency` · `Math` → `Memory` → `Container` → … → `Diagnostics` → `LogSink`, table in
  `Scripts/lint/gate/CheckCoreLayers.py`): a file includes only its own folder or a lower tier, `.cpp` included. A lower tier that must
  reach up takes an interface or a function pointer (`ILockObserver`, `IAllocationTracker`, `RaceDetectContext::setReportFunction`).
  Recompute the table with `Scripts/lint/report/RunCoreLayerGraph.py`. Enforced by `CheckCoreLayers.py`.
- Use the project type aliases from `Types.h`.
- Prefer Core and Engine facilities over STL or direct system facilities when
  they meet the need.
- **Ask platform, architecture and compiler questions with the macros CMake defines**, never with compiler
  built-ins (`_WIN32`, `__linux__`, `_MSC_VER`, `__clang__`, `__GNUC__`, `_M_X64`, `__x86_64__`, `__aarch64__`, ...):
  `SW_PLATFORM_WINDOWS` / `SW_PLATFORM_LINUX` (macOS is not a supported platform), `SW_X64` / `SW_ARM64`, and
  `SW_COMPILER_CLANG` (clang-cl included) / `SW_COMPILER_MSVC` (cl.exe only) / `SW_COMPILER_GCC`. clang-cl defines both
  `__clang__` and `_MSC_VER`, so "may I use an MSVC extension" (`__forceinline`, `__declspec`, MS intrinsics,
  `__FUNCSIG__`) is `SW_PLATFORM_WINDOWS` — Windows builds only with an MS-ABI toolchain — not `SW_COMPILER_MSVC`.
  The engine builds 64-bit only, so there are no 32-bit branches. The one file that reads built-ins is
  `Core/Common/TargetMacroCheck.h`, which fails the build when CMake's choice disagrees with the compiler.
  Enforced by `CheckTargetMacros.py`.
- **Ask the build target (`SW_TARGET_TYPE`: Game, Client, Server) only through `SW_WITH_CLIENT_CODE` (Game, Client),
  `SW_WITH_SERVER_CODE` (Game, Server) and `sw::build::kTargetName`**, and only inside `.cpp` bodies — never gate reflection
  declarations (`REFLECT`, `PROPERTY`) or a header's class layout on them. A feature that splits between client and server
  splits into modules instead: shared `GF_<X>`, server-only `GF_Server_<X>`, client-only `GF_Client_<X>` (manifest `_listTarget`).
- **Read time through `Core/Time/MonotonicClock.h`**, never a `std::chrono` clock (`steady_clock`, `high_resolution_clock`,
  `system_clock` — aliases and `using namespace` included): `MonotonicClock::nowNanoseconds()`, `Stopwatch` for elapsed time,
  `Deadline::afterMilliseconds( ms )` + `isExpired()` for a bounded wait. Tests too. Duration values (`sleep_for`) are fine.
  A UTC timestamp (expiry, record time, event windows) is `WallClock::nowUnixMilliseconds()` (`Core/Time/WallClock.h`); services take it
  as a `nowMs` parameter so tests can pass a fake time.
  Exceptions live in one table with their reason. Enforced by `CheckClockReads.py`.
- Construct into memory you already hold with `sw_placement_new( pMemory ) T( ... )`
  (`Core/Memory/Memory.h`), never a bare `new ( pMemory ) T( ... )`. Enforced by
  `CheckCodeConventions.py` (`Style/PlacementNew`).
- Allocate on the heap through the sw allocator, never a bare `new T` / `new T[n]`: `sw_new T( ... )` / `make_unique<T>`
  for one object, `sw_new_array<T>( n )` + `sw_delete_array( p, n )`, `make_unique<T[]>( n )` or `vector<T>` for arrays. CRT `new` is
  invisible to memory tags and leak checks. Enforced by `CheckCodeConventions.py` (`Style/RawNew`).
- **Borrow with a pointer, keep with a handle.** A `GameObject*` / `Component*` you do not own is valid
  only inside the current call (at most the current frame). Anything kept across frames — members,
  selection lists, undo records — holds a `GameObjectHandle` / `ComponentHandle` (`Core/Container`) and
  resolves it on each use through the owning manager (`resolveGameObject` / `resolveComponent`, `nullptr`
  once the target is gone). Handles are ids that are never reused, so they survive renames; editor undo,
  play-session restore and hot reload recreate objects with their original ids. Ids are unique across the
  process, but a handle resolves only through the manager (scene) that owns the object. Structural links the
  object model maintains itself (owner, scene hierarchy, registries) stay raw pointers.
- **Saved state never points at another object by name.** A parent reference is the parent's id (a scene file's
  own entity id, or the runtime id in same-process snapshots), resolved by `ObjectStateBatch` after every
  object of the batch is loaded. Any new path that loads object state goes through a batch
  (`ObjectLoadContext::_pBatch` + `finish()`); names are uniquified by the manager, so a lookup by name lands on
  a namesake.
- Do not spell out buffer/path-size magic numbers (e.g. `char buf[64]`,
  `fixed_string<256>`, `StringBuilder<32>`). Use the sentinels in the
  `constant` namespace (`Core/Common/Defines.h`) instead: `kMaxBuffer16`
  through `kMaxBuffer8192`, and `kMaxPathSize` for filesystem paths.

## Constants — where a constant lives

One meaning, one definition. A second copy of a value compiles, passes tests, and drifts the day one side changes.

- **Used in one `.cpp`** → that file's `XxxInternal` struct as `static constexpr` (or inside the function). Do not hoist it "in case".
- **Used across one module** → the owning type as `static constexpr` (`MeshAssetFormat::kExtension`), or the module's namespace in its
  types header (`audio::kSampleRate` in `AudioTypes.h`). No `*Constants.h` grab-bag per folder. A constant a delay-loaded module reads by
  reference must not be a static member of an exported (`SW_GF_API`) class — it becomes a data import the delay-load rejects; use a
  namespace-scope `inline constexpr` (`kMaterialColorParameter`).
- **A contract between modules or backends** → the contract header: `RHITypes.h` `constant` block (backend ↔ backend),
  `bindingslots.hlsli` (shader ↔ C++, pure `#define` numeric literals, read through `ShaderBindingSlots.h` as `shaderslot::k*`),
  `Core/Common/Defines.h` · `Engine/Common/EngineDefines.h` `constant` (engine-wide). If two sides must agree, there is one definition and both
  read it — even when the values happen to match today. A per-side copy with a "must equal X" comment is the bug, not the fix.
- **Well-known values have one home**: π family, √2, e → `MathUtil` (`kPi` · `kTwoPi` · `kHalfPi` · `kPi64` · `kDegreeToRadian` · `kSqrt2` ·
  `kInvSqrt2` · `kEuler`; shaders use `common.hlsli`); gravity → `PhysicsSystem::getConfiguredGravity(Magnitude)` at run time (the physics
  settings table; `constant::kDefaultGravity` is only its default, shaders get it through a material or root constant); golden-ratio,
  splitmix64 and FNV-1a → `HashUtil`; four-character tags → `FourCcUtil::make( "SWHF" )` (file byte order). `CheckWellKnownConstants.py`
  blocks the literals elsewhere.
- **Sentinels** keep a domain name but take their value from `invalid_index` (`static constexpr uint32 kNotRegistered = invalid_index::kUint32;`).
  An all-bits mask (`kAllLayers`) is not a sentinel and keeps its literal.
- **No aliases.** `static constexpr uint32 kFrameCount = constant::kMaxFrameCountInFlight;` is a second name for one concept — use the original.
  A local name that adds meaning to a shared value is fine only when the meaning differs (`kUnitQuadHalfDiagonal = MathUtil::kInvSqrt2`).
- **Gameplay tuning values** (speeds, distances, chances, intervals, model scales) are data: a `PROPERTY` on the component (default in the
  constructor, data without the key keeps it), or a field of the settings / catalog struct a kit already takes from the game. Performance
  thresholds (spin counts, parallel thresholds, buffer and pool limits) stay named constants next to their use; only one that someone needs
  to change without a rebuild becomes a `gv_*`. Never a shared header.
- **Names**: `kPascalCase`, including `MathUtil`. Same meaning, same name across formats: a file or blob format declares `kMagic` · `kVersion` ·
  `kExtension` on its owning type; a state section declares `kStateTag` · `kStateVersion`; a second format in the same scope prefixes the
  format (`kBinMagic`). A value with a unit says it in the name unless the type does (`kTimeoutSeconds`, `kBudgetMilliseconds`, `kMaxBytes`,
  `kSlopeRadians`); a converted value goes through a named rate (`constant::kNanosecondsPerSecond`), not a bare `1000`.
- **Literals that stay literal**: `0` · `1` · `-1` · `2` in their obvious uses, test expectations, rows of an initializer table, log and assert
  strings, reflection metadata (`PROPERTY( Max = ... )` — the parser reads literals). Everything else that repeats or needs a comment to be
  understood gets a name. `RunRepeatedConstants.py` reports what repeats.

## Helpers: Util vs Internal

- Shared helpers used by more than one translation unit belong on a `XxxUtil`
  static struct in a header (for example `SerializerUtil`, `MaterialUtil`). Do
  not name those headers or types `Internal`.
- **One class or struct definition per named `namespace` block.** A file that defines
  several classes closes the block after each `};` and reopens it for the next, so each
  class folds on its own (`fixer/FormatNamespaceBlocks.py` does it; template
  specializations of one name stay together, the anonymous namespace stays one block).
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
  `VulkanRHIResourceFactoryPipeline.cpp` uses `VulkanRHIResourceFactoryPipelineInternal`, not
  `VulkanRHIResourceFactoryInternal`. Enforced by `CheckCodeConventions.py`
  (`Naming/DuplicateInternalHelper`, full-scan only).
- The same holds for a constant declared directly in the anonymous namespace (`constexpr int32 kLimit = 4;`):
  two `.cpp` files with the same bare name collide in a unity build. Put it inside the TU's `XxxInternal`
  struct as `static constexpr`, or give it a name no other `.cpp` uses. Enforced by `CheckCodeConventions.py`
  (`Naming/DuplicateAnonymousConstant`, full-scan only); constants inside a struct or a function are not affected.

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
  - `SW_GLOBAL_VARIABLE` (and its test forms) declares `extern`; it is deliberately external linkage and stays out.
  - `main` and functions declared in a header stay at namespace scope.

## Writing — docs, comments, commit messages

- Docs, comments and commit messages follow `docs/10_WritingDocs.md`: sentence rules (§4) and the term table (§5 —
  established loanwords and English terms, no coined native words). Log and assert strings stay English.

## C++ style

- Follow `.clang-format`.
- When a constructor exists, initialize fields in the constructor (not in the
  header), in declaration order. Use brace initialization — except an iterator pair, which takes
  parentheses (`_listValue( list.begin(), list.end() )`): braces pick the `initializer_list` constructor and store
  the two iterators as elements (`Style/IteratorPairBraces`). Put one initializer
  per line, with subsequent lines beginning with `,`. An initial value has exactly
  one home: writing it in both places hides which one wins (the constructor does) and
  invites changing only one of them. Enforced by `CheckCodeConventions.py`
  (`Style/HeaderMemberInitializer`, full-scan only — it has to read the header and the
  `.cpp` together). Three cases keep their header defaults, because there the header is
  the only place the value can live: a class whose default constructor is `= default` or
  defined inline in the header, a delegating constructor (`: Self( ... )` cannot carry
  member initializers — the target carries them), and a type with no constructor at all.
- A field that default-initialization leaves indeterminate (integer, float, `bool`, enum, pointer, an array of those,
  `atomic<scalar>`, a bit-field) has its value **somewhere**: every constructor with an initializer list (copy and move
  constructors included) lists it, or it has a header default. A class whose default constructor is `= default` needs
  header defaults for them — a bit-field cannot have one before C++20, so such a class writes its constructor.
  `-Wreorder-ctor` and `Style/ConstructorOrder` only order the fields that are listed; a field missing from the list
  starts as garbage and nothing else notices. Byte arrays used as buffers (`utf8 _arrStaticBuffer[N]`) are exempt.
  Enforced by `CheckCodeConventions.py` (`Style/ConstructorInitializesEveryField`, full-scan only); a type it cannot
  classify from the text (a struct, an alias with two meanings) is skipped rather than guessed.
- Arrange fields to minimize byte padding; use bit packing where appropriate.
- Do not compare booleans through negation: write explicit comparisons such as
  `if (_bValid == false)`. Explicitly compare pointers to `nullptr` when needed.
- `uint8` boolean members (`_b*`, whether or not they are bitfields like `uint8 _bFlag : 1;`) are assigned and
  compared with `SW_TRUE` / `SW_FALSE` — never `true`/`false`, and never a bare `1`/`0`: write
  `_bFlag = SW_TRUE;`, `if (_bFlag == SW_TRUE)`, `if (_bFlag == SW_FALSE)`. A bare `1` reads as a count;
  the macro says it is a state. Assigning `true` to a bitfield can also warn. Enforced by
  `CheckCodeConventions.py` (`Style/BitfieldBoolean`, full-scan only — it has to read the declaration to know
  the type, and it skips any name that is declared `bool` somewhere else, preferring a miss over a false
  positive). Real `bool` members keep `true`/`false`.
- Omit braces for a single-line `if` body. If an `else`/`else if` is present,
  use braces for any multi-line blocks — when one branch of the chain needs
  braces, every branch keeps them. Loops (`for`/`while`/`do`) always keep their
  braces, even for a single-statement body (an empty body is `{}`, not `;`). Enforced by `FormatBranchBraces.py`
  (clang-format's `RemoveBracesLLVM` is not used: it strips loop braces too); the gate `CheckLoopBraces` and
  the fixer's loop pass keep the loop rule — a loop whose head and body have a comment or preprocessor line
  between them is reported for a manual fix.
- A `switch` over an enum that handles **every** enumerator has **no** `default:` (LLVM coding standard): adding an
  enumerator and forgetting a `case` then stops the build (`-Werror=switch`), and a `default:` in a fully covered
  switch is itself an error (`-Werror=covered-switch-default`) because it would silence that check. A switch that
  handles only some enumerators uses `default:` and does not list the rest (`-Wswitch-enum` and `-Wswitch-default`
  are off). Out-of-range values are rejected where they enter (deserialization refuses unknown enumerators); a
  function that returns from every `case` returns its fallback after the switch. No per-file `#pragma` for switch
  diagnostics.
- A `switch` `case`/`default` whose body is more than one statement takes braces;
  a one-statement body does not. `break;` counts as a statement, so
  `case A: doIt(); break;` across two lines gets braces and `case A: return X;`
  does not. The `break;` goes **inside** the braces. A body holding a preprocessor
  directive is left alone — its extent is not decidable from the text, and an
  opening and closing brace on opposite sides of an `#if` compiles on one platform
  only. **Within one `switch`, if any case takes braces, every case with a body takes
  them** (the same consistency rule as an `if` chain; a fall-through label with no body
  stays bare). A switch whose cases are all one statement stays a table. Enforced by
  `FormatBranchBraces.py`; clang-format's `InsertBraces` cannot express this (it never
  looks at case labels).
- Do not use `if` initializers. For unclear conditions or conditions with three
  or more parts, name the condition in a local variable first.
- Use `auto` only for iterators, structured bindings, or similarly complex
  types.
- Avoid lambdas unless they offer a performance benefit.
- Apply `const` wherever it is appropriate unless doing so harms performance.
- For range comparisons, place the variable in the middle (between lower and upper bounds) to reflect mathematical range notation: write `kMin <= value && value <= kMax` instead of `value >= kMin && value <= kMax`.

## Editing traps (편집 함정)

- 한 함수에서 **여러 구간을 빼낼 때는 뒤쪽 구간부터** 한다. 앞쪽을 먼저 빼면 뒤쪽 줄 번호가 밀린다.
- 파일을 스크립트로 고칠 때 CRLF 를 보존한다. 이 저장소는 CRLF 다.
- **bash heredoc 은 `\` 를 뭉갠다**(`'\0'` 이 널 바이트가 된 적이 있다). 백슬래시가 든 내용은 파일로 써서 넘긴다.
- **파서를 고친 뒤 "`.gen.cpp` 가 다시 만들어졌나" 를 산출물 시각으로 판단하지 말 것.** 내용이 같으면 파일을 다시 쓰지 않는다.
  다시 만들었는지는 옆의 `<이름>.gen.cpp.stamp` 시각으로 본다.
- `grep -v` 로 거를 때 이름이 비슷한 다른 것(`TestArchive` 등)까지 걸러지지 않는지 본다.
- **Windows PowerShell 5.1 의 `Get-Content` · `Set-Content` 로 소스를 고치지 말 것** — UTF-8 한국어 주석을 CP949 로 읽어 되돌릴 수 없게 깨고 BOM 을 붙인다. 파이썬(`encoding='utf-8'`)으로 고친다.
- **실패한 커밋 뒤에는 스테이징이 남는다** — 다음 커밋 전에 `git status`. Git Bash heredoc 은 `\\` 를 뭉갤 수 있다 — 스크립트는 파일로 써서 돌린다.
- 패치 스크립트를 다시 돌릴 때 바꿀 글(old)이 새 글(new)의 접두이면 같은 내용이 두 번 들어간다 — 멱등하게 짠다. 큰 if-체인을 접을 때는 잘라낸 구간의 타입을 grep 으로 세어 사이의 다른 분기가 같이 잘리지 않았는지 본다.
- `rm -rf` 할 폴더 안에 셸의 작업 디렉터리가 있으면 반쯤 지워지고 뒤의 `cp -r` 이 그 안에 중첩 복사된다 — 저장소 루트로 옮긴 뒤 지운다. 옛 코드로 App · 벤치를 돌릴 바이너리는 전체 빌드로 만든다(테스트 타깃만 빌드하면 모듈 DLL 이 새 ABI 로 남는다).
