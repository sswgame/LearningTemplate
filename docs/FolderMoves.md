# FolderMoves

> 질문: 지금 잘못 놓인 파일 · 폴더를 어디로 옮기고 무엇을 합치는가?
> 독자: 구조 정리 묶음을 하는 에이전트(통합 담당).
> 결론: 판정 기준은 [FolderLayout](agents/FolderLayout.md). 이 문서는 남은 이동 목록이다 — 끝난 줄은 지우고, 다 끝나면 문서를 지운다(인계 = 남은 일만).

## 0. 요약

| 항목 | 값 |
| --- | --- |
| 옮길 파일(헤더 기준, .cpp · Tests 짝은 따라감) | **약 80 개** (Core 25 · Engine 24 · GameFramework 13 · Editor 3 · ModuleManager 8 · App · Server 7 — 3 · 5 · 10.1 절) |
| 새 폴더 | `Core/Algorithm` · `Core/Encoding` · `Core/Name` · `Core/Registry` · `Engine/Visual`(Rendering + 2D 개명) · `GameFramework/Runtime/Gameplay/Condition` · `Engine/UI/Menu` · `Engine/UI/Service` · `ModuleManager/Dev/LiveReload`(옮김) |
| 지울 빈 폴더 | `Core/Console`, `Core/Diagnostics/Dev` (git 에 파일 없음) |
| 영향 include 줄 합계(경로 이동) | **약 3,500** (Core 만 약 3,000 — `StringUtil` 658 · `hashed_string` 620 · `VectorUtil` 522 · `StringBuilder` 270 · `FourCcUtil` 127 · `fixed_string` 115 · `formatString` 91 …) |
| 합칠 파일 묶음 | 14 묶음(파일 33 → 14), 아래 9 절 |
| 약어 표기 어긋남 | 파일 · 폴더 이름 31 개, 우리 코드 식별자 약 200 개(아래 11 절) |

**우선순위**(include 가 많은 Core 부터, 한 묶음 = 한 커밋 · `Scripts/dev/MoveSource.py` 로 경로 일괄 치환):

1. Core/String 재편 — `Core/String`(티어 4, 로그 없는 글자 도구)·`Core/Name`(티어 6, intern 이름)·`Core/Registry`(티어 6)·`Core/Encoding`(티어 4). StringUtil · UTF8Util · formatString 이 Container 에서 들어온다. include 약 1,950.
2. `Core/Algorithm`(티어 2) — VectorUtil · CompareUtil · UnionFindUtil. include 약 540.
3. 스레드 간 큐(ConcurrentQueue · WorkStealingDeque · SpscQueue · LockedSwapQueue · SharedVectorPool)를 `Core/Concurrency/Queue/`(Container 위 티어)로, VarInt · LittleEndian 을 Serialization 으로. include 약 50.
4. Engine 자리 틀림(Localization 의 창구 · 서브시스템, Gameplay/InstancedStruct, Animation · Character 갈라짐, Profiling 개발 도구, BugReport, Rendering → Visual 개명).
5. GameFramework `Runtime/Foundation/Utility` 해체, `World/Query` 의 조건 어휘 분리.
6. Dev 기능 정리(ModuleManager LiveReload, AppDevTools · RHIBackendSwitcher 가르기, Engine 의 `Dev/` 셋 → DevTools).
7. 합칠 파일, 약어 표기(새 약어 린트 포함).

---

## 2. Core — 옮길 파일

