## Naming

### C++

- Namespaces: `sw` and nested namespaces (for example, `sw::editor`).
- Classes, structs, enums: `PascalCase`; interfaces: `I` + `PascalCase`.
- Functions: `camelCase`; members: `_camelCase`; locals: `camelCase`.
- Constants: `kPascalCase`; globals: `gv_camelCase`.
- Global variables come in two kinds (`Core/GlobalVariable/GlobalVariableManager.h`); every macro takes the type first
  (`bool`, `int32`, `float32`, `sw::string` or a reflected enum). Runtime settings someone would change in the editor use
  `SW_GLOBAL_VARIABLE( type, name, default, desc )`. Bench, automation and diagnostic switches use `SW_TEST_GLOBAL_VARIABLE`: they
  stay settable with `-gv_*` but are hidden from the editor panel and presets and are **not registered in Shipping**. Use
  `SW_TEST_GLOBAL_VARIABLE_SHIPPED` only when scripts must drive the shipped executable with it — the list and the reason per variable
  live in `Scripts/lint/rules/CheckGlobalVariableKinds.toml` `[shipped]`, and a SHIPPED definition outside it stops the gate. Release is not
  Shipping: test variables stay registered in Release, so measurement and bench switches (`gv_bench*`) and crash switches (`gv_crashTest`)
  are plain `SW_TEST_GLOBAL_VARIABLE`. An `extern` is `SW_EXTERN_GLOBAL_VARIABLE( type, name )` for
  every kind; `CheckGlobalVariableKinds.py` blocks a type that differs from the definition.
- Static variables: `s_camelCase`; private statics: `_s_camelCase`.
- Macros: `SW_SCREAMING_CASE`.
- Raw pointers use a `p` prefix (`pObject`, `_pObject`); double pointers use
  `pp` (`_ppMember`, `ppMember`). Triple pointers or higher (`ppp`, `_ppp`, `***`) are strictly forbidden as architectural flaws.
- Associative containers use a `map` prefix (`map`, `_map`); fixed arrays use `arr` (`_arr`); variable arrays/lists use a `list` prefix (`list`, `_list`); sets use a `unique` prefix (`unique`, `_unique`, e.g. `_uniqueIds`). Do not use a `List` suffix for variable arrays/lists. Except for the `unique` prefix (`_uniqueIds`, `outUniqueIds`) and raw byte buffers (`_bytes`, `outBytes`), all container and parameter names MUST use singular form (e.g. `_listActor`, `_listItem`, `_mapIDToName`, `outListItem`, `outListHandle`; plural forms like `_listActors` are strictly forbidden). Byte vectors (`vector<uint8>`, `vector<int8>`, `vector<utf8>`) whose names contain the word `byte`/`bytes` (e.g. `_bytes`, `_rawBytes`, `bytes`, `outBytes`, `pOutBytes`) omit the `list` prefix.
- Function parameters use `camelCase`. Output parameters (Out-parameters) must start with an `out` prefix (`out` + PascalCase, e.g. `outValue`, `outConfig`, `outX`) with containers following `out` in singular form (`outListItem`, `outMapData`, `outArrBuffer`; `outUniqueIds` allows plural). Exceptionally, raw pointer output parameters place the `p`/`pp` prefix before `out`: `pOut...` (pointer, e.g. `pOutBuffer`, `pOutAPI`, `pOutResult`), `ppOut...` (double pointer), `pInOut...` (inout pointer, e.g. `pInOutSize`). In/Out parameters use `inout` / `pInOut` (e.g. `inoutSkeleton`, `pInOutSize`). A bare `out` names nothing and is blocked
  in header declarations (`CheckCodeConventions` rule `Naming/BareOutParameter`).
- Use descriptive names; do not use opaque abbreviations or loop counters such
  as `i`, `j`, or `k` (use at least `index`).
- **GPU resource verbs are a closed vocabulary.** A class that owns RHI resources derives from
  `RHIRenderResource` and names its device-lifecycle methods from this table only. Do not invent
  synonyms (`upload`, `applyToGPU`, `shutdownAllGPU`, `isUploaded`, `isReady`, `releaseGPU`):

  | Verb | Meaning |
  | --- | --- |
  | `initRHI( pDevice )` | Create this object's GPU resources on `pDevice`. Idempotent: return `true` when already resident. |
  | `updateRHI( pDevice )` | Push changed CPU data to resources that already exist. |
  | `releaseRHI( pDevice )` | `pDevice` is **still alive** — hand the resources back and clear the handles. |
  | `forgetRHI( pDevice )` | `pDevice` is **already gone** — clear the handles only; calling destroy here is use-after-free. |
  | `isRHIValid()` | Are this object's GPU resources live right now? |

  `releaseRHI` / `forgetRHI` / `initRHI` are never called in a loop from outside. `IRHIDevice` broadcasts
  them to the whole registry (`RHIRenderResource::releaseAllFor` / `forgetAllFor` / `initAllFor`), so a
  new resource class is covered the moment it derives. Per-frame buffer managers that are not assets
  (`GPUScene`) keep their own vocabulary — they are not registry members.

