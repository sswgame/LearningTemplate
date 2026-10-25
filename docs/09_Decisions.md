# 결정 기록

> 질문: 이 제안은 이미 정했거나 기각했는가?
> 독자: 같은 제안을 다시 하려는 사람, 정한 이유를 찾는 사람.
> 결론: 1 ~ 4절이 이 문서에 있고, 5절의 영역별 결정은 `docs/decisions/` 의 영역 파일 열둘이다. 새 결정은 상용 엔진과 비교한 근거 한 줄과 함께 맞는 영역 파일의 끝에 더한다.

다시 볼 조건이 붙은 결정은 그 조건이 오면 다시 엽니다. 아직 남은 일은 [작업 방식](11_Workflow.md) 6절의 네 곳에 있습니다.

## 1. 작업 방향

- **쪼개기보다 공통부 추출.** 긴 함수를 나누면 코드가 옮겨 갈 뿐 총량은 그대로다. 중복은 증상이고 원인은 "매번 다시 만들어야
  하는 구조" 다. 원인을 없앤다.
- **추가 · 변경이 쉬운 구조를 먼저 만든다.** 판단 기준은 "하나 더하려면 몇 곳을 고쳐야 하는가" 다. 복사해야 할 것이 남아 있으면 그
  자리가 다음 리팩터 대상이다. 목록은 하나(표 · X-macro · 폴더 열거)로 둔다. 여러 파일이 같은 enum 을 `switch` 하는 자리는
  "백엔드 · 스테이지마다 정말 다른 일을 하는가" 를 먼저 묻는다(그러면 정당하다).
- **한 곳에 넣은 고침은 형제에게도 넣는다.** 가장 잦은 결함 모양이다(한 함수만 상한을 안 봄 · 셋 중 하나만 폴백 없음).
  고칠 때 같은 일을 하는 형제를 grep 으로 찾고, 원인이 같은 결함이 여럿이면 구조(한 창구 · 단언 · 게이트)로 막는다.
- **실패는 조용하면 안 된다.** 버린 값 · 못 읽은 칸 · 못 만든 폴더는 그 자리에서 무엇이 왜인지 알린다. 실패할 수 있는 bool 은
  `[[nodiscard]]` 이고, 일부러 버릴 때는 `(void)호출(); // 이유` — 실패 가능 동사(load · read · apply …)의 결과만 이유가 필수다(`CheckDiscardReason`).
- **상용 엔진의 같은 자리와 견준다.** 새 구조는 언리얼 · 유니티 · Godot 의 같은 자리를 먼저 보고 따르되, 일부러
  다르게 한 것은 이유를 적는다(예: `hashed_string` 은 FName 규칙이지만 숫자 꼬리는 없다).
- **성능 주장은 Release 숫자로만 한다.** 숫자 없는 최적화는 하지 않는다. 이전 · 이후를 같은 조건에서 번갈아 재고, 평균이 아니라
  p50 · p99 · 최악 프레임을 본다([검증과 측정](08_Verification.md) 2절).
- **주석 · 커밋 메시지는 한국어.** 문서 주석(`/** */` · `///<`)은 "~합니다" 체, 함수 본문 `//` 주석은 "~다" 체로 한 블록 안에서 통일한다.
  직역어 대신 표준 용어(thundering herd · 락 컨보이 · 브로드캐스트 · 조인 · continuation · 오버플로 · 분기)를 쓴다. 식별자 · 로그 ·
  assert 문자열 · `NOLINT` 줄은 그대로 둔다. 주석을 고치기 전에 구현을 읽는다 — 복사해 붙인 설명이 실제 동작과 다른 곳이 많았다.
  규칙은 [AGENTS.md](../AGENTS.md)와 `docs/agents/`, [04_CodingGuidelines.md](04_CodingGuidelines.md).
- **자산 처리 용어는 넷이다(상용 엔진 기준).** Cook = 배포 · 실행용 플랫폼 데이터로 바꾸기(셰이더 · 씬 · 프리팹 · 팩, `--cook-*`), Import = 원본 → 엔진 형식(텍스처, `--import-textures`), Generate = 빌드가 만드는 코드 · 헤더, Bake = 미리 계산한 결과(라이트맵 · 내비 — 지금은 없다). 한국어 "굽다" 는 쓰지 않는다. 이름을 바꿀 때 별칭(`Alias` · 옛 CLI 철자 · 옛 형식 리더)을 두지 않고 데이터를 다시 쓴다.
- **새 기능 로드맵보다 백로그의 결함 · 정리 · 성능 · 테스트 · CI 항목을 먼저 닫습니다**(2026-10-06, 사용자 결정). 온라인 서비스 키트는 함께 진행합니다. 보류로 정한 것(`hashed_string` 숫자 꼬리, CC0 리소스 배치와 MechArena)은 그대로 둡니다.

## 2. 안 하기로 한 것 (다시 제안하지 말 것)