| 현재 경로 | 새 경로 | 까닭 | include 수 |
| --- | --- | --- | --- |
| Core/String/StringUtil.h | Core/String/StringUtil.h | UTF 변환 · 분할 · 트림 — 문자열 도구. String 을 티어 4 로 내리면 pchLow · Log(5) · Subsystem(4) 이 그대로 쓴다 | 658 |
| Core/String/UTF8Util.h | Core/String/UTF8Util.h | UTF-8 글자 경계 판정 | 7 |
| Core/String/formatString.h | Core/String/formatString.h | printf 포맷 | 91 |
| Core/String/StringBuilder.h · string_splitter.h · JSONStringUtil.h · MarkupTagScanner.h · EnumNameTable.h | (그대로, 폴더 티어만 6 → 4) | 로그를 안 쓴다(.cpp 도 SW_LOG 0) | 270 · 17 · 5 · 6 · 1 (경로 불변) |
| Core/String/fixed_string.h | Core/String/fixed_string.h (그대로) — 단 SW_LOG 14 곳을 `SW_LOW_LEVEL_*` 로 | 고정 용량 문자열은 String 의 일. 로그 매크로가 티어 4 를 막는다. **로그를 못 바꾸면** Core/Name 으로 | 115(경로 불변) |
| Core/Name/hashed_string.h | Core/Name/hashed_string.h | intern 이름(FName) — 이름 풀 · 로그를 쓴다 | 620 |
| Core/Name/TagID.h | Core/Name/TagID.h | intern 태그 식별자 | 36 |
| Core/Registry/RegistrationList.h | Core/Registry/RegistrationList.h | **사용자 지적 예.** 레지스트리 공통 구조(중복 거절 · 정렬 · 모듈 범위 · NameRegistry) — 문자열 일이 아니다. Delegate · hashed_string · 로그를 써서 티어 6 | 50 |
| Core/Encoding/Base64Util.h | Core/Encoding/Base64Util.h | 바이트 ↔ 글자 인코딩 | 8 |
| Core/Encoding/HexUtil.h | Core/Encoding/HexUtil.h | 〃 | 19 |
| Core/Encoding/FourCCUtil.h | Core/Encoding/**FourCCUtil.h** | 네 글자 표식 → uint32. 약어는 모두 대문자(NamingCpp Acronyms) | 127 |
| Core/Serialization/VarIntUtil.h | Core/Serialization/VarIntUtil.h | LEB128 · ZigZag — 쓰는 곳이 ByteStream · BitStream · Network | 6 |
| Core/Serialization/LittleEndianUtil.h | Core/Serialization/LittleEndianUtil.h | 파일 · 네트워크 바이트 순서 | 30 |
| Core/Algorithm/VectorUtil.h | Core/Algorithm/VectorUtil.h | 연속 배열에 쓰는 static 템플릿 알고리즘 — Foundation 만 include. Delegate(4) · Subsystem(4) · Container .cpp 가 쓰니 티어 2 | 522 |
| Core/Algorithm/CompareUtil.h | Core/Algorithm/CompareUtil.h | 정렬 · 이진 탐색 비교자 | 18 |
| Core/Algorithm/UnionFindUtil.h | Core/Algorithm/UnionFindUtil.h | 유니온 파인드 | 2 |
| Core/Concurrency/Queue/SpscQueue.h | Core/Concurrency/Queue/SpscQueue.h | 스레드 간 자료 구조는 Concurrency 아래 한 폴더(사용자 결정 — 하는 일로 묶는다). vector 를 쓰는 형제가 있어 Container 위 티어 | 1 |
| Core/Concurrency/Queue/ConcurrentQueue.h · WorkStealingDeque.h | Core/Concurrency/Queue/ | 〃 | 6 · 2 |
| Core/Concurrency/Queue/SharedVectorPool.h | Core/Concurrency/Queue/SharedVectorPool.h | 〃(갈래 C 가 Concurrency 바로 아래로 옮겨 티어 역류) | 2 |
| Core/Concurrency/Queue/LockedSwapQueue.h | Core/Concurrency/Queue/LockedSwapQueue.h | 〃 — 지금 Concurrency(1) 가 Container/vector.h(3) 를 include 하는 티어 역류가 이 이동으로 풀린다 | — |
| Core/Process/ThreadCrashStack.h | (그대로) | Diagnostics(10) 가 더 맞지만 File(8) · Task(7) · Log/Sink 가 include — 옮기면 티어 역류 | 16 |
| Core/Math/SecureRandom.h | (그대로) | OS 암호 난수 — Math 의 난수 묶음. Hash 는 해시만 | 9 |
| Core/Container/DependencyGraph.h | (그대로) | 노드 · 간선 자료 구조 + 위상 정렬 — 컨테이너. Algorithm(2) 은 vector 를 못 쓴다 | 11 |
| Core/Container/string.h | (그대로) | std::basic_string 래퍼 — 다른 컨테이너 래퍼와 한 틀(UE FString 도 Containers) | 1175 |

- 층 표 고침: `CoreLayers.toml` 에 `Algorithm = 2`, `String = 4`, `Encoding = 4`, `Name = 6`, `Registry = 6`; `Container` 주석에서 "문자열 타입과 StringUtil · formatString" 을 지운다. Engine 의 GF `RayMath` · `OrientationUtil` · `ValueNoise` 가 Core/Math 로 들어온다(5 절).
- **Core/Hash/HashUtil.h 는 그대로**(해시는 인코딩이 아니다 — UE Core/Hash, Godot core/crypto 의 hashing).
- 빈 폴더 `Core/Console`, `Core/Diagnostics/Dev` 를 지운다.

### 2.1 "여러 유틸을 한 폴더로" 판단 (사용자 질문)

| 파일 | 자리 | 언리얼 | Godot | 판단 |
| --- | --- | --- | --- | --- |
| Base64Util · HexUtil · FourCCUtil | **Core/Encoding** | Misc/Base64.h, String/BytesToHex.h | core/crypto/crypto_core(b64), core/string(hex) | 바이트를 글자로 적는 일 — 한 폴더. 이름은 하는 일(`Encoding`), `Utility` 같은 잡동사니 이름은 쓰지 않는다 |
| VarIntUtil · LittleEndianUtil | **Core/Serialization** | Serialization/(FArchive 의 packed int) | core/io/marshalls.h(encode_uint32 · varint) | 정수를 바이트로 쓰는 일 — 쓰는 쪽(ByteStream · BitStream)과 같은 폴더. Encoding 으로 묶지 않는 까닭: 바이트 직렬화의 부품이라 Serialization 과 갈라 두면 같은 일이 두 폴더 |
| HashUtil | **Core/Hash** 유지 | Core/Hash | core/crypto(hashing) | 해시는 되돌릴 수 없는 요약 — 인코딩이 아니다 |
| JSONStringUtil · MarkupTagScanner | Core/String 유지 | | | 글자 안 이스케이프 · 스캔 — 문자열 일 |

---

## 3. Engine — 옮길 파일

| 현재 경로 | 새 경로 | 까닭 | include 수 |
| --- | --- | --- | --- |
| Engine/Reflection/InstancedStruct.h | Engine/Reflection/InstancedStruct.h | 형식을 런타임에 고르는 리플렉션 값(UE FInstancedStruct 는 CoreUObject/StructUtils). 같은 층 1 | 4 |
| Engine/Presentation/IPlayerNoticeSink.h | Engine/Presentation/IPlayerNoticeSink.h | UI 없이 게임이 거는 표현 창구 — 다른 I*Sink · I*Driver 와 같은 자리(층 2 → 2) | 4 |
| Engine/Presentation/ISubtitleSink.h | Engine/Presentation/ISubtitleSink.h | 〃 | 4 |
| Engine/Asset/EngineDefaultAssetsSubsystem.h | Engine/Asset/EngineDefaultAssetsSubsystem.h | 엔진 데이터(`EngineDefaultAssets`)를 읽어 소유하는 기동 서브시스템 — 짝인 `Asset/PackEngineSettingsSubsystem.h` 와 같은 자리. 문화권 표도 올리므로 층 2 는 유지 | 4 |
| Engine/Animation/Component/LocomotionWarpingComponent.h | Engine/Animation/Component/LocomotionWarpingComponent.h | 컴포넌트 — Animation/Component 가 있다 | 2 |
| Engine/Animation/Component/MotionWarpingComponent.h | Engine/Animation/Component/MotionWarpingComponent.h | 〃 | 4 |
| Core/Math/DualQuaternion.h | Core/Math/DualQuaternion.h | 순수 수학(Core/Math · Foundation 만 include) — Renderer(GPUSceneBuilder)도 쓴다. UE Core/Math/DualQuat.h | 3 |
| Engine/Animation/Notify/AnimationNotifyComponent.h · AnimationNotifyHandlers.h · AnimationNotifyTable.h | Engine/Animation/Notify/ | 애니메이션 알림이 Animation/Notify(FootContact · Listener) 와 Character/AnimationNotify 로 갈렸다. 둘 다 층 6 | 4 · 2 · 7 |
| Engine/Foundation/AnimationNotifyPhase.h | (그대로) | `Object/Component/Component.h`(층 5)가 include — Animation(6)으로 옮기면 층 역류. 층 0 에 둔 까닭이 맞다 | 2 |
| Engine/Animation/Component/PoseModifierComponent.h | Engine/Animation/Component/PoseModifierComponent.h | 리그 에셋(IK · 제약)을 돌리는 후처리 컴포넌트 — Animation/Rig 의 실행 쪽. 캐릭터 형상 다리(`CharacterPoseUtil`)만 Character 에 남는다 | 8 |
| Engine/Console/DebugOverlayState.h | Engine/Console/DebugOverlayState.h | 모듈 사이 디버그 오버레이 값(게임이 쓰고 에디터가 읽음) — 계측이 아니다. 디버그 HUD 레지스트리 옆. 게임이 쓰므로 DevTools(게임이 못 봄)는 안 된다 | 5 |
| Engine/Profiling/FrameProfileReport.h | DevTools/Profiling/FrameProfileReport.h | 프로파일 한 회분 보고(개발). **막힘**: `FrameProfiler.h` 가 include — 보고 형식 의존을 끊은 뒤 | 4 |
| DevTools/Profiling/ProfilerTraceExport.h | DevTools/Profiling/ProfilerTraceExport.h | Chrome Trace 내보내기(개발). **막힘**: Engine.cpp · Automation · Renderer/Capture/BugReport 가 부름 → EngineFrameHooks 로 거는 자리로 바꾼 뒤 | 9 |
| Engine/Profiling/SubsystemTimingReport.h | DevTools/Profiling/SubsystemTimingReport.h | 기동 시간 요약(개발 구성만). **막힘**: Engine.cpp 가 부름 — 위와 같다 | 4 |
| Engine/Profiling/Tracy/TracyProfilerBackend.h | DevTools/Profiling/Tracy/TracyProfilerBackend.h | 외부 프로파일러 연결(개발). ProfilerBackend 가 고르는 대신 DevTools 의 `ProfilerLaunchSwitches` 가 `IProfilerBackend` 를 등록하게 뒤집는다 | 2 |
| DevTools/Client/Capture/BugReport.h | DevTools/Client/Capture/BugReport.h | 쓰는 곳이 Editor 셋뿐 + 개발 명령 — "Engine 에 편집기만 부르는 함수 금지"(AGENTS.md) 위반. Editor 는 DevTools 를 본다 | 5 |
| Engine/UI/Screen/UIScreenSwitch.h | DevTools/Client/UI/UIScreenSwitch.h | 개발 스위치(gv)로 화면 열기 — 개발. **막힘**: UISystem.cpp · Games/Empty/Client/Dev 가 include(게임은 DevTools 를 못 봄) → 게임 쪽은 엔진 확장점(화면 등록)으로 | 5 |
| Engine/UI/Menu/KeyRebindScreen.h · OptionsMenuScreen.h · PauseMenuScreen.h · SettingsConfirmScreen.h | Engine/UI/Menu/ | 구체 엔진 메뉴 화면 — 화면 틀(UIScreen · UIScreenTypes)과 섞였다 | 2 · 3 · 2 · 2 |
| Engine/UI/Service/UINotificationService.h · UISubtitleService.h · UITooltipService.h | Engine/UI/Service/ | 화면 스택 밖 서비스(알림 · 자막 · 툴팁) — Input/UIClipboardService 와 같은 종류 | 4 · 4 · 2 |
| Engine/Config/NetEmulationPresetTable.h | (그대로) | Net(6) 이 더 맞지만 Automation · GF · DevTools · Editor 가 쓰고 Config(1) 이 읽는 표 — 층 1 이 필요 | 6 |
| Engine/Net/HTTPClient.h | Engine/Net/HTTPClient.h | 범용 HTTP 창구 — GF Online(BlockingHTTPClient)도 쓴다. 텔레메트리 전용이 아니다. 같은 층 6 | 4 |
| Core/Spatial/PolygonTriangulation.h | Core/Spatial/PolygonTriangulation.h | 구멍 있는 다각형 삼각 분할 — Core 만 쓰고 Editor MeshFracture 도 쓴다. Container 를 써서 Math(1) 은 안 되고 Spatial(4) | 4 |

---

## 4. 폴더 자체가 잘못 나뉜 곳

| 폴더 | 문제 | 고칠 모양 |
| --- | --- | --- |
| `Engine/Rendering` ↔ `Engine/Renderer` | 이름이 모호하다 — Rendering = 렌더용 **씬 컴포넌트**(Light · Mesh · Probe · MeshLODSystem · TextureStreamingSystem, 층 6), Renderer = **파이프라인**(층 7) | `Engine/Rendering` → **`Engine/Visual`**(컴포넌트는 `Visual/Component`, LOD · 스트리밍 선택은 `Visual/` 바로 아래). `Engine/2D`(스프라이트 · 2D 빛 · 패럴랙스 — 같은 종류)도 `Engine/Visual/2D` 로. Renderer 는 그대로. 대조: 언리얼 Engine/Components(UPrimitiveComponent) ↔ Renderer 모듈, Godot scene/3d(VisualInstance3D) ↔ servers/rendering. `Components/Rendering` 은 버린다 — 이 저장소는 "기능 폴더가 자기 컴포넌트를 든다"(Physics/Component) |
| `Engine/Graphics` ↔ `Engine/Renderer` | 경계는 맞다(Graphics = GPU 자원 · CPU 그래픽 데이터, 층 4). 다만 `Graphics/View/IEngineRenderDriver.h` 는 엔진 루프 창구라 `Presentation/IEngineUIDriver` · `IEngineAudioDriver` 와 갈렸다 | 창구 셋을 한 자리(`Presentation`)에 — `FrameViewports` 등 Graphics 형식을 쓰면 층 2 가 못 본다 → 그 형식만 앞으로 선언으로 바꾸고 옮긴다(include 6) |
| `Engine/Graphics/Canvas` ↔ `Engine/Renderer/Canvas` | 층으로 가른 것(그리기 목록 4 ↔ GPU 렌더 7) — **맞다** | 그대로 |
| `Navigation/Component` · `Audio/Component` · `Physics/Component` | Component 폴더에 씬 서비스 · 질의 · 검사가 섞였다: `SceneNavigation` · `NavMeshRuntime` · `NavPathSolver` · `NavTileRebaker` · `NavGeometryCollector` · `NavDebugView` · `NavAgentCrowdSync` · `NavAreaNameValidation` / `SceneAudio` · `Physics*AudioOcclusionQuery` / `ScenePhysics` · `SocketPhysicsBody` · `PhysicsNameValidation` | `Animation/Scene` 과 같은 틀로 `<기능>/Scene/` 을 만들어 옮긴다(Navigation 8 · Audio 3 · Physics 3 파일) |
| `Engine/Animation/Notify` ↔ `Engine/Character/AnimationNotify` | 같은 일이 둘로 갈림 | 3 절대로 Animation/Notify 로 합침 |
| 부력: `Physics/Component/BuoyancyVolumeComponent` ↔ `Environment/Water/BuoyancyComponent` | 물 부피와 부력 힘이 두 폴더 | 부피 컴포넌트를 `Environment/Water` 로(물 기능이 자기 컴포넌트를 든다). include 1 |
| `Engine/UI/Screen` | 틀 · 구체 메뉴 · 서비스가 한 폴더 | `UI/Menu`, `UI/Service` 로 가름(3 절) |
| `Engine/Profiling` ↔ `DevTools/Profiling` ↔ `Core/Diagnostics` | 런타임 계측과 개발 보고가 섞임 | 7 절대로 가름 |
| `Engine/Observability` ↔ `Engine/Telemetry` | Observability = 서버 운영 지표 · 디버그 채널(ServerAdminChannel 이 씀 — 제품), Telemetry = 클라이언트 사건 · 크래시 보고 | 경계는 맞다. 이름만 겹쳐 보임 — 그대로, 판정 기준표에 한 줄씩 |
| `Engine/Compression` ↔ `Core/Compression` | Core = 내장 코덱 · 레지스트리, Engine = 외부 라이브러리 코덱(lz4 · zstd · zlib — Core 는 ThirdParty 를 링크하지 않음) | 맞다(UE 도 Oodle 등은 플러그인). 그대로 |
| `Engine/Content` ↔ `Engine/Asset` ↔ `Asset/Cook` ↔ `Renderer/Cook` | 쿠킹이 층마다 갈림(읽기 기록 1 · 팩 쿠커 2 · 셰이더 쿠커 7) | 층 이유로 맞다. 그대로 |
| `Engine/Net` ↔ `Core/Network` ↔ `Object/GameObject/Service/{NetObjectTable,SceneNetLink}` | 전송 · 연결(Core) / 게임 세션(Engine 6) / 씬 번호표(층 5) | 층 이유로 맞다 |
| `Engine/Gameplay` ↔ `GameFramework/Runtime/World/Query`(GameConditions · GameFlags · ObjectActions) | 조건 · 동작 어휘가 "World/Query"(기하 질의 폴더)에 섞였다 | GF 쪽을 `Runtime/Gameplay/Condition` 으로(5 절) |
| `GameFramework/Runtime/Online/*` ↔ `Kits/Feature/Online/OnlineCore/Server/*` | 같은 하위 폴더 이름(HTTP · Directory · Identity · Ledger · Mail · Security · Store · Config · Service)이 두 곳 | Base(DLL) ↔ 키트(서버 전용) 경계라 합칠 수 없다. **이름은 그대로 두되** 판정 기준: "클라이언트 · 서버 공용 = Runtime/Online, 서버 전용 = OnlineCore/Server" |
| `Editor/Common/Config/EditorThemeConfig · EditorThemeTokens · EditorConfig` ↔ `Editor/Common/GUI/Theme` | 테마가 두 폴더 | Config(0) 는 ImGui 백엔드(0)가 읽는 값, GUI/Theme(4) 는 그리기 — 층 이유. 그대로. `EditorConfig.h` 의 이름이 "테마" 와 안 맞으니 `EditorAppearanceConfig` 로 |
| `Editor/Common/Workspace/HotReload` ↔ `DevTools/AssetReload` | 편집기 쪽 캐시 연쇄 vs 런타임 파일 감시 | 맞다(편집기는 Editor 에) |
| `ModuleManager/LiveReload` ↔ `ModuleManager/Dev` | 둘 다 개발 전용인데 LiveReload 만 CMake 줄(`sw_excludeUnbuiltSources(… "/Source/ModuleManager/LiveReload/" …)`)로 따로 뺀다 | `LiveReload/` 를 `Dev/LiveReload/` 로 — `Dev/` 패턴 하나가 받고 특례 줄이 사라진다(8 절) |

---

## 5. GameFramework · Editor · DevTools · ModuleManager · App · Server — 옮길 파일

| 현재 경로 | 새 경로 | 까닭 | include 수 |
| --- | --- | --- | --- |
| Core/Math/Ray.h | Core/Math/Ray.h 에 합침 | 광선 ↔ 기본 도형 판정 — Core/Math/Ray 의 일(Core 만 씀). `SW_GF_API` → `SW_API` | 11 |
| Core/Math/OrientationUtil.h | Core/Math/OrientationUtil.h (YawUtil 을 합침) | 앞 · 위 방향 → 오일러, 요 구하기 — 같은 일(9 절) | 15 |
| GameFramework/Runtime/Foundation/Random/ValueNoise.h | Core/Math/ValueNoise.h | 시드 고정 노이즈 — 범용 수학 | 3 |
| GameFramework/Runtime/Foundation/Random/GameRandom.h | GameFramework/Runtime/Foundation/Random/GameRandom.h | 게임 규칙 난수(GF 에 남는다). `Utility` 폴더 해체 | 54 |
| GameFramework/Runtime/Foundation/Grid/GridTopology.h · FacingDir.h · GridOpenEntry.h | GameFramework/Runtime/Foundation/Grid/ | 〃 | 21 · 3 · 3 |
| GameFramework/Runtime/World/Lifetime/LifeSpanUtil.h | GameFramework/Runtime/World/Lifetime/LifeSpanUtil.h | 수명이 다하면 지우는 컴포넌트 도우미 — FadeOutComponent · PrefabPool 옆 | 2 |
| GameFramework/Runtime/Foundation/GameData/DepletingValue.h | GameFramework/Runtime/Foundation/GameData/DepletingValue.h | 깎이는 수치(체력 · 내구도) — StatBlock 옆 | 21 |
| GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h | GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h | 규칙 설정 구조체의 범위 검사 — 데이터 읽기 | 30 |
| GameFramework/Runtime/Gameplay/Condition/GameConditions.h · GameFlags.h · ObjectActions.h | GameFramework/Runtime/Gameplay/Condition/ | 조건 · 동작 어휘 — WorldQuery(기하)와 다른 일, Engine/Gameplay 의 GF 쪽 | 16 · 32 · 1 |
| GameFramework/Runtime/Gameplay/Appearance/MaterialTintCache.h | GameFramework/Runtime/Gameplay/Appearance/MaterialTintCache.h | 색마다 머티리얼 인스턴스 캐시 — 표현 창구(I*Sink)가 아니다 | 13 |
| Editor/Common/Workspace/NetPlay/EditorStateHashCompare.h | Editor/Common/Workspace/NetPlay/EditorStateHashCompare.h | Multiplayer 창 Sync 탭 — 짝(EditorNetProfilerHistory · EditorRemoteInspector)이 NetPlay | 3 |
| Editor/Common/EditorUtil.h | 둘로: `makePythonScriptCommand` → Editor/Common/Task/EditorExternalToolJob.h, `appendCategoryBadge` → Editor/Common/Widgets/EditorWidgets.h | 잡동사니 Util — 하는 일이 둘 | 6 |
| ModuleManager/LiveReload/*(8 헤더) | ModuleManager/Dev/LiveReload/ | 개발 전용 — `Dev/` 패턴 하나로(8 절) | 각 1~5 |
| App/AppDevTools.h `bindRenderThreadHooks` | DevTools/Client/Render/(디바이스 제거 시험) | 개발 명령 `rhi.removeDevice` — 백엔드 교체기와 함께 10 절 | 3 |
| App/AppDevTools.h `pollShellActions` | ModuleManager/Host/Dev/HostDevTools | 리로드 단축키 → `IHostDevLayer` — App · Server 공용 개발 계층 자리(DevTools 는 ModuleManager 를 못 봄) | (위와 같음) |
| App/AppDevTools.h `getFixedFrameDelta` + App/Dev/AppDevFrameTime.cpp | Engine/Foundation/DeterministicRun | **진행 중(작업자 B)** — 결정적 실행 정책 | — |

판정해 그대로 두는 호스트 파일: `App/RuntimeUIHost` · `UserSettingsHost` · `AppAssertDialog` · `AppHeadlessTasks` · `EditorModuleManager`(App 만 에디터 모듈을 올림), `Server/ServerAdminChannel` · `ServerObservability` · `IServerDebugControl` + `Server/Dev`(운영 채널 · 서버 개발 조종 — 서버 실행 파일만), `Programs/*`(도구 실행 파일 — 맞다), `ModuleAPI/*`(C-ABI 계약 — 맞다).

---

## 6. 이름이 모호하거나 겹치는 폴더 쌍 (저장소 전체)

| 쌍 | 판정 |
| --- | --- |
| Engine/Rendering ↔ Engine/Renderer | **바꾼다** → Engine/Visual ↔ Engine/Renderer (4 절) |
| Engine/2D ↔ Engine/Graphics/2D | Graphics/2D = 2D 렌더 설정 · 스프라이트 메시 빌더(층 4). Engine/2D → Engine/Visual/2D 로 바꾸면 겹침이 풀린다 |
| Engine/Profiling ↔ DevTools/Profiling | **가른다**(7 절): 런타임 = 계측 · 포트, DevTools = 보고 · 내보내기 · 외부 연결 |
| Engine/Console ↔ DevTools/Console ↔ (빈) Core/Console | Engine/Console = 개발 명령 · HUD 등록 확장점, DevTools/Console = 콘솔 UI · 해석. 맞다. Core/Console 은 빈 폴더 — 지운다 |
| Engine/Foundation ↔ Core/Foundation | 층 0 끼리 — 맞다(타깃이 다름) |
| Engine/Serialization ↔ Core/Serialization | 리플렉션 직렬화 ↔ 바이트 스트림 — 맞다 |
| Engine/Text ↔ Core/String ↔ Engine/Localization | 글 배치 · 폰트 / 문자열 도구 / 번역 — 맞다 |
| Engine/Environment ↔ GF/Runtime/World/Environment | 엔진 하늘 · 안개 · 물 컴포넌트 ↔ 게임 날씨 · 시계 — 맞다 |
| Engine/Navigation ↔ GF/Runtime/Character/Navigation | 내비메시 ↔ 캐릭터 이동 명령 — 맞다 |
| Engine/Automation ↔ GF/Runtime/Gameplay/Automation | 실행기 ↔ 게임 시나리오 단계 — 맞다 |
| Engine/Presentation ↔ GF/Runtime/Foundation/Framework/Presentation | 엔진 창구 ↔ 게임 창구 — 맞다(MaterialTintCache 만 나감) |
| Engine/Gameplay ↔ GF/Runtime/Gameplay | 엔진의 조건 · 동작 어휘(층 1) ↔ 게임플레이 기능 — 이름이 겹친다. Engine 쪽을 **`Engine/Behavior`** 로 바꾸는 안(행동 트리 · 스케줄 · 대화가 쓰는 어휘)을 권한다 |
| Engine/Dialogue ↔ GF/Runtime/Gameplay/Dialogue | 그래프 · 커서 ↔ 러너 컴포넌트 — 맞다 |
| GF/Runtime/Online ↔ GF/Kits/Feature/Online/OnlineCore/Server | 4 절 — 경계 이유로 둔다 |
| Editor/Common/Workspace/Asset ↔ Editor/AssetActions ↔ Editor/Common/Asset | 레지스트리(2) / 구체 동작(6) / UI 없는 임포터(1) — 층 이유로 맞다 |
| Editor/Common/Input ↔ Editor/Common/Commands | 입력 어댑터 · 팝업(4) ↔ ImGui 없는 커맨드(3) — 맞다 |

---

## 7. Engine/Profiling 파일마다 (런타임 계측 vs 개발 도구)

언리얼 대조: 계측(SCOPE_CYCLE_COUNTER · TRACE_CPUPROFILER_EVENT_SCOPE · TraceLog)은 Core 에 있고 Shipping 에서 접힌다. 보고 · 분석(Insights · stat 명령 출력 · CSV 내보내기)은 개발 도구다.

| 파일 | 판정 | 자리 | 막는 것 |
| --- | --- | --- | --- |
| FrameProfiler.h (SW_PROFILE_SCOPE · 프레임 경계) | 런타임 계측 | Engine/Profiling 유지 | include 80+ |
| IProfilerBackend · IProfilerGPUTimeline · IProfilerMemoryOutput | 포트(RHI 가 GPU 구간을 냄) | Engine/Profiling 유지(IProfilerMemoryOutput 은 IProfilerBackend 에 합침 — 9 절) | |
| ProfilerBackend.h(+Subsystem) | 계측을 포트로 내보내는 갈래 — UE TraceLog 자리 | Engine/Profiling 유지 | Engine.cpp · Renderer |
| Tracy/TracyProfilerBackend | 외부 프로파일러 연결 — 개발 | DevTools/Profiling/Tracy | ProfilerBackend 가 Tracy 를 고르는 줄 → DevTools 가 등록 |
| FrameProfileReport | 보고 — 개발 | DevTools/Profiling | FrameProfiler.h 가 include |
| ProfilerTraceExport | 내보내기 — 개발 | DevTools/Profiling | Engine.cpp · Automation · BugReport |
| SubsystemTimingReport | 기동 시간 보고 — 개발 | DevTools/Profiling | Engine.cpp |
| MemoryBudgetMonitor | 예산 감시(데이터 예산 · 프레임 검사) — 배포에서도 경고를 낼 수 있음 | Engine/Profiling 유지. `memory.report` 명령 출력만 DevTools | Config · CoreServices 가 씀 |
| DebugOverlayState | 계측 아님 | Engine/Console | |

→ 막는 것 셋(Engine.cpp 직접 호출)은 같은 모양이다: **엔진 루프가 개발 보고를 이름으로 부른다**. `EngineFrameHooks` · 기동 단계 확장점으로 DevTools 가 붙게 바꾸면 셋이 함께 풀린다.

---

## 8. `Dev/` 폴더 전수 — DevTools 로 갈 수 있나

| 폴더 | 내용 | 판정 | 까닭 |
| --- | --- | --- | --- |
| ModuleManager/Dev | ModuleReloadDriver(핫 리로드 수명 · 요청) | **남는다** | `LiveReloadManager`(ModuleManager)를 쥔다 — DevTools 는 ModuleManager 를 못 본다(TargetLayers devtools gate). 대신 형제 `LiveReload/` 를 `Dev/LiveReload/` 로 넣어 특례 CMake 줄을 없앤다 |
| ModuleManager/Host/Dev | HostDevTools · HostDevLayer.cpp(DevToolsHost 를 호스트 범위에 붙임) | **남는다** | 호스트 쪽 "붙이는 줄" 자체 — 이것이 DevTools 를 부르는 자리 |
| App/Dev | AppDevFrameTime.cpp(gv_fixedFrameDelta) | **진행 중(작업자 B)** → Engine/Foundation/DeterministicRun | 결정적 실행 정책 |
| Server/Dev | ServerDebugControl.cpp | 남는다 | 서버 실행 파일의 개발 조종(IServerDebugControl 구현), DevTools/Console 을 이미 씀 |
| Engine/Audio/Dev | AudioDumpListener.cpp(73 줄) | **DevTools/Client/Audio 로** | 공개 헤더 + `AudioRenderListener` 등록만 씀. Audio 는 클라이언트 전용이라 DevTools/Client |
| Engine/Environment/Dev | EnvironmentFreezeSwitch.cpp(83 줄) | **DevTools/Diagnostics 로** | Foundation 서비스 · DeterministicRun 만 씀 |
| Engine/Animation/Scene/Dev | AnimationRewind(577 줄) | **DevTools/Client/Animation 으로** | `AnimationFrameObserver` 확장점 + 공개 헤더만 씀. 쓰는 곳이 Editor(DevTools 를 봄) |
| Engine/Scene/Dev | SceneWatches.cpp | 남는다 | 감시 값은 기능 폴더가 자기 자리에서 등록하는 틀(EngineLayers `Automation/Watch` 주석, Editor `*Probes`) |
| Engine/Config/Dev | GameAutoplay | 남는다 | 게임의 `Dev/*Autoplay.cpp` 10 개가 include — 게임은 DevTools 를 못 본다 |
| Engine/Graphics/Material/Dev | 머티리얼 그래프 컴파일러 · 쿠크 · 노드 등록 | 남는다 | 헤드리스 쿠킹(엔진)과 Editor 가 함께 씀. UE 도 머티리얼 컴파일러는 Engine(WITH_EDITOR) |
| GameFramework/Runtime/Foundation/GameData/Dev | GameSettingsLaunchOverride.cpp | 남는다 | GF 내부(GameSettingsOverride) — DevTools 는 GF 를 못 본다 |
| Games/*/Dev(11 게임) | 자동 플레이 · 스트레스 테스트 · 데모 | 남는다 | 게임 내부 |
| Core/Diagnostics/Dev | (빈 폴더) | **지운다** | |

