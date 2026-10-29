> 질문: C++ 이름(대소문자, 접두어, 컨테이너, 클래스 접미사, 함수 어휘)을 어떻게 짓는가?
> 독자: C++ 이름을 짓거나 바꾸는 사람.
> 결론: 타입은 `PascalCase`, 함수와 지역은 `camelCase`, 멤버는 `_camelCase`, 상수는 `kPascalCase`, 전역은 `gv_camelCase`. 컨테이너는 접두어 + 단수(`_listItem`). 클래스 접미사와 함수 어휘는 아래 표를 따른다.

## Naming

### C++

| Thing | Form |
| --- | --- |
| Namespaces | `sw` and nested (`sw::editor`) |
| Classes, structs, enums | `PascalCase`; interfaces `I` + `PascalCase` |
| Functions, locals, parameters | `camelCase` |
| Members | `_camelCase` |
| Constants | `kPascalCase` |
| Globals | `gv_camelCase` |
| Static variables | `s_camelCase`; private statics `_s_camelCase` |
| Macros | `SW_SCREAMING_CASE` |

- Descriptive names. No opaque abbreviations, no loop counters `i` / `j` / `k` (use at least `index`).
- **Global variables** (`Core/GlobalVariable/GlobalVariableManager.h`), every macro takes the type first:
  - `SW_GLOBAL_VARIABLE( type, name, default, desc )`: runtime settings someone changes in the editor.
  - `SW_TEST_GLOBAL_VARIABLE`: bench, automation and diagnostic switches. Settable with `-gv_*`, hidden from the editor panel, **not registered in Shipping**; use `_SHIPPED` only when scripts must drive the shipped executable.
  - `SW_EXTERN_GLOBAL_VARIABLE( type, name )`: the `extern` for every kind.

#### Pointers and containers

| Kind | Prefix | Example |
| --- | --- | --- |
| Raw pointer | `p` | `pObject`, `_pObject` |
| Double pointer | `pp` | `ppMember`, `_ppMember` |
| Triple pointer or more | forbidden (architectural flaw) | |
| Associative container | `map` | `_mapIDToName` |
| Fixed array | `arr` | `_arrBuffer` |
| Variable array / list | `list` (never a `List` suffix) | `_listItem` |
| Set | `unique` | `_uniqueIds` |
| Byte vector (`vector<uint8>`, `<int8>`, `<utf8>`) whose name has `byte` / `bytes` | no `list` prefix | `_bytes`, `outBytes`, `pOutBytes` |

All container and parameter names are **singular** (`_listActor`, `outListHandle`; `_listActors` is forbidden). The only plurals are the `unique` prefix (`_uniqueIds`, `outUniqueIds`) and raw byte buffers (`_bytes`).

#### Parameters

- Output parameters start with `out` + PascalCase (`outValue`, `outConfig`). A container follows `out` in singular (`outListItem`, `outMapData`, `outArrBuffer`).
- Raw pointer outputs put `p` / `pp` before `out`: `pOutBuffer`, `ppOutObject`.
- In/out parameters: `inout` / `pInOut` (`inoutSkeleton`, `pInOutSize`).
- A bare `out` names nothing and is blocked in header declarations.

#### GPU resource verbs (closed vocabulary)

A class that owns RHI resources derives from `RHIRenderResource` and names its device-lifecycle methods from this table only (no `upload`, `applyToGPU`, `isReady`, `releaseGPU`):

| Verb | Meaning |
| --- | --- |
| `initRHI( pDevice )` | Create this object's GPU resources on `pDevice`. Idempotent: return `true` when already resident |
| `updateRHI( pDevice )` | Push changed CPU data to resources that already exist |
| `releaseRHI( pDevice )` | `pDevice` is **still alive**: hand the resources back and clear the handles |
| `forgetRHI( pDevice )` | `pDevice` is **already gone**: clear the handles only; calling destroy here is use-after-free |
| `isRHIValid()` | Are this object's GPU resources live right now? |

