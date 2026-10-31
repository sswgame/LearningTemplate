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
| `…Backend` | Platform or library implementation of an interface (`XAudio2AudioBackend`, `ImGuiDX12RendererBackend`) |
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

### 용어 — 같은 단어 한 뜻

폴더 · 형식 · 함수 이름에 쓰는 낱말의 뜻이다(2026-10 구조 개편에서 정했다). 새 이름이 이 표와 다른 뜻으로 같은 낱말을 쓰면 이름을 바꾼다.

| 규칙 | 근거 |
|---|---|
| Asset = 콘텐츠(`Engine/Asset`, 데이터 폴더 `Content/`), Resource = GPU · RHI 자원만 | 언리얼 Content · AssetManager · FRenderResource |
| Cook = 원본을 런타임 바이너리로 굽는 일(업계 공용어 — 언리얼 · 프로스트바이트 · id), Pawn · Possess = 조종자가 쥐는 몸 · 쥐기(게임 프레임워크 공용어). 한 엔진 · 게임만의 고유 이름 · 브랜드 · 약어는 내용으로 바꾼다(`BugIt` → `BugReport`, 7 절 H) | 이름은 내용으로 |
| Pack(`.pack`) · Watch(자동화 감시 값, Probe 는 라이트 · 리플렉션 프로브만) · Scene(씬 컨테이너, World 는 월드 공간 뜻만) | 같은 단어 한 뜻 |
| Module 은 동적으로 올리는 코드 단위만. 렌더러 확장점은 `RenderExtension`, 모듈 로더는 `IModuleLoader` · `ModuleLoadStage` · `ModuleUnloadStage`. 모듈 문맥의 Image → Library | 언리얼 SceneViewExtension |
| Host = 엔진을 돌리는 프로세스(App · Server · 테스트 하네스)와 그 쪽(`HostService` — 엔진 쪽 짝은 `EngineServices`). 네트워크 끝점 `NetEndpoint*`, 에디터 패널 진입점 `IEditorPanelService`, 온라인 키트 서버 서비스 `IOnlineServerService`(짝 `IOnlineClientService`) | 같은 단어 한 뜻 |
| Runtime = 모든 호스트에서 도는 것(키트 `Runtime/`) · 실행 중. 모듈 C-ABI 계약은 `Source/ModuleAPI` | 언리얼 모듈 형식 Runtime · ServerOnly |
| Automation = 시나리오 자동 테스트(`Engine/Automation`). UI 의 시나리오 단계는 `UI/Scenario/UIScenarioSteps` — "UI Automation" 은 Windows UIA 만 | 언리얼 Automation System |
| 정적 함수만 있는 타입은 대상을 인자로 받는 상태 없는 라이브러리면 도메인 이름 그대로. 고칠 것은 서브시스템 상태 위의 정적 파사드뿐(`EditorPlaySession` 이 서브시스템) | 언리얼 FJsonSerializer · FPaths |
| 낮은 층 · 높은 층이 서로 알아야 하면 위가 아래에 건다: 팩 매니저(엔진 범위에 건 `PackManager`), 메시 무효화(엔진 범위의 `IMeshDerivedCache`), 서브시스템 순서(위 층의 의존 칸) | 언리얼 FPlatformFileManager · 모듈 등록 |
| 키트 하나의 에디터 확장은 그 키트 폴더의 `Editor/`(런타임 · 서버로 나뉜 키트는 `<기능>/Editor/`). `Plugin/Editor` 에는 GameFramework 자체와 여러 키트를 잇는 확장만 | 언리얼 플러그인의 <X>Editor 모듈 |
| 폴더 · 파일 이름은 내용으로: `Engine/FileSystem`(층 1 콘텐츠 파일 시스템) · `Engine/Foundation` · `Core/{Foundation,Hash}` · `Engine/2D`(층 6, 언리얼 Paper2D) · `Graphics/LOD` · `RHI/{Backend,Transfer,Diagnostics}` · 에셋 파일 `*Asset` · XML 정의 `*Def` | |
| 콘텐츠(씬 · 프리팹)가 쓰는 형식의 모듈은 그 도메인이 가진 것이다 — `game/<팩>` 은 그 게임 매니페스트 의존, `common/` 은 모든 게임이 가진 것, `editor/` 는 엔진 · EditorModule, `engine/` 은 엔진만. configure 가 맞대고 멈춘다(`Scripts/generate/ContentModules.py`, 씬을 고친 뒤에는 다음 configure 가 본다) | 언리얼 .uproject Plugins |
| 둔 것: `Editor*` · `Automation*` 접두어(타입 이름은 전역 유일), App 폴더(실행 파일 호스트 부품 — 언리얼 Launch), `*Info` 정적 사실 표(언리얼 FPixelFormatInfo), `Core/Container` 등 폴더 이름과 같은 접두어 묶음(언리얼 Containers 처럼 평평하게), `IWindowTitleTarget`(창 제목을 거는 길을 하나로 막는 이음매 — `MainWindowSubsystem` 이 비공개 상속, 테스트가 가짜를 넣는다) | |