---

## 9. 합칠 파일

기계 추출: 헤더 ≤ 60 줄이면서 자기 .cpp 밖 includer 가 같은 폴더 형제 1~2 개뿐인 것 167 개 → 내용을 읽고 골랐다. 대조: 언리얼은 기능 하나에 .h/.cpp 한 쌍, 수백~수천 줄이 흔하다(Containers/Array.h 3 천 줄+). Godot 도 core/templates 한 파일에 형식 하나. 20 줄짜리 선언 파일을 한 곳만 쓰는 것은 두 엔진 모두 드물다.

| 합칠 파일 | 합친 파일 | 까닭 | 줄 수 합(헤더) | include 수 |
| --- | --- | --- | --- | --- |
| Engine/Animation/Rig/RigBuiltinNodes.h · RigConstraintNodes.h · RigSecondaryNodes.h | Engine/Animation/Rig/RigBuiltinNodes.h | 각각 `registerNodes` 선언 하나(18 줄), RigAsset.cpp 만 씀. .cpp 셋(800 줄+)은 빌드 시간 때문에 둔다 | 54 | 2 · 2 · 2 |
| Core/Compression/BuiltinCompressionCodecs.h · RleCompressionCodec.h | Core/Compression/BuiltinCompressionCodecs.h | 내장 코덱 둘, 같은 두 .cpp 만 씀 | 78 | 4 · 4 |
| Engine/Compression/EngineCompressionCodecs.h · ZstdCompressionCodec.h · ZlibCompressionCodec.h | Engine/Compression/EngineCompressionCodecs.h (EngineCompressionCodecUtil.h 도 합침) | 등록 목록 하나만 씀. 헤더는 ThirdParty 를 안 넣으니 합쳐도 됨, .cpp 는 라이브러리별로 둔다 | 125 | 3 · 3 · 5 |
| Core/Foundation/EnumUtil.h | Core/Foundation/EnumUtil.h | 23 줄 기본 템플릿, EnumUtil.h 만 include | 23 | 1 |
| Engine/Localization/CultureInfo.h | Engine/Localization/CultureInfo.h | 구조체 하나(20 줄), CultureInfo 만 씀 | 20 | 2 |
| Engine/Profiling/IProfilerBackend.h | Engine/Profiling/IProfilerBackend.h | 인터페이스 23 줄, IProfilerBackend.h 만 include | 23 | 1 |
| Engine/Module/ModuleLoadStageSubsystem.h | Engine/Module/ModuleLoadStageSubsystem.h | 기동 · 종료 짝 — 쓰는 곳이 Load 쪽 하나 | 37 | 2 |
| ModuleManager/ModuleLoaderSubsystem.h | ModuleManager/ModuleLoaderSubsystem.h | 24 줄, ModuleLoaderSubsystem 만 씀 | 24 | 2 |
| Engine/Sequencer/SequenceAsset.h | Engine/Sequencer/SequenceAsset.h 의 선언 절 | extern 셋(17 줄), SequenceAsset.cpp 만 씀 | 17 | 2 |
| Engine/Serialization/Serializer/SerializeContext.h | Engine/Serialization/Serializer/SerializeContext.h | 함수 선언 하나(21 줄), SerializeContext.cpp 만 씀 | 21 | 2 |
| Engine/Object/GameObject/Serialization/ObjectStateSerializer.h | Engine/Object/GameObject/Serialization/ObjectStateSerializer.h | 22 줄, ObjectStateSerializer.cpp 만 씀 | 22 | 2 |
| Engine/Animation/Notify/AnimationNotifyTable.h | (Animation/Notify 로 옮긴 뒤) AnimationNotifyTable.h | 31 줄, AnimationNotifyTable.cpp 만 씀 | 31 | 2 |
| Editor/Common/Commands/Scene/EditorBakeSession.h · EditorBakeRunState.h | Editor/Common/Commands/Scene/EditorBakeSession.h | 라이트 · 반사 프로브 굽기 세션 둘이 같이 쓰는 경로 · 진행 상태 | 69 | 3 · 3 |
| Core/Math/OrientationUtil.h + GF Utility/Math/OrientationUtil.h | Core/Math/OrientationUtil.h | 둘 다 회전 ↔ 방향 변환(32 + 26 줄) — 같은 일이 두 타깃에 | 58 | 5 · 15 |