### Class suffixes — Manager · Registry · System · Service · Backend

A class suffix says what the class does with the objects it names. Pick the one that matches, the way the commercial engines use it:

| Suffix | Meaning | Commercial counterpart |
| --- | --- | --- |
| `…Manager` | Owns the lifetime of a set of runtime objects and decides the create / destroy / load policy. | Unity `SceneManager`, Unreal `UAssetManager` |
| `…Registry` | Lookup table filled by registration, keyed by name or ID. No lifetime policy. | Unreal `AssetRegistry` |
| `…System` | Runs every frame over many objects / components with the same logic. | Unity DOTS `System` |
| `…Service` | Replaceable function window reached through service binding or a remote endpoint. | PlayFab · EOS · Nakama services |
| `…Backend` | Platform or library implementation of an interface. | `JoltPhysicsBackend`, `XAudio2AudioBackend` |

A C function table is `…API` (`GameAPI`, `EditorAPI`, `ModuleAPI`).

Fixed naming rules that go with the table:

- **`Anim` is spelled out as `Animation`.** Identifiers with an `Anim` prefix are being renamed and no new one is added. The only
  exception is a name that refers to an outside term, such as Unreal's `AnimInstance` or `AnimMontage`.
- **A boolean variable or member keeps the `b` prefix** (`bVisible`, `_bOpen`), as in Unreal. Only a function that returns a
  boolean follows the `is` / `has` / `can` / `should` rule.
- **`Scene` owns the data of one scene** (Unreal Level / World, Unity Scene). `GameObjectManager` manages the lifetime of that
  scene's objects, and `GameObjectStore` is the storage it uses. Their header descriptions say each role, not "one scene" for all three.

### Function names

A reader who knows one of these names must be able to guess the rest; that is the whole point.
`CheckFunctionVocabulary.py` enforces six checks on header declarations — acronym runs (`AcronymRun`), the banned
verbs below (`BannedVerb`), abbreviations in function names (`Abbreviation`: `Attr` → `Attribute`), `check*`
predicates (`CheckVerb`), `string_view`/`hashed_string` name pairs (`NamePair`) and bare getters (`BareGetter`).
Declarations that predate a check sit in the gate's exemption table, `[exemption]` in `Scripts/lint/rules/CheckFunctionVocabulary.toml`
(key `<Rule>:<HeaderStem>::<function>`, the reason
"개명 예정 <new name>" or "도메인 용어"); a new declaration is never added there unless the verb is
the domain's own term (`UUID::generate`, `SurfaceBvh::build`), and a fixed name is removed from the table — a full run
reports keys that no longer match. The `on*` and spell-it-out rules are kept by review.

- **An acronym is written in capitals, everywhere.** What counts as an acronym is one list,
  `acronym` in `Scripts/lint/rules/AcronymRegistry.toml` (read by `Scripts/common/AcronymRegistry.py` as `kAcronym`); a shortening (`Nav`, `Anim`, `Info`) is not one. Types, files, namespaces,
  enums, folders, modules and test executables: `UISystem`, `GPUScene`, `HTTPClient`, `UISystem.h`. Functions, variables and
  members: capitals in the middle or at the end (`updateUI`, `queryAABB`, `entityID`, `pUISystem`, `_pGPUScene`), all lower case
  as the first word (`uiSystem`, `_gpuScene`, `_id`). Two capital acronyms never touch (`RHIUI…` hides the boundary — spell one
  out or put a word between). Lower-case extensions and resource paths (`*.ui.xml`), `gv_` prefixes and third-party names keep
  their spelling (a third-party member that looks like ours goes in `kExternalName`: Box2D `bodyIdA`, Tracy `queryId`).
  `CheckAcronymSpelling.py` blocks Pascal spellings and touching capitals of the acronyms in `kEnforced` (every registered acronym);
  `AcronymRun` blocks a capital run in a function name that is not one registered acronym (`bindComputeUAV` — `UAV` is not
  registered, so it stays `bindComputeUav`). A new acronym is added to `acronym` · `enforced` in that data file and rewritten across the tree with
  `FormatAcronymSpelling.py` in one commit.
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
  | advance one frame / one fixed step / gather input | `update` · `step` · `poll` | `tick` (outside the tick system) |
  | move a clock by a given amount (`advance( seconds )`, `advanceDay()`) | `advance` | — |
  | put a value or an object into a container this object owns / take it out (the container decides order or key) | `add` / `remove` | `push` outside a stack or queue |
  | hand over something that is called or looked up later — a callback, observer, handler, task, factory, cache — whose lifetime the caller keeps and ends with the pair | `register` / `unregister` | `add*Handler`, `add*Observer`, `add*Listener`, `remove*Handler` |
  | join to the end of an ordered sequence (text, bytes, a list whose order is the meaning) | `append` | `add` for text |

  Other spellings of get-or-create (`findOrAdd*`, an `ensure*` that returns) are aligned when their function is touched.
  `tick` names only the tick system itself (`SceneTickScheduler`, `TickBatchRegistry`, component `onTick`) and behavior-tree
  node ticks, as Unreal's `FTickFunction` · `BTTask::TickTask`; everything else that advances a frame or a time is `update`
  (`CheckFunctionVocabulary` `BannedVerb`, the domain uses sit in its exemption table).
  A variable or member holding a count is `xCount` (`itemCount`, `_listenerCount`); `countX()` is a function that counts by scanning.