- **PIX 캡처(DX12 전용 — WinPixGpuCapturer.dll)는 하지 않았다**(2026-10-10) — RenderDoc in-app API(`RenderDocCapture`, `-renderdoc`)가 네 백엔드를 덮는다. DX12 타이밍 분석이 필요해지면 다시 본다.
- **지금 하지 않는 구조 후보 — 다시 볼 조건과 함께**(2026-10-03 상용 엔진 비교로 결정):
  - 트랜스폼 SoA 2 단계(UE 액터도 AoS, 측정 근거가 생기면).
  - 선행 조건 스케줄러(시스템이 서로의 결과에 기대기 시작하면 — UE `AddTickPrerequisite`). 2026-10-06 키트 조립 점검: 키트 디렉터 사이 순서는 한 오브젝트에 붙인 순서가 주고(`TickRegistry` 가 한 오브젝트의 항목을 한 워커가 붙은 순서로 돌린다), 다른 오브젝트 사이는 디렉터의 `_tickAfter`(규칙 서브틱 + 선행 조건)가 준다. 키트 코드끼리의 의존은 0 곳이라 근거가 아직 없다.
  - 에셋 로더 등록제는 재개했다(2026-10-20 — 다시 볼 조건이던 대여섯 종을 넘어 지금 20 종 이상이다. UE `UFactory` 에 해당). 단위는 [구조 단순화](plans/StructureSimplification.md) 의 AT.
  - 참조 카운트 RHI 핸들(한 리소스를 여럿이 나눠 들기 시작하면 — UE `TRefCountPtr`).
  - Mesh/Material `SlotHandle`(하지 않는다 — `shared_ptr` 이 수명과 RT 안전을 한 번에 준다). `ResourceUtil` 소유 객체화(하지 않는다 — UE `FPaths` 도 정적).
  - 링크 단위 분할(증분 링크 시간이 문제가 되면 별도 PR). GameFramework 기반(약 58k 줄)을 `GFS_*` DLL 여럿으로 쪼개는 안도 같은 이유로 하지 않는다(2026-10-05 재확인). 기반 폴더의 층은 폴더 층 게이트(`CheckGameFrameworkLayers`)로 지킨다.
  - `UnitStatsComponent` 를 기반(`Combat/`)으로(2026-10-05 — 쓰는 게임 · 씬 · 프리팹 0, 옮기면 데미지 숫자(UI 층 4) · `MonsterDef` · `DamageAppliedEvent` 셋을 끊어야 층이 맞는다. 체력 읽기는 `Combat/HealthSourceComponent` 가 맡는다). 다시 볼 조건: 다른 키트 · 게임이 피해 입구(`takeDamage` · 무적 · `DamageAppliedEvent`)를 이 키트 없이 쓰려 할 때. 그때는 `DamageAppliedEvent` 도 기반으로, `setStats( MonsterDef )` 는 키트의 도우미로, 데미지 숫자는 `HealthListenerComponent` 파생으로 내리고, `CheckGameFrameworkLayers` 자가 테스트의 키트 헤더 예를 바꾸고, `generated/GF_ActionCombat/UnitStatsComponent.gen.*` 을 지운 뒤 re-configure 한다.
  - 피해 입구 인터페이스(19 곳 — 인자 모양이 넷이다. 키트 투사체 · 공격 판정을 다른 체력 모델(어빌리티 시스템 · 게임 컴포넌트)에 쓰는 게임이 생기면 `Combat/` 에 `takeDamage( 양, 쏜 쪽 )` 하나를 `HealthSourceComponent` 옆에 — 언리얼 `AActor::TakeDamage`).
  - 키트 아이템/효과 처리기 등록부(2026-10-05 전수: enum switch 4 곳 · 19 case, 두 키트가 나누는 종류 enum 0 — 키트마다 하는 일이 다르고 `-Werror=switch` 를 잃는다. 데이터로 효과를 더할 게임은 기반 GAS 의 `registerExecutionClass` — 언리얼 GameplayEffect 자리 — 를 쓴다. 두 키트가 같은 종류 집합을 나누게 되면 다시 본다).
  - `TimedModifierSet`(시간 제한 수정자는 이름 붙은 `Countdown` 슬롯이고 쌓기는 `extendTo` · `start` 로 이미 정해져 있다, 목록형은 위쳐 물약 한 곳 — 일반형은 GAS 의 지속 이펙트 · 스택 정책. GAS 밖 키트 둘 이상에 목록형이 생기면 다시 본다).
  - API 통합 남은 판단(다음 훑기).

- **도구 버전을 "최신 자동" 으로 두는 것.** 네트워크 의존이 생기고 빌드 재현성이 떨어진다. 버전 키 하나로 고정하고 올릴 때만 의도적으로
  올린다. clang-format 은 버전이 곧 출력이라 고정이 아니면 안 된다.
- **`EditorContext` 의 소유 구조를 더 쪼개는 것.** 조회 · 생명주기를 두 TU 로 갈라 조회만 하는 코드가 ImGui 없이 링크되게 해 둔 것으로
  충분하다(그래서 `Test/EditorTest` 가 성립한다). 매니저 소유를 밖으로 빼는 것은 영향이 크고 얻는 것이 없다.
- **`Source/Engine/Common/EngineServices.h` 와 `Source/Engine/Config/RHIBackendType.h` 를 위 티어로 옮기는 것**(2026-10-10, Engine 폴더 재배치 0-3 의 4).
  `EngineServices.h` 는 Engine include 가 0 이고 서비스 이름을 전방 선언으로만 아는 찾기 창구다. 모든 티어의 180 파일이 쓰므로 위로 옮기면 prelude 예외가
  하나 더 생길 뿐이다(언리얼의 `GEngine` · 서브시스템 찾기도 Engine 코어 모듈에 있다). `RHIBackendType` 은 `EngineConfig` 가 드는 설정값이라 `Graphics/RHI` 로 옮기면
  Config → Graphics → Config 순환이 된다. 같은 결정에서 `IRenderSurface` 는 쓰는 쪽인 `Graphics/RHI` 로, 서버 설정 둘은 `Config/Server/` 로 옮겼다.
- **clang-tidy 의 `bugprone-throwing-static-initialization` 남은 건에 `noexcept` 를 붙이는 것.** 전역 변수 등록자 · 설정 싱글턴은
  `string` · `variant` 를 들어 실제로 던질 수 있다. 분석기를 침묵시키는 대신 정보를 지우는 거래다.
- **네트워크에서 하지 않는 것**: NAT 통과, 리플렉션 속성 복제(언리얼 Iris), 복제용 RPC, 외부 네트워크 라이브러리입니다. 암호 라이브러리만 예외입니다(5-9 "네트워크 보안").
  리팩토링 N13 ~ N21 · N18b 는 끝났고 N18c 는 측정으로 기각했습니다(3절).
- **Engine 에 남겨 두는 폴더**(2026-10-10 사용 현황 조사, 엔진 분할 0-3). 에디터는 `GameFramework` 를 include 할 수 없으므로(`CheckEngineLayers` 의 Editor 금지 목록) 에디터가 쓰는 폴더
  (`Dialogue` · `Sequencer` · `Destruction` · `Environment` · `Automation` · `UI` · `Animation` · `Scene` 등)는 Engine 말고 둘 곳이 없습니다.
  서버 실행 파일도 `GameFramework` 를 모르므로 서버가 `MetricRegistry` · `OpsHTTPEndpoint` 를 쓰는 `Observability` 도 남습니다.
  `Character` 는 에디터가 쓰는 `Destruction` 이 쓰고, `Spatial` 은 `Character/Fit` 이 씁니다. `Telemetry` 는 `Engine` · App · 게임이 쓰고, `Compression` 은 Core 인터페이스의 코덱 공급자라 Core 가 서드파티를 몰라야 하는 규칙 때문에 남습니다.
- **SOLID 리팩토링에서 핫 경로라 손대지 않는 것**(2026-10-12 감사). `IRHICommandList` · `IRHIResourceFactory` 의 가상 호출 구조, `GPUSceneBuilder` 내부 병렬 루프, `PhysicsOverlapWorld` 셀 해시,
  Box2D · Jolt 어댑터의 switch(외부 라이브러리 경계), RHI 백엔드 `CommandContext` 큰 파일(백엔드 하나의 기록이라 한 이유로 바뀐다), `AbilitySystemComponent` 의 modifier switch(닫힌 열거 + `-Wswitch`)입니다.
  의도된 예외는 `App.cpp` 의 `kArrHeadlessTask` 표, `EditorPanelManager::syncWithRegistry`, 에디터 `findSubsystem<T>()`(모듈 경계 주입), 엔진 루프의 서브시스템 등록부입니다.