**합치지 않는 것(까닭):**

| 후보 | 까닭 |
| --- | --- |
| Online 키트 `*KitConfig.h` 9 개(23~40 줄, 각 키트 .cpp 하나만 씀) | `REFLECT()` 설정 형식 — 9 키트가 같은 틀(설정 = 파일 하나). 하나만 합치면 틀이 깨진다 |
| `IPhysics{Character,Joint,SoftBody,Vehicle}Scene.h`, `INavMesh{Debug,Dynamics,Query}.h` | 면(facet) 인터페이스 — Body · Query 면은 따로 include 하는 쪽이 있어 틀을 유지 |
| `RHI/GL/Platform/{EGL,GLX,WGL}PlatformContext.h`, `Backend/Platform/*FileDialog.h` | 플랫폼별 파일 |
| `Editor/Panels/Settings/{Modules,Preferences,Shortcuts}Panel.h` | 패널은 정적 등록 단위 — 패널 하나 = 파일 하나(편집기 층 규칙) |
| `Editor/SelfTest/EditorSettleJudge.h`, `Viewport/EditorOrbitMath.h` | ImGui 없이 테스트하는 순수 판정 · 편집기 층 prelude 잎 헤더 — 일부러 갈랐다 |
| `*Fwd.h`(IPhysicsSceneFwd · PackedTypesFwd) | 컴파일 시간용 앞 선언 |
| `GimmickEvents.h` · `ControlEvents.h`(25 줄) | 이벤트 형식 — 발행 · 구독 양쪽이 include 하는 계약. 지금은 한 곳만 쓰지만 이벤트는 받는 쪽이 늘어날 자리 |
| Client/Server · Dev/ · Tests/ 로 가른 짝 | 빌드 구성 경계 |

