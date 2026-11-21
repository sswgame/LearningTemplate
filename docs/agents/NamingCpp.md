# NamingCpp

> 결론: 타입은 `PascalCase`, 함수와 지역은 `camelCase`, 멤버는 `_camelCase`, 상수는 `kPascalCase`, 전역은 `gv_camelCase`. 컨테이너는 접두어 + 단수(`_listItem`). 함수 첫 단어 · 금지 이름 · 약어 · 모든 낱말은 린트 `naming-words` 가 강제한다(표 `Config/Rules/Lint/NamingWords.json`).

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

- Descriptive names. No opaque abbreviations, no loop counters `i` / `j` / `k` (at least `index`).
- **Global variables** (`Core/GlobalVariable/GlobalVariableManager.h`), type first:
  - `SW_GLOBAL_VARIABLE( type, name, default, desc )`: runtime settings changed in the editor or the shipped game.
  - `SW_TEST_GLOBAL_VARIABLE`: bench, automation and diagnostic switches; folded to a constant in Shipping, declared only in dev-only places (the macro's `static_assert` checks `__FILE__`).
  - `SW_EXTERN_GLOBAL_VARIABLE( type, name )` (`SW_EXTERN_TEST_GLOBAL_VARIABLE`): the `extern`.

#### Pointers and containers

| Kind | Prefix | Example |
| --- | --- | --- |
| Raw pointer | `p` | `pObject`, `_pObject` |
| Double pointer | `pp` | `ppMember` |
| Triple pointer or more | forbidden | |
| Associative container | `map` | `_mapIDToName` |
| Fixed array | `arr` | `_arrBuffer` |
| Variable array / list | `list` (never a `List` suffix) | `_listItem` |
| Set | `unique` | `_uniqueIds` |
| Byte vector named `byte` / `bytes` | no prefix | `_bytes`, `outBytes` |

All container and parameter names are **singular** (`_listActors` is forbidden); the only plurals are the `unique` prefix and byte buffers.

#### Parameters

- Outputs: `out` + PascalCase (`outValue`); a container follows `out` in singular (`outListItem`). Raw pointer outputs: `pOutBuffer`, `ppOutObject`. In/out: `inout` / `pInOut`. A bare `out` is blocked in header declarations.

#### GPU resource verbs (closed vocabulary)

A class that owns RHI resources derives from `RHIRenderResource` and uses only these (no `upload`, `applyToGPU`, `isReady`, `releaseGPU`):

| Verb | Meaning |
| --- | --- |
| `initRHI( pDevice )` | Create GPU resources; idempotent |
| `updateRHI( pDevice )` | Push changed CPU data to existing resources |
| `releaseRHI( pDevice )` | Device **still alive**: hand resources back, clear handles |
| `clearRHI( pDevice )` | Device **already gone**: clear handles only |
| `isRHIValid()` | Are the GPU resources live now? |

Never loop `releaseRHI` / `clearRHI` / `initRHI` from outside: `IRHIDevice` broadcasts them (`releaseAllForDevice` …).

### Class suffixes

| Suffix | Meaning |
| --- | --- |
| `…Manager` | Owns the lifetime of a set of runtime objects and the create / destroy / load policy |
| `…Registry` | Lookup table filled by registration; no lifetime policy |
| `…System` | Runs every frame over many objects with the same logic |
| `…Service` | Replaceable function window reached through service binding or a remote endpoint |
| `…Backend` | Platform or library implementation of an interface |
| `…API` | A C function table |

- Spell `Anim` out as `Animation`, except outside terms (`AnimInstance`).
- A boolean variable or member keeps the `b` prefix (`_bOpen`); a function returning a boolean starts with `is` / `has` / `can` / `should`.
- Name a graphics type as a game programmer would in Unreal or Unity. `Slot` is only a shader binding slot or a position in a ring; a type owning a buffer is named after the buffer. No bookkeeping metaphors (`Ledger`): `…Tracker`, `…Registry`, `…Allocator`.
- `Scene` owns one scene's data; `GameObjectManager` manages its objects' lifetime; `GameObjectStore` is the storage.

### Function names

Use a banned verb only when it is the domain's own term (`UUID::generate`, `SurfaceBVH::build`).

#### One verb per concept

| Concept | Verb | Never |
| --- | --- | --- |
| Bring to life / take down | `initialize` / `shutdown` | `setup`, `startup`, `cleanup`, `teardown` |
| Hand out / take back memory or a slot | `allocate` / `free` | `alloc`, `dealloc`, `dispose` |
| Build and return a new value | `create` (owning) · `make` (plain value) | `build`, `construct`, `generate` |
| Refill existing state | `rebuild` · `populate` | `build` |
| Look up | `get` (always there) · `find` (may miss) · `tryGet` (`bool` + out, only when "none" cannot be returned) | `fetch`, `retrieve`, `lookup`, `obtain` |
| Get, loading on a miss, shared | `acquire( path )` | `getOrLoad` |
| Get, creating on a miss | `getOrCreate` | `findOrAdd`, `ensure` that returns something |
| Make sure it exists, return nothing | `ensure*` | |
| Work out a value | `compute` | `calculate`, `calc` |
| Advance a frame / fixed step / gather input | `update` · `step` · `poll` | `tick` (outside the tick system) |
| Move a clock by an amount | `advance( seconds )` | |
| Put into / take out of an owned container | `add` / `remove` | `push` outside a stack or queue |
| Hand over something called later, ended by the pair | `register` / `unregister` | `add*Handler`, `add*Observer`, `add*Listener` |
| Join to the end of an ordered sequence | `append` | `add` for text |

- `tick` names only the tick system and behavior-tree node ticks.
- A count variable is `xCount`; `countX()` counts by scanning.

#### Acronyms

- Capitals everywhere: `UISystem`, `GPUScene`, `updateUI`, `entityID`; all lower case only as the first word (`uiSystem`, `_gpuScene`). A shortening (`Nav`, `Anim`, `Info`) is not an acronym; `Ok` is a word (`bOk`, `isOk`).
- Two capital acronyms never touch (`RHIUI…`).
- Lower-case extensions and paths, `gv_` prefixes and third-party names keep their spelling.
- A new acronym goes to `_acronyms` and is rewritten everywhere in one commit; third-party spellings and serialized keys go to `_acronymKeep` with a reason. Lint `naming-words` enforces.

#### Type suffixes

| Suffix | What the data is for |
| --- | --- |
| `Def` | Authored data definition (from a file, shared, immutable); a catalog sub-struct is a `Def` |
| `Desc` | Creation description handed to `create*` |
| `Spec` | A granted runtime instance (GAS `AbilitySpec`) |
| `Config` | Preset or deployment configuration; a service's `initialize` configuration |
| `Settings` | User or game settings changed at run time; project-wide engine tables (`PhysicsSettings`) |
| `Params` | Call arguments bundled into one value |
| `Context` | The environment handed to a callee for one call, pass or frame. A per-thing record the owner keeps is a `Record` |
| `Unit` | A game-play unit, an animated skeletal mesh in `AnimationSystem`, or a measurement unit |

`RT` means render thread. A render target is spelled `RenderTarget`.

#### Names and strings

- **A name is a `hashed_string`.** Take `const hashed_string&`; literals convert implicitly, `string_view` / `string` stay explicit (`hashed_string( text )`).
- Never declare a `string_view` overload beside the `hashed_string` one. A lookup that must not intern gets its own name (`findStringByText`).
- `hashed_string` follows `FName`: `==` ignores case; no `operator<` — sort with `HashedStringLexicalLess` (output) or `HashedStringFastLess` (lookup).
- An empty name gives empty text through `c_str()` / `view()`; display text comes from `toString()` (`"None"`).
- Enumerators: `EnumInfo::getNameByValue` returns the name (`hashed_string`), `toString` returns display text. Never take text out of a name with `c_str()`.
- String type by content: a per-frame name is `hashed_string` or a `string_view` into existing storage; `fixed_string<N>` only for code-bounded text where the heap is off limits (crash, log, signal paths) or a fixed buffer is required (ImGui); up to 15 characters `string` is heap-free (SSO).

#### Predicates, callbacks, results

- A predicate reads as a question: `is` / `has` / `was` / `can` / `should` or a third-person verb (`supportsX`, `usesX`). `check*` is not a predicate (`bool` → `is*`, asserting `void` → `assert*`). A getter paired with `setX()` is `getX()` / `isX()`.
- `on*` is a notification handler, never the registering call.
- Spell words out unless a repo type abbreviates it (`XMLNode::attribute()`, not `attr()`).
- A `bool` from a fallible verb (`load` · `save` · `read` · `write` · `parse` · `apply` · `remove` · `spawn` · `attach` …) is `[[nodiscard]]`; dropping it is `(void)call(); // why`.

### 용어 — 같은 단어 한 뜻

새 이름이 같은 낱말을 다른 뜻으로 쓰면 이름을 바꾼다. 새 낱말은 `Config/Rules/Lint/NamingWords.json` `_words`(문맥마다 갈리면 `_scopedWords`)에 먼저 올리고, 옛 이름은 `_bannedNames` 에 둔다(린트 `naming-words`).

이름 · 경로는 도구로 바꾼다(`py -3 -m Scripts rename-symbol` · `move-source`, 여러 쌍은 `rename-symbol --pairs <표>`) — 코드 · 문서 · 데이터의 직렬화 이름을 함께 고친다. 옛 이름을 일부러 적는 줄에는 `keep-old-name` 표지(Markdown 은 `<!-- keep-old-name -->`).

| 규칙 | 근거 |
|---|---|
| Asset = 콘텐츠(`Engine/Asset`, `Content/`), Resource = GPU · RHI 자원만 | 언리얼 Content · FRenderResource |
| 업계 공용어는 그대로(Cook · Pawn · Possess). 한 엔진 · 게임 고유 이름 · 브랜드는 내용으로(`BugReport`) | 이름은 내용으로 |
| Pack(`.pack`) · Watch(자동화 감시 값 — Probe 는 라이트 · 리플렉션 프로브, 카메라 충돌 프로브, 운영 상태 프로브만. 확인용 컴포넌트는 `…TestComponent`, 자체 테스트 상태는 `…TestState`) · Scene(씬 컨테이너 — World 는 월드 공간 뜻만) | 같은 단어 한 뜻 |
| Module 은 동적으로 올리는 코드 단위만(모듈 문맥의 Image → Library). 렌더러 확장점은 `RenderExtension` | 언리얼 SceneViewExtension |
| Host = 엔진을 돌리는 프로세스와 그 쪽(`HostService`, 짝 `EngineServices`). 네트워크 끝점 `NetEndpoint*`, 에디터 패널 진입점 `IEditorPanelService`, 온라인 키트 `IOnlineServerService` · `OnlineClientService` | 같은 단어 한 뜻 |
| Runtime = 모든 호스트에서 도는 것(키트 `Runtime/`). 모듈 C-ABI 계약은 `Source/ModuleAPI` | 언리얼 모듈 형식 |
| Automation = 시나리오 자동 테스트(`Engine/Automation`, 실행기 `DevTools/Client/Automation`). "UI Automation" 은 Windows UIA 만 | 언리얼 Automation System |
| 대상을 인자로 받는 상태 없는 정적 함수 묶음은 도메인 이름 그대로 | 언리얼 FPaths |
| 낮은 층 · 높은 층이 서로 알아야 하면 위가 아래에 건다 | 언리얼 FPlatformFileManager |
| 키트 하나의 에디터 확장은 그 키트의 `Editor/`. `Plugin/Editor` 는 GameFramework 자체와 여러 키트를 잇는 확장만 | 언리얼 플러그인 <X>Editor |
| 폴더 · 파일 이름은 내용으로([FolderLayout](FolderLayout.md)). 에셋 파일 형식은 `*Asset`, XML 정의는 `*Def` | |
| 콘텐츠가 쓰는 형식의 모듈은 그 도메인이 가진다(`game/<팩>` · `common/` · `editor/` · `engine/`) — configure 가 멈춘다(`Scripts/generate/content_modules.py`) | 언리얼 .uproject Plugins |
| 한 판의 규칙 · 상태 컴포넌트는 `GameMode`. `Director` 는 페이싱 · 카메라 연출만. 차지할 슬롯은 `SmartObject`, 장비는 `Equipment` | 언리얼 AGameModeBase, Lyra |
| Def 여러 개를 담은 파일은 `<X>Catalog`. `Table` 은 가중 추첨 · 구간 표와 StringTable 만, `Set` 은 GAS 이름과 이름 → 항목 묶음만, 코드 등록 표는 `Registry` | 한 이름 |
| 막고 끝내기 `flush`, 큐를 꺼내 처리 `poll`, 꺼내 넘기기 `take`. `drain` 은 서버 드레인 · 게임 수치 소모만 | 언리얼 FlushRenderingCommands |
| 기록 `record`, 지우기 `remove` · `clear` · `untrack`, 결과 채택 `apply` · `attach`, 허가 `allow` · `accept` | 낱말 한 벌 |
| 형식 끝말: 리플렉션으로 읽는 칸 묶음 `Params`(`Fields` 는 칸 자체를 다루는 도구만), 비동기 일 하나 `Job`, 결과 `Result`, 테스트 조립 `Fixture`(`Rig` 은 애니메이션 리그만) | 낱말 한 벌 |
| `Reply` 는 키트 응답 본문, `OnlineResponse` 는 전송 봉투. `Since` 는 시간을 세는 칸만 | 뜻이 다르다 |
| 저작 정의는 `Def` 하나. 값의 갈래를 고르는 열거형은 `…Kind` — `…Type` 은 C++ · 리플렉션 형식과 외부 API 용어만 | 저장소 다수 |
| 읽기 · 쓰기 동사는 원본 종류를 넣고 짝을 맞춘다: `loadFromAsset` · `loadFromFile`/`saveToFile` · `loadFromXMLText`/`saveToXMLText`. `read*`/`write*` 는 바이트 스트림 · Archive, `parse*` 는 글 → 스칼라만. `createGameObject` ↔ `destroyGameObject` | 언리얼 LoadFromFile |
| 에디터 창 하나의 상태 · 명령은 `<창>ViewModel`(폴더 `Model/`), 문서 사본과 편집 연산은 `<문서>EditModel`([EditorUI](EditorUI.md)). `Document` 는 메모리에 든 파일 내용만 | MVVM |
| 알림: 버스의 "바뀌었다" 는 `<X>Changed` · `<X>Revoked`, 플레이어 알림 `<X>Notification`, 운영 공지 `Announcement`. `Notice` 는 영어 관용구만 | Apple · Google 서버 알림 |
| 은유 대신 내용: 셈 `Stats`, 기록 `History`, `InstancedStruct`. 매크로는 낱말마다 `_`(`SW_THEME_COLOR`) | 언리얼 FCookStats |
| 둔 것: `Editor*` · `Automation*` 접두어, App 폴더, `*Info` 정적 사실 표, 폴더 이름과 같은 접두어 묶음, `IWindowTitleTarget` | |