- **구조 리팩토링 후보를 옮길지 가르는 기준**(2026-10-12). 줄 수가 줄어서가 아니라 앞으로 고칠 곳이 줄어서 옮깁니다. 하나라도 걸리면 옮기지 않고 이유를 남깁니다.
  - **반쪽 이전 금지**: 한 클래스 안에 옛 방식과 새 방식이 섞이면 더 헷갈립니다(예: UserSettings 공급자만 옮기고 핸들러는 남기는 것). 통째로 옮기거나 통째로 둡니다.
  - **훅 · 인터페이스는 쓰는 곳이 둘 이상일 때**: 쓰는 곳이 하나뿐인 일반화는 호출 시점 순서를 숨기고 읽는 비용만 늘립니다. 순서에 뜻이 있으면 호출 자리에 두고 이유를 주석으로 적습니다.
    다만 SOLID 와 부딪히면 SOLID 가 이깁니다(구체 의존을 남기는 쪽이 더 큰 부채).
  - **동작이 같으면 코드를 늘리지 않습니다**: 한도 없는 대기에 시한 · 경고를 붙이는 식으로 상수와 분기만 느는 변경은 하지 않습니다.
  - **의미가 조금씩 다른 복붙은 합치지 않습니다**: 조건이 콜백 · 인자로 새는 공용 함수는 복붙 넷보다 읽기 어렵습니다.
  - **옮기면 검증 한 줄이 따라옵니다**: 공용 구조가 주는 규칙(중복 거절 · 리로드 대체 · 순서)을 지키는지 보는 단언을 그 자리의 테스트에 둡니다.
- **SOLID 는 성능을 해치지 않는 선에서 반드시 지킵니다**(2026-10-12, 사용자 결정). 성능은 측정으로 해치지 않음을 보이거나 핫 경로가 아님(프레임당 한두 번 · 목록 순회 한 번 수준)을 코드로 보입니다.
  S: 클래스 하나가 한 가지 이유로만 바뀝니다(일이 몰린 클래스는 나누는 쪽이 기본). O: 같은 모양의 `if`/`switch` 분기나 직접 호출이 늘어나는 자리는 레지스트리 · 관찰자 뒤로 뺍니다.
  L: 구현이 계약을 좁히지 않고 관찰자 훅의 기본 동작은 중립(빈 함수 · `kRunning`)입니다. I: 한 훅에 무관한 일을 얹지 않습니다. D: 위 티어가 구체 클래스를 직접 알지 않습니다.
  감사(31건)에서 L 위반은 Box2D 관절 하나뿐이고 I 는 약했습니다 — 약점은 S(큰 클래스)와 O/D(직접 호출 · 종류별 분기)였습니다. 핫 경로에는 가상 호출을 넣지 않고, 뽑은 클래스는 값 멤버로 두어 비가상으로 직접 부릅니다.
- **SOLID 단위 S1 ~ S22 에서 옮기지 않은 것**(2026-10-12, 위 기준에 걸림).
  - 서버 `pause`/`resume`/`step` 을 개발 명령으로(S3): Shipping 이 `quit`/`stop`/`status`/`help` 를 직접 처리해야 해 한 함수에 두 방식이 섞이고, 쓰는 곳이 `ServerApp` 하나이며,
    에디터가 같은 이름 `pause`·`stop`·`step` 을 이미 다른 뜻(플레이 세션)으로 등록해 전역 레지스트리에서 충돌합니다. 두 `quit` 은 뜻이 달라(표준 입력 루프 bool ↔ `ShutdownSignal`) 합치지 않습니다.
  - 패스 종류 표의 실행 함수 칸(S5): `bSkipEffect` 만 표 비트로 옮겼습니다. 실행 switch 9 case 를 표로 옮기면 쿠커 · 검증이 읽는 데이터가 `FrameRenderer` 에 의존하고(D 위반) 새 패스를 더할 때 고칠 곳도 줄지 않습니다.
  - `ProfilerPanel` 탭 분리(S8, `IProfilerTab`): 탭들이 패널 상태(`_scopeHistory` · 타임라인 · 불러온 트레이스)를 나눠 써서 탭만 떼면 반쪽 이전이고, 함수 표만 두면 고칠 곳 수가 같습니다.
  - 인자 없는 `Scene( name )` · `GameObjectManager()` · `SceneManager()` 생성자(S10): 호출처가 약 90 곳과 500 곳입니다. 만들 때 `makeFromBoundEngine()` 으로 묶음을 한 번 읽고 클래스 안 접근은 모두 묶음을 거치므로 반쪽 이전이 아닙니다.
  - 에디터 셸의 프레임 앞 정리 · 프레임 열기 · 프레임 닫기(S12): ImGui 순서와 렌더 스레드 인계(`DrawSnapshotHandoff`)가 위치를 정하는 프레임 계약이라 `updateUI` 에 둡니다.
  - 분해에서 남긴 것: 모달 · 결과 창 플래그와 이름 입력 버퍼(S19, ImGui 팝업 시점), `verifyModuleBindings` · `isBoundTo`(S20, 모든 컨텍스트 핸들을 함께 보는 검사), `ModuleShadowCopy` 인스턴스화(S20, 번호가 프로세스 전역이어야 섀도 폴더 이름이 겹치지 않음),
    `UserSettingsManager` 의 키 바인딩 겹침 · 품질 번들 맞추기(`setPendingAt` 상태 기계와 안 떨어짐), `SceneNavigation` 의 에이전트 루프 · 런타임 찾기 · 설정 로드, `App` 의 `updateShellActions` · 모듈 카탈로그 해석,
    `IRHIDevice` 의 `supportsNativeBindlessSampling` · `applyVSyncInternal`, `FractureComponentBase` 의 그룹 재구성 · 물리 어댑터(S15), `FrameRenderer` 의 트랜지언트 계획(S16),
    `Engine::update` 의 백엔드 교체 · `pollShaderReloadHotkey` · `MaterialCache::initializePending` · 관찰자 루프(S13, 수명 계약 · 순서가 뜻).
  - S16 의 **동작 변화**: 엔진 서비스가 묶이지 않은 실행은 GPU 계측이 꺼집니다(전에는 단언).
- **폴더 · 파일 구조는 기계적으로 합치지 않습니다**(2026-10-12 감사, R8). 25 줄 이하 파일이 97 / 2,932 개이고 큰 클래스를 나눈 가족 대부분이 파일마다 120 줄을 넘으며, 작은 조각(`EditorAssetTypeFocus.cpp` 36 줄)은 의존을 분리한 것입니다.
  합치면 층 규칙(`CheckEngineLayers`)과 그래픽스 구조 재편의 파일 분할 기준을 해칩니다. 한다면 `Scripts/lint/report/` 에 같은 접두 `.cpp` 가족과 줄 수를 보고하는 스크립트를 두고,
  120 줄 이하 조각이 의존 분리가 아닌 가족만 사람이 봅니다(지금 후보는 `RenderView.cpp` 13 줄 정도).