---

## 10. 실행 중 바꾸기 · 핫스왑 · 다시 읽기 — 제품 / 개발

| 기능 | 지금 자리 | 판정 | 제품 자리 | 개발 자리 |
| --- | --- | --- | --- | --- |
| RHI 백엔드: 기동 때 고르기, "다음 실행에 쓸 백엔드" 기록(`recordStartBackend` · `findCommittedBackend`) | App/RHIBackendSwitcher | **제품** | Engine/Renderer/RHISubsystem(+ Engine/Config/RHIBackendType · 사용자 설정). 바꾼 설정은 재시작에 적용 — UE 도 RHI 변경은 재시작 | |
| RHI 백엔드: 실행 중 교체(`requestSwap` · `applyIfPending` · gv_rhiBackend 훅 · 시나리오 `SwapBackend` · 감시 값 `RHI.*`) | App/RHIBackendSwitcher | **개발**(백엔드 비교 · 시험) | | DevTools/Client/Render/RHIBackendSwap — **막힘**: 교체 때 에디터 모듈을 다시 만든다(`EditorModuleManager`, App 전용). 호스트가 "모듈 재생성" 창구를 `IHostDevLayer` 로 내주면 풀린다 |
| 디바이스 제거 시험(`removeDeviceForTest` · `rhi.removeDevice`) | App/RHIBackendSwitcher + AppDevTools | 개발 | | DevTools/Client/Render |
| 모듈 핫 리로드 | ModuleManager/LiveReload + Dev | 개발 | | ModuleManager/Dev/LiveReload(8 절) |
| 셰이더 라이브 재컴파일 | DevTools/Client/Shader | 개발 — 맞다 | | |
| 에셋 · 설정 다시 읽기(파일 감시) | DevTools/AssetReload · FileWatch | 개발 — 맞다 | 확장점(Engine/Content/ContentFolderWatch · Asset/Cache/AssetReloadWatch · Serialization/ReloadResult)은 Engine 에 남는다 — 포트는 아래, 구현은 위 | |
| 편집기 에셋 핫 리로드 · 도구 기본값 다시 읽기 | Editor/Common/Workspace/HotReload · Config/EditorToolDefaultsReload | 편집기 — 맞다 | | |
| 화면 해상도 · 창 방식 · 언어 · UI 테마 · 키 리매핑 실행 중 적용 | Engine/UserSettings · Localization · UI/Style · Input/Map | **제품**(옵션 메뉴 즉시 적용은 배포 게임의 기능) | 그대로 | |
| 개발 스위치로 화면 열기 | Engine/UI/Screen/UIScreenSwitch | 개발 | | DevTools/Client/UI(3 절, 막힘 있음) |