- **Type suffixes say what the data is for.** `Def` — authored data definition (read from a file, shared, immutable);
  `Desc` — creation description handed to a `create*` call; `Spec` — a granted runtime instance in the Unreal GAS sense
  (`AbilitySpec` ↔ `FGameplayAbilitySpec`; the authored data is the `Def`); `Config` — preset or deployment configuration
  (project, server, build, and the `initialize` configuration of a service, host, transport or store driver); `Settings` — user or
  game settings someone changes at run time (project-wide engine settings tables such as `PhysicsSettings` · `UIScaleSettings` keep
  the Unreal `U*Settings` name, and so do match rules a game picks per match); `Params` — call arguments bundled into one value.
  A sub-struct of a catalog file is a `Def` (`BrZoneDef` inside `BrCatalog`).
  `Context` — the environment a caller hands to a callee for one call, pass, frame or operation (`RenderGraphPassContext`,
  `UIPaintContext`, `SerializeContext`); a graphics API object keeps the API's own term (`RHICommandContext`). A record the owner
  keeps for each thing it manages is a `Record` (`LiveReloadManager::ModuleRecord`), not a `Context`.
  `Unit` is a game-play unit (`RTSUnit`, `SRPGUnit`), an animated skeletal mesh in `AnimationSystem`, or a measurement unit
  (`ReflectUnit`); the three live in separate namespaces and each header says which one it means.
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
- **Pick the string type by what it holds, not by habit.** A name on a per-frame path is a `hashed_string` or a
  `string_view` into storage that already exists — return a view, not a copy. `fixed_string<N>` is only for text whose
  bound is a code constant (never data, a path or user input) where the heap is off limits (crash, log and signal
  paths) or a fixed buffer is required (ImGui input). It has no length field: `size()` scans the first `N` characters for a
  null (O(length), so not in a loop condition), and outside code may write up to `max_size()` characters into `data()`
  with or without a terminator. Overflow truncates with a warning; use `try_assign` / `try_append` where a truncated
  value is a wrong value. A null is never data in it — null-separated lists (Win32 filters, multi-select results) go in
  a `wstring` / `vector` that carries its own length.
  Up to 15 characters `string` is already heap-free (SSO), so `fixed_string` buys nothing there. No bulk rewrites.

- **A predicate reads as a question.** Start with `is` / `has` / `was` / `can` / `should`, or use a
  third-person verb (`supportsX`, `usesX`, `requiresX`, `matchesX`, `allowsX`, `overlapsX`).
  `check*` is not a predicate — a `check*` that returns `bool` is an `is*`/`has*`, and one that returns
  `void` and asserts is an `assert*`. A getter paired with `setX()` is `getX()` / `isX()`, never bare `x()`.
- **`on*` means "this happened"** — a notification handler, never the call that registers one. Registering
  is `register*` / `unregister*` (a registration call named `onLanguageChanged` that returns a handle
  reads as the notification it registers for).
- **Spell the word out** unless one of this repo's own type names abbreviates it. `XMLNode` spells
  `attribute()`, not `attr()`, because the type beside it is `XMLAttribute`; `TagQueryExpr::…Expr` and
  `ShaderEngineCbMember`'s `…Cb…` are fine because the type carries the same short form.
- **A fallible verb returns a result you must not drop silently.** A `bool` whose name starts with a fallible verb
  (`load` · `save` · `read` · `write` · `parse` · `apply` · `remove` · `spawn` · `attach` … — the list lives in
  `CheckFallibleNodiscard.py`) is `[[nodiscard]]`, so dropping it stops the build. Dropping it on purpose is
  `(void)call(); // why that is fine` — the reason on the same line or the line above (`CheckDiscardReason.py`):
  the callee already logs the failure, the file is optional, a test asserts the state below. Other `(void)` casts
  (an unused query result, an unused parameter) need no reason.