- **파일이 40 개를 넘는 평면 폴더는 기능 묶음 하위 폴더로 나누고, 한 클래스를 나눈 파일 가족은 가르지 않습니다**(2026-10-15). 언리얼 모듈의 `Private/` 하위 폴더,
  유니티 `Runtime/` 하위, Godot `scene/` 이 기능 묶음으로 나누는 것과 같은 단위로 알고 있습니다.
  `Editor/Common/Commands` 는 `Asset` · `Scene` · `Play` · `Dev`(등록부 · 백그라운드 입출력은 루트), `Engine/Object/GameObject` 는 `Registry` · `Tick` · `Service` · `Serialization`
  (`GameObject` · `GameObjectManager` · `GameObjectStore` 는 루트), `Engine/Renderer/Frame` 은 `View` · `Packet` · `Resource` · `Capture` · `Timeline`(`FrameRenderer*` 가족은 루트)입니다.
  에디터 층 규칙은 `Common/<폴더>` 단위로 읽고, 오브젝트 모델 코어 규칙은 `GameObject` 트리 전체(`CheckEngineLayers.toml` 의 `coreTree`)입니다.
- **스크립팅 언어 층(Lua · C# · 비주얼 스크립팅)은 두지 않는다**(2026-10-17, 사용자 결정). 게임 로직은 C++ 모듈과 핫리로드로 짠다. 비프로그래머용 층이 필요해지는 게임이 생기면 다시 연다. 백로그 `D4-9` 의 비주얼 스크립팅은 순위 목록에만 둔다.

## 3. 기각한 것 — 측정값과 함께 (다시 제안하지 말 것)

측정이 근거인 결정은 끝에 한 줄로 조건을 적는다: `[측정: 기계 · 규모 · 날짜 · 근거 커밋]`. 알 수 없는 값은 `미상`. 조건이 바뀌면(빌드 구성 · 프레임 길이 · 기계) 이 기각은 "무조건 금지"가 아니라 "그 조건에서 이득 없음"이므로, 다른 문서가 "하지 말 것"을 말할 때는 이 절의 항목을 링크해 조건째 읽게 한다. 재측정이 필요한 항목은 [후보 문서](plans/Candidates-project.md)의 `DecisionMeasurementColumn` 에 남긴다.

- **`runParallelByCost` 를 모든 병렬 호출처로 넓히기**(2026-10-16): 트랜스폼 계층의 고정 임계값(쓰기 1024 · 플러시 2048)과 GPU 씬(2048 / 4096)은 손익분기에 맞춘 값이라, 트랜스폼 세 곳을 바꿔 재니 쓰기 적용은 61 ~ 66 → 64 ~ 67 us 로 같고 틱 8000 은 86 ~ 99 → 105 ~ 107 us 로 조금 느려져 되돌렸다. 항목당 비용이 크고 개수가 달라지는 곳에만 쓴다 — 씬 틱(작은 씬)과 애니메이션 평가. [측정: 기계 미상 · Release 트랜스폼 세 곳 · 2026-10-16 · e3542d9db]

- **`common` 지연 import(PEP 562 `__getattr__`)**(2026-10-06, 12 회 중앙값): `import common` 누적 81.0 → 1.8 ms 이지만 `GenerateCookContract.py` 한 번이 110.5 → 110.1 ms — 생성기는 결국 같은 하위 모듈을 다 올린다. [측정: 기계 미상 · 12 회 중앙값 · 2026-10-06 · 0df62d4cd]
  기준(생성기 30 % 이상)에 한참 못 미치고, 하위 모듈 이름과 같은 공개 이름(`BuildTree`)이 import 순서에 따라 모듈로 바뀌는 함정이 생긴다.
- **N18c 메시지 버퍼 풀**(2026-10-06, Release `NetReplicationBenchTest` 16 × 1000, 3 회): N18b 뒤 틱당 할당 1133 중 서버 복제 544 — "전체 − 서버 복제" 589 < 문턱 1 000.
  (N18b 전 25182 · 서버 복제 16886, ReplicationServer tick p50 5.4 → 3.5 ms.) [측정: 기계 미상 · Release `NetReplicationBenchTest` 16 × 1000, 3 회 · 2026-10-06 · 38a18ee1c, 기각 e18e5674f]
- **TaskManager 후보 셋**(Release, 큐브 8000, 16 스레드, 5~8 회 번갈아): 스핀 워커 2 개 제한 + 적응 예산(GT 839→846, RT.ExecutePacket 395→436 us), 워커별 노드
  자유 목록(GT 839→917, RT.Graph 187→237 us), 워커 지정 틱 쓰기 적용(queued 78→80 us, p99 163→212 us). 셋 다 손해 또는 잡음. [측정: 기계 미상 · Release 큐브 8000, 16 스레드, 5~8 회 · 날짜 미상 · 커밋 미상]

- **사용자 결정**: mimalloc · SIMD 수학 · 프리페치는 쓰지 않는다. 사용처가 0 이어도 상용 엔진에 대응이 있는 공개 API 는 남긴다(남긴 기능에는 테스트를 붙인다).
  - SIMD 수학을 미룬 까닭은 성능 근거가 아니라 플랫폼 파편화 우려입니다(사용자 확인, 2026-10-17). 다시 볼 조건: 프로파일에서 수학이 병목인 핫 루프가 나올 때, 또는 지원 플랫폼 목록이 확정될 때. 다시 볼 때의 후보 방식(제안, 미결정): 한 추상 계층 뒤에 SSE2 · NEON · 스칼라 백엔드를 컴파일 때 고르고, 스칼라 백엔드를 항상 빌드해 테스트하며, 일반 `Vec3` 형식이 아니라 배치 커널(변환 · 컬링 · 스키닝)에만 적용합니다.
- **GPUScene · 렌더**: `DrawCandidate` SoA(구조체를 78 % 키워도 불변, 거의 선형 — 예전의 "28~62 배" 는 GT 가 GPU 를 기다린 착시), 발행 배열 풀(75 vs 76~85 us), 영속 렌더 씬을 **종류** 축으로
  (8 → 1024 에 평평), 인스턴스 채우기 병렬화(모든 크기에서 인라인에 짐), raw 페이로드 워커 쓰기 · `MeshInstanceBatch::updateParallel`, 배치 정렬(잡음), DX12 루트 상수 드로우 ID 주입
  (ExecuteIndirect 2 배 느림 — 인스턴스 슬롯 스트림이 대안), 클리어 `DontCare`(0 us — 타일 GPU 로 가면 다시), 점 샘플러(122 → 121), 머티리얼 CB 워커화(6 us), PSO 병렬 생성(9 %, 드라이버가 직렬),
  `executeCommandLists` 일괄(리스트 수 그대로), 꼬리 재방출 그룹 캐시(98 → 98).
  DX12 온라인 블록 잠금의 워커별 목록(잠금이 드는 씬 배치 기록 전체가 p50 36~65 us = RT 프레임의 2~4 %, 큐브 8000 · 도형 5 · 디퍼드).
  `GPUInstance` 128 → 96 B(모든 인스턴스가 매 프레임 올라가는 최악에서도 증분 상한이 RT ~57 · GT ~65 us, 월드 float3x4 · 블렌드 비트 묶기는 모든 셰이더 · 계약 테스트를 건드린다).
  추가 뷰 사이 그림자 공유(2026-10-06, Release 큐브 8000 + 바닥, `-gv_benchViews=4` — 512² 캡처 카메라 넷, 번갈아 4 쌍): 추가 뷰의 그림자 패스를 빼도 `GPU.Frame` 이
  쌍마다 256 · 390 · −47 · 169 us 줄 뿐(뷰당 ~50 us, 추가 뷰 넷이 GPU 프레임을 0.65 → 1.8~2.1 ms 로 늘리는 몫의 ~15 %). 프레임 순서를 바꾸는 구조 변경에 비해 작다. [측정: 위 그림자 공유 한 건만 — 기계 미상 · Release 큐브 8000 + 바닥, `-gv_benchViews=4` · 2026-10-06 · 7b1d868d4. 나머지 GPUScene · 렌더 항목은 날짜 · 커밋 미상 — 2026-10-03 의 백로그 정리 a55a90f0b 이전 값]
- **히치**: 펜스 시그널을 Present 앞으로 · 백버퍼 수 · 인라인/즉시 제출 — 분포가 그대로였다. 어댑터 강제 선택은 A/B 로 악화.
  2026-10-06 재측정(Release, 큐브 100 · VSync 끔, 제안서 측정 3 회씩): 꼬리(p99 2.4~4.2 ms · 최악 12~37 ms)는 DX11 에도 같은 크기로 있고(DX11 은 Present 안, DX12 · Vulkan 은
  BeginFrame 펜스) Present 호출 자체는 p99 0.2 ms — GPU 백프레셔다. VSync 켬은 DXGI 가 판당 많아야 한 주기를 놓친다. 대기 가능 스왑체인으로 고칠 히치가 없다.
  Vulkan 스왑체인 이미지를 4 개로(+1) 늘려도 VSync 주기 놓침이 줄지 않았다(12 판 중 6 판 최악 13~27 ms — 3 개는 22 판 중 2 판). [측정: 기계 미상 · Release 큐브 100 · VSync 끔, 3 회씩 · 2026-10-06 재측정(첫 측정 2026-09-20 baaa78ee8) · 커밋 미상]
- **틱 · 오브젝트**: 인라인 `TickItem`(GameObject 192 → 232 B), 적용 단계를 틱에 합치기(칸당 +124 B), 회전 사원수 캐시(+28 B), 축별 sin/cos 건너뛰기(18.0 → 18.7 ns), 쓰기 정렬의
  `id % 버킷`(150 → 450 us) · 키를 건에 넣기(64 → 80 B), 오브젝트 id 표 2 단 디렉터리(1 억 스폰이면 800 MB) · 늘 견주기(5.6 → 6.4 ns), 엔티티당 XML 재파싱 구조 변경(2 ms 뿐). [측정: 기계 미상 · 규모 미상 · 날짜 미상 · 커밋 미상 — 2026-10-03 의 백로그 정리 a55a90f0b 에 이미 있던 값]
- **패딩 재배치 나머지**(2026-10-06, `RunPaddingReport --preset Ninja-Release --min-saving 8` 69 개): GameObject(168 → 160, 풀 칸 176 → 160)만 했다. 나머지는 인스턴스가
  적거나 나눠 쓴다 — Material · MaterialInstance 16 B(캐시 · `MaterialTintCache` · 스프라이트 공유, 수백 개 이하), Mesh 8 B(`MeshCache` 공유), 컴포넌트(RigidBody 16 · SkeletalMesh 16 ·
  Camera 8 — PROPERTY 순서 고정), 싱글턴(InputManager 64 · FrameProfiler 8). 수천 개가 만들어지는 타입이 나오면 그것만 한다. 위치 초기화 표(EditorAssetTypeInfo ·
  AssetMatchRow · CommandRow)는 모든 행을 같이 바꿔야 한다. [측정: 기계 미상 · `RunPaddingReport --preset Ninja-Release --min-saving 8` 69 개 · 2026-10-06 · 529e83933]
- **전역 소형 블록 할당자**(2026-10-06): Release 프레임당 sw 할당 7.7 회(120 프레임 × 3) — 회당 ~100 ns 로 잡아도 프레임당 1 us 아래. 기동(Release 856~1207 ms)의
  sw 할당은 Debug 기준 상주 블록 8 만 개 — 회당 100 ns 면 8 ms(1 % 아래)라 절반을 줄여도 기동의 0.5 %. 프레임당 할당이 다시 늘거나 큰 맵 로드의 프로파일에서
  할당이 5 % 를 넘으면 다시 잰다. [측정: 기계 미상 · Release 120 프레임 × 3, 기동은 Debug 상주 블록 · 2026-10-06 이전 · 529e83933(첫 기록 678b343bf, 2026-09-21)]
- **파일 시스템을 플랫폼 API 로 다시 짜기**(2026-10-06, Release 스크래치 · 2920 파일): 존재 확인 p50 10.9 vs 10.3 us · 순회 6.5~9.4 vs 7.6~10.0 ms — 시간은 같고
  할당만 준다(존재 확인 1 → 0, 순회 항목당 3.1 → 1.0). 그 할당은 기동 때만이고 상주가 아니다. `Core/File/Std` 로 감싼 뒤(Debug `FileUtilBenchTest`): 존재 확인 호출당 sw 할당자 밖 2 · sw 4
  (구분자 정규화 · UTF-16 변환 문자열), 순회 파일당 밖 2.3 · sw 6.5. 줄이려면 `toPath` 를 스택 버퍼 변환으로(sw 0).
  다시 재는 법: App Debug `-gv_memoryReport=1` 의 "cumulative since start" 줄 · `FileUtilBenchTest`. 한 경로가 프로파일에 보이면(Release 기동이나 프레임에서 1 ms 이상 · 프레임마다 부르는 경로) 그 함수만 `File/Windows` · `File/Linux` 로 옮긴다. [측정: 기계 미상 · Release 스크래치 2920 파일 · 2026-10-06 · a6cd49bb9]
- **물리**: 파괴 잎 볼록 껍질을 워커로 나눠 짓기(Jolt 잡 시스템, 2026-10-06) — `FractureBenchTest` p50 20.7 → 24.4 ms 로 느려졌다. 워커 15 개가 청크(잎 13 개)마다
  ~10 ms(잎당 ~750 us — 직렬 55 us 의 14 배)를 썼다: 껍질 짓기는 할당이 많고 Jolt 할당이 엔진 `Memory::allocate` 로 가서 경합한다. 할당을 풀어 주지 않는 한 다시 하지 말 것.
  주의: Jolt 백엔드 TU(`Physics/Jolt/*.cpp`, PCH 없이 Jolt 정의 · 대상 기능으로 컴파일)에 `Engine/Common/EngineParallel.h` 를 넣자 물리 전체가 깨졌다(질량 · 자세가 쓰레기) —
  그 TU 에서는 엔진 태스크 대신 이미 쓰는 Jolt 잡 시스템(`JoltJobSystem`)을 쓴다. [측정: 기계 미상 · `FractureBenchTest`(잎 13 개 청크, 워커 15 개) · 2026-10-06 · 3ba538f4d]
- **태스크**: 워커 스핀 늘리기(2 → 50 us 면 SMT 형제를 빼앗아 GT 889 → 1305 us), hot pool(이득 없음), 스레드별 목록 머리 패딩(잡음), 공유 풀 위 GT 병렬화를 레인 없이(RT 170 → 261 us),
  워커 깨우기 사슬, `TaskArgs` 인라인 4 칸(기록 122 → 196 us), `PagedArray` 통합의 첫 측정(번갈아 재니 차이 없음 — 측정 착시).
  [측정: 스핀 · hot pool · 레인 없는 GT 병렬화 — 기계 미상 · 큐브 100 · 2000 × 워커 14/16, 조건 변수 시절의 0.7 ms 프레임, 흔들림 ±40 %, p99 없음 · 2026-09-20 · 4846237f0. 프레임 8 ms 이상에서 다시 재기 전까지는 "그 조건에서 이득 없음"이다. "레인 없이" 기각은 High · Normal · Low 큐가 생겨 전제가 없어졌으니 레인 있는 설계로 다시 판단한다]
- **도구 · 빌드**: 린트를 파일마다 프로세스로(5.3 → 9.4 s — 덩어리 프로세스는 7.5 → 2.25 s), 짝 헤더 메모이즈(차이 없음), `formatstring` 비템플릿 부분
  `.cpp` 분리(41 → 44 s) · 타입 소거 배열, `ContainerTypeMap` 선형 탐색 개선(파서 시간은 libclang), 팩 코덱 Zstd(−0.1 %) · LZ4(+28 %) — 이미 압축된 자산이라 Zlib 유지(압축 안 된 자산이
  들어오면 다시 잰다), 린트 파일 공유 캐시(C++ 2500 개 28.7 MB 를 다 읽어도 0.15–0.7 s — 게이트는 정규식에 쓴다), 린트 CTest 를 한 프로세스로(기동 0.17 s × 항목 수 —
  항목별 보고 · TIMEOUT 을 잃는다), 린트 증분 해시 캐시(훅 바닥 ~2–4 s 에 교차 파일 규칙 재작성 — 보류), `flatMapInProcesses` 16 → 8 워커(혼자 돌면 3.3 → 3.7 s 로
  지고 CPU −40 % — `ctest -j` 겹침에서 재 볼 것). [측정: 기계 미상 · 린트 5.3 → 9.4 s 등 항목별 규모는 본문 · 날짜 미상 · 커밋 미상 — 2026-10-03 의 백로그 정리 a55a90f0b 에 이미 있던 값. `flatMapInProcesses` 8 워커는 `ctest -j` 겹침에서 재측정 필요]
- **구조**: 백엔드 `*RHIResource.h` · `*RHICommandContext.h` 공통 기반(겹침이 전부 override 선언), `ResourceCache<T>`(나머지 39 % 가 소유 방식), 모듈 팩토리 골격 공통화(공통 4 줄),
  RHI · GF leaf CMakeLists 를 부모 루프로(디렉터리 스코프), `FindWindowsTools` 파이썬 이전, `EngineTest` 에 `GF_*` 자동 링크, `SW_ASSERT_NULL`, `formatstring` 인자 수 컴파일 검사의 매크로 판
  (C++20 으로 올리면 `consteval` 포맷 타입으로 옮긴다), "자유 `static` 함수 금지" 린트(오탐), "CommandList 가 RecordingState 를 소유" 린트, `CheckCodeConventions` 매개변수 · 지역변수 사슬,
  `Cb` · `Fbo` 풀어 쓰기, 되돌리기 스냅샷 바이너리 통일, `StringBuilder::appendFormat` 잘림(버퍼를 늘려 다시 포맷한다), `EditorViewportClient` 쪼개기(공통 빼기로 간다),
  RTS 버킷 · MechArena · BattleRoyale 근접 질의를 엔진 `SpatialHashGrid2D` 로(RTS 는 ~40 줄이 순서 계약을 들어 핸들 정렬 격자로 바꾸면 자동 목표 동점 · 첫 빈 광물 ·
  밀어내기 합이 바뀐다, BR 은 근접 질의가 없다, Mech 는 조종사 몇 명 전수 검사가 격자보다 싸다), 키트 칸 저장소 템플릿 `Grid2D<T>`(덮는 자리 4 곳에서 4 줄,
  저장소 모양이 키트마다 다르다).
- **게임 · 키트**: 복셀 청크 메시 제자리 갱신 — 다시 짓기는 블록이 바뀐 청크만(Debug VoxelCraft 자동 플레이 5,000 프레임에 2 회, 프레임당 최대 4 개), 새 `Mesh` 가
  렌더 스레드가 든 옛 메시와 겹치지 않는 길.
  카트 트랙을 공용 `SplinePath` 로 — 카트는 XZ 로 달려 랩 · 고스트 거리가 수평 길이다(3D 호 길이로 바꾸면 같은 트랙의 값이 바뀐다), 공유되는 것은 누적 거리 표 하나, 쓰는 게임 0.
- **늘 상위에 오는 정당한 중복**(`RunDuplicateCode`): 백엔드 인터페이스 선언 · 레이스 래퍼 전달 · 플랫폼 구현 · enum 레이블 나열 · 서비스 로케이터 둘(`sw::editor` 는 nullptr, `sw::game` 은
  assert) · `MaterialPacking` 숫자 case(`-Wswitch-enum`) · DX12 상태 조회 · `TypeInfo` 생성자 · RLE · 콜스택 관문 · 셰이더 반사 D3D11/12(확인 중 — 백로그 `D2`) · Win32 마우스 case · include 묶음.
- **콜드 · 워크트리용 프리셋 `Ninja-Debug-Cold`(PCH 끔 + 유니티)**(BuildSpeed 4 단계, 2026-10-11, PC `SW`, 풀 빌드 `--clean-first`): 캐시 없이 115.2 s(개발) 대 147.3 s,
  어느 구성도 이기지 못한다. PCH + 유니티는 145.2 s 로 개발보다 느리다. [측정: PC `SW` · 풀 빌드 `--clean-first` · 2026-10-11 · 1fed3f45f]
- **`Engine.dll` 을 층 DLL 로 쪼개 증분 빌드를 줄이는 안**(BuildSpeed 9, 2026-10-16, PC `SW`, Debug): `.cpp` 하나를 고친 증분 13.9 s 중 `Engine.dll` 링크는 1.9 s, 컴파일 1.6 s 였고 나머지 약 10 s 는
  임포트 라이브러리 수정 시각 때문에 킷 · 게임 DLL 84 개가 다시 링크된 것이었다(`sw_keepImportLibraryIfUnchanged` 로 해결, 5 s). 헤더를 고친 비용은 DLL 경계가 아니라 PCH 에 든 헤더가 정하므로
  (`GameObject.h` 가 TU 약 2,200 개) 분할로 줄지 않는다(언리얼도 헤더를 모듈 공개 헤더로 좁혀 팬아웃을 줄이는 쪽이 본체로 알고 있다). 링크 단위 분할은 엔진 분할 계획 3 단계의 다른 이유가 생길 때만 한다. [측정: PC `SW` · Debug, `.cpp` 하나 증분 · 2026-10-16 · 04024dda7]
- **킷 밖 묶음의 PCH 공유**(BuildSpeed 3 단계, 2026-10-11): PCH 를 따로 짓는 것은 테스트 9 · RHI 백엔드 4(`<타깃>_EXPORTS` 만 다름) · App · Server · ModuleHost 3 · 에디터 확장 · `SWGame` 이다.
  PCH 하나가 약 2.7 CPU 초라 다 합쳐도 16 스레드에서 2 ~ 3 s(측정 편차 ±3 s 안) — 킷(PCH 60 개 → 1, −20.6 s)만 했다. [측정: 기계 미상 · 16 스레드 · 2026-10-11 · 65ee88bb4]
- **ReflectionParser 강제 include PCH**(2026-10-06): Debug `ReflectionParserTest` 35 케이스 25.8 초(여섯 조각, 조각당 약 4.3 초 · 한도 30 초), 파서 한 번
  1.1~1.3 초 중 공통 include 0.5 초 — 조각당 약 2 초를 위해 depfile 의존(PCH 안 헤더를 `clang_getInclusions` 가 내는가) 재검증을 떠안지 않는다. [측정: 기계 미상 · Debug `ReflectionParserTest` 35 케이스 · 2026-10-06 · 529e83933(첫 기록 a55a90f0b, 2026-10-03)]
- **롤백에 파괴 상태 싣기**(사용자 결정 2026-10-06): 롤백 키트(격투)에 파괴물이 없고 조각 물리(Jolt)는 되감지 못한다(Chaos GC 도 롤백 없음). 롤백 게임이
  파괴물을 쓰면 `saveState` 에 `makeNetworkSnapshot` 을 싣고(바뀐 사건 수일 때만) `loadState` 가 즉시 적용하는 창구를 더한다.
- **훅의 `CheckHeaderSelfContained` 를 하위 프로세스로 띄우기**(`preCommitRunsInBackground` 같은 선언, 2026-10-10): 파일 단위 게이트 중 시간을 내는 것은 이것 하나(헤더 10 개 ~2 s 벽)이고,
  트리 전체 게이트(`CheckConfigReference` 5~14 s)는 이미 먼저 띄워져 있어 그 시간 안에 겹친다. 홀로 남는 커밋에서는 겹칠 상대가 파일 단위 게이트들(합 <0.5 s)뿐이고 기동 0.2 s 를 낸다. 넣지 않았다. [측정: 기계 미상 · 헤더 10 개 · 2026-10-10 · 4304eb03f]
- **GPUScene 배치 키 중복 계산 제거**(투명 배치 헤드 키 재계산, 인스턴스마다 셰이더 경로 해시): `GT.GPUScene.build` 3670 → 3761 us 로 차이가 없었습니다. [측정: 기계 미상 · 규모 미상 · 날짜 미상 · 커밋 미상 — 첫 기록 e6973186b, 2026-09-09]
- **`CheckConfigReference` 를 파일 단위(바뀐 구조체만)로 나누기**(2026-10-11, Scripts 개편 1 단계 뒤). 커밋 훅의 바닥은 트리 전체 게이트 여섯이 각자 `Source/` 를 읽는 값(조용할 때 1 ~ 1.4 s)이 만듭니다.
  단독 실행 중앙값(5 회): `CheckConfigReference` 1.49 → 0.99 s, `CheckDuplicateTypeNames` 1.35 → 1.03 s, `CheckEngineLayers` 2.00 → 1.09 s, `CheckModuleTargets` 1.44 → 1.21 s, 커밋 훅(`REFLECT` 헤더 하나) 2.31 → 1.92 s.
  하나만 0 으로 만들어도 나머지 다섯이 같은 높이로 남습니다. 더 내리려면 한 프로세스가 읽고 여럿이 훑는 구조가 필요한데 파이썬 GIL 때문에 지금의 동시 실행을 잃습니다.
  파일마다 보는 소스 게이트는 그 구조로 옮겼습니다(2026-10-17, `SourceScan` — 트리를 한 번 걷고 파일을 코어 수만큼 프로세스에 나눠 읽고 가린 뒤 게이트 전부가 훑는다).
  트리 전체 대조 게이트 중 무거운 `CheckNameReferences` · `CheckKitNamespaces` 는 커밋 훅에서 빼 CI 린트 잡으로 넘겼습니다([검증과 측정](08_Verification.md) 4절). [측정: 기계 미상 · 단독 실행 5 회 중앙값 · 2026-10-11 · 7a5fe1016(문서 이동 커밋 — 값을 처음 적은 커밋은 미상)]
- **비소스 린트 게이트(Python · CMake · 문서 · 텍스트)의 공유 훑기**(2026-10-25): 읽기가 값이 아니다. 단독 순차 실행 46 린트 합 161.7 s 중 파일을 읽는 비소스 게이트 열둘(`CheckPythonConventions` 1.95 ·
  `CheckPythonMinimumVersion` 3.02 · `CheckScriptLayers` 1.0 · `CheckScriptLayout` 0.46 · `CheckScriptEntryPoints` 1.53 · `CheckScriptCommonHelpers` 0.41 · `CheckCmakeConventions` 0.43 · `CheckCmakeReadme` 0.34 ·
  `CheckTextFilesAreText` 1.5 · `CheckExecutableBits` 3.49 · `CheckDocPaths` 0.77 · `CheckStatusBoard` 0.30 s)의 합이 15.2 s, 이 가운데 프로세스 기동 바닥(가장 빠른 린트 0.18 s)이 2.2 s 다. 저장소 전체 걷기 0.4 s ·
  파이썬 303 개 읽기 0.03 s · 파이썬 `ast.parse` 0.54 s · 문서와 CMake 404 개 읽기 0.21 s 라, 전부 공유해도 줄어드는 것은 CPU 합 3~4 s(동시 실행 벽시계로는 1 s 안쪽)다. 남는 값은 게이트 자신의 일이다 —
  `ast.walk` 두 번(`CheckPythonConventions` · `CheckPythonMinimumVersion`), 파일 23,500 개의 머리 바이트 열기(`CheckExecutableBits`), 파일 6,300 개의 NUL 바이트 읽기(`CheckTextFilesAreText`) — 파일 집합이 서로 달라 공유할 수도 없다.
  동시 실행(`lint-suite`, 코어의 절반 8 개)에서 게이트가 5~60 s 로 보이는 것은 경합이다(같은 `CheckCmakeConventions` 가 단독 0.43 s, 동시 12.5 s). 덩어리는 다른 곳이다 — `CheckVerifyPending` 단독 52 s
  (`VerifyManifest.findTestCaseInternal` 이 테스트 케이스 169 개마다 `Test/` 전체를 이은 글에 `\b…\s*,\s*…\b` 정규식을 돌린다, 프로파일에서 24.4 s 가 `re.search`) 하나가 합의 3분의 1이다. 구현하지 않고
  그 정규식을 먼저 본다. [측정: 이 PC · 단독 순차 1 회 · 다른 작업자와 겹침 · 2026-10-25 · 233d5ef35]

## 4. 옛 이름 → 지금 이름 (`git log` 을 읽을 때)

이름만 바뀐 것은 적지 않습니다. 별칭을 두지 않으므로(5-4) 코드에 옛 이름이 남지 않고, `git log -S '<옛 이름>'`(커밋 메시지 속 용어는 `git log --grep`)으로 찾습니다.
여기에는 뜻이 바뀌었거나, 여러 곳으로 나뉘었거나, 개념이 없어져 `git log` 만으로는 이유가 보이지 않는 것만 둡니다.

| 옛 이름 | 지금 이름 | 남긴 이유 |
|---|---|---|
| `EngineLoop` · `_engineLoop` | `Engine` · `_engine`(네임스페이스 `engine` 은 그대로 — 5-3) | `Engine` 이 이제 클래스 이름이다 |
| Overworld `PlayerController` | `OverworldTileMover` | `PlayerController` 는 조종자(`PlayerControllerComponent`)의 뜻으로 쓴다. 의도를 받는 몸은 조종자가 아니다 |
| `RtsAiController` · `SrpgAiController` | `RTSCommanderAI` · `SRPGCommanderAI` | 명령형 장르의 AI 는 폰을 조종하지 않는다 |
| `InputSnapshot` · `InputHistoryBuffer` · `InputReplay::play` | 삭제 — 행동 층은 `ControlIntent` · `ControlIntentHistory`, 재생은 `InputManager::attachVirtualInput`(가상 입력 원천) | 입력 기록 개념이 없어졌다 |
| `GameStateComponent` 등 `GameState*` | `SessionState*` | 언리얼의 GameState 와 뜻이 다르다 |
| 지연 로드(`bindDelayLoadImports` · `DelayLoad.cmake` · `CheckDelayLoadSites`) | 없음 — `ModuleShadowCopy::writeShadowCopy`, 게이트 `CheckNoDelayLoad` | 개념을 제거했다 |
| `FileUtil::fileExists` · `directoryExists` | `exists`(파일 · 폴더 모두) · `isDirectory` · `isRegularFile`(파일만) | 같은 질문의 뜻이 바뀌었다 |
| `transformNormal` | `transformVector`(방향 변환) | 법선 변환(역전치)이 아니다 |
| `EditorThemePreset` · `editor::style::kOk` 등 색 상수 | 토큰 파일 `Resource/editor/theme/<이름>.tokens.json` · `EditorThemeUtil::get*Color()` | 테마가 코드 열거에서 데이터로 옮겨 갔다 |
| `Engine/Object/Animation/` | `Engine/Object/SceneAnimation/` | 애니메이션 데이터 폴더 `Engine/Animation/` 과 구분한다 |
| `Engine/DevTools/` | `Editor/Common/Localization/` · `Engine/Input/` · 명령마다 소유 코드 옆(`*DevCommands.cpp` 등) | 여러 곳으로 나뉘었다 |
| `PhysicsTypes.h` 의 핸들 | `Engine/Common/PhysicsHandle.h` | 컴포넌트 모델 코어가 Physics 를 include 하지 않게 하려는 것이다 |
| `initialize` 설정 `*Settings` · 카탈로그 `*Settings` | 서비스 · 호스트 설정은 `*Config`, 카탈로그 항목은 `*Def`(C-26 ③) | 한 이름이 뜻에 따라 둘로 나뉘었다 |

일부러 둔 용어: stamp · kit · cook · orphan · chord · pin.

## 5. 영역별 결정

결정마다 결정한 날, 근거, 상용 엔진의 방식을 적습니다. 코드와 헤더 주석에 반영된 결정은 한두 문장으로만 적고, 자세한 계약은 그 코드의 헤더 주석에 있습니다.
결정을 바꾸려면 여기 적힌 근거와 상용 엔진의 방식을 먼저 확인하고, 바꾼 결정은 같은 커밋에서 그 영역 파일을 고칩니다.

**새 결정은 아래 영역 파일 가운데 맞는 하나의 끝에 한 줄로 더합니다.** 병렬 작업자가 서로 다른 영역 파일을 고치면 병합이 충돌하지 않습니다. 영역 번호(`5-4` 등)와 제목은 이 문서가 정하고, 본문은 `decisions/5-<번호>_<이름>.md` 에 있습니다. 한 영역을 읽는 명령은 `py -3 -m Scripts doc 09_Decisions --section 5-4` 입니다(그 영역 파일 전체를 보입니다). 맞는 영역이 없으면 가장 가까운 파일에 더하고 새 영역 파일은 만들지 않습니다.

### 5-1. 문서와 저장소

[5-1_DocsRepo.md](decisions/5-1_DocsRepo.md) — 7 KB.

### 5-2. 빌드, 스크립트, CI

[5-2_Build.md](decisions/5-2_Build.md) — 48 KB.

### 5-3. Core 와 플랫폼

[5-3_CorePlatform.md](decisions/5-3_CorePlatform.md) — 40 KB.

### 5-4. 데이터, 직렬화, 설정

[5-4_DataConfig.md](decisions/5-4_DataConfig.md) — 43 KB.

### 5-5. 그래픽스와 RHI

[5-5_GraphicsRHI.md](decisions/5-5_GraphicsRHI.md) — 38 KB.

### 5-6. 오브젝트, 조종, 입력

[5-6_ObjectsInput.md](decisions/5-6_ObjectsInput.md) — 27 KB.

### 5-7. 런타임 UI 와 글자

[5-7_RuntimeUI.md](decisions/5-7_RuntimeUI.md) — 12 KB.

### 5-8. 게임프레임워크와 키트

[5-8_GameFramework.md](decisions/5-8_GameFramework.md) — 13 KB.

### 5-9. 온라인 서비스, 네트워크, 보안

[5-9_OnlineNet.md](decisions/5-9_OnlineNet.md) — 37 KB.

### 5-10. 전용 서버

[5-10_Server.md](decisions/5-10_Server.md) — 16 KB.

### 5-11. 에디터와 외부 공개

[5-11_Editor.md](decisions/5-11_Editor.md) — 45 KB.

### 5-12. 기능 대기열 결정 (2026-10-17)

[5-12_FeatureQueue.md](decisions/5-12_FeatureQueue.md) — 42 KB.