### 10.1 호스트(App · Server) 파일마다 — 개발 → DevTools, 호스트 본체 → 그대로/합침, 엔진 일 → Engine

기준: 호스트에는 "그 실행 파일만 아는 것"(창 · OS 서비스 · 에디터 모듈 호스트 · 프로세스 수명)과 붙이는 줄만 남긴다. 엔진 헤더만 쓰는 일은 엔진 기능이고, 개발 구성에만 뜻이 있는 일은 DevTools 다.

| 파일(줄 h/cpp) | 하는 일 | 판정 | 새 자리 | 까닭 · 막는 것 |
| --- | --- | --- | --- | --- |
| App/AppAssertDialog (22/92) | 사람이 보는 에디터 실행(Windows Debug)에서 단언을 대화상자로 묻기, 감시 값 `App.AssertDialogInstalled` | **개발** | DevTools/Client/Diagnostics/AssertDialog | Core · Engine 헤더만 씀. `setLowLevelReporter` 같은 단언 보고 확장점에 DevTools 기동 때 붙는다. 호스트에 파일로 둘 까닭이 없다. **확인할 것:** App.cpp:436 이 DevTools 기동보다 앞에서 거는 이유(기동 중 단언)가 있으면 DevTools 의 가장 이른 단계(`DevToolsHost` 연결)로 옮긴다 |
| App/AppDevTools (36/57) + App/Dev/AppDevFrameTime.cpp | 디바이스 제거 시험 훅 · 리로드 단축키 · 고정 프레임 시간 | **개발 · 셋으로 가름** | `bindRenderThreadHooks` → DevTools/Client/Render, `pollShellActions` → ModuleManager/Host/Dev/HostDevTools, `getFixedFrameDelta` → **진행 중(작업자 B)** Engine/Foundation/DeterministicRun | 다 옮기면 파일이 사라진다 |
| App/RHIBackendSwitcher (111/359) | 백엔드 기동 선택 기록 + 실행 중 교체 + 디바이스 제거 시험 | **제품 / 개발로 가름** | 10 절 표 | 실행 중 교체는 EditorModuleManager 를 다시 만들어 막힘 — `IHostDevLayer` 창구 |
| App/RuntimeUIHost (46/62) | 런타임 UI 에 OS 클립보드 · 스크린 리더 · "끝내기" 잇기 | **엔진 일** | Engine/UI/UIPlatformBinding(가칭) | Engine 헤더만 씀(UISystem · UIClipboardService · PlatformClipboard · IWindow). 호스트만 아는 것은 "에디터 안인가" 깃발 하나 — 인자로 받는다. UE GameViewportClient 도 Engine |
| App/UserSettingsHost (42/74) | 확인 카운트다운 · 화면 요청(창 방식 · 해상도 · VSync)을 렌더 스레드와 적용 | **엔진 일(제품)** | Engine/UserSettings/DisplaySettingsApplier(가칭) | Engine 헤더만 씀(UserSettingsManager · RenderThread · IRHIDevice · IWindow). UE GameUserSettings::ApplySettings 도 Engine. 주 창은 이미 `Window/MainWindowSubsystem` |
| App/AppHeadlessTasks (63/197) | 엔진 Headless 단계가 못 하는 헤드리스 작업 표(에디터 모듈 · 초상화 일괄 · 크래시 업로드) | 호스트 본체 | 그대로 | EditorModuleManager(App 전용)를 쓴다. 작업 표 하나라는 제 일이 있어 App.cpp(701)에 합치지 않는다 |
| App/EditorModuleManager (191/527) | App 전용 모듈 호스트(에디터 인스턴스) | 호스트 본체 | 그대로 | App 만 에디터를 올린다 |
| App/AppEntry · main.cpp · Android/AndroidMain.cpp · App.h/.cpp | 진입점 · 앱 본체 | 호스트 본체 | 그대로 | |
| Server/ServerObservability (94/237) | 운영 지표 · 상태 · 운영 HTTP · 틱 지표 · 느린 틱 덤프 | **엔진 일(제품, 서버)** | Engine/DedicatedServer/ServerObservability | Engine/Observability · DedicatedServer 를 쓰고 서버 루프와는 델리게이트로만 잇는다. OpsHTTPEndpoint · ServiceHealthRegistry · ServerFrameHistory 가 이미 Engine/DedicatedServer |
| Server/ServerAdminChannel (64/140) | 관리 채널(루프백 · 토큰, help · status · maintenance · shutdown), 모든 구성 | **엔진 일(제품, 서버)** | Engine/DedicatedServer/ServerAdminChannel | 위와 같다 — 서버 상태를 델리게이트로 묻는다(서버 루프를 모름) |
| DevTools/Console/ServerDebugControl.h (51) + DevTools/Console/ServerDebugControl.cpp (247) | 서버 개발 조종(표준 입력 개발 명령 · 디버그 채널) | **개발** | DevTools/Console/ServerDebugControl(서버도 짓는 비클라이언트 DevTools) | DevTools/Console · Engine/Observability 만 씀. Server 는 DevTools 를 본다 |
| Server/ServerApp · main.cpp · Windows/WindowsServiceHost | 서버 본체 · OS 서비스 호스트 | 호스트 본체 | 그대로 | |