Never call `releaseRHI` / `forgetRHI` / `initRHI` in a loop from outside: `IRHIDevice` broadcasts them to the whole registry (`releaseAllFor` / `forgetAllFor` / `initAllFor`). Per-frame buffer managers that are not assets (`GPUScene`) keep their own vocabulary.

### Class suffixes

A suffix says what the class does with the objects it names, the way the commercial engines use it:

| Suffix | Meaning |
| --- | --- |
| `…Manager` | Owns the lifetime of a set of runtime objects and decides the create / destroy / load policy (Unreal `UAssetManager`) |
| `…Registry` | Lookup table filled by registration, keyed by name or ID. No lifetime policy (Unreal `AssetRegistry`) |
| `…System` | Runs every frame over many objects / components with the same logic (Unity DOTS `System`) |
| `…Service` | Replaceable function window reached through service binding or a remote endpoint |
| `…Backend` | Platform or library implementation of an interface (`JoltPhysicsBackend`, `XAudio2AudioBackend`) |
| `…API` | A C function table (`GameAPI`, `EditorAPI`, `ModuleAPI`) |

Fixed rules that go with it:

- Spell `Anim` out as `Animation`, except a name that refers to an outside term (Unreal's `AnimInstance`).
- A boolean variable or member keeps the `b` prefix (`bVisible`, `_bOpen`), as in Unreal. Only a function that returns a boolean follows `is` / `has` / `can` / `should`.
- `Scene` owns the data of one scene. `GameObjectManager` manages the lifetime of that scene's objects; `GameObjectStore` is the storage it uses.

### Function names

A reader who knows one of these names must be able to guess the rest. Use a banned verb only when it is the domain's own term (`UUID::generate`, `SurfaceBvh::build`).

#### One verb per concept

| Concept | Verb | Never |
| --- | --- | --- |
| Bring an object to life / take it down | `initialize` / `shutdown` | `setup`, `startup`, `cleanup`, `teardown` |
| Hand out and take back memory or a slot | `allocate` / `free` | `alloc`, `dealloc`, `dispose` |
| Build and return a new value | `create` (owning) · `make` (plain value) | `build`, `construct`, `generate` |
| Refill state that already exists | `rebuild` (from scratch) · `populate` (fill from a source) | `build` |
| Look something up | `get` (always there) · `find` (may miss, returns value or pointer) · `tryGet` (may miss, `bool` + out parameter, only when "none" cannot be returned) | `fetch`, `retrieve`, `lookup`, `obtain` |
| Get, loading on a miss, shared ownership | `acquire( path )` (the caches) | `getOrLoad` |
| Get, creating on a miss | `getOrCreate` | `findOrAdd`, `ensure` that returns something |
| Make sure something exists, return nothing | `ensure*` | |
| Work out a value from inputs | `compute` | `calculate`, `calc` |
| Advance one frame / fixed step / gather input | `update` · `step` · `poll` | `tick` (outside the tick system) |
| Move a clock by a given amount | `advance( seconds )`, `advanceDay()` | |
| Put into / take out of a container this object owns (the container decides order or key) | `add` / `remove` | `push` outside a stack or queue |
| Hand over something called or looked up later (callback, observer, handler, task, factory, cache) whose lifetime the caller ends with the pair | `register` / `unregister` | `add*Handler`, `add*Observer`, `add*Listener`, `remove*Handler` |
| Join to the end of an ordered sequence (text, bytes, a list whose order is the meaning) | `append` | `add` for text |

- `tick` names only the tick system itself (`SceneTickScheduler`, `TickBatchRegistry`, component `onTick`) and behavior-tree node ticks. Everything else that advances a frame or time is `update`.
- A variable or member holding a count is `xCount` (`itemCount`, `_listenerCount`); `countX()` is a function that counts by scanning.

#### Acronyms

- An acronym is written in capitals, everywhere. A shortening (`Nav`, `Anim`, `Info`) is not an acronym.
- Types, files, namespaces, enums, folders, modules, test executables: `UISystem`, `GPUScene`, `UISystem.h`.
- Functions, variables, members: capitals in the middle or at the end (`updateUI`, `entityID`, `pUISystem`), all lower case as the first word (`uiSystem`, `_gpuScene`).
- Two capital acronyms never touch (`RHIUI…`): spell one out or put a word between.
- Lower-case extensions and resource paths, `gv_` prefixes and third-party names keep their spelling.
- A new acronym is added to the acronym list and rewritten everywhere in one commit.

#### Type suffixes

| Suffix | What the data is for |
| --- | --- |
| `Def` | Authored data definition (read from a file, shared, immutable). A sub-struct of a catalog file is a `Def` (`BattleRoyaleZoneDef` in `BattleRoyaleCatalog`) |
| `Desc` | Creation description handed to a `create*` call |
| `Spec` | A granted runtime instance, Unreal GAS sense (`AbilitySpec` ↔ `FGameplayAbilitySpec`; the authored data is the `Def`) |
| `Config` | Preset or deployment configuration (project, server, build; the `initialize` configuration of a service, host, transport or store driver) |
| `Settings` | User or game settings changed at run time. Project-wide engine tables (`PhysicsSettings`, `UIScaleSettings`) keep the Unreal `U*Settings` name, and so do match rules a game picks per match |
| `Params` | Call arguments bundled into one value |
| `Context` | The environment a caller hands a callee for one call, pass, frame or operation (`RenderGraphPassContext`, `SerializeContext`). A graphics API object keeps the API's term (`RHICommandContext`). A record the owner keeps per managed thing is a `Record`, not a `Context` |
| `Unit` | A game-play unit, an animated skeletal mesh in `AnimationSystem`, or a measurement unit; each header says which |

`RT` means render thread (`RT.Frame`, `RT.BeginFrame`). A render target is spelled `RenderTarget`.

#### Names and strings

- **A name is a `hashed_string`, and only that.** Take names as `const hashed_string&`. A string literal converts implicitly (`isActionDown( "Jump" )`); pointers, `string_view` and `string` stay explicit so interning dynamic text is visible (`hashed_string( text )`).
- Never declare a `string_view` overload next to the `hashed_string` one (a literal call is ambiguous). A lookup that must **not** intern gets its own name: `findStringByText( string_view )`.
- `hashed_string` follows Unreal `FName`: `==` and `getHash()` ignore case, `c_str()` is the spelling you wrote (`isEqual( other, NameCase::CaseSensitive )` for "did the name change?"). There is no `operator<`: sort with `HashedStringLexicalLess` (stable, for output) or `HashedStringFastLess` (intern order, lookup only).
- **Pick the string type by what it holds.**
  - A name on a per-frame path is a `hashed_string` or a `string_view` into existing storage; return a view, not a copy.
  - `fixed_string<N>` only for text whose bound is a code constant (never data, a path or user input) where the heap is off limits (crash, log and signal paths) or a fixed buffer is required (ImGui input). `size()` scans for a null (O(length), not in a loop condition), overflow truncates with a warning (use `try_assign` / `try_append` where truncation is wrong), and a null is never data in it.
  - Up to 15 characters `string` is already heap-free (SSO).

#### Predicates, callbacks, results

- **A predicate reads as a question**: `is` / `has` / `was` / `can` / `should`, or a third-person verb (`supportsX`, `usesX`, `requiresX`, `matchesX`, `allowsX`, `overlapsX`).
  - `check*` is not a predicate: a `bool` one is `is*`/`has*`, a `void` one that asserts is `assert*`.
  - A getter paired with `setX()` is `getX()` / `isX()`, never bare `x()`.
- **`on*` means "this happened"**: a notification handler, never the call that registers one. Registering is `register*` / `unregister*`.
- **Spell the word out** unless one of this repo's own type names abbreviates it. `XMLNode` has `attribute()`, not `attr()`, because the type beside it is `XMLAttribute`; `TagQueryExpr::…Expr` and `ShaderEngineCbMember`'s `…Cb…` are fine because the type carries the same short form.
- **A fallible verb returns a result you must not drop silently.** A `bool` whose name starts with `load` · `save` · `read` · `write` · `parse` · `apply` · `remove` · `spawn` · `attach` … is `[[nodiscard]]`. Dropping it on purpose is `(void)call(); // why that is fine`. Other `(void)` casts need no reason.