→ 옮기면 App 은 App · AppEntry · AppHeadlessTasks · EditorModuleManager(+ RHIBackendSwitcher 의 제품 쪽이 Engine 으로 가면 그것도 사라짐), Server 는 ServerApp · WindowsServiceHost 만 남는다.

---

## 11. 약어 표기 어긋남 (NamingCpp: 약어는 형식 · 파일 · 폴더 · 식별자 중간에서 모두 대문자)

FourCC 표기: FourCC 는 "Four-Character Code" 의 약어 **CC** 를 품는다 → 규칙대로 `FourCC`. 새 이름 `FourCCUtil.h`(클래스 `FourCCUtil`), 식별자 `listFourCC` · `kDx10FourCC` · `getLegacyFourCCTable` · `findDXGIFormatOfFourCC`.
지난 일괄 변경(1dafbae0c9 — Vfx → VFX, Gpu → GPU, Id → ID, Json → JSON …) 뒤 도구를 지워(0e6bc0706c) **새 어긋남을 막는 린트가 없다** — 아래가 그 뒤 생겼거나 빠진 것이다. 구조로 막으려면 약어 목록 + 식별자 검사를 `py -3 -m Scripts lint` 에 둔다(바깥 이름은 예외 목록).

| 약어 | 어긋난 파일 · 폴더 | 우리 코드 식별자(예) | 수 | 비고 |
| --- | --- | --- | --- | --- |
| CC | Core/Encoding/FourCCUtil.h | FourCcUtil, DDSLegacyFourCc, listFourCc, kArrFourCcRow, kPixelFormatFourCcFlag | 9 | |
| ID | — | AssetId, DeviceId, EventId, InstanceId, AccountIdHex, DuplicateId, 테스트 이름 *WithoutId* 다수 | 약 60(ImGui · NDK · Win32 바깥 이름 제외) | 바깥: ActiveId · DockId · GetCurrentThreadId · AInputEvent_getDeviceId 는 그대로 |
| RESP | GameFramework/…/CacheStore/Server/Driver/Resp/ (폴더 + 파일 10) | RespCodec, RespCommand, FakeRespServer | 27 | Redis RESP 프로토콜 |
| HPACK | OnlineCore/Server/HTTP/Hpack.h · HpackHuffmanTable.h · HpackStaticTable.h | HpackDecoder, HpackEncoder | 11 | RFC 7541 |
| APNs / FCM | LiveOps/Server/Push/Provider/Apns/ · Fcm/ (폴더 + 파일) | ApnsPushProvider, FcmPushProvider | 26 | `APNSPushProvider` · `FCMPushProvider` |
| AEAD | — | INetAead, NetAeadAlgorithm, createAead, _sendAead | 12 | |
| AT-SPI | Engine/UI/Accessibility/AtSpiMapping.h · Linux/AtSpiAccessibilityBridge.h | AtSpiBridge, AtSpiState | 9 | `ATSPIMapping` |
| UIA | Engine/UI/Accessibility/Windows/UiaAccessibilityBridge.h | UiaAccessibilityBridge | 3(우리 것) | Uia* Win32 API 는 바깥 이름 |
| LZ4 | Engine/Compression/EngineCompressionCodecs.h | Lz4CompressionCodec | 1 | |
| RLE | Core/Compression/BuiltinCompressionCodecs.h | RleCompressionCodec, makeRleSettings | 4 | |
| OS | Editor/Common/Backend/ImGuiOsInputFilter.h | ImGuiOsInputFilter | 1 | `ImGuiOSInputFilter` |
| PBR | Editor/Common/Asset/Tests/TestModelImportPbr.cpp | ModelImportPbrMaterial, readPbrMaterial, bPbrSections | 12 | |
| PCM | — | AudioPcm, convertPcm, kTagPcm, bUnsupportedPcmLayout | 12 | |
| DPI | — | DpiScale, FontScaleDpi | 3(우리 것) | ImGui ConfigDpi* · GetDpiForWindow 는 바깥 |
| UTF | — | Utf8ToUtf16…, StringUtilUtf8Validation, roundTripUtf8 | 약 14 | 형식 `utf8` 은 소문자 첫 낱말이라 OK |
| XML | — | RenderPipelineXml, SettingsXmlRoundTrips, makeCurveXml | 약 15 | |
| UI | — | EmscriptenUiEvent(바깥), gv_editorUiScale, gv_stressTestUi* | 8 | gv_ 이름은 명령줄 데이터 — 바꾸면 스크립트 · 시나리오도 같이 |
| LOD | — | maxLod, gv_meshLodForce, gv_animationLod, gv_gpuInstanceLod | 5 | 〃 |
| URL | — | baseUrl, tokenUrl, jwksUrl, DataRouterUrl | 6 | |
| JSON · GPU · DDS · MSDF · FNV · CRC · HDR · ASCII · ABI · API · HKDF · RPC · TLS · DLC · PNG · FPS · FOV · SDF · RGBA · IME | — | JsonRoundTrip, GpuContextType, kDdsExtension, MsdfAtlas…, computeHkdfSHA256, PFN_GetModuleAbiVersion … | 각 1~6 | |
| OK | — | _bOk, _bAeadOk, RequestToABlockerIsASilentOk | 103 | 약어 목록에 없음 — "OK" 를 약어로 볼지 목록에서 정한다(정하기 전엔 바꾸지 않는다) |

---

## 12. 그대로 두는 것(검토했고 맞다)

`Core/Foundation/ASCIIUtil · HexDigitUtil`(Hash 티어 0 이 씀), `Core/Container/{InlineAllocator,IDAllocator,SpanAllocator,IndexFreeList}`(할당 정책 — UE Containers 와 같음. EventBuffer · ConsumerGate 는 Core/Event, KeyedPendingList 는 RHIRetireList 로 이미 옮겼다), `Core/Task/LockFreeObjectPool`(로그 · 시간을 써 Memory 로 못 감), `Core/Network/Replication/*`, `Core/Spatial/PositionWelder`, `Engine/Asset/GraphCommentBox`(그래프 에셋 공용 직렬화 조각), `Engine/Serialization/Serializer/{AssetRefBase,DataPath}`(층 1 이 필요), `Engine/Object/Component/CameraComponent` · `Registry/CameraRegistry`(씬이 카메라를 고름 — 층 5), `Engine/Foundation/*Handle.h`(아래 층 앞 선언), `Engine/Graphics/Debug/VisualLogger`(디버그 그리기 큐 위), `Engine/Observability/DebugChannel`(Server 운영 채널이 씀 — 제품), `Engine/Config/NetEmulationPresetTable`(3 절).

---

## 13. 근본 원인과 같은 결함

- **근본 원인:** 폴더 자리를 정하는 기준이 "처음 쓰는 쪽" · 이름 낱말(Util · Registry · Component)이었고, 하는 일 기준표가 없었다. Core 티어 검사는 Log(5) 위만 보고(`Scripts/lint/CoreTiers.py`), 아래 티어 역류(Concurrency → Container)와 "폴더 일과 내용이 다름"은 아무것도 잡지 않는다.
- **같은 결함이 있을 곳:** 새 파일이 들어올 때마다 다시 생긴다 — 1 절 기준표를 `docs/agents/Includes.md`(또는 새 `FolderLayout.md`)의 정본으로 옮기고, `CoreTiers.py` 를 모든 티어로 넓히며, `<기능>/Component/` 안 `*Component` 가 아닌 파일 · 새 `Util/Utility` 폴더 · 약어 소문자 꼬리를 린트로 막는 것이 구조적 해법이다.
