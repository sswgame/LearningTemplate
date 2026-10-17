# 결정 기록

이 저장소에서 정한 방향과, 하지 않기로 한 것과, 측정으로 기각한 것을 모은 문서입니다. 같은 제안이 다시 나왔을 때 그때의 근거와 측정값을 찾는 곳입니다.
다시 볼 조건이 붙은 결정은 그 조건이 오면 다시 엽니다. 아직 남은 일은 [백로그](backlog/README.md) 에 있습니다.

## 1. 작업 방향

- **쪼개기보다 공통부 추출.** 긴 함수를 나누면 코드가 옮겨 갈 뿐 총량은 그대로다. 중복은 증상이고 원인은 "매번 다시 만들어야
  하는 구조" 다. 원인을 없앤다.
- **추가 · 변경이 쉬운 구조를 먼저 만든다.** 판단 기준은 "하나 더하려면 몇 곳을 고쳐야 하는가" 다. 복사해야 할 것이 남아 있으면 그
  자리가 다음 리팩터 대상이다. 목록이 여럿이면 한쪽만 늘어난다 — 목록은 하나(표 · X-macro · 폴더 열거)로 둔다.
  여러 파일이 같은 enum 을 `switch` 하는 자리를 셀 때는 "백엔드 · 스테이지마다 정말 다른 일을 하는가" 를 먼저 묻는다(그러면 정당하다).
- **한 곳에 넣은 고침은 형제에게도 넣는다.** 지금까지 가장 잦았던 결함 모양이다(한 함수만 상한을 안 봄 · 셋 중 하나만 폴백 없음).
  고칠 때 같은 일을 하는 형제를 grep 으로 찾는다. 원인이 같은 결함이 여럿이면 그 원인을 구조(한 창구 · 단언 · 게이트)로 막는다.
- **실패는 조용하면 안 된다.** 버린 값 · 못 읽은 칸 · 못 만든 폴더는 그 자리에서 무엇이 왜인지 알린다. 실패할 수 있는 bool 은
  `[[nodiscard]]` 이고, 일부러 버릴 때는 `(void)호출(); // 이유` — 실패 가능 동사(load · read · apply …)의 결과만 이유가 필수다(`CheckDiscardReason`). 그 밖의 `(void)` 는 이유를 요구하지 않는다.
- **상용 엔진의 같은 자리와 견준다.** 새 구조를 정할 때 언리얼 · 유니티 · Godot 의 같은 자리를 먼저 보고 그 모양을 따르되, 일부러
  다르게 한 것은 이유를 적는다(예: `hashed_string` 은 FName 규칙이지만 숫자 꼬리는 없다).
- **성능 주장은 Release 숫자로만 한다.** 숫자 없는 최적화는 하지 않는다. 이전 · 이후를 같은 조건에서 번갈아 재고, 평균이 아니라
  p50 · p99 · 최악 프레임을 본다([검증과 측정](08_Verification.md) 2절 참고).
- **주석 · 커밋 메시지는 한국어.** 문서 주석(`/** */` · `///<`)은 "~합니다" 체, 함수 본문 `//` 주석은 "~다" 체로 한 블록 안에서 통일한다.
  직역어 대신 표준 용어(thundering herd · 락 컨보이 · 브로드캐스트 · 조인 · continuation · 오버플로 · 분기)를 쓴다. 식별자 · 로그 ·
  assert 문자열 · `NOLINT` 줄은 그대로 둔다. 주석을 고치기 전에 구현을 읽는다 — 복사해 붙인 설명이 실제 동작과 다른 곳이 많았다.
  규칙은 [AGENTS.md](../AGENTS.md) 와 [04_CodingGuidelines.md](04_CodingGuidelines.md).
- **자산 처리 용어는 넷이다(상용 엔진 기준).** Cook = 배포 · 실행용 플랫폼 데이터로 바꾸기(셰이더 · 씬 · 프리팹 · 팩, `--cook-*`), Import = 원본 → 엔진 형식(텍스처, `--import-textures`), Generate = 빌드가 만드는 코드 · 헤더, Bake = 미리 계산한 결과(라이트맵 · 내비 — 지금은 없다). 한국어 "굽다" 는 쓰지 않는다. 이름을 바꿀 때 별칭(`Alias` · 옛 CLI 철자 · 옛 형식 리더)을 두지 않고 데이터를 다시 쓴다.
- **새 기능 로드맵보다 백로그의 결함 · 정리 · 성능 · 테스트 · CI 항목을 먼저 닫습니다**(2026-10-06, 사용자 결정). 온라인 서비스 키트는 함께 진행합니다. 보류로 정한 것(`hashed_string` 숫자 꼬리, CC0 리소스 배치와 MechArena)은 그대로 둡니다.

## 2. 안 하기로 한 것 (다시 제안하지 말 것)

- **PIX 캡처(DX12 전용 — WinPixGpuCapturer.dll)는 하지 않았다**(2026-10-10) — RenderDoc in-app API(`RenderDocCapture`, `-renderdoc`)가 네 백엔드를 덮는다(언리얼 RenderDoc 플러그인 · 유니티 Frame Capture 자리). DX12 타이밍 분석이 필요해지면 다시 본다.
- **지금 하지 않는 구조 후보 — 다시 볼 조건과 함께**(2026-10-03 상용 엔진 비교로 결정): 트랜스폼 SoA 2 단계(UE 액터도 AoS, 측정 근거가 생기면) ·
  선행 조건 스케줄러(시스템이 서로의 결과에 기대기 시작하면 — UE `AddTickPrerequisite` 모양. 2026-10-06 키트 조립 점검: 키트 디렉터 사이 순서는 한 오브젝트에
  붙인 순서가 주고 — `TickRegistry` 가 한 오브젝트의 항목을 한 워커가 붙은 순서로 돌린다 — 다른 오브젝트 사이는 디렉터의 `_tickAfter`(규칙 서브틱 + 선행 조건),
  키트 코드끼리의 의존은 0 곳이라 스케줄러의 근거가 아직 없다) · 에셋 로더 등록제(종류가 대여섯이 되면 — UE `UFactory`) ·
  참조 카운트 RHI 핸들(한 리소스를 여럿이 나눠 들기 시작하면 — UE `TRefCountPtr`) · Mesh/Material `SlotHandle`(하지 않는다 — `shared_ptr` 이 수명과 RT 안전을 한 번에
  준다) · `ResourceUtil` 소유 객체화(하지 않는다 — UE `FPaths` 도 정적) · 링크 단위 분할(증분 링크 시간이 문제가 되면 별도 PR) — GameFramework 기반(약 58k 줄)을 `GFS_*` DLL 여럿으로 쪼개는 안도 같은 이유로 하지 않는다(2026-10-05 재확인). 기반 폴더의
  층은 폴더 층 게이트(`CheckGameFrameworkLayers`)로 지킨다. · `UnitStatsComponent` 를 기반(`Combat/`)으로(2026-10-05 — 쓰는 게임 · 씬 · 프리팹 0, 옮기면 데미지 숫자(UI 층 4) · `MonsterDef` · `DamageAppliedEvent` 셋을 끊어야 층이 맞는다. 체력 읽기는 `Combat/HealthSourceComponent` 가 맡는다. 다시 볼 조건: 다른 키트 · 게임이 피해 입구(`takeDamage` · 무적 · `DamageAppliedEvent`)를 이 키트 없이 쓰려 할 때 — 그때는 `DamageAppliedEvent` 도 기반으로, `setStats( MonsterDef )` 는 키트의 도우미로, 데미지 숫자는 `HealthListenerComponent` 파생으로 내리고, `CheckGameFrameworkLayers` 자가 시험의 키트 헤더 예를 바꾸고, `generated/GF_ActionCombat/UnitStatsComponent.gen.*` 을 지운 뒤 re-configure) · 피해 입구 인터페이스(19 곳 — 인자 모양이 넷이다. 키트 투사체 · 공격 판정을 다른 체력 모델(어빌리티 시스템 · 게임 컴포넌트)에 쓰는 게임이 생기면 `Combat/` 에 `takeDamage( 양, 쏜 쪽 )` 하나를 `HealthSourceComponent` 옆에 — 언리얼 `AActor::TakeDamage`)
  · 키트 아이템/효과 처리기 등록부(2026-10-05 전수: enum switch 4 곳 · 19 case, 두 키트가 나누는 종류 enum 0 — 키트마다 하는 일이 다르고 `-Werror=switch` 를
  잃는다. 데이터로 효과를 더할 게임은 기반 GAS 의 `registerExecutionClass` — 언리얼 GameplayEffect 자리 — 를 쓴다. 두 키트가 같은 종류 집합을 나누게 되면 다시 본다) ·
  `TimedModifierSet`(시간 제한 수정자는 이름 붙은 `Countdown` 슬롯이고 쌓기는 `extendTo` · `start` 로 이미 정해져 있다, 목록형은 위쳐 물약 한 곳 — 일반형은 GAS 의
  지속 이펙트 · 스택 정책. GAS 밖 키트 둘 이상에 목록형이 생기면 다시 본다) · API 통합 남은 판단(다음 훑기).

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
    `IRHIDevice` 의 `supportsNativeBindlessSampling` · `applyVSyncInternal`, `FractureComponentBase` 의 그룹 재구성 · 물리 어댑터(S15, 반쪽 이전), `FrameRenderer` 의 트랜지언트 계획(S16),
    `Engine::update` 의 백엔드 교체 · `pollShaderReloadHotkey` · `MaterialCache::initializePending` · 관찰자 루프(S13, 수명 계약 · 순서가 뜻).
  - S16 의 **동작 변화**: 엔진 서비스가 묶이지 않은 실행은 GPU 계측이 꺼집니다(전에는 단언).
- **폴더 · 파일 구조는 기계적으로 합치지 않습니다**(2026-10-12 감사, R8). 25 줄 이하 파일이 97 / 2,932 개이고 큰 클래스를 나눈 가족 대부분이 파일마다 120 줄을 넘으며, 작은 조각(`EditorAssetTypeFocus.cpp` 36 줄)은 의존을 분리한 것입니다.
  합치면 층 규칙(`CheckEngineLayers`)과 그래픽스 구조 재편의 파일 분할 기준을 해칩니다. 한다면 `Scripts/lint/report/` 에 같은 접두 `.cpp` 가족과 줄 수를 보고하는 스크립트를 두고,
  120 줄 이하 조각이 의존 분리가 아닌 가족만 사람이 봅니다(지금 후보는 `RenderView.cpp` 13 줄 정도).
- **파일이 40 개를 넘는 평면 폴더는 기능 묶음 하위 폴더로 나누고, 한 클래스를 나눈 파일 가족은 가르지 않습니다**(2026-10-15). 언리얼 모듈의 `Private/` 하위 폴더,
  유니티 `Runtime/` 하위, Godot `scene/`(`2d` · `3d` · `main` · `resources` · `gui`)가 기능 묶음으로 나누는 것과 같은 단위로 알고 있습니다.
  `Editor/Common/Commands` 는 `Asset` · `Scene` · `Play` · `Dev`(등록부 · 백그라운드 입출력은 루트), `Engine/Object/GameObject` 는 `Registry` · `Tick` · `Service` · `Serialization`
  (`GameObject` · `GameObjectManager` · `GameObjectStore` 는 루트), `Engine/Renderer/Frame` 은 `View` · `Packet` · `Resource` · `Capture` · `Timeline`(`FrameRenderer*` 가족은 루트)입니다.
  층 규칙은 그대로 따라갑니다. 에디터 층은 `Common/<폴더>` 단위로 읽고, 오브젝트 모델 코어 규칙은 `GameObject` 트리 전체(`CheckEngineLayers.toml` 의 `coreTree`)입니다.
- **스크립팅 언어 층(Lua · C# · 비주얼 스크립팅)은 두지 않는다**(2026-10-17, 사용자 결정). 게임 로직은 C++ 모듈과 핫리로드로 짠다. 비프로그래머용 층이 필요해지는 게임이 생기면 다시 연다. 백로그 `D4-9` 의 비주얼 스크립팅은 순위 목록에만 둔다.

## 3. 기각한 것 — 측정값과 함께 (다시 제안하지 말 것)

- **`runParallelByCost` 를 모든 병렬 호출처로 넓히기**(2026-10-16): 트랜스폼 계층의 고정 임계값(쓰기 1024 · 플러시 2048)과 GPU 씬(2048 / 4096)은 손익분기에 맞춘 값이라, 트랜스폼 세 곳을 바꿔 재니 쓰기 적용은 61 ~ 66 → 64 ~ 67 us 로 같고 틱 8000 은 86 ~ 99 → 105 ~ 107 us 로 조금 느려져(앞 항목마다 본문을 여러 번 부르는 비용) 되돌렸다. 항목당 비용이 크고 개수가 달라지는 곳에만 쓴다 — 씬 틱(작은 씬)과 애니메이션 평가(아래).

- **`common` 지연 import(PEP 562 `__getattr__`)**(2026-10-06, 12 회 중앙값): `import common` 누적 81.0 → 1.8 ms 이지만 `GenerateCookContract.py` 한 번이 110.5 → 110.1 ms — 생성기는 결국 같은 하위 모듈(GeneratedFile · CookContract · 그 앞의 탐색 순서)을 다 올린다.
  기준(생성기 30 % 이상)에 한참 못 미치고, 하위 모듈 이름과 같은 공개 이름(`BuildTree`)이 하위 모듈 import 순서에 따라 모듈로 바뀌는 함정이 생긴다.
- **N18c 메시지 버퍼 풀**(2026-10-06, Release `NetReplicationBenchTest` 16 × 1000, 3 회): N18b 뒤 틱당 할당 1133 중 서버 복제 544 — "전체 − 서버 복제" 589 < 문턱 1 000.
  (N18b 전 25182 · 서버 복제 16886, ReplicationServer tick p50 5.4 → 3.5 ms.)
- **TaskManager 후보 셋**(Release, 큐브 8000, 16 스레드, 5~8 회 번갈아): 스핀 워커 2 개 제한 + 적응 예산(GT 839→846, RT.ExecutePacket 395→436 us), 워커별 노드
  자유 목록(GT 839→917, RT.Graph 187→237 us), 워커 지정 틱 쓰기 적용(queued 78→80 us, p99 163→212 us). 셋 다 손해 또는 잡음.

- **사용자 결정**: mimalloc · SIMD 수학 · 프리페치는 쓰지 않는다. 사용처가 0 이어도 상용 엔진에 대응이 있는 공개 API 는 남긴다(남긴 기능에는 시험을 붙인다).
  - SIMD 수학을 미룬 까닭은 성능 근거가 아니라 플랫폼 파편화 우려입니다(사용자 확인, 2026-10-17). 다시 볼 조건: 프로파일에서 수학이 병목인 핫 루프가 나올 때, 또는 지원 플랫폼 목록이 확정될 때. 다시 볼 때의 후보 방식(제안, 미결정): 한 추상 계층 뒤에 SSE2 · NEON · 스칼라 백엔드를 컴파일 때 고르고, 스칼라 백엔드를 항상 빌드해 테스트하며, 일반 `Vec3` 형식이 아니라 배치 커널(변환 · 컬링 · 스키닝)에만 적용합니다.
- **GPUScene · 렌더**: `DrawCandidate` SoA(구조체를 78 % 키워도 불변, 거의 선형 — 예전의 "28~62 배" 는 GT 가 GPU 를 기다린 착시), 발행 배열 풀(75 vs 76~85 us), 영속 렌더 씬을 **종류** 축으로
  (8 → 1024 에 평평), 인스턴스 채우기 병렬화(모든 크기에서 인라인에 짐), raw 페이로드 워커 쓰기 · `MeshInstanceBatch::updateParallel`, 배치 정렬(잡음), DX12 루트 상수 드로우 ID 주입
  (ExecuteIndirect 2 배 느림 — 인스턴스 슬롯 스트림이 대안), 클리어 `DontCare`(0 us — 타일 GPU 로 가면 다시), 점 샘플러(122 → 121), 머티리얼 CB 워커화(6 us), PSO 병렬 생성(9 %, 드라이버가 직렬),
  `executeCommandLists` 일괄(리스트 수 그대로), 꼬리 재방출 그룹 캐시(98 → 98).
  DX12 온라인 블록 잠금의 워커별 목록(잠금이 드는 씬 배치 기록 전체가 p50 36~65 us = RT 프레임의 2~4 %, 큐브 8000 · 도형 5 · 디퍼드).
  `GPUInstance` 128 → 96 B(모든 인스턴스가 매 프레임 올라가는 최악에서도 증분 상한이 RT ~57 · GT ~65 us, 월드 float3x4 · 블렌드 비트 묶기는 모든 셰이더 · 계약 시험을 건드린다).
  추가 뷰 사이 그림자 공유(2026-10-06, Release 큐브 8000 + 바닥, `-gv_benchViews=4` — 512² 캡처 카메라 넷, 번갈아 4 쌍): 추가 뷰의 그림자 패스를 빼도 `GPU.Frame` 이
  쌍마다 256 · 390 · −47 · 169 us 줄 뿐(뷰당 ~50 us, 추가 뷰 넷이 GPU 프레임을 0.65 → 1.8~2.1 ms 로 늘리는 몫의 ~15 %). 프레임 순서를 바꾸는 구조 변경에 비해 작다.
- **히치**: 펜스 시그널을 Present 앞으로 · 백버퍼 수 · 인라인/즉시 제출 — 분포가 그대로였다. 어댑터 강제 선택은 A/B 로 악화.
  2026-10-06 재측정(Release, 큐브 100 · VSync 끔, 제안서 측정 3 회씩): 꼬리(p99 2.4~4.2 ms · 최악 12~37 ms)는 DX11 에도 같은 크기로 있고(DX11 은 Present 안, DX12 · Vulkan 은
  BeginFrame 펜스) Present 호출 자체는 p99 0.2 ms — GPU 백프레셔다. VSync 켬은 DXGI 가 판당 많아야 한 주기를 놓친다. 대기 가능 스왑체인으로 고칠 히치가 없다.
  Vulkan 스왑체인 이미지를 4 개로(+1) 늘려도 VSync 주기 놓침이 줄지 않았다(12 판 중 6 판 최악 13~27 ms — 3 개는 22 판 중 2 판).
- **틱 · 오브젝트**: 인라인 `TickItem`(GameObject 192 → 232 B), 적용 단계를 틱에 합치기(칸당 +124 B), 회전 사원수 캐시(+28 B), 축별 sin/cos 건너뛰기(18.0 → 18.7 ns), 쓰기 정렬의
  `id % 버킷`(150 → 450 us) · 키를 건에 넣기(64 → 80 B), 오브젝트 id 표 2 단 디렉터리(1 억 스폰이면 800 MB) · 늘 견주기(5.6 → 6.4 ns), 엔티티당 XML 재파싱 구조 변경(2 ms 뿐).
- **패딩 재배치 나머지**(2026-10-06, `RunPaddingReport --preset Ninja-Release --min-saving 8` 69 개): GameObject(168 → 160, 풀 칸 176 → 160)만 했다. 나머지는 인스턴스가
  적거나 나눠 쓴다 — Material · MaterialInstance 16 B(캐시 · `MaterialTintCache` · 스프라이트 공유, 수백 개 이하), Mesh 8 B(`MeshCache` 공유), 컴포넌트(RigidBody 16 · SkeletalMesh 16 ·
  Camera 8 — PROPERTY 순서 고정), 싱글턴(InputManager 64 · FrameProfiler 8). 수천 개가 만들어지는 타입이 나오면 그것만 한다. 위치 초기화 표(EditorAssetTypeInfo ·
  AssetMatchRow · CommandRow)는 모든 행을 같이 바꿔야 한다.
- **전역 소형 블록 할당자**(2026-10-06): Release 프레임당 sw 할당 7.7 회(120 프레임 × 3) — 회당 ~100 ns 로 잡아도 프레임당 1 us 아래. 기동(Release 856~1207 ms)의
  sw 할당은 Debug 기준 상주 블록 8 만 개 — 회당 100 ns 면 8 ms(1 % 아래)라 절반을 줄여도 기동의 0.5 %. 프레임당 할당이 다시 늘거나 큰 맵 로드의 프로파일에서
  할당이 5 % 를 넘으면 다시 잰다.
- **파일 시스템을 플랫폼 API 로 다시 짜기**(2026-10-06, Release 스크래치 · 2920 파일): 존재 확인 p50 10.9 vs 10.3 us · 순회 6.5~9.4 vs 7.6~10.0 ms — 시간은 같고
  할당만 준다(존재 확인 1 → 0, 순회 항목당 3.1 → 1.0). 그 할당은 기동 때만이고 상주가 아니다. `Core/File/Std` 로 감싼 뒤(Debug `FileUtilBenchTest`): 존재 확인 호출당 sw 할당자 밖 2 · sw 4
  (구분자 정규화 · UTF-16 변환 문자열), 순회 파일당 밖 2.3 · sw 6.5 — 감싸기 전 밖 1 · 순회 3.1(스크래치). 줄이려면 `toPath` 를 스택 버퍼 변환으로(sw 0).
  다시 재는 법: App Debug `-gv_memoryReport=1` 의 "cumulative since start" 줄 · `FileUtilBenchTest`. `std::filesystem` 은 `Core/File/Std` 뒤에 감췄으니 한 경로가 프로파일에 보이면 그 함수만
  `File/Windows` · `File/Linux` 로 옮긴다(기준: Release 기동이나 프레임에서 1 ms 이상 · 프레임마다 부르는 경로).
- **물리**: 파괴 잎 볼록 껍질을 워커로 나눠 짓기(Jolt 잡 시스템, 2026-10-06) — `FractureBenchTest` p50 20.7 → 24.4 ms 로 느려졌다. 워커 15 개가 청크(잎 13 개)마다
  ~10 ms(잎당 ~750 us — 직렬 55 us 의 14 배)를 썼다: 껍질 짓기는 할당이 많고 Jolt 할당이 엔진 `Memory::allocate` 로 가서 경합한다. 할당을 풀어 주지 않는 한 다시 하지 말 것.
  주의: Jolt 백엔드 TU(`Physics/Jolt/*.cpp`, PCH 없이 Jolt 정의 · 대상 기능으로 컴파일)에 `Engine/Common/EngineParallel.h` 를 넣자 물리 전체가 깨졌다(질량 · 자세가 쓰레기) —
  그 TU 에서는 엔진 태스크 대신 이미 쓰는 Jolt 잡 시스템(`JoltJobSystem`)을 쓴다.
- **태스크**: 워커 스핀 늘리기(2 → 50 us 면 SMT 형제를 빼앗아 GT 889 → 1305 us), hot pool(이득 없음), 스레드별 목록 머리 패딩(잡음), 공유 풀 위 GT 병렬화를 레인 없이(RT 170 → 261 us),
  워커 깨우기 사슬, `TaskArgs` 인라인 4 칸(기록 122 → 196 us), `PagedArray` 통합의 첫 측정(번갈아 재니 차이 없음 — 측정 착시).
- **도구 · 빌드**: 린트를 파일마다 프로세스로(5.3 → 9.4 s — 덩어리 프로세스는 7.5 → 2.25 s), 짝 헤더 메모이즈(차이 없음), `formatstring` 비템플릿 부분
  `.cpp` 분리(41 → 44 s) · 타입 소거 배열, `ContainerTypeMap` 선형 탐색 개선(파서 시간은 libclang), 팩 코덱 Zstd(−0.1 %) · LZ4(+28 %) — 이미 압축된 자산이라 Zlib 유지(압축 안 된 자산이
  들어오면 다시 잰다), 린트 파일 공유 캐시(C++ 2500 개 28.7 MB 를 다 읽어도 0.15–0.7 s — 게이트는 정규식에 쓴다), 린트 CTest 를 한 프로세스로(기동 0.17 s × 항목 수 —
  항목별 보고 · TIMEOUT 을 잃는다), 린트 증분 해시 캐시(훅 바닥 ~2–4 s 에 교차 파일 규칙 재작성 — 보류), `flatMapInProcesses` 16 → 8 워커(혼자 돌면 3.3 → 3.7 s 로
  지고 CPU −40 % — `ctest -j` 겹침에서 재 볼 것).
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
  어느 구성도 이기지 못한다. PCH + 유니티는 145.2 s 로 개발보다 느리다.
- **`Engine.dll` 을 층 DLL 로 쪼개 증분 빌드를 줄이는 안**(BuildSpeed 9, 2026-10-16, PC `SW`, Debug): `.cpp` 하나를 고친 증분 13.9 s 중 `Engine.dll` 링크는 1.9 s, 컴파일 1.6 s 였고 나머지 약 10 s 는
  임포트 라이브러리 수정 시각 때문에 킷 · 게임 DLL 84 개가 다시 링크된 것이었다(`sw_keepImportLibraryIfUnchanged` 로 해결, 5 s). 헤더를 고친 비용은 DLL 경계가 아니라 PCH 에 든 헤더가 정하므로
  (`GameObject.h` 가 TU 약 2,200 개) 분할로 줄지 않는다. 언리얼은 모듈 DLL 을 쪼개되 헤더는 모듈 공개 헤더로 좁혀 팬아웃을 줄이는 쪽이 본체로 알고 있다. 링크 단위 분할은 엔진 분할 계획 3 단계의 다른 이유가 생길 때만 한다.
- **킷 밖 묶음의 PCH 공유**(BuildSpeed 3 단계, 2026-10-11): 컴파일 정의 차이를 `compile_commands.json` 에서 타깃별로 모으면 PCH 를 따로 짓는 것은
  시험 9(`SW_TEST_INTERNAL` 공통, EngineTest 는 `SW_GF_IMPORTS` · 킷 `FlagOps.gen.h` 강제 포함, EditorTest · EditorUITest 는 서드파티 정의가 더 붙는다) · RHI 백엔드 4(`<타깃>_EXPORTS` 만 다름) ·
  App · Server · ModuleHost 3 · 에디터 확장 · `SWGame` 이다. PCH 하나가 약 2.7 CPU 초라 다 합쳐도 16 스레드에서 2 ~ 3 s(측정 편차 ±3 s 안) — 킷(PCH 60 개 → 1, −20.6 s)만 했다.
- **ReflectionParser 강제 include PCH**(2026-10-06): Debug `ReflectionParserTest` 35 케이스 25.8 초(여섯 조각, 조각당 약 4.3 초 · 한도 30 초), 파서 한 번
  1.1~1.3 초 중 공통 include 0.5 초 — 조각당 약 2 초를 위해 depfile 의존(PCH 안 헤더를 `clang_getInclusions` 가 내는가) 재검증을 떠안지 않는다.
- **롤백에 파괴 상태 싣기**(사용자 결정 2026-10-06): 롤백 키트(격투)에 파괴물이 없고 조각 물리(Jolt)는 되감지 못한다(Chaos GC 도 롤백 없음). 롤백 게임이
  파괴물을 쓰면 `saveState` 에 `makeNetworkSnapshot` 을 싣고(바뀐 사건 수일 때만) `loadState` 가 즉시 적용하는 창구를 더한다.
- **훅의 `CheckHeaderSelfContained` 를 하위 프로세스로 띄우기**(`preCommitRunsInBackground` 같은 선언, 2026-10-10): 파일 단위 게이트 중 시간을 내는 것은 이것 하나(헤더 10 개 ~2 s 벽)이고,
  트리 전체 게이트(`CheckConfigReference` 5~14 s)는 이미 먼저 띄워져 있어 그 시간 안에 겹친다. 이 게이트가 홀로 남는 커밋(내용 규칙으로 트리 전체 게이트가 선 경우)에서는 겹칠 상대가
  파일 단위 게이트들(합 <0.5 s)뿐이라 벌 수 있는 것이 그만큼이고, 기동 0.2 s 를 낸다. 넣지 않았다.
- **GPUScene 배치 키 중복 계산 제거**(투명 배치 헤드 키 재계산, 인스턴스마다 셰이더 경로 해시): `GT.GPUScene.build` 3670 → 3761 us 로 차이가 없었습니다.
- **`CheckConfigReference` 를 파일 단위(바뀐 구조체만)로 나누기**(2026-10-11, Scripts 개편 1 단계 뒤). 커밋 훅의 바닥은 트리 전체 게이트 여섯이 각자 `Source/` 를 읽는 값(조용할 때 1 ~ 1.4 s)이 만듭니다.
  도구 비용을 줄인 뒤 단독 실행 중앙값(5 회)은 `CheckConfigReference` 1.49 → 0.99 s, `CheckDuplicateTypeNames` 1.35 → 1.03 s, `CheckEngineLayers` 2.00 → 1.09 s, `CheckModuleTargets` 1.44 → 1.21 s,
  커밋 훅(`REFLECT` 헤더 하나) 2.31 → 1.92 s 였습니다. 그래서 하나만 0 으로 만들어도 나머지 다섯이 같은 높이로 남습니다.
  더 내리려면 트리 전체 게이트들이 파일 읽기를 나누는 구조(한 프로세스가 읽고 여럿이 훑기)가 필요한데, 파이썬 GIL 때문에 지금의 동시 실행을 잃습니다.
  파일마다 보는 소스 게이트는 그 구조로 옮겼습니다(2026-10-17, `SourceScan` — 트리를 한 번 걷고 파일을 코어 수만큼 프로세스에 나눠 읽고 가린 뒤 게이트 전부가 훑는다, GIL 은 프로세스로 피함).
  위의 트리 전체 대조 게이트 중 무거운 `CheckNameReferences` · `CheckKitNamespaces` 는 커밋 훅에서 빼 CI 린트 잡으로 넘겼습니다([검증과 측정](08_Verification.md) 4절 "커밋 훅과 린트 잡의 나눔").

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

결정마다 결정한 날, 근거, 상용 엔진에서 같은 기능을 어떻게 하는지를 적습니다. 이미 코드와 헤더 주석에 반영된 결정은 한두 문장으로만 적고, 자세한 계약은 그 코드의 헤더 주석(주의 · 계약)에 있습니다.
결정을 바꾸려면 여기 적힌 근거와 상용 엔진의 방식을 먼저 확인하고, 바꾼 결정은 같은 커밋에서 이 절을 고칩니다.

### 5-1. 문서와 저장소

- **약어는 대문자로 쓴다**(사용자 결정 2026-10-10 — 이미 대문자인 `AABB` · `RHI` · `TagID` 와 철자가 하나가 되고, 함수 이름에서도 약어가 보인다). 목록은 `Scripts/lint/rules/AcronymRegistry.toml` 한 자리(읽기 · 철자 판정은 `Scripts/lint/AcronymRegistry.py`), 대문자 약어가 이어 붙으면 낱말을 사이에 두거나 뜻이 같은 쪽을 뺀다(`RTSCommanderAI` · `HTTPAddress` · `kRTSCatalogXML` · `RHITimestamp` · `OpenSSLContext`). `SSL` 은 제품 이름표(`OpenSSL`)로 둔다 — 약어로 올리면 `_pSsl` 같은 OpenSSL 핸들 이름까지 바뀌고 이어 붙음은 이름 하나뿐이었다. 파이썬 · CMake 식별자는 대상이 아니다.
- **클래스 접미어는 하는 일로 고릅니다**(사용자 결정 2026-10-13 — 상용 엔진이 같은 접미어를 같은 뜻으로 쓴다). `…Manager` 는 런타임 오브젝트 집합의 수명과 생성 · 파괴 · 로드 정책을 소유하고(유니티 SceneManager, 언리얼 AssetManager),
  `…Registry` 는 등록으로 채우는 이름 · ID 조회 테이블이며 수명 정책이 없습니다(언리얼 AssetRegistry). `…System` 은 매 프레임 많은 오브젝트 · 컴포넌트에 같은 로직을 돌리고(유니티 DOTS System),
  `…Service` 는 서비스 바인딩이나 원격 엔드포인트로 닿는 교체 가능한 기능 창구(PlayFab · EOS · Nakama 서비스), `…Backend` 는 인터페이스의 플랫폼 · 라이브러리 구현(`JoltPhysicsBackend`, `XAudio2AudioBackend`)입니다.
  C 함수 테이블은 `…API`(`GameAPI` · `EditorAPI` · `ModuleAPI`)입니다. 함께 정한 규칙 셋: `Anim` 은 `Animation` 으로 풀어 씁니다(Anim 접두 식별자는 개명 중이며 새로 만들지 않고, 언리얼 `AnimInstance` 같은 외부 용어를 가리킬 때만 예외).
  불린 변수 · 멤버의 `b` 접두(`bVisible`)는 언리얼 관습으로 허용하고 불린을 돌려주는 함수만 is · has · can · should 규칙을 따릅니다.
  `Scene` 은 씬 하나의 데이터를 소유하고(언리얼 Level/World · 유니티 Scene), `GameObjectManager` 는 그 씬 오브젝트의 수명을, `GameObjectStore` 는 저장소를 맡습니다. 규칙 원문은 [AGENTS.md](../AGENTS.md) "Class suffixes" 절입니다.
- **같은 기준으로 바꾼 이름**(2026-10-13): 온라인 호스트 쪽 인터페이스는 클라이언트 쪽 `IOnlineClientService` · `registerClientService` 와 짝을 맞춰 `IOnlineHostService` · `registerHostService` 로 했습니다.
  키트가 나눠 쓰는 공유 상태는 언리얼 GameState(월드 단위 복제 상태)와 뜻이 달라 `SessionStateComponent` · `SessionStateRefs` 로 했습니다.
- **저장소에 LICENSE 파일을 두지 않습니다**(2026-10-06). 라이선스를 정하는 것은 사용자의 일이라, README 의 MIT 배지와 develop 배지도 뺐습니다.
- **코딩 규칙의 원본은 `AGENTS.md` 하나입니다**(2026-10-06). [04 코딩 규칙 예시](04_CodingGuidelines.md)는 규칙을 다시 적지 않는 한국어 예시 모음입니다.
  언리얼도 Coding Standard 한 문서에 규칙을 두고 예시는 따로 둡니다. `CLAUDE.md` 는 에이전트가 매번 읽는 한국어 요약이고, 원본이 따로 있는 규칙(에이전트 표, 편집 함정, 작업 방식)은 한 줄과 링크로만 가리킵니다.
- **모듈 폴더마다 README 를 강제하지 않습니다**(2026-10-06). 있는 README 는 모두 [문서 지도](02_DocumentMap.md)에 있어야 하고, `CheckDocPaths` 가 이것을 확인합니다.
- **문서만 바뀐 커밋에서도 CI 린트 잡은 돌고, 빌드 잡만 건너뜁니다**(2026-10-06). 바뀐 파일은 git 으로 직접 판정하고, 비교할 기준 커밋을 얻지 못하면 안전하게 빌드합니다.
- **백로그는 할 일 목록만 둡니다**(2026-10-07). 끝낸 일의 교훈은 그 코드의 헤더 주석(주의 · 계약)에, 결정과 기각한 안은 이 문서에, 검증 방법은 [검증과 측정](08_Verification.md)에 둡니다.
  대형 저장소가 할 일은 이슈 트래커에, 설계 결정은 결정 기록(ADR)에, 지식은 코드 옆 문서에 두는 방식과 같습니다.
- **게임 리소스는 상업적 사용 가능, 출처 표기 의무 없음, 재배포 허용(CC0 · 퍼블릭 도메인 급)만 넣습니다**(사용자 결정). 저장소가 GitHub 에 공개되기 때문입니다.
  CC BY 처럼 표기 의무가 있거나 재배포를 막는 리소스는 내려받기 스크립트로 저장소 밖에 두는 방식으로도 쓰지 않습니다. 후보는 라이선스 원문으로 확인하고, `credits.md` 에는 감사 표기만 합니다.
  쓸 수 있는 예는 Kenney, KayKit, VRoid 의 CC0 샘플입니다. Quaternius 는 쓰지 않습니다(5-12). 게임별 CC0 리소스 배치와 MechArena 는 "안정화된 뒤" 로 보류했습니다([백로그](backlog/project.md) `H-1`).
- **문서는 사람이 읽는 순서로, 한국 개발자가 쓰는 말로 씁니다**(2026-10-07, 사용자 결정). 모듈 README 는 무엇이고 왜 있나, 머릿속 그림, 따라 해 보기, 작동 원리, 확장하는 법, 함정과 주의, 더 볼 곳 순서이고 폴더 트리와 클래스 표는 쓰지 않습니다.
  버전, 빌드, 베이크, 테이블, 슬롯, 레지스트리, 엔드포인트 같은 업계 용어를 그대로 쓰고 순우리말 조어를 만들지 않습니다. 지침 전문은 [문서 쓰기 지침](10_WritingDocs.md)이고, 코드 주석과 커밋 메시지에도 같은 기준을 씁니다.

### 5-2. 빌드, 스크립트, CI

- **GPU 잠금은 공유(두 프로세스)와 독점으로 나눈다 — 기능 확인 실행은 공유로 병렬, 측정 · 장치 제거 실험은 독점**(2026-10-15, `shared-lock acquire gpu --shared`). 결정적 시나리오와 hostgpu 는 서로의 수치를 오염시키지 않아
  직렬로 돌 까닭이 없었고 GPU 실행이 검증의 병목이었다. 리더 · 라이터 잠금과 같은 모양이며 독점이 먼저다(독점이 기다리는 동안 새 공유는 물러난다) — 측정이 공유 실행에 굶지 않는다.

- **게임 여럿을 한 빌드에서 짓습니다 — 모듈 이름 `SWGame` 은 그대로, 타깃은 게임마다 `SWGame_<게임>`(활성 게임도), 산출물은 `Bin/Modules/SWGame`(활성) · `Bin/Games/<게임>/SWGame`, 고르기는 `SW_GAMES`(Dev 전용)입니다**(2026-10-13, `C-25`).
  게임 팩끼리 PCH 하나(`sw_game_pch`)를 나눠 씁니다. 실행 중에 키트를 더하고 빼는 경로는 만들지 않습니다(2026-10-14 사용자 결정 — 키트 조합은 빌드 · 기동 때 정한다). 게임별 CMake 프리셋(`Ninja-Debug-<게임>`)도 두지 않습니다(2026-10-14 사용자 결정 — `Ninja-Debug` 하나 + `-game=`).
  기동 때 고른 게임이 닿지 않고 다른 게임 팩만 닿는 키트는 호스트 로더가 매니페스트를 카탈로그에 넣지 않습니다 — CMake 가 쓰지 않는 키트의 매니페스트를 `Bin/Modules` 에 두지 않는 것과 같은 기준이고,
  카탈로그(`ModuleCatalog`)는 다른 게임을 모릅니다(유니티 패키지가 프로젝트 manifest 에 없으면 오르지 않는 것과 같다). 아무 게임도 닿지 않는 키트는 언리얼 `EnabledByDefault` 처럼 켜진 채 둡니다(온라인 서버 키트).
  전용 서버는 오디오를 디코드하지 않습니다 — 서버 패키지가 오디오를 빼므로(`target_excluded_asset_kinds`) 클립 캐시가 `ResourcePaths::isExcludedForHost` 를 보고 경고 없이 건너뜁니다(언리얼 전용 서버와 같다).
  언리얼은 엔진 바이너리를 한 번 짓고 프로젝트 모듈만 프로젝트 폴더에 내며, 유니티 · Godot 도 에디터 하나가 여러 프로젝트를 엽니다. 키트는 고른 게임들의 의존 합집합(`SW_BUILD_UNUSED_KITS=OFF`, 프리셋 `Ninja-Debug-Lean`),
  Dev 는 실행할 때 `-game=<이름>` 으로 고르고(언리얼이 프로젝트를 명령줄로 받는 것과 같다, `GameSelection`), 기본은 `Bin/Modules/ActiveGame.txt` 라 게임 이름이 엔진 TU 에 구워지지 않습니다.
  Shipping 은 활성 게임 하나를 정적으로 링크하고 게임 정체성을 빌드 시점에 굽습니다(실행 시점 선택의 비용을 배포본에 지우지 않는다). Debug 깨끗한 빌드 게임 1개 148초 → 8개 152초,
  활성 게임을 바꾼 뒤 다시 짓기 5.5초(게임 모듈 22 TU 만).

- **Windows 지연 로드(`/DELAYLOAD`)를 쓰지 않습니다 — 모듈끼리는 일반 import 이고 핫 리로드가 섀도 복사본의 import 이름을 세대 이름으로 바꿉니다**(2026-10-12).
  lld 20.1.8 의 x64 지연 로드 썽크(`__tailMerge_<dll>`)가 xmm0~3 을 `__delayLoadHelper2` 호출의 홈 공간에 저장해, 처음 묶는 호출의 float 인자가 망가졌습니다
  (LLVM 이슈 51941 · bz52599, 고침은 #149521 로 LLVM 21.x 에 들어감). 모든 지연 import 를 코드가 돌기 전에 미리 묶는 우회는 이미지 목록을 놓치면(DbgHelp 열거 실패)
  조용히 다시 망가졌습니다. 후보와 결론:
  (a) 도구 사슬을 LLVM 21 로 올리기 — 근본이지만 clang-format · 컴파일러 · 경고 전부가 같이 움직여 이 결함 하나로는 크고, 올린 뒤에도 지연 로드가 필요한 자리가 없어 하지 않음.
  (b) 다시 올리지 않는 의존(GameFramework — App 은 `loadSharedModule` 로 원본을 한 번 올린다)만 일반 import 로 — 고친 CI 실패는 모두 이것이었지만 키트 → 키트 · SWGame → 키트 ·
  에디터 확장 → EditorModule 은 다시 올리는 의존이라 썽크가 남는다. (c) 모듈이 자기 첫 진입점 · DllMain 에서 스스로 묶기 — DllMain 은 로더 락 안이라 다른 DLL 을 올릴 수 없고,
  첫 진입점보다 먼저 도는 정적 초기화가 이미 썽크를 지난다. (d) 첫 호출을 float 인자 없는 함수로 만드는 프라이머 — 썽크는 import 마다라 함수마다 프라이머가 필요해 지킬 수 없다.
  (e) **고른 것**: 리눅스 SONAME 바꾸기와 같은 장치를 Windows 에 — 복사본 파일 이름을 같은 길이의 세대 이름(`GF_Mai0003.dll`, 복사본마다 제 폴더)으로 하고 의존하는 복사본의
  import 표 이름을 제자리에서 바꾼다. 로더가 이미 올라온 같은 이름의 이미지를 쓰므로 원본이 두 번 올라오지 않고, 썽크 자체가 없다. 없을 수 있는 시스템 DLL(Media Foundation ·
  XAudio2 · TracyClient)은 LoadLibrary + 함수 표, 늘 있는 d3dcompiler_47 은 일반 import.
  상용 엔진 비교: 언리얼은 모듈 DLL 을 정적 import 하고 핫 리로드 때 번호 붙은 새 이름으로 다시 링크하며(지연 로드 없음 — 이 방식은 그 다시 링크를 복사 시점에 한다),
  유니티는 관리 코드를 도메인 리로드로 갈아 네이티브 import 를 건드리지 않고, Godot GDExtension 은 `get_proc_address` 함수 표로 엔진 API 를 받아 import 자체가 없다(시스템 DLL 쪽이 이 방식).
  가드는 빌드 `CheckNoDelayLoad`, 런타임 `LiveReloadManager` 의 지연 import 이미지 거절(Debug 단언), 산출물 `ArchitectureTest.NoBuiltImageHasDelayLoadedImports` 입니다.

- **Shipping 의 RHI 백엔드는 쿠킹 테이블의 이름(`DirectX11`, `DirectX12`, `Vulkan`, `OpenGL`)으로만 고릅니다**(2026-10-06). 별칭이나 그 플랫폼에 없는 백엔드를 주면 configure 가 멈춥니다.
  언리얼의 TargetRules 도 플랫폼에 없는 RHI 를 빌드 전에 거부합니다.
- **App 의 백엔드 명령줄 철자는 백엔드마다 하나입니다**(2026-10-07). `-dx11` · `-dx12` · `-vk` · `-gl` 이고, 쿠킹 표의 `command_line_name` 칸이 정본입니다.
  `-vulkan` · `-directx12` · `-d3d11` · `-spirv` · `-opengl` 같은 다른 철자는 받지 않습니다. 셰이더 폴더 이름(`dx11` · `dx12` · `vulkan` · `opengl`)은 따로 있는 칸이라 그대로입니다.
- **vcpkg 라이브러리 등록 함수는 `sw_addVcpkgPackage`(못 찾으면 configure 실패)와 `sw_addVcpkgHeaderOnly` 두 가지입니다**(2026-10-06). 언리얼 ThirdParty 모듈의 두 종류(라이브러리, 헤더 전용)와 같습니다.
- **서드파티 헤더는 그 라이브러리 타깃을 링크해서만 얻습니다. vcpkg include 폴더를 모든 타깃에 거는 전역 경로는 두지 않습니다**(2026-10-15). 언리얼 Build.cs 의 명시 의존과 같습니다.
  전역 경로는 project() 전(설치 전)에 폴더가 있을 때만 걸려, 설치가 남은 PC 에서만 통과하고 새 CI 러너에서 지는 include 누락을 만들었습니다.
- **키트 빌드 순서는 모듈 매니페스트 의존 관계의 위상 정렬입니다**(2026-10-06). 폴더 이름 순서로는 공유 키트가 먼저 정의된다는 보장이 없습니다. UBT 도 모듈 의존 그래프로 순서를 정합니다.
- **스크립트 이름과 도움말 규칙**(2026-10-06): 도움말은 한국어로 씁니다. `Scripts/dev/` 의 이름은 동사로 시작하고(`Make*`, `Run*`), "Generate" 는 빌드가 만드는 생성물에만 씁니다.
  `py -3 -m Scripts gate|fix|report|selftest <이름>` 은 폴더를 찾아 부르고, CMake와 커밋 훅과 CI 는 경로로 부릅니다.
- **게이트는 파일을 고치지 않습니다**(2026-10-06). include 순서와 namespace 블록을 고치는 일은 픽서(`FormatIncludeOrder`, `FormatNamespaceBlocks`)가 하고, 픽서의 대상 파일은 `--files` 하나로 고릅니다.
  포맷터는 내용 밖의 바이트(BOM, 줄끝)를 바꾸지 않습니다. clang-format 과 같은 원칙입니다.
- **configure 중의 파이썬 생성기는 한 프로세스로 돌립니다**(2026-10-06). configure 시간의 29 % 가 파이썬 기동이었고, 한 프로세스로 묶자 configure 가 3,486 ms 에서 2,898 ms 로 줄었습니다.
- **커밋 훅의 트리 전체 게이트는 코어 수만큼만 동시에 띄우고, 린트 CTest 는 동시에 4 개씩 돌립니다**(2026-10-06). ninja 와 UBT 의 ParallelExecutor 도 동시 작업 수를 프로세서 수로 묶습니다.
  린트 CTest 는 4 와 8 이 측정 오차 안에서 같고 4 가 CPU 를 절반만 씁니다. CI 린트 잡은 처음부터 실패를 막는 단계입니다.
- **Release 와 Shipping 도 디버그 정보를 만들고, 기본은 줄 정보만(`SW_RELEASE_DEBUG_INFO=lines`)입니다**(2026-10-06). 배포물의 PDB 는 `Symbols/`, 테스트 실행 파일의 PDB 는 `TestBin/` 에 둡니다.
- **vcpkg 포트를 고칠 때는 트리플릿이 아니라 오버레이 포트(`ThirdParty/<pkg>/vcpkg-port/`)를 씁니다**(2026-10-06). 트리플릿 파일의 해시는 모든 포트의 ABI 에 들어가므로, 트리플릿을 고치면 공유 설치 트리 전체를 다시 빌드합니다.
  포트 버전을 올릴 때는 오버레이도 같이 올립니다.
- **의존성 기준선은 vcpkg 레지스트리 커밋 하나(`builtin-baseline`)로 올리고, 같은 커밋으로 `Tools/vcpkg` 를 맞춥니다**(2026-10-10). 남은 판은 `py -3 -m Scripts deps-outdated` 가 보입니다.
  OpenSSL 4(폐기 API 삭제 — libpq 의 `openssl` 기능이 따라오는지 먼저 본다), glad2(로더 생성기 · 헤더 모양이 바뀌어 GL 백엔드 초기화를 다시 쓴다), DXC 최신(구운 셰이더 전부와 네 백엔드 그림이 바뀐다)은
  기준선과 따로 올립니다. 언리얼도 엔진 판과 함께 ThirdParty 를 올리고 셰이더 컴파일러는 따로 올립니다.
- **Dev `Bin` 은 실행 파일 · 엔진 · 서드파티 DLL 만 두고, 모듈은 `Bin/Modules`, PDB 는 `Bin/Symbols`, 시험은 `TestBin` 에 냅니다**(2026-10-10). 언리얼이 플러그인 DLL 을
  플러그인의 `Binaries/` 에, 유니티가 네이티브 플러그인을 `Plugins/` 에 따로 두는 것과 같습니다. 서드파티 DLL 폴더 · Vulkan 레이어 json 은 옮기지 않았습니다.
  모듈이 쓰는 서드파티 DLL 도 `Bin` 한 벌입니다(vcpkg applocal 이 모듈 옆에 사본을 두지 않게 한다 — 언리얼이 서드파티 DLL 을 `Binaries/ThirdParty` 한 자리에 모으는 것과 같은 방향).
- **개발 기본 빌드(`all`)는 시험을 짓지 않습니다**(2026-10-10). Debug CPU 시간의 26 %(시험 614 TU)가 시험이었습니다. `Test/` 는 `EXCLUDE_FROM_ALL` 이고,
  시험 실행 파일은 `AllTests` 타깃이 모읍니다. CI · 검증 프리셋(`CI-*`, `*-Shipping*`, `Ninja-Debug-ASAN`)은 build 프리셋의 `targets`(`all` · `AllTests`)로 시험까지 짓습니다.
  `ctest` 는 셋업 픽스처(`BuildTestBinaries` — `all` · `AllTests` 를 짓는 항목, 실행 파일 시험이 요구한다)로 먼저 짓습니다. 이미 지은 트리면 몇 초이고, 옛 실행 파일로 통과하는 일도 막습니다.
  `py -3 -m Scripts test` 도 먼저 짓습니다. 비교한 다른 길은 둘입니다. 캐시 옵션으로 `all` 에 넣고 빼면 프리셋마다 configure 가 갈리고, 같은 폴더에서 바꿀 때 재구성이 돕니다.
  `all-with-tests` 를 `all` 전부에 의존시키려면 디렉터리를 훑어야 하고, `ALL` 없는 사용자 타깃을 가려낼 수 없습니다(CMake 가 알려 주지 않는다).
  언리얼은 시험을 별도 타깃으로 짓고, CMake 관례(`BUILD_TESTING`)도 시험을 따로 켭니다.
- **게이트가 나눠 쓰는 글 읽기(include 줄 · 층 폴더 · include → 파일 · 주석 가리기 · 줄 번호)는 `Scripts/common/CodeText.py` 한 자리이고, 3,000 줄이 넘는 게이트는 규칙 묶음 패키지(`Scripts/lint/conventions/`)로 나눕니다**(2026-10-10).
  clang-tidy 가 검사를 모듈(`bugprone` · `readability` …)별 파일로 두고 공용 매처를 `utils/` 에 두는 것과 같은 모양입니다. 층 게이트 다섯(Core · Core/Network · Engine · GameFramework · ModuleTargets)을
  표 하나로 구동하는 한 구현은 하지 않았습니다 — 판정이 서로 다르고(같은 층 허용 · 배선 예외 · 대상 집합) CTest 이름 · 자기 시험이 게이트마다 달려 있어, 도우미만 나눴습니다.
- **`cmake/Engine/` 은 책임 하나에 파일 하나입니다**(2026-10-10). 677 줄 `ModuleTargets.cmake` 를 컴파일 규칙 · 지연 로드 · ABI 도장 · 모듈 레지스트리 · 모듈 팩토리 · 실행 파일 링크로,
  613 줄 `ModuleManifest.cmake` 를 해석 · 켜짐 조회로 나누고 옛 산출물 지우기는 `BuildLayout.cmake` 로 옮겼습니다. UBT 가 ModuleRules · TargetRules · 링크 규칙을 따로 두는 것과 같습니다.
  구성 결과(네 프리셋의 TU 별 명령 · 링크 엣지 · ninja 타깃 · File API 답)는 전과 같고, 다른 것은 EditorTest 의 오브젝트 순서(폴더 훑기)뿐입니다.
  하지 않은 정리: 시험 폴더 머리의 "Server 타깃이면 건너뛴다" 세 줄(`return()` 은 부른 파일에 있어야 해 함수로 줄지 않는다), App · Server · ModuleHost · 도구 타깃의 공통 기본값 함수
  (타깃마다 정의 · PCH · 링크가 달라 두세 줄을 줄이고 링크 순서를 흔든다), QA 스크립트 시험 등록 함수(셋의 인자가 달라 풀어 쓴 것이 읽기 쉽다), `ConfigConstants.h.in` 을 파이썬 생성기로
  옮기기(손으로 고른 C++ 상수 묶음이라 얻는 것이 없다), zlib · LZ4 · Zstd 를 ThirdParty 래퍼 타깃으로(리눅스 CI 에서 비었던 임포트 타깃을 받치는 `ZLIB_LIBRARIES` 링크가 빠진다).
- **`NOMINMAX` · `WIN32_LEAN_AND_MEAN` 은 CMake 컴파일 정의입니다**(2026-10-10). 헤더에서만 정의하면 그보다 먼저 `windows.h` 를 include 한 TU 에 min/max 매크로가 생깁니다.
  Godot 도 빌드 정의(`platform/windows/detect.py`)로 줍니다. 헤더의 `#if !defined` 정의는 빌드 플래그 없이 헤더를 읽는 도구를 위해 남깁니다.
- **서드파티 고지(`THIRD_PARTY_NOTICES.txt`)는 빌드가 모읍니다**(2026-10-06). 대상은 매니페스트가 끌어오는 의존 전부이고 개발 전용 라이브러리도 넣습니다. 고지가 넘치는 것은 해가 없지만 빠지면 라이선스 위반입니다.
- **규칙 예외 감사에서 정한 넷**(2026-10-07, 아직 적용 전 — [백로그](backlog/project.md) `C-11`): ① 두 플랫폼이 같은 명시적 경고 목록을 씁니다(clang-cl 의 `-Wall` 은 `-Weverything` 이라 플랫폼마다 경고가 달랐습니다).
  ② `(void)` 와 이유 주석은 `[[nodiscard]]` 가 붙은 실패 가능 함수에만 요구합니다. ③ 백엔드 명령줄 철자는 `-dx12`, `-dx11`, `-vk`, `-gl` 하나씩만 둡니다.
  ④ 출처를 모르는 리소스는 저장소에서 생성한 것이나 CC0 로 확인한 것으로 바꿉니다.
- **린트의 규칙 데이터는 `Scripts/lint/rules/<이름>.toml` 에 두고 `Scripts/common/RuleData.py` 하나가 읽습니다**(2026-10-11). clang-tidy 가 검사 설정을 `.clang-tidy`(YAML)로,
  언리얼이 금지 API · 예외를 `.ini` 로 코드 밖에 두는 것과 같습니다. 형식은 TOML 입니다 — 주석이 되고, 표 · 배열만으로 예외 표와 목록이 그대로 적히며, JSON 은 주석이 없고
  YAML 은 표준 라이브러리에 읽기가 없습니다. CI 린트 잡의 파이썬은 3.10 이라 `tomllib`(3.11+)이 없어, 쓰는 문법만 아는 부분 집합 읽기를 같이 두고
  `CheckExemptionTables` 가 두 읽기의 결과를 견줍니다(파이썬을 올리면 CI 잡마다 `setup-python` 이 붙고 WSL 클론도 따라 올려야 한다).
  - 게이트 예외는 크기와 상관없이 모두 옮겼습니다(한 줄짜리 넷 포함) — "예외는 한 자리" 를 셀프테스트가 지키려면 게이트 소스에 표가 하나도 없어야 합니다.
  - 목록은 사람이 늘리는 데이터만 옮겼습니다: Core · Engine · GameFramework 티어, 제품 낱말, 금지 동사 · 줄임말, 약어 등록부, 엔진 루트 파일, Win32 일반 이름, 내장 매크로,
    실패 가능 동사, 입력 장치 조회 · 폰 쪽 파일, 셰이더 줄임말.
  - 옮기지 않은 것: 정규식이 든 표(`RunDocStyle.kListCoinedTerm` · `CheckWellKnownConstants` · `CheckScriptLayout` · `CheckScriptCommonHelpers` 의 규칙 — 정규식은 쓰는 코드 옆에),
    여러 칸짜리 레코드(`CheckThirdPartyIsolation._kListLibraryRule` — 두 게이트가 읽고 부분 집합에 표 배열이 없다, `CheckModuleTargets._kListFolderTarget` 7 줄),
    `Constants.py` 의 경로로 짓는 표(`CheckEngineLayers` 의 금지 규칙), 언어가 정한 낱말(HLSL 키워드 · 한정자, C++ 내장 타입), 명명 판정 표(`conventions/NamingVocabulary` —
    주체 × 컨테이너 칸이 판정 코드의 일부), 확장자 목록(파일 고르기 설정이지 규칙이 아니다).
- **Python 모양 규칙은 이미 80 % 넘게 쓰던 관습만 올렸습니다**(2026-10-11, [AGENTS.md › Python](../AGENTS.md#python)). 새 취향은 만들지 않고 소수를 고친 뒤 `CheckPythonConventions` 에 걸었습니다.
  Ruff 의 같은 규칙(`D100` · `FA102` · `ANN` · `UP007` · `PLW1514` · `PTH`)과 같은 내용이지만, 외부 도구를 CI · 커밋 훅에 깔지 않고 규칙 데이터 · 예외 표가 있는 AST 게이트 하나로 둡니다.
  - 올린 것(추적 파일 218 개 기준): 모듈 docstring 217, `from __future__ import annotations` 207(빠진 열하나는 코드 없는 `__init__` 여섯 · 스크립트 넷 · 세션 훅),
    반환 타입 표기 1,590/1,754 함수 · 매개변수 2,185/2,419(빠진 것의 3/4 이 `EditorIconFont` 한 파일), `X | None` 314 대 `Optional` 2,
    글 입출력의 `encoding=` 243 호출 대 0, `pathlib` 198 파일 대 `os.path` 17 파일(그중 `join` · `dirname` · `isfile` 을 쓰던 아홉 개만 고쳤다 — `normpath` · `relpath` 같은 글자 연산은 pathlib 에 없다),
    `sys.exit` 134 파일 대 `raise SystemExit` 9 파일, `__main__` 가드 `sys.exit(main())` 132 대 `raise SystemExit(main())` 4, `ArgumentParser(description=…)` 50/51.
  - 종료는 `sys.exit` 하나입니다. `raise SystemExit` 와 동작이 같아(같은 예외를 던진다) 고르는 기준은 수뿐이고, 쿠커의 `raise SystemExit(…) from exc` 는 `SystemExit` 이 트레이스백을 찍지 않아 `from` 이 보이지 않습니다.
  - 출력은 `print` 그대로입니다(72 파일). 공용 콘솔 도우미는 없고(`common` 은 import 때 콘솔을 UTF-8 로 맞출 뿐이다), 로깅 래퍼를 새로 들이면 새 취향입니다. 게이트 · 픽서 · 보고서는 기반 클래스가 찍습니다.
  - 걸지 않은 것: 지역 컨테이너 접두어(`list` · `map` · `unique`) 368/533 = 69 %(맵은 46 %), 셔뱅 78 % · `coding` 줄 76 %(라이브러리 모듈에는 없는 것이 맞다), 광범위 `except Exception`
    13/169 처리기(모두 설정 단계 · 세션 훅처럼 계속 가야 하는 경계라 산문 규칙만 둔다), `subprocess` 직접 호출(`Scripts/` 는 `CheckScriptCommonHelpers` 가 이미 막고, 밖은 스크립트를 자식 프로세스로 띄워
    보는 시험 · Blender 안에서 도는 애드온 · VS Code 확장 도구라 `common` 을 쓸 수 없다), 모듈 상수 위치(이미 159 파일이 모듈 수준 — 함수 안 `kSize` 는 C++ 의 지역 `const` 와 같다).
  - 예외 하나: `.claude/hooks/SessionGuard.py` 는 Claude Code 세션 설정이라 에이전트가 고치지 않고, 어떤 예외에도 세션을 막지 않게 0 으로 끝나는 모양을 따로 둡니다.

- **빌드 속도 개선은 계획의 1 ~ 6 단계까지 하고, 7 ~ 9 단계는 필요할 때만 합니다**(2026-10-10, 사용자 결정). 7 은 헤더 영향 범위(팬아웃) 감시 보고서, 8 은 Debug 디버그 정보 줄이기(`-gline-tables-only` 계열),
  9 는 링크 단위 분할(엔진 분할 계획 3 단계 뒤)입니다. Release 기준선은 측정하지 않습니다. 남은 단계는 [빌드 속도 계획](plans/BuildSpeed.md)에 있습니다.
- **서드파티는 Jolt(3D 물리) · Box2D(2D 물리) · ACL(애니메이션 압축)**(2026-10-04, 사용자 결정), **Recast & Detour · Tracy**(2026-10-05), **OpenSSL · SQLite · libpq**(2026-10-06)입니다.
  모두 vcpkg 로 들이고, 라이선스는 MIT · zlib(Recast) · BSD-3(Tracy) · Apache-2.0(OpenSSL) · 퍼블릭 도메인(SQLite) · PostgreSQL License(libpq, `openssl` 기능만)입니다.
  엔진 인터페이스 뒤에 감싸고(`IPhysicsScene3D` · `IPhysicsScene2D`, ACL 은 `Engine/Animation/Codec/ACL`, OpenSSL 은 `GameFramework/Base/Online/Security/OpenSSL`) 헤더 경계는 `CheckThirdPartyIsolation.py` 가 지킵니다.
  Recast & Detour 는 정적 링크(`RecastNavigation::Recast` · `Detour` · `DetourCrowd` · `DetourTileCache`)이고, Tracy 는 클라이언트만 쓰며 Windows 에서는 공유 `TracyClient.dll` 하나를 씁니다.
  Tracy 는 clang-cl 트리플릿의 C++14 기본값으로 빌드되지 않아 C++17 오버레이 포트(`ThirdParty/tracy/vcpkg-port/tracy`)를 두고, Shipping 은 링크하지 않으며, 뷰어(tracy-profiler.exe)는 저장소에 넣지 않습니다.
  OpenSSL 은 Windows 에서 지금 트리플릿대로 DLL 이고, SQLite 는 `GF_SQLStore`, libpq 는 `GF_Server_SQLStore` 의 CMakeLists 에서만 링크합니다.
- **테스트 골든 기준은 기계별로 만들지 않습니다**(2026-10-06, 사용자 결정). 기준은 그린 장치(GPU 이름, 드라이버)를 함께 적고, 다른 기계에서 지면 그 기계에서 `--record` 해 드라이버 차이인지 회귀인지 가릅니다.
- **조건 세 부분 이상은 이름을 붙이고 기준선은 0 입니다**(2026-10-15, `CheckConditionParts`). 언리얼 코딩 표준의 "복잡한 식은 이름 있는 지역 변수로" 와 같은 쪽입니다.
  `if` 는 바로 앞에 `const bool` 을 두어 평가 순서와 횟수를 그대로 둡니다. `else if` 는 식에 부작용이 없고 앞 조건의 보호(널 검사)가 필요 없을 때만
  체인 앞으로 올리고, 부작용이 있으면(`++` · 출력 인자를 채우는 호출) `else { const bool …; if ( … ) }` 로 감쌉니다. `while` 은 `while ( true )` 와
  이름 붙인 `break` 로 바꾸고, 숫자 범위 검사는 `StringUtil::isDigitChar` 로 한 부분으로 줄입니다(언리얼 `FChar::IsDigit` 에 해당). 이름 없는 괄호 묶기로 부분 수만 줄이지 않습니다.
- **구성마다 없는 기능의 테스트는 등록한 채 `SW_TEST_REQUIRES_FEATURE( 기능 )` 으로 선언합니다**(2026-10-15, 테스트 틀 개편 — 같은 날 정한 "등록하지 않는다" 를 바꿨습니다). 언리얼은 `WITH_DEV_AUTOMATION_TESTS` 로 컴파일에서 빼는 것으로 알고 있는데, 그러면 구성마다 무엇이 빠졌는지가 보이지 않습니다. 선언으로 건너뛰면 요약이 기능별로 세고(`Skipped because this build lacks a feature`), 그것만 남은 스위트는 빈 스위트 판정에서 빠지며, 환경 탓 건너뜀(`SW_TEST_SKIP`)만 그 판정에 듭니다. 본문이 쓰는 심볼이 없으면 본문만 `#if` 로 감쌉니다.
- **테스트 실행 파일의 라벨 · 샤드 · 제한 시간 · 호스트 분리는 `Test/TestPolicy.json` 한 곳에 둡니다**(2026-10-15). 언리얼 Gauntlet 이 테스트 구성을 데이터로 들고 유니티가 카테고리를 속성으로 다는 것으로 알고 있습니다. CMake 인자로 흩어 두면 이 PC 에서 시간 초과가 날 때마다 폴더마다 숫자를 손으로 올렸습니다. 이제 한 실행 시간(`seconds`)에서 샤드 수(30 초 단위)와 제한 시간(네 배, 최소 60 초)을 계산하고, 라벨 표에 없는 라벨은 configure 가 멈추며, 폴더 · 줄 · 비용표의 짝은 `CheckTestSuites` 가 봅니다.
- **제한 시간은 멈춤을 잡는 그물이고 느려짐은 시간 예산이 판정합니다**(2026-10-15). 빌드가 겹치면 같은 케이스가 두세 배 느려지는 PC 에서 제한 시간을 빠듯하게 두면 가짜 실패가 납니다. `SW_TEST_TIME_BUDGET` 은 `--enforce_budgets` 실행(부하 검증 `<타깃>_Load`, 직렬)에서만 케이스를 지게 하고, 평소에는 넘은 케이스를 요약에 찍습니다. 언리얼 `PerfFilter` · 유니티 Performance 처럼 부하 스위트(`SW_TEST_LOAD_SUITE`)는 기본 집합에서 빠집니다.
- **제한 시간의 구성별 배율도 `Test/TestPolicy.json` 의 `timeoutScale` 데이터입니다**(2026-10-16). 리눅스 ×2, 새니타이저 ×10 이고 겹쳐 곱하지 않습니다. 근거는 CI 실측(리눅스 Debug EngineTest 조각이 Windows 계산값의 1.9~4 배)이고 `_timeoutScaleReason` 에 실행 id 와 함께 둡니다. 언리얼 Gauntlet 이 플랫폼별 타임아웃 배수를 구성 데이터로 두는 것과 같은 자리입니다. 테스트가 띄우는 자식 프로세스 시한도 같은 값(`SW_TEST_TIMEOUT_SCALE`)을 씁니다.
- **Jolt 잡 시스템 어댑터는 `WaitForJobs` 에서 실행 중이던 잡이 나오기를 기다립니다**(2026-10-16). Jolt 는 장벽에 붙기 전에 끝난 잡과 스텝 스레드 사이에 선후를 만들지 않아(relaxed `SetBarrier`, 업스트림 master 도 같다) TSan 이 다음 스텝의 `Step` 생성을 경쟁으로 봅니다. vcpkg 포트 패치 대신 어댑터에서 스레드 칸별 들어감 · 나옴 수로 그 순간 실행 중이던 것만 기다립니다(전체 수가 0 이 되기를 기다리면 다른 씬이 계속 잡을 내는 동안 끝나지 않습니다).
- **바깥 빌드(터미널 · IDE)의 핫 리로드는 빌드 성공 스탬프가 바뀔 때만 올립니다**(2026-10-17). 언리얼 Live Coding · 유니티 도메인 리로드는 컴파일이 끝난 뒤에 올리는데, 바깥 빌드는 DLL mtime 디바운스만 있어 연쇄 빌드가 모듈을 하나씩 다시 쓰는 도중의 반쯤 링크된 집합을 올릴 수 있었습니다(실제로는 감시 시작 줄이 빠져 바깥 빌드는 아예 올리지 않고 있었습니다).
  `ModuleBuildStamp`(ALL) 가 동적 모듈 산출물에 파일 단위로 의존해 모두 링크된 뒤에만 `Bin/Modules/ModuleBuild.stamp` 를 다시 쓰고, `LiveReloadManager` 는 모듈 폴더를 감시하다 스탬프가 바뀌면 원본이 로드한 것보다 새 모듈을 연쇄 교체합니다. DLL 쓰기 자체는 리로드를 부르지 않습니다.
  모듈 타깃 하나만 지으면(`--target GF_X`) 스탬프가 바뀌지 않습니다 — 리로드하려면 `--target ModuleBuildStamp`(바뀐 모듈만 다시 링크한다)나 기본 빌드를 씁니다.
- **소프트 바디는 바퀴 차처럼 3D 씬의 역할 인터페이스(`IPhysicsSoftBodyScene`)로 감싸고, 천은 고정 정점이 트랜스폼을 따르는 컴포넌트(`ClothComponent`)입니다**(2026-10-17).
  유니티 `Cloth` 는 max distance 0 정점이, 언리얼 Chaos Cloth 는 고정 가중치 정점이 메시를 따르고 나머지가 시뮬레이션되는 것과 같은 모양입니다. 서술자는 정점 · 삼각형 · 고정 정점 목록이라 천과 헤어 카드(띠)가 같은 길을 씁니다.
  Jolt 몸 자리는 만든 때 그대로 둡니다(`mUpdatePosition` 끔) — 정점이 질량 중심 기준이라 몸이 따라 움직이면 고정 정점의 로컬 위치를 매 스텝 다시 맞춰야 합니다. 2D 는 Box2D 에 소프트 바디가 없어 상속하지 않습니다(부르면 컴파일 오류).
- **샤드는 케이스 비용으로 고르게 나눕니다(LPT)**(2026-10-15). 스위트 안에서 번갈아 나누면 10~40 초짜리 케이스가 한 조각에 몰려 `EngineTest_NoGPU_Shard2` 혼자 104 초(다른 조각 42~64 초)였습니다. 비용표(`caseCosts` → `--test_costs`)는 저장소에 든 데이터라 모든 조각이 같은 배정을 계산합니다. 실행마다 잰 시간 파일을 쓰지 않는 것은 나란히 도는 조각이 서로 다른 파일을 읽으면 케이스가 빠지거나 두 번 돌기 때문입니다.
- **진 실행은 다시 돌릴 명령을 스스로 출력합니다**(2026-10-15). gtest 는 섞기 씨앗만, 언리얼 Automation 은 테스트 이름만 남기는 것으로 알고 있습니다. 우리 요약은 작업 폴더 · 실행 파일 · 진 케이스 필터 · 스위트 종류 · 씨앗 · 엔진 인자 · ctest 항목 · 구성을 한 줄로 적고, `--test_repeat` 면 케이스마다 재현율을, `ScopedTrace`(gtest `SCOPED_TRACE`) 가 걸렸으면 조건(백엔드)을 붙입니다.
- **테스트가 띄우는 프로세스는 공용 하네스 하나(`test::runProcessWithDeadline`)로 시한을 지킵니다**(2026-10-15). 자식 테스트 · 서버 · App 이 시한 감시를 저마다 짰고 AppSmoke · AppCook 은 시한이 없었습니다. 새니타이저 배수도 이 한 곳입니다.
- **판정과 보고는 실행기 밖의 창구로 더합니다**(2026-10-15). 케이스를 지게 하는 규칙은 `ITestCaseJudge`, 출력 형식은 `ITestRunListener` 입니다(gtest `TestEventListener` 와 같은 자리). 호스트 스위트의 예상 밖 Error · 시간 예산 · 콘솔 출력이 첫 구현입니다.
- **부하 검증은 밖의 실행기(`py -3 -m Scripts load`) + 엔진 측정 보고 + 데이터 넷(`Test/Load` 의 프로필 · 예산 · 기준선 · 묶음)입니다**(2026-10-15). 언리얼 Gauntlet 이 밖에서 띄우고 CSV 프로파일러를 읽어 판정하고, 유니티 Performance Testing 이 측정 그룹을 기준선과 견주는 것으로 알고 있습니다.
  우리는 같은 자리에 둘을 더했습니다 — 예산 실패를 병목 갈래(CPU 게임 스레드 · 렌더 스레드 · GPU · 메모리 · IO · 서버)로 나눠 적고(같은 판의 `RT.BeginFrame` · `GT.Packet.submit` 비율로 누가 누구를 기다렸는지 가른다), 판정은 평균이 아니라 p50 · p99 · max 의 여러 판 가운데 값으로만 합니다.
  예산(제품 목표)과 기준선(이 기계의 회귀)은 따로 둡니다. 판정은 Release 만(Debug 는 경합 검출기가 부풀리고 Shipping 은 보고가 없다) — `<타깃>_Load` 도 Release 에서만 `--enforce_budgets` 를 받습니다.
- **부하 시나리오는 자동화 시나리오 형식에 `<Load>` 한 줄을 더한 것이고, 서버 쪽(`<LoadScenario driver="loadbot|cluster">`)도 같은 줄을 씁니다**(2026-10-15). 형식을 하나 더 만들면 App · 봇 · 클러스터가 클라이언트 수 · 램프업 · 시간 · 회선을 저마다 다른 이름으로 받게 됩니다(Nakama 부하 도구 · Locust 는 봇 쪽만 압니다).
  엔진은 `<Load>` 를 읽어 형식을 검사하고 시작 프레임에 측정 창(`gv_profileSeconds`, 늦게 건 목표를 `FrameProfileSession` 이 읽는다)을 열 뿐, 메시 · 회선 같은 값은 실행기가 명령줄로 옮깁니다(`-gv_benchMeshes`, `-gv_netEmuPreset` — 회선 프리셋 표를 밖에 다시 적지 않으려고 프리셋 이름 변수를 더했다).
  부하 시나리오는 `<Pass/>` 를 두지 않고 프레임 시한 대신 벽시계 그물만 봅니다. `automation/load/` 하위라 `AppScenarioTest` 가 줍지 않습니다.
- **하드웨어 프로필(LowEnd · Mid · HighEnd)은 기기 없이 소프트웨어로 흉내 내고 결과에 `emulated` 를 적어 실제 기기 측정(Native)과 기준선 키를 가릅니다**(2026-10-15). 언리얼 Device Profiles 는 실제 기기를 고르는 자리로 알고 있는데, 기기가 없을 때 같은 판정을 미리 돌리는 길이 없습니다.
  CPU 코어 · 메모리는 Job Object(멈춘 채 만든 뒤 넣는다 — 워커 수가 정해지는 순간과 겨루지 않게), 워커 풀은 친화도를 따르게 엔진을 고쳤고(`thread::getUsableProcessorCount` — `hardware_concurrency` 는 친화도 · 컨테이너 제한을 못 본다), 디스크는 `gv_ioEmuRead*`, VRAM 은 사양 조사가 보고하는 값(`gv_emulatedGPUMemoryMb`)입니다.
  품질 단계는 이미 있는 Scalability(`graphics.quality` 프리셋 — 그림자 · 시야 거리 · 식생 밀도 · 후처리 · 렌더 스케일)를 프로필이 고르거나(`quality`) 흉내 낸 사양으로 엔진 자동 선택이 고릅니다(`auto`). 자동 조정은 먼저 밖에서 단계를 내려 가며 예산을 지키는 첫 단계를 찾는 `load scale` 로 두었고, 실행 중 동적 조정(언리얼 Dynamic Resolution · 유니티 Adaptive Performance)은 측정으로 이득이 보일 때 들입니다(백로그 `C-29`).
- **디스크 흉내는 장치 하나 · 큐 깊이 1 · 고해상도 마감 대기입니다**(2026-10-15). 스레드마다 `sleep_for` 하던 첫 판은 Windows 타이머 주기(약 15.6 ms)로 반올림돼 1 ms(SATA SSD) 와 8 ms(HDD) 가 같은 지연이 됐고, 첫 측정의 "기동 6.4 s" 는 그 착시였습니다(열기 385 번 × 15.6 ms).
  여러 스레드가 나눠 읽으면 빨라지는 것처럼 보이지 않게 모든 읽기가 한 장치 시각(`busy until`)에 줄을 섭니다 — 병렬 읽기로 얻는 이득은 실제 기기에서 따로 잽니다. 언리얼 · 유니티에는 같은 흉내가 없고(실기기 Device Profile), 우리는 기기 없이 기동 · 히치 예산을 먼저 거는 쪽을 골랐습니다.
- **느슨한 트리의 `.meta` 스캔은 쿠킹한 팩과 같은 도메인(engine · common · 활성 게임)만 훑습니다**(2026-10-15). 다른 게임 팩의 GUID 는 이 게임이 닿지 않고, 팩 경로(`assetregistry.txt`)도 그 셋만 읽습니다. 기동의 파일 열기 385 → 316(렌더 벤치)입니다.
- **플레이 중 처음 쓰는 메시는 판을 열 때 워커로 미리 읽어 쥡니다(`MeshPreloadSet`)**(2026-10-15). 언리얼 `FStreamableManager::RequestAsyncLoad` 의 핸들, 유니티 Addressables 의 미리 읽기와 같은 자리입니다. 메시 캐시가 약한 참조라 묶음이 쥐어야 남습니다.
  `MeshComponent` 를 비동기로 바꾸는 길(로드 동안 안 그림)은 모든 게임 · 테스트의 "지정하면 바로 그린다" 계약을 깨므로 두지 않았습니다. StarSkirmish LowEnd 최악 프레임 25.8 → 4.1 ms.
- **Warning 은 조치가 필요한 것만 남기고, 예상된 상황(지원하는 사용법 · 정상 폴백)은 Info 입니다**(2026-10-15). 언리얼 verbosity 관례(Warning 은 고칠 것, 예상된 일은 Log · Verbose)와 같습니다.
  같은 이름 오브젝트에 번호 붙이기(`GameObjectStore`, 언리얼 `MakeUniqueObjectName` 도 조용하다), 씬 없는 상태 스냅샷(`GameInstanceBase`)을 Info 로 내렸습니다.
- **자동화 시나리오와 호스트 스위트는 선언하지 않은 Warning · Error 한 줄로 실패합니다**(2026-10-15). 언리얼 Automation 의 `AddExpectedError` 와 "경고를 실패로" 설정을 합친 자리입니다.
  시나리오는 실행기(`AutomationRunner::applyLogGate`, 시나리오 구간 + 프로세스 전체의 빠진 현지화 키)와 `AppScenarioTest`(출력 전체, 기동 · 종료 포함) 두 겹이고,
  허용은 시나리오의 `<ExpectLog>` 와 전역 목록 `Resource/engine/automation/allowedlogs.json`(항목마다 이유 필수) 둘뿐입니다. 호스트 스위트는 `SW_TEST_DEFENSIVE_SCOPE` · `SW_TEST_KNOWN_LOG` 입니다.
  CPU 단위 테스트(nogpu)는 아직 Error 도 세지 않습니다 — 일부러 깨진 픽스처가 많아 경고 1,500 줄 남짓이 표식 없이 나옵니다(백로그).
- **`--check-text` 는 빠진 번역도 실패로 봅니다 — 프로젝트의 출시 문화권(`*.locproject.json` 의 `stagedCultures`)만**(2026-10-15, 2026-10-16 기준 변경). 언리얼 Localization Dashboard 는 완성도만 보여 주지만,
  런타임이 원문으로 대체하며 아무 경고도 내지 않아 한국어 실행에서도 빠진 번역이 드러나지 않았습니다. 처음에는 "번역 테이블 파일이 있는가" 로 대상을 정했는데, 파일 하나를 지우면 게이트가 조용히 꺼졌습니다.
  지금은 언리얼 패키징 설정의 Cultures to Stage 처럼 출시 목록을 명시하고(`cultures` 의 부분 집합, 아니면 로드 오류), 목록에 있는데 테이블이 없으면 전부 빠진 번역으로 집니다.
  목록 밖 문화권(엔진 `ja`, PO 만 낸 것)은 번역 중으로 보고 개수만 보고합니다. 게임 설정 하나에 두는 안은 엔진 · 에디터 프로젝트가 팩 설정이 없어 버렸습니다.
  CI 는 App 을 띄우지 않으므로 같은 판정을 `LocalizationGatherTest.RepositoryProjectsAreUpToDate`(EditorTest, nogpu)가 합니다.

### 5-3. Core 와 플랫폼

- **macOS 는 지원 대상이 아닙니다**(2026-10-04). 코드는 지웠고 `CheckTargetMacros` 가 `SW_PLATFORM_MACOS` 를 막습니다. 복원하려면 그 전 git 기록에서 찾습니다.
  `Vcpkg.cmake` 의 APPLE 분기와 레거시 스탬프 이관, `arm64-osx` · `x64-osx` 트리플릿은 손대지 않는 파일이라 사용자가 정리합니다.

- **파일 존재 술어는 `FileUtil::exists`(무엇이든), `isDirectory`, `isRegularFile` 셋입니다**(2026-10-06). `exists` 는 `std::filesystem::exists` 와 같은 뜻입니다. 경로 전용 타입(`FilePath`)은 만들지 않습니다.
  파일 오류는 그 자리에서 경고하고 `[[nodiscard]] bool` 로 돌려주며, 실패를 알리지 않는 시도는 `FileUtil::tryRemoveFile` 하나입니다. 언리얼 `IFileManager::Delete` 의 `bQuiet` 에 해당합니다.
- **`std::hash<fixed_string>` 은 대소문자를 구분합니다**(2026-10-06). `string` 과 `wstring` 의 해시와 같은 규칙입니다.
- **`fixed_string` 은 길이 필드 없이 배열 하나만 듭니다**(2026-10-11). `size()` 는 앞 N 칸의 경계 있는 검색이고, 길이를 정하는 연산이 꼬리를 0 으로 채워
  바깥이 `data()` 에 널 없이 써도 길이가 맞습니다. 캐시하던 `mutable` 길이는 const `size()` 가 써서 읽기끼리 데이터 레이스였습니다.
  EASTL `fixed_string` · folly `FixedString` 은 길이를 들지만 바깥이 버퍼에 직접 쓰는 계약이 없습니다. 이 저장소는 ImGui · `formatstring` · Win32 가 `data()` 에 쓰므로 C 배열과 같은 계약을 택했습니다.
  넘침은 지금처럼 잘라 경고하고, 잘림이 틀린 값이면 `try_assign` · `try_append` 를 씁니다. 측정은 Release, 같은 PC 에서 옛 · 새 바이너리를 번갈아 세 번 돌린 중앙값입니다(`FixedStringBenchTest`).

  | 항목 | 전 | 후 |
  |---|---|---|
  | `sizeof` `fixed_string<15>` · `<31>` · `<63>` · `<160>` | 20 · 36 · 68 · 168 | 16 · 32 · 64 · 161 |
  | `sizeof` `fixed_wstring<15>` | 36 | 32 |
  | `sizeof` `CrashBreadcrumbStore` · `CrashContextStore` | 5380 · 4996 | 5156 · 4660 |
  | `fixed_string<64>::size()`(길이 8~47) | 5.7 ns | 5.7 ns |
  | `fixed_string<64> = const utf8*` | 8.6 ns | 8.8 ns |
  | `fixed_string<64> = fixed_string<64>` | 8.4 ns | 2.0 ns |
  | `fixed_string<64> == fixed_string<64>` | 6.1 ns | 5.5 ns |
  | `clear()` + 짧은 `append` — `<64>` · `<8192>` | 7.9 · 10.1 ns | 5.5 · 68.7 ns |

  `clear()` 가 O(N)이 된 것이 유일한 손해입니다. `fixed_string<8192>` 를 되풀이해 비우는 호출처는 없습니다(8KB 는 로그 줄 버퍼 하나이고 만들 때 한 번 0 으로 채웁니다).
  꼬리를 길이만큼만 채우는 안은 ImGui 가 널 뒤에 남긴 옛 글자를 지우지 못해 기각했습니다. 작은 배열(256 바이트 이하)의 대입은 배열 전체를 먼저 0 으로 채웁니다. 크기가 컴파일 때 정해져 그 자리에 펼쳐지기 때문입니다.
- **Win32 API 는 W 버전을 이름으로 부르고, A 버전도 막습니다**(2026-10-06). 전역 `UNICODE` 정의는 대상마다 정의가 빠지면 말없이 A 버전으로 돌아가서 택하지 않았습니다. 언리얼, 유니티, Godot 모두 Windows 경계에서는 W 버전만 씁니다.
  X11 창 제목은 `XStoreName` 과 `_NET_WM_NAME` 을 함께 적습니다. SDL2, GLFW, Godot 과 같은 방식입니다.
- **벽시계는 `WallClock` 하나만 읽습니다**(2026-10-06). 서비스는 현재 시각을 매개변수로 받고, 테스트는 가짜 시각을 넣습니다.
- **문자열 타입은 담는 것으로 고르고, `string` 을 `fixed_string` 으로 일괄 치환하지 않습니다**(2026-10-10). 규칙은 [AGENTS.md](../AGENTS.md#function-names) 에 있습니다.
  `sw::string` 은 MSVC STL 의 SSO 라 15바이트까지 힙을 쓰지 않고, `sizeof` 는 32입니다. `fixed_string<N>` 은 4의 배수로 올린 (N+1) 에 4를 더한 크기이고,
  N 을 넘으면 잘라 내며 경고하고, `size()` 는 매번 `strlen` 입니다. 그래서 짧은 이름은 이득이 없고, 긴 텍스트는 잘리며 스택만 커집니다.
  후보를 조사했더니 프레임 경로를 지나는 것은 0건이었습니다. 프레임 경로의 이름은 `hashed_string` 이나 기존 저장소를 가리키는 `string_view` 로 넘깁니다
  (`FrameRenderer::resolvePresentSource` 가 사본 대신 뷰를 돌려줍니다). `fixed_string` 은 상한이 코드 상수이고, 힙이 금지된 경로(크래시 · 로그 · 시그널)나
  고정 버퍼가 필요한 곳(ImGui 입력)에만 씁니다. 언리얼도 `FString` 을 기본으로 두고, 인라인 버퍼(`TStringBuilder<N>`, 넘치면 힙으로 늘어남)는 임시 조립에만 씁니다.
- **Engine 폴더 사이의 작은 고리는 "위층이 아래층 등록표에 등록" 과 "둘을 다 아는 층으로 옮기기" 로 풉니다**(2026-10-11, 엔진 분할 1 단계). 자동화 실행기는 UI 를 모르고 UI 가 단계 · 로딩 화면 시작 차단 조건(`SW_AUTOMATION_START_BLOCKER`)을 올립니다 — 언리얼 Automation 도 모듈이 테스트 · 래치 명령을 등록하고 실행기는 그 모듈을 모릅니다.
  정점 애니메이션 굽기(`MeshVertexAnimationBaker`)는 `Object/SceneAnimation` 으로, 물리 디버그 선 어댑터는 조립점 `Engine.cpp` 로 옮겨 `Graphics` 가 `Animation` · `Physics` 를 include 하지 않습니다 — 언리얼 AnimToTexture 도 애니메이션과 렌더를 함께 드는 별도 플러그인입니다.
- **엔진 내장 에셋 캐시는 기능 폴더가 등록하고 `AssetManager` 는 등록표만 봅니다**(2026-10-11, 엔진 분할). 캐시 클래스의 .cpp 에 `SW_BUILT_IN_ASSET_CACHE( MaterialCache )` 한 줄을 두고(`Resource/Cache/BuiltInAssetCache.h`),
  매니저는 생성자에서 표를 훑어 소유 캐시를 만들고 종류 이름 순으로 올립니다. 타입으로 찾는 창구는 `getAssetCache<T>()` 입니다(`getMaterialCache` 넷은 지웠습니다).
  언리얼 서브시스템(모듈이 타입을 올리고 엔진이 인스턴스를 만들며 `GetSubsystem<T>()` 로 찾는다) · 고도 `register_types` 의 `add_resource_format_loader` 와 같은 방향입니다 — 유니티 `ScriptedImporter` 도 특성으로 등록되고 `AssetDatabase` 는 구현 타입을 모릅니다.
  **에셋 캐시는 서브시스템으로 만들지 않습니다**(2026-10-14, C-27 ② 판정). 캐시는 경로 → 에셋 표일 뿐 세울 것 · 내릴 것이 없고(`IAssetCache` 에 초기화 · 종료가 없다),
  캐시끼리 기대는 순서도 없다(비우기 순서는 종류 이름 순으로 고정했고, 에셋 사이 참조는 공유 포인터가 든다). 판정 기준 "수명 범위 · 초기화/종료 순서 · 의존이
  있는 서비스" 에 들지 않는 값 저장소다. 수명은 이미 `AssetManager`(소유 캐시) · 소유한 시스템(UI 문서 · 게임 데이터 캐시의 등록/해제)이 정한다. 언리얼도
  자산 캐시(`FStreamableManager` · 셰이더 맵 캐시)를 서브시스템으로 두지 않는 것으로 알고 있다.
  그래서 `Source/Engine/Resource/AssetManager.cpp` 의 배선 예외가 없어졌습니다. 모듈(게임 · 키트)의 캐시는 이 표가 아니라 `registerAssetCache` 로 올리고 내립니다(표 항목은 내리지 않으므로).
- **씬마다 하나인 기능 서비스는 기능 폴더가 등록하고 `GameObjectManager` 는 레지스트리만 봅니다**(2026-10-12, 엔진 분할 2 단계). 서비스 .cpp 에 `SW_SCENE_SERVICE( SceneNavigation, Navigation )` 한 줄을 두면 매니저가 생성자에서 만들고, 그 줄의 프레임 단계에서 `update` 를, `clear` 에서 `onSceneCleared` 를 부르며, 찾는 창구는 `getSubsystem<T>()` 입니다(`Object/GameObject/Service/ISceneService.h`).
  씬 서비스는 서브시스템이고 매니저의 씬 범위(`SubsystemScope`, 부모는 엔진 범위)가 소유한다(2026-10-14, C-27 ①) — 선언한 의존으로 세우고 같은 단계의 갱신도 그 순서다.
  언리얼 `UWorldSubsystem` · `UTickableWorldSubsystem` 과 같은 자리로 알고 있다. 프레임 단계 갱신(`update`)은 서브시스템 계약 밖의 씬 서비스 몫으로 둔다 —
  초기화 · 종료는 수명이고 갱신은 프레임 단계 표(`SceneFrameStepList.xxx`)의 자리라서.
  언리얼은 `UNavigationSystem` 이 월드에 등록되는 서브시스템이고(`UWorld::GetSubsystem<T>()`), 유니티의 `NavMeshManager` 도 씬 코어 밖 모듈이라 같은 방향입니다 — 그래서 `GameObject/` 는 `Component/Navigation` 을 include 하지 않습니다(`getSceneNavigation` 은 지웠습니다).
- **`AABB` 는 Core 수학 타입입니다**(2026-10-11, 엔진 분할). `Engine/Physics/Collision/AABB.h` → `Core/Math/AABB.h`(`Frustum.h` 옆) — 언리얼 `FBox` · `FBoxSphereBounds` 와 유니티 `Bounds` 도 물리가 아닌 수학 타입입니다. 레이어를 보는 겹침 질의 `queryOverlaps` 만 물리의 `CollisionLayers.h` 에 남고, `Spatial` 은 티어 4 → 1 로, Object → Physics include 는 41 → 36 으로 줄었습니다.

- **서브시스템(초기화 · 종료 짝 + 의존 순서)은 Core 의 계약입니다**(2026-10-14). `ISubsystem` · `SubsystemCollection` 은 `Core/Subsystem/`(티어 4 — 로그를 남기지 않아
  Log 아래)에 있고, 호스트 종류는 Core 의 비트 마스크(`SubsystemHostMask`)이며 비트의 뜻(클라이언트 · 전용 서버)은 엔진(`EngineSubsystemHost`)이 정합니다.
  언리얼은 서브시스템을 `UEngineSubsystem` · `UWorldSubsystem` 처럼 수명 범위마다 나누고 `ShouldCreateSubsystem` 으로 호스트를 거르며, 순서는 `InitializeDependency`
  호출로 실행 중에 정하는 것으로 알고 있다. 여기서는 같은 모양(서브시스템이 이름 · 의존 · 호스트를 스스로 선언하고 목록이 호스트로 거른다)을 취하되 두 가지를 더 했다:
  의존은 선언 전체로 위상 정렬하고(동점은 이름 순 — 결정적), 의존 칸은 다른 서브시스템의 이름 상수(`kSubsystemName`)를 가리켜 철자가 틀리면 컴파일 오류다.
  모르는 이름 · 순환 · 호스트 어긋남은 `EngineInitSequenceTest` 가 호스트 종류마다 걸러진 실제 목록으로 잡는다(`EveryHostKindSortsItsOwnSubsystems`).
  목록은 로그를 남기지 않고 오류 글을 돌려준다 — 로거도 이 목록의 서브시스템이 될 수 있게 하기 위해서다. 정렬 오류는 부른 쪽이 표준 오류로 낸다.
  이름 상수는 층을 따라 아래로만 include 되므로, 낮은 층(비동기 IO · 물리)이 높은 층(모듈 이미지 · 리소스)보다 늦게 서야 하면 높은 층이 역방향 의존(`getSubsystemDependents`)으로 적는다 —
  목록에 없는 이름은 건너뛰어 "있으면 그 뒤" 인 선택 의존을 겸한다. 정방향 의존은 없으면 오류다. 언리얼에는 역방향 칸이 없는 것으로 알고 있다(층 규칙이 없어 필요가 없다).
- **서브시스템이 서비스를 소유하고, 컬렉션 하나가 범위 하나입니다**(2026-10-14, 정리 ④).
  호스트(엔진 루프 · 테스트 하네스)는 세 범위를 든다 — 부트스트랩(진단 도구 · 이름 풀 · 로거 · 명령줄+전역 변수 · 크래시 핸들러 · 리소스 루트 · 메모리 예산 ·
  정적 저장소), 서비스(이벤트 · 지역화 · 커맨드 스택 · 디버그 도구 · 프로파일러 · 외부 프로파일러 · 크래시 보고 · 백엔드 레지스트리), 엔진(기동 단계).
  메모리 예산은 추적만 부트스트랩에서 켜고 예산 표는 첫 프레임 끝에 읽는다 — 표는 리플렉션으로 읽는데 타입 등록부가 엔진 범위다(2026-10-14, C-27 ③;
  언리얼도 LLM 은 기동 초에 켜고 예산은 설정 뒤에 읽는 것으로 알고 있다). 엔진을 세우지 않는 도구(OnlineLoadBot)도 같은 부트스트랩 서브시스템을 쓴다(C-27 ⑥). 아래 범위가 다 선 뒤 위 범위가 서고,
  위 범위가 다 사라진 뒤 아래 범위가 내려간다. 서비스 범위는 "엔진 범위의 누가 소멸자에서 부르든 살아 있는" 옛 수명(저장소가 기동 앞에 만들고 뒤에 놓던 것)을
  구조로 지킨다 — 엔진 범위로 내리려면 쓰는 쪽마다 의존을 적어야 하는데, 소멸자 · 종료 경로의 쓰임을 모두 찾을 수 없어 택하지 않았다.
  세 범위는 게임 · 에디터와 같은 `SubsystemScope`(5-8)이고 타입 해시로 찾는다. 언리얼은 범위마다(엔진 · 월드 · 게임 인스턴스) 컬렉션이 있고
  `GetSubsystem<T>()` 가 부를 때마다 맵을 찾는 것으로 알고 있다. 여기서 엔진 창구(`engine::getSubsystem<T>()` 의 칸)는
  소유 · 채우기 없이 **바인딩 때 한 번 찾아 둔 조회 캐시**로 남는다 — 캐스트마다 타입 레지스트리를 묻는 자리가 이름 조회를 하지 않게. 필수 행이 비면 호스트가
  서지 않는다(옛 `CheckEngineServiceBinding` 게이트를 대신한다). 서브시스템이 아닌 행(값 묶음 · 소유한 서브시스템 안의 객체)은 호스트가 그 객체를
  범위에 `bind` 한다. 테스트는 하네스 범위를 부모로 둔 범위에 같은 타입을 넣어 서비스를 갈아 끼운다(`test::ScopedEngineServiceOverride`).
  하네스는 자기 호스트 비트(`kTestHarness`)로 서서 클라이언트 전용(글꼴 · UI · 텔레메트리 · 플레이어 설정)을 표의 자리로만 둔다.
  모듈(게임 · 에디터)이 받는 호스트 서비스도 범위다(정리 ④c): 호스트가 `SubsystemScope` 에 표의 행을 그 타입으로 걸어 포인터를 넘기고
  (`engine::bindModuleServices` — 게임 모듈에는 `GameVisible` 행만, 에디터에는 모듈 컴파일러까지), 모듈은 자기 범위 다음에 그 범위에서 찾는다.
  서비스 id 열거형 · id 배열 표(`ModuleServiceTable`) · 호스트 서비스 목록은 지웠다 — 찾는 열쇠는 범위와 같은 타입 해시 하나다. 엔진 루프 ·
  부트스트랩 · 헤드리스 헤더는 호스트만 include 한다(`CheckEngineLayers` 가 Games · GameFramework · Editor 에서 막는다).
- **서비스 접근은 소유 수명마다 한 창구, 한 어휘다 — `getSubsystem<T>()`(반드시 있음, `T&`) · `findSubsystem<T>()`(없을 수 있음, `T*`)**(2026-10-15, C-28 마감).
  엔진은 `engine::`, 씬은 `GameObjectManager::`, 게임은 `game::`(게임 범위 → 호스트), 에디터는 `editor::`(에디터 범위 → 호스트)이고 소유 객체(`EditorContext` · `GameInstanceBase`)의 멤버도 같은 두 이름이다.
  이름 붙은 엔진 접근자 서른 개(`engine::getTypeRegistry()` …) · `getBoundEngineServices` · `getService<T>` · `getSceneService<T>` · `isServiceBound<T>` 는 지웠다(별칭 없음).
  언리얼이 범위 객체마다 `GetSubsystem<T>()` 를 두는 것과 같은 모양으로 알고 있고, 유니티 서비스 로케이터 · Godot `Engine::get_singleton( "이름" )` 의 이름 조회는 철자 오류가 실행 때에야 드러나 택하지 않았다.
  우리 쪽 확장 셋: ① 엔진 칸은 바인딩 때 한 번 찾고 템플릿이 칸 색인을 컴파일 때 고른다 — 목록 밖 타입이나 빌 수 있는 행(`SW_ENGINE_SERVICE_OPT`)에 `getSubsystem` 을 쓰면 `static_assert` 다.
  ② `CheckServiceAccess` 가 옛 창구와 새 `static getInstance()` 를 막는다(상태 없는 코덱 · 드라이버 · 링크 때 등록 표만 예외 표에 이유와 함께). ③ 목은 범위로 넣는다
  (`test::ScopedEngineServiceOverride` · `game::ScopedGameScope` · `editor::ScopedEditorScope`). 호스트가 게임 서비스를 묶지 않은 실행(쿠커 · 테스트 하네스)에서 게임 창구는 엔진 범위를 본다
  (`Engine/Common/EngineScope.h` — 게임 모듈을 돌리지 않으니 가릴 것이 없다). GameFramework 의 표 읽기가 타입 레지스트리를 이 창구로 찾는다.
- **스레드는 `sw::thread`, 백그라운드 잡의 상태는 한 번에 읽는다**(C-24). `Source/` 에서 `std::thread` 를 직접 만들면 `CheckStdThread` 가 막는다
  (예외: 래퍼 자신 · 크래시 재현 테스트). `sw::thread` 는 합류 대기를 잠금 관찰자에 알려 `join` 순환을 교착 검출기가 보게 하고, 로더 락 안의
  `join`(`DllMain` · 모듈 정적 소멸자)은 멈추는 대신 단언한다. `Test/` 의 쓰고 버리는 스레드는 대상이 아니다. 에디터 백그라운드 잡의
  `EditorBackgroundTask::take` 는 결과 유무와 대기 여부를 같은 락 안에서 읽어 `EditorTaskPoll`(`Idle` · `Running` · `Ready`) 하나로 돌려준다 —
  `take` 가 비었다고 `isPending` 을 따로 읽으면 그 사이 끝난 결과를 새 요청이 버린다(E-9 Quick Open 에서 나온 결함). 요청 여부는 `take` 의
  반환값으로 정하고, `isPending` 은 화면 표시용이다. 풀을 가리키는 핸들(`TaskHandle` · `TaskStageHandle`)의 수명 순서는 세대가 아니라
  참조 계수로 닫혀 있다 — 매니저가 사라질 때 남은 핸들이 풀을 이어받고(`TaskNodePool::detachOwner`) 마지막 핸들이 풀을 소멸시킨다.
- **엔진 루프 클래스는 `Engine` 이고 서비스 네임스페이스 `engine` 은 그대로 둔다**(2026-10-15, 정리 ⑥). 둘은 대소문자만 다르지만 C++ 에서 클래스(`sw::Engine`)와
  네임스페이스(`sw::engine`)는 다른 이름이고, 이 저장소의 규칙(타입은 PascalCase · 네임스페이스는 소문자, 예 `editor`)이 그대로 구분한다. 네임스페이스를 바꾸지 않는 까닭: `engine::` 가 엔진 범위 창구(`engine::getSubsystem<T>()`)의
  수명 이름이라(`game::` · `editor::` 와 짝) 이름만 바꾸는 큰 변경은 얻을 것이 적다. 클래스를 `EngineLoop` 로 두지 않는 까닭: 이 객체는 루프만이 아니라 부트스트랩 · 서비스 · 엔진 세 범위와 프레임 관찰자 ·
  종료 코드를 소유하는 엔진 호스트의 본체다. 언리얼의 `UEngine`(`GEngine`)도 틱 루프와 서브시스템 범위를 함께 가진 한 객체로 알고 있다(기억에 의존). 로컬 변수 `engine` 은 네임스페이스를 가리지 않는다 —
  한정 이름 조회(`engine::`)는 타입과 네임스페이스만 본다. 별칭 · 옛 이름은 두지 않는다(파일 `EngineLoop.h` · `.cpp` → `Engine.h` · `.cpp`, 멤버 `_engineLoop` → `_engine`).
- **호스트 범위가 엔진 범위의 부모다 — 엔진 앞에 서서 뒤에 사라지고, 호스트가 엔진에 내주는 것을 든다**(2026-10-15, 정리 ⑥). App · ServerApp 이 `SubsystemScope _host` 를 `_engine` 보다 앞에 두고
  (`Engine::initialize` 의 마지막 인자로 넘겨 부트스트랩 범위의 부모로 잇는다. 사슬은 엔진 → 서비스 → 부트스트랩 → 호스트). 거기에 거는 것은 둘이다.
  ① **모듈 이미지 창구**(`ModuleImageHostSubsystem`, `IModuleImageHost` 로 드러낸다): 엔진의 `ModuleTypes` 단계가 `loadTypeProviders` 를, `ModuleImages` 의 종료가 `onScenesReleased` 를
  자기 범위에서 부모로 올라가며 찾아 부른다. 옛 `Engine::setModuleTypeLoader` · `setOnScenesReleased` 와 델리게이트 멤버 둘은 지웠다 — 엔진이 호스트의 델리게이트를 들던 고리가
  "부모 범위의 호스트에 의존" 이 됐고, 호스트 범위가 없는 하네스는 자리만 둔다. DLL 언로드는 그대로 씬 뒤 · 서비스 앞(엔진 범위 종료의 `ModuleImages` 자리)에서 일어난다 —
  엔진 범위 종료는 모든 서브시스템에 `stopSubsystem` 을 먼저 돌린 뒤 `shutdownSubsystem` 을 도는 두 패스이고, 훅은 `ModuleImages` 의 종료 패스에서 `getSubsystemDependents`(입력 · 오디오 · 씬 · 비동기 IO) 뒤에 불린다.
  ② **자동화 구동기**(`AutomationSession`, App 만): 서브시스템이 됐다. 초기화는 하는 일이 없고 `-scenario` 를 읽은 App 이 엔진이 선 뒤 `begin` 으로 시작한다. 구동기는 엔진 서비스(로그 리스너 · 입력)를 빌려 쓰므로
  엔진 종료 앞에서 내려야 하는데 호스트 범위는 엔진 종료 안에서도 창구 때문에 살아 있어야 한다 — 그래서 창구가 `getSubsystemDependents` 로 구동기를 뒤에 서는 것으로 적고
  App 이 `shutdownDependentsOf( 창구 )` 로 구동기만 먼저 내린다(그 API 는 원래 백엔드 교체용이다). `Dev` 호스트 비트는 두지 않았다: 자동화는 Shipping 구성에서도 쓴다
  (`AppScenarioTest` 가 Shipping 에서 백엔드 스위치 없이 시나리오를 돌린다)라 `#if !defined( SW_SHIPPING )` 로 감쌀 개발 도구가 아니다. 개발 도구가 이 범위에 오면 호스트가 그 한 자리에서 `#if` 로 넣는다.
  언리얼은 엔진 서브시스템(`UEngineSubsystem`)과 게임 인스턴스 서브시스템의 부모 · 자식으로 수명을 가르고, `FEngineLoop` 이 모듈 로드 순서를 쥐는 것으로 알고 있다(기억에 의존).
  `OnlineServerRuntime` 의 `_bHostStarted` · `_bSchedulerStarted` 는 지웠다 — 호스트(`OnlineServiceHost::shutdown`)는 자기 초기화 비트를 보고 스케줄러는 목록을 비울 뿐이라 시작 여부를 밖에서 적을 필요가 없었다.
  테스트 하네스(`TestHostRuntime`)는 호스트 범위를 두지 않는다 — 모듈 DLL 을 올리고 내리지 않고 타입은 리플렉션 단계가 이미 모았다(`EngineInitSequenceTest.ModuleImageStepsReachTheHostScopeThroughTheParentChain` 이
  범위 사슬 찾기를, 호스트 범위가 없을 때의 무동작을 지킨다).
- **압축 해제의 원본 크기 상한은 코덱이 낸다 — `ICompressionCodec::decompressBound` 는 순수 가상이다**(2026-10-17, B-5). 야간 퍼저 `Compression` 대상이 81 바이트 입력(RLE · 원본 620 MB · 압축 0 바이트)으로
  10 초 타임아웃을 냈다. 헤더 검사는 절대 상한(`kMaxUncompressedSize`, 1 GiB)만 봐서 통과했고, 620 MB 를 resize(0 채우기)한 뒤에야 빈 페이로드로 실패해 clear 가 원소마다 소멸 루프를 돌았다.
  뿌리는 헤더 값을 압축 크기와 견주지 않고 할당한 것이다. 최대 팽창률은 포맷마다 다르므로(RLE 65 · LZ4 255 · Deflate 1032 · Zstd 32768배) 코덱이 답하고, `CompressionStream` 은 두 해제 경로 모두
  할당 전에 `findCodecForHeader` 에서 견준다. 기본 구현("모르면 무한대")을 두지 않는 까닭: 그 코덱으로 적힌 스트림은 다시 헤더를 그대로 믿게 되어 같은 결함이 조용히 돌아온다. 절대 상한은 남긴다 —
  팽창률만으로는 Zstd 100 KB 가 3 GB 를 요구해도 통과한다. 같은 날 `vector` 의 소멸 루프는 `destroyRangeInternal` 하나로 모아 소멸자가 할 일이 없는 타입이면 루프를 두지 않고(`if constexpr`),
  `resize` 의 값 초기화는 산술 · 열거 · 포인터 타입의 64 칸 이상 구간만 `std::memset` 으로 한다(멤버 포인터의 널은 0 바이트가 아닌 ABI 가 있어 사용자 구조체는 넣지 않았다.
  짧은 구간까지 memset 으로 하면 크기가 변수인 함수 호출이 되어 `resize( size() + 1 )` 벤치가 Release 에서 두 배 느려졌다). 최적화 빌드는 원래 그 루프를 지우므로 Debug 와 계측 빌드의 비용만 준다.

### 5-4. 데이터, 직렬화, 설정

- **엔진 데이터에는 별칭을 두지 않습니다**(2026-10-03). 실제로 출시한 게임 데이터가 없으므로, 이름을 바꾸면 데이터를 다시 씁니다. 옛 `SaveGame` 파일도 읽지 않습니다. 계약은 Serialization README 에 있습니다.
  - 이 결정은 지금 이관할 데이터가 없다는 사실에 기댄 것이지 영구 금지가 아닙니다(사용자 확인, 2026-10-17). 출시한 데이터가 생겼을 때 바로 쓰도록 별칭(`Alias` · `ValueAlias` · `Removed`)과 이관 등록은 미리 갖춥니다([이름 변경 리다이렉트 계획](plans/RenameRedirects.md)).
- **이관 함수를 호출부에 넘기던 API 를 타입 옆 한 번 등록으로 바꿨습니다**(2026-10-17). 연결 방식만 바뀐 것이고 정책은 위 결정 그대로입니다. 스키마가 바뀌면 기본은 데이터를 다시 쓰는 것이고, 이관은 값 계산이 불가피한 예외입니다. `SW_SCHEMA_MIGRATION` 이 (타입, 기록된 버전)으로 `SchemaMigrationRegistry` 에 올리고, 세 형식의 버전 붙은 읽기가 타입과 그 부모로 찾으므로 `deserializeVersioned` · `Archive` 는 이관 함수를 받지 않습니다. 필요한데 없으면 오류로 멈추고, 같은 자리의 중복 등록은 모든 구성에서 프로세스를 멈춥니다. 같은 버전에서 지운 필드를 버려도 되는지는 이관이 아니라 orphan 정책이라 호출부가 컨텍스트로 정합니다(`setAllowUnknownProperties`, 오브젝트 상태만 켬).
  - 등록은 생성기가 모으지 않고 정적 등록자로 했습니다([이름 변경 리다이렉트 계획](plans/RenameRedirects.md) R5 초안과 다름). 이관 함수는 손으로 쓰는 C++ 이라 생성기가 읽을 선언이 없고, 애노테이션으로 함수 이름을 적어도 결국 같은 이미지의 정적 저장소에 함수 포인터를 둡니다. 언리얼 `FCustomVersionRegistration` 과 같고, 기존 `RegistrationList` 가 핫 리로드 대체와 언로드 때 빼기를 이미 맡습니다.
  - 찾은 이관 함수는 잠금 밖에서 부릅니다. 그래서 모듈 언로드는 직렬화가 도는 동안 하지 않습니다. 지금 언로드 경로(`LiveReloadManager::drainTasksBeforeUnload`)는 비동기 씬 로드를 취소하고 작업을 비운 뒤 내립니다.
- **설정 파일에는 기본값과 다른 값만 적습니다**(2026-10-06). 언리얼의 `Default*.ini` 와 같은 규칙이고, `ConfigFileSchemaTest` 가 기본값을 다시 적은 키를 막습니다.
- **설정 값의 출처는 하나씩입니다**(2026-10-06). 시작 씬은 팩의 `gamesettings.xml`, 창 제목은 게임 프리셋의 `GameConfig::_windowTitle`(언리얼 ProjectName), 고정 스텝은 `EngineConfig::_fixedDeltaTime` 입니다.
  물리는 고정 스텝 위에 서브스텝 수(`_subStepCount`)만 둡니다. 유니티의 `Time.fixedDeltaTime` 이 하나인 것과 같습니다. 기본 RHI 도 한 곳에만 적습니다.
- **앱이 다시 쓰는 에디터 상태는 `Saved/Editor/` 에 둡니다**(2026-10-06). `Config/Editor/` 에는 사람이 쓰는 설정만 남깁니다.
- **텍스처와 모델 임포트 설정은 키 테이블입니다**(2026-10-06). 리플렉션 구조체로 만들지 않았고, `PROPERTY( Config )` 메타도 지웠습니다. 엔진 타입에 키트 설정 필드를 두지 않고, 키트 설정은 팩 데이터에 둡니다.
- **자체 JSON 로더를 리플렉션 구조체로 옮기는 기준**(2026-10-12). 모듈 매니페스트, `AnimationLODSettings`, `MemoryBudgetFile`, `FacialRig`, `LipSync` 는 옮겼습니다. 다음은 옮기지 않습니다.
  `TextureImportConfig` · `ModelImportConfig` 는 적지 않은 값이 부모 프리셋을 따르는 "적지 않음" 의미가 필요한데 리플렉션은 "없음" 과 "0" 을 가르지 못합니다.
  `CrashReportService` 는 동적 JSON 문서이고, JWT · Chrome 트레이스 · VRM 은 외부 형식입니다.
  `CultureInfo` 는 적지 않은 필드가 부모 문화권을 따르고(`parent`), `LocalizationDocuments` 는 키가 데이터인 문자열 표이며, `AnimationGraphAsset` 은 노드 종류마다 필드가 다른 그래프라
  고정 구조체 하나로 읽을 수 없습니다(2026-10-14 R4 판정).
- **자체 포맷 에셋 로더 넷도 리플렉션으로 읽고 씁니다**(2026-10-17, C-19 R4). `SpriteClipAsset` · `SequenceAsset` · `DialogueGraphAsset` · `Skeleton` 은 키가 PROPERTY 이름이고,
  저장소 데이터는 새 키로 다시 썼습니다(옛 키 별칭 없음, 임포트 결과 스켈레톤은 `--import-models` 로 다시 만듦). "적지 않음" 을 초기값으로 읽으면 안 되는 칸(스켈레톤 전부,
  대화 노드 · 링크 id 와 핀)은 `PROPERTY( Required )` 로 두어 빠지면 오류입니다 — `ConfigManager::readConfigJSON` 이 구조체 칸과 구조체 목록 원소 안까지 봅니다.
  종류(시퀀스 항목 · 트랙, 대화 노드)는 정수 대신 열거자 이름이고, 모르는 종류를 읽어 두고 적용만 않던 규칙과 모르는 노드 종류를 Dialogue 로 읽던 폴백은 없앴습니다.
  범위 밖 프레임 번호도 자르지 않고 오류입니다. 스프라이트 구간이 프레임 밖이면 자르고 알리는 것만 남겼습니다(에디터가 프레임을 지운 뒤 저장할 수 있다).
- **`sw::vector<bool>` 은 쓰지 못하고, 불 목록은 `DynamicBitset` 입니다**(2026-10-17, A-18). 기본 구성의 `sw::vector<bool>` 은 원소마다 바이트였고 `SW_ENABLE_STL_CONTAINER` 구성에서는
  비트로 묶는 `std::vector<bool>` 이라, 같은 이름이 구성마다 다른 의미였습니다(STL 구성에서 리플렉션 래퍼가 원소 주소를 잡지 못해 컴파일이 멈춤). `VectorElementCheck` 가 두 구성에서
  같은 `static_assert` 로 막습니다. 언리얼 `TArray<bool>` 은 바이트 배열이고 비트 묶음은 `TBitArray` 로 따로 둡니다(기억에 의존). 리플렉션은 `DynamicBitset` 을 값 하나로 다룹니다.
  자기 기술형은 '0' · '1' 글(텍스트 처리기), 고정 배치형은 `MaxCount` 가 필요한 `bits`(개수 + 비트마다 1 비트)이고, 원소 주소를 쓰는 컨테이너 래퍼를 거치지 않습니다.
- **`Min` · `Max` 범위 검사는 시퀀스 원소까지 늘 봅니다**(2026-10-17, A-15). `SerializerUtil::collectRangeErrors` 에서 범위를 고르는 인자(`RangeCheckScope`)를 지웠습니다.
  설정 JSON · 데이터 XML · 세이브가 같은 `Min` · `Max` 를 다르게 지키던 것을 한 계약으로 합쳤고, 프리셋 표가 원소마다 따로 부르던 검사도 지웠습니다.
- **열거자 이름의 정본은 리플렉션 `ENUM()` 입니다**(2026-10-15, 같은 해 10-12 의 "enum 이름 테이블 45 곳 유지" 를 뒤집음). 철자가 열거자 이름과 같은 손 이름 표 · `toString( Enum )` 은 지우고
  `getEnumName( value )`(`Engine/Reflection/EnumName.h`)로 읽습니다. 언리얼은 `UENUM` + `UEnum::GetValueAsString`, Godot 은 ClassDB 열거 상수가 이름의 유일한 출처로 알고 있습니다(기억에 의존).
  손 표는 `static_assert` 가 개수만 지키고 순서 · 철자의 어긋남은 못 막았고, 쓰는 곳이 거의 로그였으므로 "배열 인덱스가 빠르다" 는 이득이 없습니다.
  남기는 것: 철자가 다른 와이어 · 데이터 · 지표 문자열(`AdminRole` 의 `"viewer"`, `HealthState` 의 소문자, 서비스 지표 라벨, `EnumNameTable` 로 읽는 데이터 이름)과 Core 의 열거형(Core 는 리플렉션 아래 티어)입니다.
  철자가 데이터에 박힌 이름 표를 리플렉션으로 옮기려면 데이터를 열거자 철자로 다시 씁니다(별칭 없음).
- **같은 빌드끼리만 읽는 바이트는 고정 배치형(B) 코덱이고, 생성기가 모든 적격 `REFLECT` 타입에 만듭니다**(2026-10-17, [리플렉션 직렬화 형식 계획](plans/ReflectionFormats.md) 단계 1).
  사용자는 B 를 선언하지 않고, 등록 API 가 `FixedLayout::findCodec` 으로 확인합니다. 세이브와 에셋은 자기 기술형(A) 그대로입니다. 언리얼의 `NetSerialize` · Iris 디스크립터처럼
  선언(`UPROPERTY`)에서 와이어 형식을 도출하는 방식이고, 손 코덱의 손 버전 상수 대신 정규 문자열 해시가 형식 변경을 잡습니다. 단계 1 에서 정한 세부는 아래와 같습니다.
  - 버전 상수 이름은 AGENTS "Constants" 를 따라 `FixedLayout::kVersion` 입니다(계획 초안의 `kFixedFormatVersion`).
  - 읽기 결과에 `TrailingData` 를 더했습니다. 남은 비트를 `OutOfRange` 로 부르면 원인이 섞이기 때문입니다.
  - `float64` 는 64 비트 원본으로 씁니다. `Step` 은 float32 에만 붙습니다.
  - 수학 타입은 `ParserDefines.h` `fixedlayout::kArrMathType` 목록으로 정합니다. 구조에서 추측하면 핸들처럼 모양만 같은 타입까지 적격이 됩니다.
  - `vector<string>` 처럼 가변 길이가 두 겹이면 `MaxCount` 하나로 길이를 정할 수 없으므로 적격이 아닙니다.
  - 다른 헤더의 중첩 타입과 기반 타입은 실행 시간에 코덱을 찾아 의존을 한 번 확인합니다(계획 8.1 위험 2).
  - 코덱이 없는 타입을 진입점에 넘기면 이유를 로그에 남기고 프로세스를 멈춥니다(모든 구성). 0 해시 같은 대체 값을 돌려주면 단계 4 의 핸드셰이크 해시에 그 값이 섞입니다.
  - 정수 경계는 float32 로 정확히 나타나야 합니다(`Min` · `Max` 를 float32 로 보관합니다). 크기가 아니라 정확성이 기준이라 `OnlineServerConfig::_serverID` 의 `Max = 2000000000` 은 그대로 씁니다.
  - `MaxCount` 는 새 애노테이션 종류 `count` 로 1 이상의 정수만 받습니다. 실수로 읽어 반올림하지 않습니다.
  - 양자화 읽기는 칸 번호를 직접 읽어 칸 수를 넘으면 `OutOfRange` 입니다. 손 코덱이 쓰는 `BitReader::readQuantizedFloat` 도 같은 규칙으로 바꿨습니다(2026-10-17). 범위 밖 칸은 실패이고, `BitWriter::writeInt` · `writeQuantizedFloat` 는 범위 밖이면 쓰지 않고 `false` 입니다.

  실행 파일 크기(Debug, 단계 전후)는 `Engine.dll` +153,088 B(+0.7 %), `GameFramework.dll` +81,408 B(+1.0 %)입니다. 이때 `REFLECT` 타입 454 개 가운데 적격은 173 개,
  적격이 아닌 것은 281 개였습니다(대부분 `MaxCount` 없는 가변 길이와 표 밖 타입입니다). 기존 `.gen.cpp` · `.gen.h` 1069 개는 덧붙은 줄만 있고, 바뀐 줄은 ABI 스탬프 하나였습니다.
- **이름 변경 리다이렉트는 선언 옆에 적고, 리다이렉트 표는 그 이름의 주인이 듭니다**(2026-10-17, [이름 변경 리다이렉트 계획](plans/RenameRedirects.md) R1~R4). 선언은 기존 키를 그대로 씁니다.
  타입 · 열거형의 옛 FQN 은 `REFLECT( Alias )` · `ENUM( Alias )`, 열거자의 옛 이름은 `ENUM( ValueAlias = "Old:New" )`, 지운 열거자의 대체는 새 키 `ENUM( Removed = "Old:Replacement" )`, 필드의 옛 이름은 `PROPERTY( Alias )` 입니다.
  유니티 `FormerlySerializedAs` 처럼 이력이 선언 옆에 있고, 언리얼 `CoreRedirects` 처럼 호출부는 모릅니다. 다만 표를 전역 하나로 모으지 않고 `EnumInfo::_mapRedirectNameToValue`, `PropertyInfo::_listAlias`,
  `TypeRegistry` 별칭 표에 둡니다. 언리얼이 전역 표인 것은 리다이렉트가 ini 에서 오기 때문이고, 여기서는 선언이 등록과 함께 오므로 주인에 붙여야 상속 필드 복사 · 핫 리로드 재등록 · 모듈 해제 묘비를 그대로 따릅니다.
  읽기는 지금 이름을 모두 본 뒤에만 옛 이름을 봅니다(`EnumInfo::findValueByNameHash` · `findValueByText`, `SerializerUtil::findPropertyIndexByNameHash` · `matchProperty`). 텍스트 읽기도 같은 표를 씁니다.
  옛 이름은 `_mapNameToValue` 에서 빠져 인스펙터 목록과 쓰기에 나오지 않습니다. 대상이 없거나, 옛 이름이 지금 열거자 · 필드이거나, 같은 옛 이름을 두 번 적으면 생성 오류입니다(예전에는 경고만 하고 버렸습니다).
  선언 없이 지우거나 바꾼 이름은 지금처럼 엄격한 읽기가 실패합니다. 리다이렉트는 바이트 배치와 무관하므로 고정 배치형 정규 문자열에 넣지 않습니다(`FixedLayoutTest.RenameRedirectsDoNotChangeTheSchemaHash`).
  `.gen` 1108 개를 전후 생성기로 비교해 바뀐 파일은 `ValueAlias` 를 쓰는 `TestSampleActor.gen.cpp` 하나였습니다.
- **`REFLECT` 타입은 비가상 베이스를 하나까지만 둡니다**(2026-10-17, [리플렉션 직렬화 형식 계획](plans/ReflectionFormats.md) 단계 7-b). 둘째 베이스와 가상 베이스는 베이스가 `REFLECT()` 인지와 상관없이
  헤더 · 줄 · 타입 · 베이스 이름과 함께 생성 오류입니다(`AstVisitor.cpp` `collectBase`). `castTo` 는 리플렉션 부모 사슬로 파생을 판단하고 C++ 상속이 보이지 않으면 `reinterpret_cast` 로 내려가므로,
  부모가 offset 0 에 있지 않으면 포인터 보정 없이 잘못된 주소가 됩니다. 전에는 `REFLECT()` 없는 둘째 베이스(인터페이스)를 받았지만 제품 코드에는 그런 타입이 없었고, 그 모양을 지키던 테스트 픽스처만 지웠습니다.
- **열거형 이름 조회는 값이 빈틈없으면 생성된 배열 인덱스이고, `getEnumName<E>` 는 `EnumInfo` 를 함수 지역 캐시에 둡니다**(2026-10-17, 같은 계획 3.13 · 단계 7-a). API 는 그대로입니다.
  생성기가 표식(`Invalid`, `Count`)을 뺀 값이 `0..N-1` 인 열거형에 intern 된 이름 배열을 만들고 열거자마다 `static_assert` 로 libclang 의 값과 컴파일러의 값을 대조합니다(생성기는 `SW_DEBUG` 없이 헤더를 읽습니다).
  캐시는 타입 테이블 세대가 같을 때만 씁니다. 모듈 해제는 묘비에서 배열 포인터를 비우고, 레지스트리 파괴도 세대를 올립니다. 이름에서 값으로 가는 경로와 리다이렉트 표는 바꾸지 않았습니다.
  배열을 받은 열거형은 198 개이고, 기존 `.gen` 은 덧붙은 줄만 있었습니다(바뀐 다른 줄은 지운 7-b 픽스처뿐). 실행 파일 크기(Release, 7-a · 7-b 전후)는 `Engine.dll` +12,800 B(+0.08 %),
  `GameFramework.dll` +5,120 B(+0.10 %), `App.exe` -512 B 입니다. 시간은 재지 않았습니다.
- **`GameObject::getTypeInfo` 는 `StaticType()` 결과를 캐시하지 않습니다**(2026-10-17, 단계 7-a 에서 넣었다가 병합에서 뺐습니다). 측정한 이득이 없고(`StaticType()` 이 이미 `TypeLookupCache` 를 거침), 함수 지역 정적 포인터는 엔진을 다시 만들면 해제된 `TypeInfo` 를 가리킵니다.
- **테스트만 쓰는 공개 타입은 상용 엔진 대응으로 가릅니다**(2026-10-15). 조사에서 제품 코드 참조 0 인 공개 타입이 654 개였습니다. "사용처 0 이어도 상용 엔진에 대응이 있는 공개 API 는 남긴다" 를 그대로 적용합니다.
  대응이 있는 것(언리얼 · 유니티 · Godot 에 같은 기능이 있는 키트 규칙 · 엔진 API)은 남기고 테스트를 붙여 둡니다. 대응이 없고 테스트를 돕기만 하는 타입(테스트용 목 · 고정값 · 한 테스트만 부르는 `Util`)은
  `Test/` 로 옮기거나 그 테스트 파일 안으로 넣습니다. 언리얼도 `Developer` · 테스트 모듈을 런타임 모듈과 가릅니다. 일괄 삭제는 하지 않고 영역을 고칠 때 그 영역의 목록을 정리합니다.
- **한 타입만 다루는 `Util` 은 그 타입의 메서드로, 한 파일만 쓰는 `Util` 은 그 `.cpp` 안으로 옮깁니다**(2026-10-15, C-26 ②). 언리얼 `FBuoyancyData` · Godot 의 값 타입 메서드처럼 데이터가 자기 계산을 듭니다(기억에 의존).
  옮긴 곳: 부력 → `BuoyancyPontoon`, 찢김 → `SurfaceMask`, 체형 모프 · 스키닝 → `AppearanceGeometry`, 팟 → `PokerPot`, JWT 서명 → `JSONWebToken`, JWK 쓰기 → `JwksKeyCache`,
  인벤토리 → 거래 항목 → `TradeClient`, 에디터 서브시스템 등록 함수 → `EditorSubsystemRegistration`, 복수형 키워드 → `CultureInfo`(규칙 표는 `CultureInfo.cpp`), RHI 환경 판정 → `App.cpp`, 프레임 단계 실행 → `ImGuiEditor.cpp`.
  테스트만 부르던 일곱(JWT 서명 · JWK · 팟 · 거래 항목 등)은 상용 엔진 · 라이브러리에 대응 기능이 있어 제품 코드에 남겼습니다.
- **확장자 → 에디터 종류의 정본은 종류 표(`EditorAssetType.cpp`) 하나입니다**(2026-10-15, C-26 ④). 엔진이 읽는 포맷은 그 타입이 `kExtension` 을 들고(`Material` · `AnimationGraphAsset` · `SpriteClipAsset` · `SequenceAsset` · `TileMapXMLData` · 오디오 데이터 둘을 더함)
  표는 그 상수를 가리키며, 씬 · 프리팹은 쿠킹 규칙(`AssetCookPath`)을 따릅니다. `EveryEngineFormatMapsToItsKind` · `EveryResourceDataKindIsData` 가 엔진 포맷과 데이터 파일 종류를 이 표에 대어 봅니다.
  언리얼은 팩토리 · 에셋 타입 액션이, Godot 은 `ResourceFormatLoader::get_recognized_extensions` 가 포맷마다 확장자를 드는 것과 같은 방향입니다(기억에 의존). 파일이 하나도 없던 맨 접미사(`.seq` · `.sprite` · `.dialogue` · `.tilemap`)는 별칭이라 지웠습니다.
- **`UIStyleField` 서술자 표는 필드마다 플래그 한 칸이고, 필드 이름은 `WidgetStyle` 리플렉션 PROPERTY 이름입니다**(2026-10-15). 플래그를 PROPERTY `Meta` 로 옮기는 안은 기각했습니다 — 커스텀 메타는 Shipping 에 없습니다.
- **이벤트(`MulticastDelegate` 필드)가 받는 애노테이션은 필드 표의 Emit 열이 `Display` 인 줄입니다**(2026-10-15). 표시 메타(Category · DisplayName · Tooltip · Meta · HideInInspector · HideInMenu)를 `Editor` 에서 갈라 `Display` 로 적고, 파서의 손 목록을 지웠습니다. 생성 코드는 같습니다.
- **게임 · 키트 튜닝 값의 데이터화**(2026-10-15, C-26 ④): 작물 색 → `CropDef::_color`(`crops.xml` 의 `color`), StarSkirmish 유닛 색과 판 규칙(사냥 주기 · 도착 반경 · 시작 광물) → 디렉터 PROPERTY,
  MeadowVillage 칸 색 → 디렉터 PROPERTY, `ActionRoom` 플레이어 조절 값 → `ActionRoomSettings`, 오버월드 걸음 시간 → `OverworldTileMoverSettings::_stepSeconds`. 와이어 양자화 범위(`MechArenaSnapshot` 등)는 형식이라 데이터로 옮기지 않습니다. 2026-10-17 부터 손 상수가 아니라 스냅샷 구조체의 `PROPERTY( Step, Min, Max, MaxCount )` 가 그 범위입니다.
  손 이름 표 중 철자가 열거자와 같은 `TelemetryUploadResult` · `CrashReportUploadResult` · `TelemetryRecordResult` · `AIDirectorEventKind` 는 `ENUM()` 으로 옮겼고, 소문자 데이터 철자인 `CrashReportConsent` · `CrashReportState` · `AIDirector` 신호 · 풀은 `EnumNameTable` 로 남깁니다.
  부하 봇은 동작 이름 · 받는 칸 · 받는 값을 동작 표 한 줄(`LoadBotScenario.cpp`)로 모았습니다.
- **유지보수성을 위한 데이터화**(2026-10-15, 사용자 기준). 코드에는 불변식(수학 상수, ABI · 프로토콜 필드 번호 · 상한, 세이브 태그 · 버전, 비트 마스크, enum 1:1 순서 표, 로그 · assert 문자열, PROPERTY 메타 리터럴)만 남기고
  튜닝값 · 이름 표 · 기본값 · 색 · 크기 · 목록 · 표는 데이터(PROPERTY, 리플렉션 데이터, `Config/*.json`, 리소스 XML)로 둡니다. 정한 모양 —
  ① **코드 폴백 정의를 두지 않는다**: 액션 룸의 내장 몬스터 · 배치와 `MonsterCatalog` 의 `default_monster` 를 지우고 카탈로그 · `ActionRoomLayout` 이 없으면 오류(언리얼 DataTable 이 행을 못 찾으면 null 인 것과 같다).
  ② **엔진 기동 설정은 바탕 층 파일**: `Config/Engine/EngineConfig.json` 이 값의 정본이고 헤더는 형식만이다(언리얼 `BaseEngine.ini`). 바탕 층은 모든 키를 적고(`ConfigFileSchemaTest`), 프로젝트 설정 창은 `REFLECT( Meta = "ConfigBaseLayer" )` 를 보고 모든 키로 저장한다.
  파일 · 구운 사본이 모두 없으면 형식(0)이 범위 검사에서 져 기동이 멈춘다. 기동 전 리소스 우선순위는 구운 사본에서 읽는다. `_window._defaultRHI` 만 쿠킹 표가 정본이다. `FixedTimestep` 의 "0 이하면 기본값" 폴백도 지웠다.
  ③ **표는 엔진 데이터 하나**: 회선 흉내 프리셋은 `engine/settings/netemulation.presets.xml`(`NetEmulationPresetTable`, 유니티 Network Simulator 프리셋 자산 자리) — Core 는 리플렉션 아래라 표를 읽지 않고 조건만 갖는다. 에디터 단추 이름도 그 표의 `_label`.
  UI 미리보기 해상도는 `editor/settings/uipreview.resolutions.xml`. 그림자 품질 단계의 한 변은 설정 스키마 선택지의 `targetValue`(`gv_shadowMapResolution`, 언리얼 Scalability ini 의 `r.Shadow.MaxResolution` 자리).
  ④ **데이터 속성은 스키마가 기본값 · 필수 · 범위를 선언한다**: `GameDataXML::readFields` 가 리플렉션 Def 를 읽고 모르는 속성 · 빠진 `PROPERTY( Required )` · `Min`/`Max` 밖 값을 이름과 함께 오류로 낸다. 읽는 코드의 리터럴 기본값과 `MathUtil::max` 로 조용히 자르기는 쓰지 않는다
  (첫 적용 `ParkLayout`, 데이터는 다시 썼다). 리플렉션 XML 파일(`ReflectedXMLFile::loadDesc`)도 모르는 이름 · 범위 밖 값을 이름과 함께 알린다.
  ⑤ 부하 봇 시나리오의 `_count` · `_timeoutMs` · `_minMs` 는 필수다(읽는 코드의 숨은 기본값 제거). ⑥ 새로 들어오는 것은 `CheckDataDrivenLiterals`(추가한 줄 검사, 전체는 기준선)가 막는다.
  에디터 색 · 아이콘 표는 디자인 토큰 작업(editorux 묶음)이 맡는다.
- **설정 · 기본 에셋의 경로 칸은 `DataPath` 형식입니다**(2026-10-16, 데이터 주도 W0). `Engine/Serialization/Base/DataPath.h` — 문자열 · 리터럴로 만들 수 없고(생성자 삭제)
  리플렉션 데이터 읽기("DataPath" 텍스트 처리기)만 값을 넣는다. 칸은 `PROPERTY( Required )` 로 두고 읽기가 빠진 칸을 오류로 알린다. 첫 적용은 `EngineDefaultAssets` 의 경로 42 칸 —
  값은 `enginedefaultassets.xml` 에만 있고, 표는 `EngineDefaultAssets::load` · 서브시스템만 만든다(`ConstructKey`, `REFLECT( Abstract )`). 못 읽으면 기동 · 쿠킹이 멈춘다. 테스트는
  `test::getEngineDefaultAssets()`(하네스가 읽은 표)로 경로를 얻는다. 언리얼 `FSoftObjectPath` 가 ini 에서만 채워지는 설정 칸과 같은 자리이고, 게이트가 아니라 형식으로 막는다.
- **게임플레이 튜닝 값은 컴포넌트 `PROPERTY` 나 키트 설정 구조체 필드로 둡니다**(2026-10-06). 언리얼의 UPROPERTY 와 데이터 에셋 방식입니다. 성능 임계값은 측정으로 정한 이름 붙은 상수로 두고, 언리얼처럼 실제로 조절할 것만 전역 변수로 만듭니다.
- **중력의 출처는 물리 설정 하나입니다**(2026-10-06). 코드는 `PhysicsSystem::getConfiguredGravity` 로 읽고, 셰이더(파도)는 머티리얼 상수로 같은 값을 받습니다. 언리얼의 `UWorld::GetGravityZ` 에 해당합니다.
- **4 글자 표식은 리틀 엔디언 FourCC 하나로 통일했습니다**(2026-10-06). 세이브와 쿠킹 바이너리를 다시 만들었습니다.
- **엔진의 각도 필드는 라디안입니다**(2026-10-06). 도 단위 필드를 두지 않고, `PropertyUnitsTest` 가 이 규칙에 예외를 두지 않습니다.
- **이름으로 다른 표를 가리키는 데이터는 저장소 전체를 정적으로 훑어 검사합니다**(2026-10-15, 참조 감사). 런타임이 모르는 이름을 기본값으로 대신하고 로그만 남기는 곳(물리 레이어 · 재질, 정렬 레이어, 내비 영역,
  입력 액션 · 레이어, 카탈로그 id)은 그 장면을 돌리기 전엔 드러나지 않습니다. 언리얼 Data Validation · 유니티 missing reference 검사는 에디터에서 연 에셋만 보지만, 여기서는 에셋 검증 규칙
  `component_reference`(기반 타입의 파생까지 · 팩별 표)로 일곱 팩 전부를 커밋 훅 · CI(nogpu)에서 봅니다. 규칙은 데이터 한 줄이고 검사기는 타입 · 칸 · 표만 압니다.

- **이름은 상용 엔진과 견주어 정한다(2026-10-10, 이름 점검).** 이름 점검이 모호하다고 한 것을 모두 바꾸지 않는다. 그대로 두는 것: `GameObjectManager`(한 씬의 `GameObject` 를 만들고 소유하고 틱한다),
  `GPUScene`(언리얼 `GPUScene` 과 같은 용어), `PhysicsOverlapWorld`(겹침 질의 전용 월드 — 강체 씬 `IPhysicsScene` 과 헷갈리지 않게 2026-10-13 `PhysicsWorld` 에서 바꿨다), `GameConfig`(프리셋 JSON) 와 `GameSettings`(팩의 `gamesettings.xml`) — 언리얼도 프로젝트
  설정과 게임 설정이 따로 있다, `GameplayAbilityDef`(카탈로그 정의: id · 클래스 이름 · `GameplayAbilityConfig`)와 `GameplayAbilityConfig`(어빌리티 하나의 설정) — 정의가 설정을 품는 층이라 둘 다 필요하다,
  `AbilitySpec`(언리얼 `FGameplayAbilitySpec` — 부여된 인스턴스), `TypeInfo` · `Info` 값 묶음, `InputManager` · `AssetManager`. 나눈다: `ResourceUtil` 은 경로 쪽 `ResourcePaths` 와 읽기 쪽 `ResourceIO` 로(언리얼 `FPaths` / `FFileHelper` 가 같은 선 — 끝났다),
  `EditorUtil` 은 경로를 `EditorPaths` 로 뺀다. `FrameRendererUtil` 은 그대로(언리얼 `RenderUtils` 도 같은 묶음). 파일 이름과 타입이 어긋난 곳은 `EditorColor.h`(색 타입 + 색 상수 묶음) · `EventType.h`(이벤트 ID 와 `IEvent`) · `Crafting.h`(제작 모듈 헤더)를 그대로 둔다 — 파일이 한 모듈의 묶음이다. 기각한 제안의 목록과 이유는 아래 표. `MathUtil::getRandom` · `getRandomRange` 는 스레드마다 씨앗이 다른 비결정 난수라 그대로 둔다(언리얼 `FMath::Rand` 와 같은 자리) — 결정적 난수는 `Core/Math/XorShift32.h` · `HashUtil::splitMix64Next`.

이름 점검(2026-10-10)의 제안 가운데 **기각한 것**(이름이 이미 맞다):

| 제안 | 이유 |
|---|---|
| `GameObjectManager` → `SceneObjects` | 하는 일을 정확히 말한다. `SceneObjects` 가 더 흐리다. 헤더 주석("씬 하나의 얼굴")만 관계를 정확히 고친다 |
| `GPUScene` → `GpuFrameData` | 언리얼의 `GPUScene`(인스턴스 데이터를 GPU 에 올려 둔 장면)와 같은 용어다 |
| `InputManager` → `InputHub` | 싱글턴 허브라는 결정(이 문서)과 `Manager` 접미 관례 그대로 |
| `AssetManager` → `AssetFacade` | 캐시 묶음이라도 `Manager` 로 일관(`GameObjectManager` · `ConfigManager` 와 같은 층)(그 안의 `getMaterialManager` 는 `getMaterialCache` 로 바꿨다) |
| `TypeInfo` 세 뜻 → `*KindRow` | `RenderPassTypeInfo` 는 "패스 종류의 정보"를 이름 그대로 담는다 |
| `OverlapInfo` · `CollisionInfo` · `HitInfo` → `*Event`/`*Contact` | 접촉 결과 값 묶음이고 `Info` 가 맞다(언리얼 `FHitResult` 와 같은 자리) |
| `ComponentRegistry` → `*Index` 등 | `Registry` 는 이 저장소에서 "등록된 것의 목록"으로 일관 |
| `WeakInternTable` → `WeakSharedTable` | `Intern` 은 `hashed_string` 과 같은 쓰임 |
| 열거형 `Auto` · `Normal` · `Custom` · `Count` | 열거형 안에서만 뜻이 서고 각자 문맥이 분명하다. `Misc = 0`(`HorrorCatalog.h`) `Misc = 0`(`HorrorCatalog.h`) 하나만 `None` 으로 바꿨다 |
| 통지 동사(`notify` · `broadcast` · `publish` …) 규칙화 | 추정에 그친다 |

약어 철자 · 어휘표 · 접미사는 AGENTS.md "Function names" 와 [코딩 규칙 예시](04_CodingGuidelines.md) 2 절에 있다.

### 5-5. 그래픽스와 RHI

- **RHI 디바이스 안은 서브시스템으로 만들지 않는다 — 부품은 타입 있는 멤버로 명시 소유하고, 종료 순서는 `IRHIDevice::shutdown` 의 네 단계가 정한다**(2026-10-14, 백로그 C-26 을 닫음).
  언리얼 RHI 도 내부 레지스트리 없이 디바이스가 부품을 명시로 들고 소멸 순서로 정하는 것으로 알고 있다. 해제 큐 · 장부 · 프레임 링 · 타임스탬프는 이미 공용 클래스이고
  핫 패스는 타입 있는 포인터로 부르므로 컬렉션으로 옮겨도 남는 이득은 종료 순서뿐인데, 실제 결함(네이티브 객체와 GPU 완료 사이)은 컬렉션 밖에 있다.
  대신 순서를 어기면 터지게 했다: 2 단계(GPU 대기) 뒤와 4 단계 뒤에 해제 큐가 비었는지, 끝난 종료 뒤에 해제가 쌓였는지를 `shutdown` 이 단언한다
  (`getPendingReleaseCount`, nogpu 테스트 `RHIDeviceShutdownTest`). 점검에서 Vulkan 이 4 단계에서 오프스크린 프레임버퍼를 해제 큐에 넣고 비우지 않던 것을 고쳤다.
- **셰이더 쿠킹 스탬프의 도구 지문은 버전 상수로 만든다 — 컴파일 옵션 판 · Vulkan 타깃 판 · 컴파일러 버전 글(`ShaderCooker::kCompilerVersion`, vcpkg `directx-dxc` 포트 버전)**(2026-10-14).
  컴파일러 라이브러리 파일의 내용 해시로 만들었더니 Windows 에서 쿠킹해 커밋한 스탬프가 리눅스(다른 라이브러리 파일)에서 모두 낡은 것으로 판정돼 리눅스 CI 다섯 잡이 졌다.
  언리얼은 셰이더 DDC 키에 컴파일러 바이너리 해시가 아니라 셰이더 포맷 · 컴파일러 버전 식별자를 넣는 것으로 알고 있다(기억에 의존 — 단정하지 않는다). 같은 원칙을 따르고,
  사람이 고치는 상수가 실제 설치와 어긋나지 않게 `TestShaderCompilerVersionPin.py` 가 vcpkg 설치 기록과 대조한다(언리얼보다 한 겹 더 지킨다).

- **런타임 UI 를 위해 RHI 에 R8 텍스처, 영역 업로드, 프리멀티플라이 블렌드, 가위 사각형을 더했습니다**(2026-10-06). 사각형 클리핑은 가위로 합니다. Slate, Dear ImGui, Godot 모두 같은 방식이고, 배치가 끊기지 않습니다.
  둥근 모서리 클리핑만 셰이더에서 합니다.
- **프리멀티플라이 블렌드는 PSO 비트(`_bPremultipliedAlpha`)이고, 머티리얼 블렌드 enum 에는 넣지 않습니다**(2026-10-06). 언리얼도 머티리얼의 `EBlendMode` 와 RHI 블렌드 상태가 따로입니다. 머티리얼이 프리멀티플라이를 원하게 되면 그때 enum 에 더합니다.
- **보기 모드는 뷰마다의 설정입니다**(`RenderViewSettings::_viewMode`, 2026-10-10). 에디터 씬 뷰 툴바는 씬 뷰에만 걸고 게임 뷰는 주 출력 모드(Lit)로 그립니다 — 언리얼 뷰포트(뷰 패밀리)마다의 View Mode, 유니티 Scene 뷰 Draw Mode 와 같습니다.
  Normals · Depth · Overdraw 는 표면 값을 보이므로 후처리 · 톤맵을 건너뜁니다(언리얼 버퍼 시각화와 같다). Overdraw 는 RHI 에 블렌드를 더하지 않고 알파 0 의 프리멀티플라이로 더하며, 깊이 테스트는 남기고 쓰기만 끕니다(테스트를 끄면 Vulkan 이 깊이 첨부 없는 PSO 로 만든다).
- **톤맵을 HDR 씬 컬러로 바꾸는 일은 하지 않았습니다**(2026-10-06). 기본 포워드 파이프라인의 Reinhard 가 흰색을 0.5 로 누르지만, 바꾸면 골든 이미지를 모두 다시 떠야 합니다. 화면이 어두운 게임은 데이터(체력 바 보임 정책 등)로 맞췄습니다.
- **점광 · 스폿 그림자는 큐브 · 배열 텍스처가 아니라 2D 그림자 아틀라스입니다**(2026-10-13). 유니티 URP · Godot 방식이고, RHI 를 바꾸지 않으며 DX11 · GL 의 엔진 텍스처 슬롯 네 개가 그대로입니다.
  방향광 캐스케이드(최대 4)도 같은 아틀라스 안의 타일이고, 추가 뷰는 자기 볼륨 · 그림자 컬링 슬롯을 씁니다.
  칸이 모자라면 주 시점의 화면 크기 대리값(경계 구 반경 / 카메라 깊이)이 큰 빛부터 받습니다(2026-10-17, `ShadowSetupUtil::computeLocalShadowPriority`) — 언리얼이 그림자 해상도 · 아틀라스 자리를
  투영한 화면 반경으로 고르는 것과 같은 기준입니다. 목록 순서였을 때는 씬에 먼저 놓인 먼 빛이 눈앞의 빛의 그림자를 밀어냈습니다. 화면 크기에 따라 칸 크기를 바꾸는 일은 하지 않았습니다(칸은 한 크기).
- **깊이 패스(그림자 · 깊이 프리패스)는 모양을 머티리얼 코드가 정하는 배치만 머티리얼 셰이더로 그립니다**(2026-10-17, `FrameRendererUtil::drawsDepthWithMaterialShader`).
  알파 컷오프는 두 깊이 패스 모두 머티리얼 셰이더의 깊이 퍼뮤테이션(`SW_PASS_SHADOW` · `SW_PASS_DEPTH_PREPASS`)에 픽셀 스테이지(`clip` 만)를 두고, 정점 변형(바람)은 그림자 패스만 정점 스테이지로 그립니다.
  나머지는 shadowdepth.hlsl 하나를 나눠 씁니다 — 언리얼이 Masked · World Position Offset 머티리얼만 자기 깊이 · 그림자 셰이더를 컴파일하고 불투명은 기본 머티리얼의 것을 쓰는 것과 같습니다.
  shadowdepth.hlsl 에 머티리얼 값을 읽게 하는 안은 기각했습니다 — 머티리얼마다 구조체 배치가 달라 공통 셰이더가 컷오프 값 · 텍스처 칸을 찾을 수 없고, 정점 변형 코드는 옮길 수 없습니다.
  뎁스 전용 PSO 도 PS 경로가 있으면 픽셀 스테이지를 둡니다(`RHIShaderRequest::resolveGraphics`). 패스 PSO 는 FrameRenderer 가 PS 경로를 비우므로 그대로입니다.
- **지형 쓰기는 "사본을 고쳐 사각형만 넘기는" 한 길입니다**(`D2-5` (7), 2026-10-15, `TerrainComponent::writeHeights` · `writeLayerWeights`). 에디터 브러시(`TerrainBrush::applyHeight` ·
  `applyLayerWeight`)가 이미 N × N 배열을 제자리에서 고치는 모양이라, 지형은 읽기용 배열을 내주고(`getHeightSamples` · `readSplatWeights`) 고친 배열에서 **바뀐 사각형만** 복사해 받습니다.
  개별 샘플 세터나 브러시 타입을 Engine 에 두지 않았고(브러시는 Editor 의 것), 되돌리기는 편집 전 사본을 같은 사각형으로 다시 쓰면 되므로 Undo 용 별도 API 가 없습니다.
  읽는 쪽(병렬 틱 · 배치 계산)과 겹치지 않게 하는 방법은 이중 버퍼나 락이 아니라 **호출 규칙**입니다 — `reloadTerrain` 과 같이 틱 밖(`executeOrDeferPostTickOnComponent`)에서, 프레임에 한 번만 부릅니다.
  락을 읽기 경로 모두에 넣으면 질의마다 비용이 붙고, 이중 버퍼는 N × N 높이를 통째로 두 벌 들며, 에디터 스트로크는 어차피 프레임에 한 번이라 둘 다 얻는 것이 없습니다.
  높이를 쓰면 닿는 청크(노멀이 이웃 샘플을 읽으므로 한 샘플 바깥까지, 경계 샘플은 양쪽 청크)의 메시와 바운드를 그 자리에서 다시 만들고, 바뀐 샘플을 쓰는 청크의 세대(`getChunkGenerations`)를 올려
  그 청크의 물리 바디만 다시 만들어지게 합니다(바디는 청크마다 하나 — 언리얼 랜드스케이프의 컴포넌트별 충돌과 같다). 내비메시는 `SceneNavigation::invalidateArea` 로 그 자리 타일만 다시 굽고,
  식생은 지형 세대가 0.3 초 동안 그대로일 때(드래그가 끝난 뒤) 다시 뿌립니다 — 칠하는 프레임마다 숲 전체를 다시 계산하지 않습니다.
  가중치는 CPU 스플랫 바이트를 고치고 바뀐 사각형만 `TextureCache::requestRegionUpdate` 로 맡깁니다. 렌더 스레드가 다음 프레임 기록 전에 RHI 영역 업로드(`uploadTexture2DRegion`, 네 백엔드)로
  한 번에 올립니다 — 업로드 비용은 호출당이라 프레임마다 사각형 하나입니다. 칠하는 텍스처는 **지형마다 자기 것**입니다(언리얼 랜드스케이프 가중치 맵 · 유니티 `TerrainData` 알파맵으로 알고 있다).
  지형을 읽을 때 그 지형만의 CPU 텍스처(`TextureCache::createCPUTexture`, `cpu/` 경로)를 가중치 에셋(`.layerweights`, 5-10 절)으로 만들어 머티리얼에 겁니다. 원본 픽셀은 텍스처가 들고 구간 업로드가 함께 고치므로
  디바이스를 다시 만들어도 칠한 그림이 남고(백엔드 교체 · 모듈 리로드는 씬을 다시 읽으므로 저장하지 않은 높이 · 스플랫은 `TerrainComponent::writeState` 가 상태 스냅샷에 싣는다), 같은 가중치 에셋을 쓰는 다른 지형은 칠해지지 않습니다.
  에디터 도구는 `TerrainBrushTool`(뷰포트 Terrain 토글)이고 드래그 하나가 되돌리기 하나입니다(닿은 사각형의 전 · 후 값만 담는다). 저장은 `saveHeightfield`(16 비트로 다시 양자화)와
  `saveSplat`(`.layerweights`)입니다. 가중치 사각형은 스플랫 **텍셀** 좌표이고 브러시 격자의 해상도는 스플랫 너비입니다(정사각형 스플랫만 브러시가 다룹니다).
- **저장하지 않은 지형 편집은 상태 스냅샷으로 넘깁니다**(2026-10-15, `64c1fef75`). 교체 전에 묻는 대신(`setSaveBlockReason`) `GameInstanceBase` 가 `TerrainComponent` 를 `registerStatefulComponent` 로 올려 마지막 다시 읽기 뒤의 높이 · 스플랫을 싣습니다. 언리얼 랜드스케이프 편집도 저장 전까지 메모리의 레벨에 남는 것으로 알고 있습니다 — 렌더러를 바꾸는 일이 편집을 잃게 하면 안 됩니다.
- **`FrameRenderer` 는 "한 씬의 프레임을 그래프로 그린다" 만 들고, 이미지를 꺼내는 일 · 진단 덮어쓰기 · 캔버스 렌더 텍스처 대상을 가릅니다**(2026-10-15, C-26 ⑤).
  캡처 · 되읽기(Present 캡처 텍스처, 트랜지언트 · 임의 텍스처 읽기와 파일 쓰기)는 `FrameCapture`(`getCapture()`) — 언리얼이 스크린샷 · 프레임 그랩(`FScreenshotRequest` · `FFrameGrabber`)을
  장면 렌더러 밖에 두는 자리이고, 렌더러의 디바이스 포인터 변수 · 주 시점 풀 · 파이프라인을 참조로 빌린다(렌더러가 `execute` 마다 디바이스를 다시 받으므로 사본을 들지 않는다).
  진단 덮어쓰기 넷은 값 하나 `FrameDebugOverrides`(언리얼 `FEngineShowFlags` 처럼 값으로 주고받는다)이고 배포본 렌더러에는 칸도 세터도 없다. 렌더 텍스처 대상(월드 공간 UI)의 빌리기 · 다시 그리기 판정은
  이미 경로 그림을 캐시에서 빌리고 대상별 사각형 자리를 들던 `CanvasRenderer` 로 옮겼다(언리얼 `FSlateRHIRenderer` 자리). 그대로 둔 것: 추가 뷰(`ViewTarget`)는 언리얼 `FViewInfo` 처럼
  장면 렌더러 안의 뷰 상태라 남기고, 미리보기 씬 렌더러 목록은 주 렌더러와 디바이스 수명 · 파이프라인 · 작업 관리자를 함께 써서 남기며(밖으로 빼면 백엔드 교체의 종료 순서를 두 곳이 맞춰야 한다),
  정점 변형 컴퓨트는 이미 `FrameMeshDeformState` 값이고 PSO 는 `RenderPSOCache` 라 더 가르지 않는다(기억에 의존한 비교).
- **메시 없는 드로우의 슬롯 0 은 장치 공용 대체 정점 버퍼가 받고, 그 크기는 계약 상수 `constant::kFallbackVertexCount`(6) 입니다**(2026-10-15, 옛 백로그 A-4).
  입력 레이아웃이 하나라 SV_VertexID 로 정점을 만드는 드로우(풀스크린 3, 캔버스 사각형 6)도 슬롯 0 을 읽는데, 백엔드 넷이 풀스크린 삼각형 셋만 담아 캔버스가 범위 밖을 읽었다
  (D3D12 디버그 레이어 경고 210 "not big enough" — D3D 는 0 을 읽어 그림은 같고, Vulkan · GL 은 견고성 기능에 기댔다). 언리얼은 이런 셰이더에 빈 정점 선언을 쓰는 것으로 알고 있지만
  PSO 마다 레이아웃을 가르면 네 백엔드의 PSO 키와 Vulkan · GL location 계약을 다시 짜야 해서, 대체 버퍼 내용(`constant::arrFallbackVertex`)을 `RHITypes.h` 한 곳에 두고 크기를 상수로 맞췄다.
  캔버스는 `static_assert` 로 이 상수를 넘지 못한다. 같은 묶음에서 DX12 타임스탬프 Resolve 가 한 번도 적지 않은 칸까지 옮기던 오류(1319)도 적은 구간만 옮기게 고쳤다.
- **Debug DX12 는 디버그 레이어 메시지를 나는 그 자리에서 로그로 내고 셉니다**(`ID3D12InfoQueue1::RegisterMessageCallback`, `IRHIDevice::getValidationMessageCount`, 2026-10-15).
  메시지 큐는 장치 실패 때만 비워서 경고가 조용히 쌓였다. 언리얼 D3D12 RHI 도 디버그 레이어 메시지를 로그로 넘기는 것으로 알고 있다. 테스트가 "이 그리기는 검증 경고가 없다" 를 단언할 수 있다(캔버스 · 타임스탬프).
- **Vulkan 은 첫 `VK_ERROR_DEVICE_LOST` 뒤로 제출 · 펜스 · 큐 · 장치 대기 · 표시를 하지 않고 프레임을 건너뜁니다**(2026-10-15, 옛 백로그 A-2). DX12 가 `GetDeviceRemovedReason` 을 보고 대기를 건너뛰는 것과 같다.
  사양은 손실 뒤의 대기가 유한 시간에 돌아온다고 하지만, 손실로 제출하지 못한 프레임 펜스는 영영 신호되지 않아 그 펜스를 `UINT64_MAX` 로 기다리면 멈춘다(변이로 확인 — 테스트가 매달린다).
  Vulkan 에는 장치를 제거하는 API 가 없어 `rhi.removeDevice` 는 이 손실 상태로 두는 것으로 흉내 낸다.
- **디퍼드 그림의 기준은 `ee6fff232`(디퍼드 블룸의 밝은 부분을 반해상도 `BloomDownsample` 로) 이후입니다**(2026-10-15). 성능 기준선 `D:\w\perfbase`(`2772e1a7a`) 와 main 의 디퍼드 차이는
  이 커밋 하나에서 생겼다(DX11 · GL 로 이분 탐색 — 바로 앞 `af9d4a9b0` 은 perfbase 와 같고 이 커밋부터 main 과 같다). 밝은 윗면 둘레의 번짐이 넓고 부드러워진 의도된 변경이라 되돌리지 않는다.
  main 의 디퍼드는 네 백엔드가 바이트까지 같다(perfbase 는 DX12 · Vulkan 과 DX11 · GL 이 3 픽셀 1 단계씩 달랐다). 포워드는 perfbase 와 같다. 사이의 `40c13f841` 근처 커밋은 DX12 가 장치 정지(DEVICE_HUNG),
  Vulkan 이 시간 초과라 그 구간을 다시 탐색할 때는 DX11 · GL 만 돌린다(반복 TDR 은 어댑터를 망가뜨린다).

### 5-6. 오브젝트, 조종, 입력

- **병렬 분배는 항목 수가 아니라 잰 비용으로 고르는 공용 API 를 둔다**(2026-10-16): 고정 임계값 16 은 가벼운 항목(~25 ns)에서 너무 낮아 작은 그룹이 깨우는 비용(~20 us)을 일보다 크게 치렀다(무버 16 개 19 us 대 직렬 1 us). `TaskManager::runParallelByCost` (엔진은 `engine::runParallelByCost`)는 호출 스레드가 앞 항목을 1, 2, 4, … 개씩 직접 돌며 비용을 재고, 경과 3 us 에서 남은 일의 예상 직렬 시간이 16 us 보다 짧으면 이어서 직렬로, 아니면 나머지 구간만 나눈다(저장하는 추정 없음 — 낡을 일이 없다). 무거운 항목은 첫 항목에서 바로 나뉜다(처음 만든 "앞 16 개를 직렬로" 안은 무거운 항목에서 병렬을 막는 결함이라 버렸다). 씬 틱의 보통 길 · 선행 조건 스테이지가 쓴다. `runParallel(count, threshold)` 는 "임계값 이상이면 나눈다"는 계약을 그대로 둔다(`TaskManagerBench` 의 디스패치 바닥 측정 · 워커 청크 테스트가 기댄다). 상용 엔진(UE 틱 그룹 · Unity 의 작은 `Update` 목록)도 일이 작으면 깨우지 않는다.
  애니메이션 평가(`AnimationSystem::runPhase`, 스킨드 캐릭터 `-gv_benchCharacters=N`, Release · 3 판 중앙값, `GT.Animation.evaluate` p50): 옛 고정 임계값 8 개는 20 개에서 122 us, 완전 직렬은 81 us(비용 기반과 같다), 100 개에서 196 us(직렬 491), 비용 기반 163 ~ 180 us, 400 개에서 786 ~ 851 us(직렬 2097, 비용 기반과 같다). 캐릭터 하나가 직렬로 약 5 us 라 20 개는 깨우는 비용이 더 크고 100 개부터 병렬이 이긴다 — `AnimationSystem` 은 비용 기반으로 옮겼다(`kParallelUnitCount` 삭제). 측정한 부하는 걷기 · 대기 클립뿐이다: IK 그래프와 랙돌이 섞인 부하는 아직 재지 않았다(NEXT.md).
  실제 게임 부하 확인(2026-10-16, Release · dx12 · 자동 플레이 900 프레임 · 3 판 번갈아, `-gv_tickCostDispatch=0` 이 전): 틱이 도는 오브젝트 칸이 2 ~ 200 개뿐이라 이득이 작다. `GT.Scene.tick.components` p50 — VoxelCraft 36 → 7 us(가벼운 뷰 68 개), 나머지 일곱 게임은 같거나 1 ~ 5 us 느림(AbilityArena 7 → 8, HarvestValley 40 → 45, StarSkirmish 40 → 45, ThemePark 36 → 40, Shooter3D 45 → 49), 꼬리(p99)는 AbilityArena 20 → 53 us 등 몇 곳이 오히려 길어졌다. `GT.Frame` p50 · p99 는 모두 같다. 원인은 무거운 디렉터가 앞 항목이라 비용을 재는 동안 호출 스레드에서 직렬로 돌고 그동안 워커가 논다는 것이다. 합성 작은 씬(무버 300)은 32 → 9 us, 큰 씬(8000 · 변동 4000)은 같다(4096 개 이상은 재지 않고 바로 나눈다). 결론: 순이득은 프레임 시간의 0.1 % 안쪽이라 기본값을 바꿀 근거는 약하다 — **`-gv_tickCostDispatch` 기본은 0(옛 방식)으로 정했다**(2026-10-16, 사용자가 상용 엔진 기준으로 위임). 스위치와 API 는 남긴다. **AAA 규모로 다시 따져도 같다**: 틱 수천 개 씬은 어느 방식이든 바로 나뉘어(4096 개 이상은 재지 않는다) 차이가 없고, 갈리는 곳은 "무거운 항목이 앞에 올 때"다 — 잰 뒤 나누는 방식은 그 항목을 직렬로 돌려 꼬리(p99)를 늘리고(AbilityArena 20 → 53 us), 고정 임계값은 청크 티켓으로 다른 워커가 나머지를 바로 가져간다. AAA 프레임(16 ~ 33 ms)에서 작은 그룹이 깨우는 비용 ~20 us 는 무시되고 최악의 꼬리가 중요하다. 비용을 다시 켜려면 프레임 사이에 비용(그룹별 · 무거운 항목 위치)을 기억해 재는 직렬 구간을 없애야 하고, 그때 이 스위치로 다시 잰다. 랙돌 100 + IK 기사 20 군중(`tickCostRagdolls`, 5 판)에서도 `GT.Scene.tick.components` p50 0 us · p99 3 us 로 전후가 같아 틱 분배는 병목이 아니었다(비용은 `GT.Animation.evaluate` ~0.5 ms). 비교 케이스는 `perf-compare --cases tickCostMovers tickCostSmallScene tickCostChurn`.
- **`PostUpdate` 그룹은 서브틱으로 바꾸지 않고, `TickPhase` · 서브틱 우선순위는 유지한다**(2026-10-16, 사용자가 상용 엔진 · AAA 기준으로 위임). `PostUpdate` 사용처 41 개 중 게임 뷰 컴포넌트 다수는 "디렉터(`PrePhysics`)가 이 프레임을 끝낸 뒤 읽는다", 카메라 · 패럴랙스 · 하이라이트는 "앞 그룹이 옮긴 뒤 읽는다"이다. 둘 다 그룹 순서(그룹 사이 장벽 하나)가 정확히 표현하는 의도이고, 서브틱 선행 조건으로 바꾸면 오브젝트 사이 의존을 핸들로 걸어야 해 복잡하다. 속도는 차이가 없다(`FollowerOrderingGroupVersusPrerequisite`, Release, 4000 쌍: 순서 없음 p50 71~86 us · 그룹 69~74 us · 선행 조건 52~68 us). 서브틱 선행 조건은 같은 그룹 안의 오브젝트 사이 순서(`GameDirectorComponent::_tickAfter`)에 쓴다. `TickPhase` 의 `Early` · `Late` · `Finalize` 와 `priority` 는 제품 코드 사용이 0 이지만 UE(TickGroup · 선행 조건) · Unity(Script Execution Order) · Godot(process priority)이 모두 오브젝트 안의 순서 지정 수단을 두므로 지우지 않는다. 사용법은 `Component.h` 의 `TickPhase` 주석. 서브틱은 같은 프레임, 같은 그룹 안의 스테이지로 돈다(다음 프레임으로 넘어가지 않는다).
- **선행 조건 스테이지는 말단 오브젝트만 증분으로 끼우고 뺀다**(2026-10-16): 팔로워 4000 에 프레임마다 하나 만들고 지우면 재구축이 프레임당 559 us 였다(선행 조건 없는 변동은 37 us). 재구축 자체를 평평한 표 · CSR · 오브젝트 순번으로 짓게 해 약 275 us 로 줄였고, 아무도 기다리지 않고 선행 조건이 모두 의존 오브젝트 밖(보통 길)인 오브젝트는 스테이지를 다시 짓지 않고 그 그룹의 레벨 0 스테이지에 끼우고 뺀다(`TickRegistry::applyStageDelta` · `removeStageItems`, 스테이지가 항목마다 오브젝트 id 와 레벨을 든다) — 약 69 us. 누군가 기다리는 오브젝트가 바뀌거나 선행 조건이 다른 의존 오브젝트를 가리키면 전체 재구축이다(순서가 바뀔 수 있다). 기각: 모든 변경을 증분으로 처리(사슬 · 순환은 전체 위상 정렬이 필요하다). 증분으로 끼운 항목의 스테이지 안 자리는 전체 재구축과 같다 — 재구축은 (순서 키, 오브젝트 id) 순이라 끼울 때도 같은 키로 정렬 삽입한다(예전에는 같은 순서 키 끝에 붙어, 예전 오브젝트를 껐다 켜면 길에 따라 실행 순서가 달랐다). `IncrementalStageLayoutMatchesFullRebuild` 가 스테이지마다 (그룹, 레벨, 오브젝트 id 차례)까지 견준다.
- **`AnimationCrowd` 의 묶음 평가 분배(`kParallelBucketCount` 8)는 비용 기반으로 옮기지 않는다**(2026-10-16, Release · 5 판 번갈아 · 혼자 돌며 잰 중앙값, `-gv_benchCharacterColumns=20 -gv_benchCharacterStagger=1 -gv_benchCrowdShare=1`): `GT.Animation.crowdBuckets` p50 이 군중 1000 에서 36 → 49 us, 300 에서 32 → 49 us 로 오히려 늘었다(p99 73 → 98 · 90 us). 묶음은 수십 개뿐이고 하나가 수 us 라, 비용을 재는 동안 호출 스레드가 앞 묶음을 직렬로 돌고 나머지를 나누는 길이 고정 임계값(8 개 이상이면 바로 나눈다)보다 느리다. `GT.Animation.evaluate` p50 은 1000 에서 720 대 720, 300 에서 294 대 327 us 로 이득이 없다. 군중 포즈 공유를 끈 스킨드 캐릭터 1000 은 묶음 경로를 타지 않아 같은 코드이고(`AnimationSystem` 은 이미 비용 기반) 차이가 잡음 안이다(p50 2883 대 3145 us, 판마다 1835 ~ 3407 us 로 흔들렸다). 랙돌 300(`-gv_benchRagdolls=300`)은 `GT.Animation.evaluate` p50 1 us 라 분배가 병목이 아니다. 이 기계에서는 판 사이 흔들림(프레임 8 ~ 12 ms)이 이득보다 크므로 근거는 `crowdBuckets` 구간의 방향(전 판 B 가 높다)이다.
- **프리팹에서 사라진 컴포넌트의 오버라이드는 경고하고 버립니다**(2026-10-06). 언리얼과 같습니다. 유니티는 남겨 둡니다.
- **"그 타입의 컴포넌트 모두" 는 타입별 레지스트리(`ComponentRegistry`)에서 찾습니다**(2026-10-06). 상호작용 대상을 프레임마다 씬 전체에서 찾던 경로가 Release 10k 오브젝트에서 190 us 를 넘었고, 레지스트리로 2~6 us 가 됐습니다.
  언리얼 서브시스템의 등록 목록과 같은 방식입니다. 공간 해시는 두지 않았습니다([백로그](backlog/gameplay.md) `G-4`).
- **조종 구조**(2026-10-06): 가상 입력은 배타 모드가 기본입니다. 게임플레이 리플레이와 네트워크는 의도(`ControlIntent`)를 싣고, `InputReplay` 는 입력 층의 QA 녹화입니다.
  전략과 경영 장르는 폰 없이 디렉터가 명령 조종자가 됩니다. 시나리오는 늘 고정 프레임 시간으로 돕니다. 탑승 중에도 피격을 받고, 쓰러지면 강제로 내립니다. 탈것의 의도는 운전석 조종자를 따릅니다.
- **같은 타입이 수천 개 틱하면 타입별 일괄 틱(`TickBatchRegistry`, 등록은 선택)으로 돕니다**(2026-10-13, 사용자 결정 "구조 변경을 한다"). 언리얼 Mass 프로세서 · 유니티 `IJobEntity` 자리입니다.
  옮기는 것은 그룹에서 항목이 그 타입의 주 틱 하나뿐인 오브젝트만이라 오브젝트 안의 순서 · 한 오브젝트는 한 스레드 규칙이 그대로이고, 정확한 타입만 가서 파생의 `onTick` 을 건너뛰지 않습니다.
  모든 컴포넌트를 ECS 저장소로 옮기는 안은 하지 않았습니다(오버라이드 72 곳 · 포인터 모델 전체) — 타입이 데이터를 배열로 옮기는 다음 단계는 측정 뒤에 정합니다.
- **1인칭 카메라는 폰의 부품이라 `Base/Control` 에 둡니다**(2026-10-06). 언리얼의 `bUsePawnControlRotation` 과 같은 자리입니다. 자동 플레이는 몸 안의 분기가 아니라 AI 조종자의 빙의입니다.
- **커서 위치 API 는 `InputManager::getMousePositionNormalized` 하나입니다**(2026-10-06). 게임 쪽 코드는 장치를 직접 묻지 않고 입력 맵 액션을 읽습니다. 패드 스틱 시점은 바인딩마다의 배율 · 프레임 시간 곱(언리얼 Enhanced Input 의 Scale · Scale By Delta Time)으로 넣었습니다(2026-10-13).
- **중첩 프리팹은 쿠킹 때 펼칩니다**(2026-10-13). 배포본이 스폰마다 원본 파일을 열고 원형에 오버라이드를 적용하는 일을 없애려는 것으로, 언리얼 Packed Level Actor 와 같습니다.
  에디터 · Dev 는 원본을 경로로 가리켜 원본을 고치면 모든 상위에 퍼지고(유니티 Nested Prefab), 순환은 로드 오류입니다.
- **내비 기하에 들어갈 컴포넌트 종류는 종류별 레지스트리(`SW_NAV_GEOMETRY_CONTRIBUTOR`)로 등록합니다**(2026-10-13). Navigation(Object 층)은 Environment(위층)를 include 할 수 없고,
  인스턴스가 플레이 중에 등록하는 `INavGeometrySource` 는 플레이 전에 도는 쿠킹에 들지 않기 때문입니다. 언리얼 `INavRelevantInterface` 처럼 종류 단위 규칙이라 쿠킹과 런타임이 같습니다.
- **패드 가상 커서는 명령 조종자(디렉터)가 플레이 동안만 켭니다**(2026-10-15). 게임 모듈의 `onInitialize` 가 켜면 에디터에서 편집 중에도 켜져 있고 조종 경계(`CheckControlBoundary`)도 넘습니다.
  언리얼에서 커서 표시 · 입력 모드를 `PlayerController` 가 정하는 것과 같은 자리입니다.
- **입력 커맨드 버퍼 둘은 하나로 모으지 않습니다**(2026-10-15, C-26 ⑥). Engine `InputActionBuffer` 는 실시간(초) 액션 이력 · 선입력이고 에디터 InputMap 패널의 콤보 시험이 쓰며,
  GameFramework `InputCommandBuffer` 는 틱 단위 넘패드 · 버튼 이력이라 롤백에서 다시 돌려도 같은 결과가 나와야 하고 바라보는 쪽을 뒤집습니다. 시간 단위(초 · 틱)와 결정성 요구가 달라
  한쪽으로 모으면 롤백이 깨지거나(실시간) 에디터 · 엔진이 GameFramework 를 알아야 합니다(틱). 언리얼도 Enhanced Input 의 실시간 Combo 트리거와 격투 게임 코드의 프레임 버퍼가 따로 있습니다(기억에 의존).

- **큰 클래스는 책임이 따로 서는 묶음을 값 객체로 가르고, 소유 객체에 그대로 넘기던 전달 함수는 지웁니다**(2026-10-15, C-26 ⑤).
  `InputMap` 의 플레이어 설정(감도 · 반전 · 데드존 · 정규화) → `InputPlayerSettings`(언리얼 `UEnhancedInputUserSettings` 의 플레이어 설정 자리),
  `AbilitySystemComponent` 의 태그 개수 → `GameplayTagCountContainer`(언리얼 `FGameplayTagCountContainer`, 막힌 어빌리티 태그도 같은 타입),
  `UISystem` 의 입력 라우터 · 오프스크린 화면 전달 함수 열다섯 → `getInputRouter()` · `getOffscreenScreens()`, `InputManager` 의 게임패드 전달 함수 다섯(쓰는 곳 없음) 삭제.
  `GameObjectManager` 는 씬의 얼굴(만들기 · 찾기 · 풀기)을 그대로 둡니다 — 언리얼 `UWorld::SpawnActor` 처럼 저장소(`GameObjectStore`)를 숨기는 자리라 전달이 곧 계약입니다.
  `InputMap` 의 리바인딩(rebind · reset · 충돌 판정)도 그대로 둡니다 — 바인딩 저장소를 직접 고치는 일이라 가르면 저장소를 밖에 내보여야 합니다.
- **시나리오의 액션 단계는 액션 값을 직접 넣지 않고 바인딩을 찾아 장치 층에 냅니다**(2026-10-15, `Action` · `ActionAxis`). 언리얼 Enhanced Input 의 `InjectInputForAction` 은 액션 값을
  넣어 트리거와 모디파이어 일부를 건너뛰고, 유니티 Input Test Fixture 는 장치 키를 누르는 것으로 알고 있습니다. 이름은 액션으로 적어 키 배치를 바꿔도 시나리오가 그대로이고,
  입력은 장치에서 내므로 트리거, 레이어, UI 입력 소비의 결함까지 잡습니다. 패드 바인딩을 쓰면 가상 패드를 먼저 연결합니다.
- **게임 로직 단언은 질의 탐침(`target`, `key`, 수 또는 글)과 값 이력(`ExpectVisited`)으로 합니다**(2026-10-15). 언리얼 Functional Test 와 유니티 PlayMode 테스트는 코드에서 액터를 직접 읽는 것으로 알고 있습니다.
  여기서는 기반(일정 시스템, AI 조종자, 폰, 판 상태)이 탐침을 한 번 등록하면 게임은 아무것도 더하지 않고 "10 시에 가게에 있다", "갈기, 심기, 물주기를 차례로 거쳤다" 를 데이터로 적습니다.
  "N 초 안에 도달" 은 새 단계를 두지 않고 `WaitUntil timeoutSeconds` 로 표현합니다.
- **결정적 재생은 `<Record>` 값을 백엔드끼리 비교하고, 녹화와 재생의 왕복으로 확인합니다**(2026-10-15). 게임 상태는 고정 틱 위의 CPU 계산이라 백엔드와 무관해야 합니다.
  기대값을 시나리오에 적지 않고 같은 실행의 네 백엔드를 서로 비교하므로 해시가 바뀌어도 시나리오를 고칠 필요가 없습니다. Gauntlet 이 여러 장치의 결과를 모으는 방식에 비교 단계를 더한 것입니다.
  녹화는 끝 프레임의 탐침 값을 `<Expect>` 로 실어 스스로를 검증합니다.
- **AI 조종자가 블랙보드, 행동 트리 실행기, 상태 이름을 가집니다**(2026-10-15). 언리얼 `AAIController` 가 Blackboard 컴포넌트와 Brain(BT) 컴포넌트를 가지는 것과 같습니다.
  그래서 GameFramework 층 테이블에서 `Actor/Control` 이 `Actor/AI` 위(2)로 올라갔습니다. 시계, 일정, AI, 빙의, 탑승을 모두 읽는 자동화 단계와 탐침은 기반 맨 위 `Gameplay/Automation` 에 둡니다.
- **행동 트리 실행기의 잎 이름은 도는 잎이 없으면 그 틱에 마지막으로 성공한 작업입니다**(2026-10-15). 한 틱에 끝나는 작업만 있는 트리(RTS 경제 AI)는 도는 잎이 늘 없어 이름이 비어 있었습니다.
  언리얼 BT 디버거가 마지막으로 실행한 노드를 보여 주는 것과 같은 정보입니다. `getActiveLeaf()`(번호)는 지금처럼 도는 잎만 돌려줍니다.
- **에디터 입력 단계는 누를 레이블이 그려질 때까지 시나리오 시계를 붙잡습니다**(`EditorClick` · `EditorDrag` · `EditorHover`, 최대 60 프레임, 2026-10-15, 옛 백로그 A-3).
  언리얼 Automation Driver 의 "보일 때까지 기다리기" · Playwright 의 자동 대기와 같다. 고정 프레임 단계는 패널이 프레임을 건너뛰거나 팝업이 늦게 열리면 가끔 졌다.
  기다림은 `WaitUntil` 과 같은 창구(`AutomationRunner::holdUntilReady` — 붙잡은 프레임은 세지 않고 입력 이벤트를 민다)이고, 기다린 프레임은 로그에 남아 다음 실패의 증거가 된다.

- **`runParallelByCost` 의 시계는 함수 포인터로 주입하고, 선행 조건 스테이지의 순서 키 비교는 한 곳에 둔다**(2026-10-16). 제품은 `MonotonicClock::nowNanoseconds` 를 넘기고(`runParallelByCostWithClock`), 테스트는 가짜 시계로 호출 스레드 선점 잡음 없이 직렬 · 분할 판단을 결정적으로 본다. 시계는 청크마다 한 번(로그 번)만 불려 핫 경로에 가상 호출이 없다. `TickRegistry` 의 재구축(`isBefore`)과 증분(`applyStageDelta`)은 `StageOrder` · `isOrderedBefore` 하나를 공유해 두 길의 순서가 어긋날 수 없다.

### 5-7. 런타임 UI 와 글자

- **저장소에는 CC0 라틴 글꼴만 둡니다**(2026-10-06, 사용자 결정). 한글과 CJK 는 OS 시스템 글꼴로 대체하고, 시스템 글꼴을 쓰는 테스트는 글리프가 있는지만 확인합니다.
- **글리프는 단일 채널 SDF(FreeType 내장)로 그립니다**(2026-10-06). 유니티 TextMeshPro, 언리얼 SDF 글꼴과 같습니다. MSDF 는 [백로그](backlog/ui.md) `G-10` 입니다. FreeType 은 `vcpkg.json` 의 엔진 직접 의존입니다.
- **백엔드 선택과 소유 객체의 서브시스템 판정**(2026-10-15, C-27 ⑤ 마감 — 코드를 다시 읽고 확정, 코드 변경 없음). 상용 엔진 비교는 모두 "~로 알고 있다" 로 기억에 의존한다.
  - **오디오는 이미 역할별 팩토리가 서브시스템 하나를 고른다.** `IAudioService::create()` 가 `SW_PLATFORM_WINDOWS` 와 `SW_WITH_CLIENT_CODE` 이면 `XAudio2AudioBackend`, 아니면 `NullAudioBackend` 를 돌려주고,
    `Engine` 는 전용 서버(`bDedicatedServer`)면 `createNull()` 을 골라 기동 단계에 `addSubsystem` 한다. 소비자는 구현 종류를 모르고 `IAudioService` 만 본다. 언리얼 오디오 믹서가 플랫폼별 `IAudioMixerPlatformInterface` 를 고르는 모양으로 알고 있다.
  - **물리는 갈아 끼우는 쌍이 아니라 함께 쓰는 둘이다.** `PhysicsSystem`(서브시스템 `Physics`) 하나가 Jolt(3D, `createScene3D`)와 Box2D(2D, `createScene2D`)를 같은 때에 쓴다. 한 역할에 구현이 둘이 아니라 역할이 둘이라 호스트 종류로 고를 것이 없다.
    그래서 팩토리나 서브시스템을 더 나누지 않는다. 언리얼은 PhysX · Chaos 처럼 한 역할의 구현을 모듈로 고르지만, 3D · 2D 를 함께 켜는 것은 Paper2D 가 Box2D 를 3D 물리와 따로 쓰는 것과 비슷하게 알고 있다.
  - **`ResourcePakManager` 는 서브시스템이 아니다.** `AssetManager`(서브시스템)가 `_pPackManager` 로 소유하고 `ResourceIO::getPackManager()` 로만 닿는다. 종료 때 `AssetManager::shutdown` 이 `unmountAll` 로 비우고(마운트를 부르는 곳은 엔진 코드에 없고 호스트 · 도구 · 테스트가 건다),
    자기만의 의존 순서나 별도 수명 범위가 없다. 서브시스템으로 올리면 `AssetManager` 와 둘 사이에 의존 칸만 늘 뿐이다. 판정 기준의 "소유한 서브시스템 안의 객체" 이고, RHI 부품과 같은 이유로 타입 있는 멤버로 둔다.
  - **`ServerServiceManager` 는 엔진 범위 서브시스템이 아니다.** 소유자가 엔진 루프가 아니라 `ServerApp`(호스트)이고, 수명이 엔진 기동 · 종료가 아니라 모듈 리로드 직전의 `stopAll` 과 리로드 뒤 `startAll` 로 되풀이된다.
    서버 서비스의 코드는 모듈(DLL)에 있어 모듈 언로드 전에 멈춰야 하므로, 엔진 범위 종료 순서가 아니라 호스트의 모듈 리로드 순서를 따른다. 전용 서버 호스트에만 있고 의존 서브시스템 선언으로 순서를 정할 것도 없다.
    **호스트 범위 서브시스템으로도 옮기지 않는다**(2026-10-15, 호스트 범위를 세우며 확인). 서브시스템의 한 쌍(`initializeSubsystem` · `shutdownSubsystem`)은 범위 수명에 한 번이고,
    되풀이하는 재기동은 `shutdownDependentsOf` → `restartStopped` 가 맡는데 이것은 "다른 서브시스템을 갈아 끼울 때 그에 기대는 것만" 내리고 다시 세우는 모양이다(RHI 백엔드 교체).
    서버 서비스는 반대로 **자기 자신**이 모듈 리로드마다 멈췄다 시작해야 하고(`onBeforeModuleReload` 의 `stopAll` → 다음 틱 `startAll`), 시작이 게임 인스턴스 · 서버 설정 · 지표 레지스트리가 선 뒤여야 한다.
    그것들은 호스트 범위가 초기화되는 시점(엔진 앞)보다 늦게 서므로 범위의 위상 순서로는 표현할 수 없다. 멈추는 자리도 범위 종료가 아니라 모듈 언로드 직전의 콜백이다.
    그래서 호스트가 `unique_ptr` 로 들고 모듈 리로드 · 종료 순서에서 직접 부르는 지금 모양이 맞다. 언리얼의 `FTickableGameObject` 서비스가 모듈 `ShutdownModule` 에서 수동으로 내려가는 것과 같은 자리로 알고 있다.
- **체력 바와 데미지 숫자는 화면 마커 위젯입니다**(2026-10-06). 값은 컴포넌트 코드가 직접 넣습니다. 마커는 오브젝트마다 하나라 뷰모델과 문서를 두면 마커 수만큼 생기기 때문이고, 언리얼의 위젯 컴포넌트 체력 바도 보통 그렇게 합니다.
- **데이터 바인딩은 뷰모델과 필드 통지입니다**(2026-10-06). 언리얼 5 MVVM 과 같은 구조이고, 알림은 필드마다의 단조 번호로 한 프레임에 한 번 반영합니다. 폴링 바인딩은 개발 편의용입니다.
- **UI 문서와 스타일은 모두 엔진 XML 입니다**(2026-10-06). 파서가 하나여서 학습하기 쉽습니다. 스타일이 정한 필드만 위젯 필드를 이기고, 정하지 않은 필드는 위젯의 기본 모습을 씁니다.
- **엔진이 기본 옵션 메뉴와 일시정지 메뉴 문서를 두고, 게임 프리셋이 덮어씁니다**(2026-10-06). 언리얼 CommonUI 와 Lyra 의 방식입니다. 메뉴는 UI 입력 맵의 `UI.Pause` 로 열리고, UI 행동 맵은 `UISystem` 이 소유하는 별도 맵입니다.
- **게임 정지는 시간 배율과 따로인 정지 요청 수입니다**(2026-10-06). 언리얼의 `SetGamePaused` 와 같습니다. UI, 페이드, 로딩 화면은 실제 경과 시간을 씁니다(유니티 `unscaledDeltaTime`).
- **접근성**(2026-10-06): 글자 크기는 배율이 줄여도 12 UI 단위 밑으로 내려가지 않습니다(Xbox 접근성 지침 101). 색각 보정은 Machado 2009 흉내 행렬과 오차 재분배이고 UI 전체에 겁니다.
- **HUD 문서 경로는 플레이어 오브젝트의 컴포넌트 필드이고, 로딩 화면 문서는 팩의 `gamesettings.xml` 에 둡니다**(2026-10-06). 언리얼의 `AHUD` 가 GameMode 에 붙는 것처럼 씬마다 다른 HUD 를 쓸 수 있습니다.
- **UI 가 먹은 입력은 `UIInputMapView` 로 거릅니다**(2026-10-13). 입력 맵의 읽기 질의와 이름 · 뜻이 같고, 먹은 액션이거나 게임 입력이 막힌 상태면 "누르지 않음" 을 돌려줍니다.
  플레이어 조종자 · 명령 조종자 · 카메라는 입력 맵 읽기 한 줄만 바꿔 씁니다(언리얼 입력 소비 · 유니티 `EventSystem.IsPointerOverGameObject` 와 같은 자리).
- **화면 마커의 위치와 거리 배율은 GPU 가 화면 앵커로 적용합니다**(2026-10-17, 백로그 C-8 측정 후 결정). 마커 위젯은 피벗을 원점에 두고 한 번 배치 · 칠하고,
  사각형은 앵커 번호(`CanvasQuad::_anchor`)를 가진다. 마커가 움직이면 앵커 표(`CanvasFrameData::_listAnchor`, 사각형 버퍼 맨 앞 원소)만 바뀌어 다시 배치 · 칠하기 · 사각형 업로드가 없다.
  모든 화면 트리가 재사용이면 그리기 목록 이어 붙이기와 같은 내용 비교도 건너뛴다. 측정(PC SW, AMD Family 25 · Release dx12 · 이름표 2,000 · 600 프레임, 전후 바이너리를 번갈아 5 판씩 세 번, 가운데 값):
  `GT.UI.Layout + GT.UI.Paint` p50 3.15 ~ 3.4 ms → 0.004 ms, 새로 잰 `GT.UI.Markers`(투영 · 앵커 쓰기, 전에는 구간 밖) 0.49 ~ 0.52 ms, `GT.Frame` p50 4.19 → 0.92 ~ 1.18 ms,
  `RT.Canvas.Upload` p50 196 → 36 ~ 49 us, 벽시계 4.3 ~ 4.7 → 1.1 ~ 1.2 ms/프레임. 남은 게임 스레드 몫은 마커마다 월드 위치를 읽고 투영하는 일(카메라 · 행렬을 한 번만 구해도 그대로)이고,
  `GT.UI.*` 합이 기준 0.5 ms 언저리다. 투영 · 컬링까지 컴퓨트로 옮기려면 GPU 가 마커의 월드 위치(GPUScene 변환)를 읽어야 해 [백로그](backlog/ui.md) `G-13` 으로 둔다.
  앵커 아래에서는 자르기를 쓸 수 없다(가위는 대상 픽셀이다 — `UIPaintPass` 가 단언한다). 앵커가 따로 바인딩을 늘리지 않으려고 사각형 버퍼 원소 하나(rect 만 읽음)를 쓴다(SRV 슬롯 16 이 Vulkan 밴드를 꽉 채운다).

### 5-8. 게임프레임워크와 키트

- **게임 · 에디터 서비스는 범위(`SubsystemScope`)에서 타입으로 찾고, 없으면 호스트 서비스 표를 본다 — 범위는 게임 인스턴스 · 에디터 컨텍스트가 소유한다**(2026-10-14).
  언리얼은 서브시스템을 범위(엔진 · 에디터 · 게임 인스턴스)마다 그 범위 객체가 소유하고 `GetSubsystem<T>()` 로 찾는 것으로 알고 있다(기억에 의존). 같은 모양을 Core 에 두고(`ISubsystemLocator` · `SubsystemScope` ·
  `locateSubsystem` 이 상위 범위로 올라간다), 게임 · 에디터 창구의 복제 템플릿(타입 해시 · 서비스 칸 trait · 로컬 표)을 지웠다. 이전의 "건 이미지" 를 반환 주소로 기록해 언로드 때 훑어 회수하던
  장치는 없앴다 — 범위가 인스턴스와 함께 사라져 풀지 않은 칸이 남을 수 없다(우리 쪽이 더 단순해진 확장). 테스트는 자기 범위를 `ScopedGameScope` · `ScopedEditorScope` 로 잠시 건다.
- **`EditorContext` 는 범위를 들고 하위 객체를 만드는 얇은 껍데기다 — 상태는 모두 에디터 서브시스템이 든다**(2026-10-14). 하위 객체마다 두던 포인터 · 접근자 열다섯은
  `getSubsystem<T>()` 하나로(언리얼 `GEditor->GetEditorSubsystem<T>()`), 컨텍스트에 직접 있던 뷰 렌더 타깃 · RHI 디바이스 · 렌더러 백엔드 · 게임 뷰 입력은 `EditorRenderViews`,
  플레이 세션 상태는 `EditorPlaySessionState`, 테마 값은 `EditorTheme` 으로 옮겼다. 셸의 독 레이아웃은 범위에 빌려 건다(`findSubsystem<EditorDockLayout>()`).
  활성 컨텍스트 정적 포인터도 지웠다 — 컨텍스트가 자기 범위에 자신을 걸어 `EditorContext::get()` 은 지금 에디터 범위의 주인을 찾는다(첫 칸이라 비교 한 번). 정적 상태는 "지금 에디터 범위" 하나뿐이다.
- **게임 인스턴스 서브시스템은 수명 · 순서가 인스턴스에 묶인 것만이다 — 화면 전환 · 로딩 화면 · 네트워크 세션**(2026-10-14, `GameInstanceSubsystem`). 게임 범위가 소유하고
  `onInitialize` 앞에 초기화, `onShutdown` 뒤에 역순 종료한다(언리얼 `UGameInstanceSubsystem` — 로딩 화면을 게임 인스턴스 서브시스템으로 두는 것도 같은 모양으로 알고 있다).
  카탈로그 · gamesettings 는 데이터라 서브시스템으로 만들지 않고 빌려 건다(`bindScopedService`). 스냅샷 상태 저장소 · 씬 로드 대기 목록은 인스턴스 흐름의 일부라 멤버로 둔다.
  GameFramework 의 남은 정적 상태(정의 약 75 곳)는 상수 · 빈 값 · 불변 표 · 상태 없는 드라이버 · 링크 때 등록 표 · 의도된 프로세스 공유(메모리 DB 드라이버)라 남긴다.
  에디터 플러그인의 함수 정적(`AdminToolSession` · `ParkLayoutPreview`)은 확장 모듈이 서브시스템을 기여하는 창구가 생기면(`C-27` ④ 와 같은 장치) 에디터 범위로 옮긴다.

- **GameFramework 최상위에는 `Base/`(장르 공통 기반)와 `Kits/`(장르 키트)만 둡니다**(2026-10-06). 게이트가 다른 폴더를 막습니다.
- **기반은 `Base/<층>/<폴더>/` 두 단이고, 층은 아래에서 `Online` < `Foundation` < `World` < `Actor` < `UI` < `Gameplay` 입니다**(2026-10-10).
  include 그래프를 재서 정했습니다. 계획의 첫 안(World · UI 가 맨 위)은 AI · 기믹 · 상호작용 → 월드 13 곳, 어빌리티 → 데미지 숫자가 거꾸로라 기각했습니다.
  UI 가 게임플레이 아래인 것은 언리얼에서 UMG 가 게임플레이 모듈 아래에 있는 것과 같습니다. 전투 수치(체력 신호)는 HP 바가 읽으므로 Actor 층에 둡니다.
- **키트 헤더는 `SW_KIT_API`, 서버 키트 헤더는 `SW_KIT_SERVER_API`, Base 헤더는 `SW_GF_API` 를 씁니다 — 키트 TU 는 `SW_GF_EXPORTS` 를 정의하지 않습니다**(2026-10-15, 백로그 C-3).
  키트 TU 가 `SW_GF_EXPORTS` 를 정의하니 include 한 Base 클래스(`Inventory` · `StatBlock` 등)가 키트 DLL 에서도 dllexport 로 풀려, 키트 DLL 55개가 `GameFramework.dll` 과 같은 심볼 4909개를 한 번 더 내보내고 있었다(`GF_WitcherRPG.dll` 은 326개 중 116개). 키트는 이제 `SW_KIT_EXPORTS`(PRIVATE) · `SW_KIT_IMPORTS`(INTERFACE)만 쓰고, 그 뒤 키트 DLL 에서 `GameFramework.dll` 과 겹치는 심볼은 0 이다. 언리얼이 모듈마다 `<MODULE>_API` 를 클래스에 달고 다른 모듈의 선언은 import 로 두는 것과 같은 모양으로 알고 있다(기억에 의존).
  함정: 겹침이 가리고 있던 결함이 하나 드러났다. 순수 인터페이스(`IAccountNameIndex` 등)의 `= default` 생성자 · 소멸자는 그 헤더를 include 하는 `.cpp` 가 DLL 에 있을 때만 내보내져서(clang-cl), 서버 키트가 파생 클래스를 만들며 dllimport 로 찾다 링크가 깨졌다. 그래서 **순수 인터페이스(순수 가상 · `= default` · `= delete` · `using` 뿐)는 내보내기 매크로를 달지 않는다**(인라인 멤버를 쓰는 쪽이 만든다). 전수 조사로 같은 모양 28개(Base 15 · 키트 13)에서 매크로를 뺐다. `CheckExportMacros` 가 새로 생기는 것을 막는다(헤더가 속한 DLL 의 매크로인지도 함께 본다).
  서버 키트는 `SW_KIT_SERVER_EXPORTS`(`SW_KIT_EXPORTS` 가 아니라)만 정의하고 폴더 `Server/` 의 헤더는 `SW_KIT_SERVER_API` 를 쓴다. 서버 키트가 include 한 공유 키트 헤더의 `SW_KIT_API` 는 dllimport 로 남는다. 모든 키트 TU 가 `SW_KIT_IMPORTS` 를 함께 정의한다(자기 내보내기 매크로가 먼저 읽힌다). 키트 PCH 하나(`sw_kit_pch`)를 나눠 쓰려면 명령줄 정의가 같아야 해서 PCH 소유자도 같은 정의를 갖는다(이전에는 키트가 키트를 링크할 때 `SW_KIT_IMPORTS` 가 달라 PCH 경고 43개가 났다). Linux 는 `-fvisibility=hidden` 을 주지 않아 기본이 default visibility 라 세 매크로가 모두 `visibility("default")` 로 같다 — 동작 차이가 없고 심볼 보간(interposition)이라 중복 내보내기로 링크가 깨지지 않는다. Release 는 Dev 와 같은 SHARED 라 매크로가 dllexport 로 풀리고, Shipping(STATIC)은 `SW_KIT_*_EXPORTS` · `SW_KIT_IMPORTS` 를 정의하지 않아 매크로가 빈다. 그 뒤 서버 키트 11개와 공유 키트의 겹침은 100 → 0 이다.
- **키트는 성격으로 나눕니다: 장르 키트는 `Kits/Genre/<그룹>/`, 장르를 가리지 않는 기능 키트(네트워크 · 온라인 · 저장 · 타일 월드 · 복셀)는 `Kits/Feature/<그룹>/`**(2026-10-10).
  언리얼 플러그인 폴더가 Runtime · Online · Experimental 처럼 성격으로 묶이는 것과 같은 단입니다. 모듈 이름(`GF_<키트>`)은 경로와 상관없이 그대로입니다.
  클라이언트 · 서버로 나뉘는 기능은 기능 폴더 하나 아래 `Shared/` · `Server/` · `Client/` 짝으로 둡니다(Nakama · PlayFab 이 서비스 단위로 클라이언트 SDK 와 서버 모듈을 묶는 것과 같은 단위).
- **모듈 이름은 공유 `GF_<X>`, 서버 전용 `GF_Server_<X>`, 클라이언트 전용 `GF_Client_<X>` 입니다**(2026-10-06). `CheckModuleTargets` 가 접두와 매니페스트의 대상(`_listTarget`)이 맞는지 봅니다.
  서버 키트는 같은 기능의 공유 키트만 include 할 수 있고, 키트끼리는 include 하지 않습니다.
- **키트 조립 규칙**(2026-10-06): 조립 테스트 게임은 `MeadowVillage` 입니다. 플레이어와 세계가 들고 있는 가방은 `Inventory`, "아이템과 개수" 값 목록은 `ItemStackList` 입니다.
  가방의 칸 구조체는 `InventorySlot` 이고, 달력은 `WorldClock` 하나입니다. 키트 하나만 쓰는 게임은 디렉터가 기반 상태를 들고 키트에 빌려 줍니다. 언리얼 Lyra 도 단일 기능 게임은 GameMode 나 GameState 에 직접 상태를 둡니다.
- **실시간 키트의 체력 직접 관리(`_hp -= damage`)를 `Vitality` 로 옮기지 않습니다**(2026-10-11). 대상은 `ConquestWorld` · `RTSWorld`(유닛 수천) · `ActionRoom` · `UnitStatsComponent` · `GhostEncounter` · `FightingMatch` 입니다.
  `Vitality` 는 실드 · 기절 · 부활 · 경직 · 무적을 가진 큰 상태 기계(필드 수십 개 + 알림 버퍼)라 유닛마다 하나씩 들면 메모리와 상태 바이트가 커지고, 키트마다 체력 의미(정수 · 실수, 0 에서 멈춤 · 1 에서 멈춤, 죽음 알림 시점)가 달라
  옮기면 수치와 세이브 · 되감기 바이트가 바뀝니다. 같은 상태 기계가 필요한 키트(배틀로얄 · 소울라이크 · 데드 바이 데이라이트 · 위쳐)만 이미 씁니다. 단순 체력은 `Vitality` 가 아니라 각 키트의 값으로 두는 것이 언리얼 `UHealthComponent` 샘플 · 유니티 개별 스크립트와 같은 쪽입니다.
- **온라인 게임의 로컬 지갑은 서버 원장의 읽기 사본입니다**(2026-10-06).
- **시퀀서는 카메라를 직접 움직이지 않고 신호만 냅니다**(2026-10-13). 엔진(시퀀서)은 GameFramework 카메라를 모르므로 컷 · 시네마틱 시작 · 끝 · 대기 뷰 · 흔들림을 신호로 내고,
  GameFramework 의 `CinematicCameraComponent` 가 디렉터 프리셋 · 블렌드로 카메라를 움직입니다(언리얼 Sequencer 의 Camera Cut · Blend In/Out 과 같은 나눔).
- **옵션 메뉴의 카메라 설정(시야각 · 흔들림 배율 · 머리 흔들림)은 컴포넌트가 읽어 디렉터에 넣고, 모드 계산은 모른다**(2026-10-17, `CameraUserSettings`).
  시야각은 1인칭 · 3인칭 모드에만 건다 — 언리얼 · 유니티 게임이 옵션 시야각을 플레이어 카메라에만 걸고 시네마틱 · 고정 카메라는 저작한 렌즈로 두는 것과 같다.
  값을 그대로 덮지 않고 변수 기본값 대비 반각의 tan 비율로 프리셋 시야각을 넓힌다. 기본값이면 저작한 화면 그대로이고, 좁게 저작한 조준 프리셋도 같은 비율로 따라간다.
  흔들림 배율은 손떨림 잡음과 충격 오프셋에 곱한다(접근성 옵션 "카메라 흔들림" 이 모든 흔들림을 줄이는 것과 같다). 머리 흔들림은 프리셋 `<HeadBob>` · `FirstPersonCameraComponent::_headBob`
  데이터로 켜고(진폭 0 이 기본), 위상은 시간이 아니라 간 거리로 간다 — 언리얼 걷기 카메라 셰이크 · 유니티 FPS 템플릿의 head bob 과 같은 자리이고, 걸음 빠르기와 어긋나지 않는다.
  엔진 서비스가 없는 실행(엔진 없이 도는 테스트)은 사용자 설정이 없는 상태가 맞아 기본값이고, 변수 관리자가 있는데 변수가 없으면 `SW_ASSERT` 로 멈춘다.
- **RTS 땅 격자 다시 칠하기는 전체를 훑되 바뀐 칸만 쓴다**(2026-10-17, `D4-4`). 64 × 64 는 4096 칸이고 땅 리비전은 다른 키트가 땅을 얻거나 반납할 때만 바뀌어 훑기 자체는 문제가 아니다.
  비용은 훑기가 아니라 부작용이었다 — 지형 · 땅으로 칠한 뒤 건물 발자국을 다시 덮어 그대로인 건물 칸이 풀렸다 막히며 격자 리비전이 올라, 상관없는 땅 변경(남의 도로)에도 모든 흐름장이 다시 구해졌다.
  막힘을 임시 배열에 다 구한 뒤 쓰므로 같은 값이면 리비전이 오르지 않는다(`RealTimeStrategyTest.WorldBuildsOnlyOnUsableLandAndRepaintsTheGridFromIt`). 바뀐 사각형만 칠하는 길은 땅 레지스트리가 변경 범위를 알려야 해서 두지 않았다(언리얼 내비 메시도 변경 영역을 받아야 타일만 다시 짓는다).

### 5-9. 온라인 서비스, 네트워크, 보안

- **DB 와 캐시**(2026-10-06, 사용자 결정): 영속 저장소는 PostgreSQL(서버)과 SQLite(개발용 단독 서버, 클라이언트 로컬)입니다. 캐시는 RESP 드라이버 하나로 리눅스는 Valkey, 윈도우는 Garnet 을 씁니다.
  Redis 7.4 이상은 RSAL/SSPL 이라 "오픈소스이고 무료" 조건 밖이고, Redis 8 의 AGPLv3 선택도 그 의무 때문에 택하지 않았습니다. 전용 서버는 윈도우와 리눅스 둘 다 1 급입니다. 남은 일은 [백로그](backlog/network.md) `D7-5` "네트워크 서비스 계층" 에 있습니다.
- **네트워크 서비스 계층**(2026-10-06, 사용자 결정): 로그인, 채팅, 거래처럼 MMO 가 아니어도 쓰는 서비스는 상태 복제(UDP)와 따로, 순서 있는 신뢰 스트림 · 긴 연결 · 요청-응답 위의 키트로 만듭니다.
  공통 기반은 GameFramework 기반 `Online/` 입니다. 서비스 틀(등록, 라우팅, 인증 문맥, 오류 코드, 버전 협상), 요청 보호(도배 제한, 멱등 키, 크기 상한), 신원 원형(`AccountID`, 세션 토큰 검증),
  저장 계약(`IServiceStore` 영속, `IEphemeralStore` 캐시, `ILocalStore` 로컬), 마이그레이션 실행기, 감사 로그, 서버 간 버스, 예약 작업, 원격 설정과 기능 플래그, 관측(지표, 구조화 로그, 추적 id)이 여기에 있습니다.
  로컬 저장의 파일 백엔드는 바이너리 · JSON · XML 형식, 원자적 쓰기, 체크섬, 선택적 압축과 암호화를 지원합니다.
  DB 드라이버는 키트입니다. `GF_SQLStore`(SQLite), `GF_Server_SQLStore`(PostgreSQL), `GF_Server_CacheStore`(RESP, 메모리 구현은 기반 `Online/Cache`)가 있습니다.
  기능도 키트입니다. `GF_Account`, `GF_ServerDirectory`, `GF_Economy`(원장, 지갑, 상점, 영수증 검증), `GF_Trade`(원장 위), `GF_Mailbox`, `GF_Chat`, `GF_Social`, `GF_Leaderboard`, `GF_Matchmaking`, `GF_LiveOps`, `GF_Admin`(GM 도구, 제재)입니다.
  부하 테스트 봇은 테스트 도구입니다.
  제품 이름은 드라이버와 제공자 폴더에만 쓰고(`CheckProductNames`, 조립점은 `Scripts/lint/rules/CheckProductNames.toml` 의 `[exemption]` 에 이유와 함께), 서버 전용 모듈은 클라이언트 Shipping 에 넣지 않습니다.
  캐시를 잃어도 영속 데이터는 맞아야 합니다(거래의 원본은 영속 트랜잭션). DB 호출은 전용 워커, 연결 풀, 비동기 완료로 합니다.
- **네트워크 보안**(2026-10-06, 사용자 결정): 스트림(서비스)은 TLS 1.3 이고, 게임 UDP 는 연결 수립 때 키를 교환(X25519)한 뒤 패킷마다 AEAD(AES-GCM 또는 ChaCha20-Poly1305)를 겁니다.
  nonce 는 패킷 번호이고 재전송 방지 창을 둡니다. 세션 키는 로그인 키트가 발급한 토큰에 묶습니다(UDP 접속 = 토큰 제시). Valve GameNetworkingSockets, 언리얼 AESGCM PacketHandler 와 같은 구조입니다.
  암호 구현은 직접 짜지 않고, Core 의 인터페이스(`Network/Security/`) 뒤에 라이브러리 하나를 둡니다.
- **UDP 패킷 압축은 끕니다**(2026-10-06, 사용자 결정). 켜는 규칙은 "15 % 이상 줄고, 압축 + 해제가 패킷당 2 us 이하" 입니다. 코덱 틀은 Core `Compression`(코덱 id 레지스트리)이고 LZ4 · zstd · zlib 은 Engine 이 등록하며, Core 네트워크는 id 로만 씁니다.
  `NetCompressionBenchTest`(Release, 3 회 가운데 값, 패킷마다 봉투 3 B, 줄지 않으면 원문)에서 레지스트리 코덱은 어느 흐름에서도 규칙을 넘지 못했습니다.
  스냅숏(평균 1009 B)은 LZ4 1 % · 1.1 us, zstd1 15 % · 11.7 us, zstd1 + 사전 16 KB 34 % · 6.0 us 였습니다. 파괴(302 B)는 LZ4 3 % · 0.37 us, zstd1 4 % · 6.4 us, 사전 35 % · 1.6 us 였습니다.
  채팅(43 B, 합성)은 LZ4 1 %, zstd 0 %, 사전 27 % · 0.74 us 였습니다. 규칙을 넘는 것은 학습 사전뿐(파괴, 채팅)이라, 사전 배포(버전 · id 협상, 사전 갱신)를 하게 되면 다시 봅니다.
  스트림 프레임 압축은 있지만 기본으로 꺼져 있습니다(`StreamEndpointConfig::_compression`, 봉투 `NetCompressionUtil`). 받는 쪽은 푼 크기를 본문 상한으로 보고 풀기 전에 거절합니다.
- **암호 라이브러리는 OpenSSL(vcpkg, Apache-2.0)입니다**(2026-10-06). Windows 에서는 지금 트리플릿대로 DLL 로 링크합니다. 비밀번호 해시는 보안 제공자의 Argon2id 이고, 기본값은 OWASP 권장(19 MiB, 반복 2)입니다.
- **인증기 없는 UDP 암호화와 자체 서명 개발용 인증서는 개발 빌드에만 있습니다**(2026-10-06). Shipping 서버는 설정 파일의 인증서와 키 경로가 필수입니다.
- **계정**(2026-10-06): 같은 계정으로 다시 로그인하면 새 로그인이 옛 세션을 밀어냅니다. 게스트를 정식 계정에 연동할 때 이미 쓰인 이름이나 플랫폼 계정은 거절합니다(자동 합치기 없음).
  탈퇴는 30 일 유예 뒤 지우고, 원장과 감사 기록에는 계정 id 만 남깁니다. PC 외부 로그인은 공개 클라이언트와 PKCE(S256)이고, 서명은 RS256 과 ES256 만 받습니다.
- **로컬 저장의 봉인은 장치 키입니다**(2026-10-06). 실수나 가벼운 변조를 막을 뿐이고, 경쟁 데이터의 원본은 서버입니다.
- **경제**(2026-10-06): 거래는 맡김 없이 양쪽이 확정하는 순간 원장 분개 하나로 정산합니다. 경매와 우편 첨부는 맡김 방식입니다. 유상과 무상 재화의 차감 순서는 데이터이고, 기본은 무상 먼저입니다.
  환불 회수에만 음수 잔액(빚)을 허용하고, 빚이 있는 동안 그 재화로 살 수 없습니다(PlayFab 의 Subtract 와 같은 자리). 원장, 우편 넣기, 제재처럼 여러 키트가 한 트랜잭션에 넣는 쓰기는 기반 `Online/` 에 둡니다.
- **우편 만료**(2026-10-06): 운영 우편과 보상 우편은 첨부가 사라지고, 플레이어 우편은 보낸 사람에게 돌아갑니다. 보관 기간은 30 일입니다.
- **응답 형식은 하나입니다**(2026-10-06). 업무 결과는 응답 본문의 첫 값이고, 오류 코드는 공통 `OnlineError` 만 씁니다. gRPC 의 status 와 본문 구분과 같습니다. 키트 클라이언트는 요청마다 완료 델리게이트를 받습니다(언리얼 Online Services).
- **운영 HTTP 엔드포인트는 기본으로 꺼져 있고(`_opsPort` 0), 켜면 `127.0.0.1` 에 바인딩합니다**(2026-10-06). Nakama 의 기본값과 같습니다. 운영 지표와 상태 엔드포인트는 엔진(`Engine/Observability`)에 있어 전용 서버가 GameFramework 없이 씁니다.
- **매칭 권한은 모드마다 캐시 임대(10 초) 하나입니다**(2026-10-06). 순위표 점수는 서버만 제출하고, 표마다 허용한 경우(`_bClientSubmit`)에만 클라이언트가 제출합니다. 길드는 소셜 키트(`GF_Server_Social`) 안에 있습니다.
- **외부 계정이나 비용이 드는 연동은 계약과 가짜 제공자까지만 둡니다**(2026-10-06). APNs, FCM 푸시와 Apple, Google, Steam 영수증 검증이 여기에 해당합니다. 금칙어 목록은 테스트용 낱말만 저장소에 둡니다.
- **부하 테스트 봇(`Tools/OnlineLoadBot`)은 Dev 구성에서만 빌드합니다**(2026-10-06).
- **시간 · 바이트 단위와 온라인 · 스트림 상한은 정본 한 곳에 모읍니다**(2026-10-15). 시간 · 바이트 단위는 `Defines.h` 의 `constant` 가, 온라인 저장소 상한은 `OnlineLimit.h` 가 갖습니다. 여러 서비스가 값만 같게 따로 적어 두던 것이라 한쪽만 바뀌는 일을 막으려는 것입니다. 재시도 횟수 `kMaxConflictRetry = 4` 는 길드 · 소셜 · 리더보드가 각자 정하던 값이 우연히 같았던 것을 한 정책으로 묶은 것이며, 서비스별로 다르게 하려면 다시 나눕니다. 스트림 큐 기본값은 보내기 줄(`kDefaultMaxQueuedSendBytes`)과 받기 적체(`kDefaultMaxPendingReceiveBytes`)가 서로 독립인 설정이라 상수를 둘로 둡니다.
- **중단점에 멈춘 쪽은 패킷 비트로 알리고 상대가 시간 초과를 늘립니다**(2026-10-11, 서버 디버깅 S2). 시간 초과를 끄는 전역 스위치(언리얼 `-NoTimeouts` · `bNoTimeouts`)는 양쪽을 미리 맞춰 띄워야 하고 진짜 끊김도 못 보게 해서, 디버거가 붙은 쪽만 알리는 연결 단위 신호(`NetDebugHold`, 대기 시한 600초)와 멈췄던 쪽의 가상 시계를 골랐습니다.
  Shipping 은 알리지도 믿지도 않습니다. 디버거 없이 멈추는 길은 Dev 서버 콘솔 `pause` · `step N` · `resume` 이며, 네트워크 스레드는 계속 돌아 연결이 삽니다.
- **키트 서버 조립의 옵션 · 메서드는 표 하나씩입니다**(2026-10-15, C-26 ①). 옵션은 키트가 (이름, 읽어 넣을 칸) 표로 `OnlineServerKitEntry::readOptions` 를 한 번 부르고, 모르는 키 · 형식 오류는 거기서 키트 이름과 함께 거절합니다.
  메서드는 `ENUM( Count = Count )` 메서드 이름 열거형 하나가 와이어 번호(`OnlineMethodTable`, 영역 + 1 부터)와 지표 라벨(snake_case)을 함께 정합니다. 언리얼의 `UPROPERTY(Config)` 가 필드 표 하나로 읽고 쓰는 것과 같은 방향입니다(기억에 의존).
  `_bStarted` 류 플래그는 키트에 남깁니다 — 지키는 대상이 키트마다 다릅니다(경제 · 계정은 `initialize` 단계 서비스, 나머지는 `start` 단계 서비스).
  옵션을 리플렉션 구조체로 옮기는 안은 기각했습니다. 키트 옵션은 같은 키를 여러 번 적는 목록이 있고 키트마다 타입이 달라, 설정 파일 하나의 고정 스키마로 읽을 수 없습니다.
- **저장 드라이버는 키트 `.cpp` 의 정적 묶기 객체(`OnlineStoreDriverBinding`)로 서버 조립에 붙습니다**(2026-10-17, D7-5). 묶기 함수만 있고 부르는 곳이 없어 SQLite · PostgreSQL · RESP 를 고른 서버가 늘 기동을 멈췄고, 언로드 정리(`ServiceStoreFactory::shutdown`)도 불리지 않았습니다.
  모듈 수명과 같은 정적 객체가 묶고 풀어서 "실렸는데 안 묶임" · "풀지 않은 함수" 를 쓸 수 없게 했습니다. 키트 조립의 `SW_REGISTER_ONLINE_SERVER_KIT`, 언리얼 모듈의 `IModularFeatures` 등록과 같은 모양입니다.
- **호스트 이름은 Core 의 막는 해석(`PlatformSocketUtil::resolveAddress`, `getaddrinfo` IPv4)으로 기동 때 한 번 풉니다**(2026-10-17, D7-5). `localhost` 만 손으로 바꾸던 자리(캐시 엔드포인트)를 이 함수로 바꿨고, 풀리지 않으면 기동 오류입니다.
  틱 · 요청 경로는 DNS 왕복만큼 막히므로 그대로 부르지 않습니다 — HTTP 클라이언트는 전용 해석 스레드(처음 이름을 받을 때 띄움)에 맡기고 `update` 가 연결을 겁니다. TaskManager 워커는 막는 일을 받지 않아 쓰지 않습니다.
  언리얼 `ISocketSubsystem::GetAddressInfo`(막는 판 + 비동기 판), libcurl 의 스레드 해석기와 같은 나눔입니다.
- **온라인 접속은 Hello 첫 필드의 온라인 스키마 해시 하나로 거절합니다**(2026-10-17, [리플렉션 직렬화 형식 계획](plans/ReflectionFormats.md) 단계 5-a). 키트마다 (영역, 판) 목록을 대조하던 협상을 없앴습니다.
  해시는 빌드에 올라온 `OnlineProtocolEntry` 전부(영역 순)와 `kBaseVersion` 을 섞은 값이고, 키트 서비스가 `getProtocol` 로 그 항목을 돌려줘야 하므로 키트가 따로 목록을 적어 빠뜨릴 곳이 없습니다.
  클라이언트가 올린 키트만 섞으면 키트 일부만 쓰는 클라이언트가 서버와 어긋나므로 빌드 전체를 섞습니다(서버와 클라이언트는 같은 빌드로 배포). 다르면 `[kVersionMismatch][서버 해시 u64]` 로 답하고 연결을 닫으며, 클라이언트는 다시 접속하지 않고 계정 키트는 `LoginResult::UpdateRequired` 를 냅니다.
  다른 키트는 키트 표보다 먼저 `OnlineReplyUtil::markError` 가 결과를 `UpdateRequired` 로 바꿉니다(키트 표의 기본값 `Unavailable` 은 재시도를 부르므로). Hello 에 다른 답이 오면 클라이언트는 `HandshakeFailed` 로 멈춥니다.
  한쪽만 키트 모듈을 끄거나(`EditorModuleOverrides`) 핫 리로드로 키트 프로토콜을 바꾸면 그쪽 해시만 달라져 접속이 거절됩니다. 같은 빌드끼리만 잇는다는 규칙대로의 동작이라 따로 맞추지 않습니다.
  언리얼 `FNetworkVersion::GetLocalNetworkVersion`(빌드 값 하나를 접속 때 비교), Unity Netcode `ProtocolVersion` 과 같은 자리입니다(기억에 의존). 옛 클라이언트를 받는 탈출구는 열지 않았습니다.
- **온라인 키트 메시지는 고정 배치형(B)이고, 키트의 메시지 타입 목록 하나가 쓰기 · 읽기와 스키마 해시를 함께 정합니다**(2026-10-17, [리플렉션 직렬화 형식 계획](plans/ReflectionFormats.md) 단계 5-b).
  11 키트의 손 코덱(필드를 하나씩 적던 `BitWriter` 코드)과 손 `kVersion` 을 지웠습니다. 키트는 `XxxProtocol::MessageTypes = OnlineMessageTypes<...>` 하나를 두고, 그 `write` · `read` 는 목록에 없는 타입이면 컴파일이 멈춥니다.
  같은 목록을 `OnlineProtocolEntry` 에 넘기므로 타입마다의 `FixedLayout::getSchemaHash` 가 온라인 스키마 해시에 섞이고, 해시용 목록을 따로 적어 빠뜨릴 곳이 없습니다.
  언리얼 Iris 가 복제 디스크립터에서 프로토콜 해시를 만드는 것과 같은 방향입니다(기억에 의존). 정한 세부는 아래와 같습니다.
  - 메서드와 알림 번호는 키트의 메서드 표(`OnlineMethodTable`, `ENUM( Count = Count )` 열거형 둘)의 열거자 순서에서 나오고, 등록 항목이 그 이름 순서를 온라인 스키마 해시에 섞습니다.
    같은 타입을 받는 두 메서드(`TradeIDRequest` 를 받는 Lock 과 Cancel, `MatchmakingIDRequest` 를 받는 일곱)의 번호를 맞바꾸면 이름 순서가 바뀌어 해시가 달라지므로, 규칙 문장 없이 형식이 막습니다.
    손으로 번호를 적던 일곱 키트를 이 표로 옮겼고, 번호가 연속이 아니던 서버 디렉터리(0 부터)와 소셜(길드가 0x10 부터)은 와이어 번호가 바뀌었습니다. 해시도 바뀌므로 옛 빌드와는 접속에서 갈립니다.
  - 메시지 머리에 그 타입의 해시(64 비트)를 그대로 싣습니다(계획 초안은 본문만 쓰기). 메서드 표는 메서드의 이름과 번호만 담고 그 메서드가 받는 타입은 담지 않으므로,
    한 메서드가 받는 타입을 목록 안의 다른 타입으로 바꾼 빌드는 핸드셰이크 해시가 같습니다. 그 어긋남은 머리 해시만 잡으므로(`SchemaMismatch`) 8 바이트를 남겼습니다.
    없애려면 메서드마다 받는 타입을 표에 적어 해시에 섞어야 하고, 그 전에는 없앤 뒤의 안전을 테스트로 보일 수 없습니다.
  - 저장소에 남는 값(채팅 기록, 라이브 운영 이벤트 · 기기, 서버 디렉터리 점검 · 공지, 거래 레코드)과 캐시 값(파티 · 로비, 접속 상태)은 A(`ServiceRecordUtil`, 판 머리)입니다. 캐시도 배포 중에 다른 빌드의 서버가 읽기 때문입니다.
    옛 손 형식 레코드를 바꾸는 변환 코드는 두지 않았습니다. 못 읽은 레코드를 어떻게 할지는 레코드마다 손으로 정하지 않고 읽기 함수가 정합니다.
    영속 레코드는 `ServiceRecordUtil::readDurable` 로 읽어, 손상이면 오류 로그를 남기고 그 요청이나 다시 읽기를 실패로 끝냅니다(건너뛰지 않습니다). 다시 만들 수 있는 캐시(파티 · 로비 · 접속 상태)는 `readCache` 로 읽어 없는 것으로 봅니다.
    A 는 `MaxCount` 를 보지 않으므로 두 읽기는 B 코덱이 있는 타입이면 언제나 B 쓰기 검사로 선언된 범위를 다시 봅니다. 없는 타입은 쓰기 때의 규칙 함수(`LiveEventRules::isValid`, 서버 디렉터리 `isValidWindow` · `isValidNotice`)를 다시 지나고, 어기면 오류로 다시 읽기를 멈춥니다.
    A 는 필드를 이름으로 짝짓고 모르는 필드(orphan)를 거절합니다. 필드를 더하면 옛 레코드의 그 필드는 기본값이 되고, 필드를 빼면 옛 레코드 전부가 읽기 실패입니다. 필드를 빼거나 뜻을 바꿀 때는 판을 올리고 데이터를 다시 씁니다.
    값 계산이 불가피한 예외만 `SW_SCHEMA_MIGRATION` 으로 그 판의 이관을 등록합니다(5-4).
    쓰기(`ServiceRecordUtil::encode`)는 `[[nodiscard]] bool` 이고 읽기가 거절할 레코드는 쓰지 않습니다(2026-10-17, 백로그 `A-17`). 실을 수 없는 필드가 있거나 B 코덱이 있는 타입인데 선언된 범위를 벗어나면 오류 로그와 false 입니다.
    바이트를 돌려주던 모양은 실패를 Debug 단언으로만 막아 Release 에서 끊긴 레코드가 저장소에 들어갔고, 범위를 넘는 값은 써지고 나서 읽을 때 손상으로 보였습니다.
    호출부는 저장소 작업을 하지 않고 요청을 `Invalid` 로 끝냅니다(재시도로 풀리지 않으므로 `Unavailable` 이 아닙니다). 요청이 없는 쓰기(채팅 기록 묶음, 접속 상태 시한 연장)는 그 값만 빼고 오류 로그를 남깁니다.
    지금은 서비스마다 손 규칙(`isValidLobbyRequest`, `PushNotificationDispatcher::registerDevice` 의 확인 같은 것)이 같은 범위를 먼저 보므로 호출부의 실패 분기는 공개 API 로 닿지 않습니다. 그래서 테스트는 쓰기 함수에서 거절을 봅니다.
  - 범위 밖 요청은 클라이언트가 보내지 않고 `kInvalidRequest` 응답으로 끝냅니다(서버가 같은 본문을 거절한 것과 같은 결과). 쓰기와 거절은 `ServiceClientCallTable::sendMessage` 하나가 맡고,
    거절 응답은 `OnlineServiceClient::rejectRequest` 가 만들어 서버 응답과 같은 처리기를 지납니다. 그래서 공통 기반 · 계정 · 거래 · 서버 디렉터리 클라이언트가 같은 약속을 가집니다.
    0 이 아닌 요청 번호를 돌려주고, 콜백(또는 받은 응답 목록)의 번호가 그 번호와 같으며, 보내지 못한 요청의 콜백은 요청 함수 안에서 바로 옵니다(초기화 전 · 멈춤과 같습니다).
    서버는 응답 메시지를 `OnlineServiceHost::respondMessage` 하나로 보내고, 쓰지 못하면 오류 로그와 `kInternal` 로 답합니다. 계정 키트만 계정을 연결에 묶은 뒤에 응답해야 하므로 몸을 먼저 써 두고 같은 약속으로 답합니다. 알림이 범위 밖이면 오류 로그를 남기고 보내지 않습니다.
  - 애노테이션은 정수 리터럴만 받으므로(`AnnotationMeta.txt`) 같은 상한을 상수로도 두는 곳(`LedgerConstant::kMaxAssetIDSize`, `ChatLimit`, `ServiceAuditEntry::kMaxStateSize` 같은 것)은
    키트 테스트 `DeclaredLimitsMatchTheConstants` 가 생성된 코덱의 정규 문자열(`FixedCodec::_pCanonical`)에서 `MaxCount` 를 읽어 상수와 묶습니다.
    값을 한 곳에 두는 구조는 생성기가 `MaxCount` 에 상수 이름을 받아 코덱에 그 식을 그대로 내보내는 것입니다. 정규 문자열(스키마 해시)에 들어갈 수는 생성기가 알아야 하므로,
    생성기가 상수 정의를 읽거나 정규 문자열을 컴파일 때 만들어야 합니다. 이 단계에서는 하지 않고 테스트로 묶었습니다.
  - **열린 질문(사용자 결정, 백로그 `F-2`)**: 서버끼리 주고받는 버스 메시지(채팅 전달, 소셜 관계 바뀜, 계정 해지 알림)도 B 인데 서버 사이에는 핸드셰이크가 없습니다.
    배포 중에 빌드가 섞이면 받는 쪽이 `SchemaMismatch` 로 조용히 버립니다. "서버 클러스터는 한 빌드로만 돈다" 를 정책으로 둘지, 버스에도 빌드 해시 확인을 둘지는 정하지 않았습니다.
  - 가변 길이가 두 겹인 칸(`vector<string>`, `vector<vector<T>>`)은 원소를 구조체로 감쌌습니다(`AccountLinkedProvider`, `MatchFormedTeam`). 클라이언트에 보내지 않는 값은 형식으로 막았습니다. 점검 허용 목록은 상태 응답의 타입(`MaintenanceView`)에 없고, 서버 안에서만 쓰는 칸(`AdminReply::_bRevokeSessions`, 레코드 판 `_version`)은 `PROPERTY` 가 아닙니다.
  - 의미만 바뀌고 배치는 그대로인 변경(단위 변경 같은 것)은 해시가 잡지 못하므로 `OnlineProtocolConstant::kBaseVersion` 하나를 올립니다.
  - 남은 손 코덱(기반 계층의 제재 버스, 접속 상태 전달 봉투와 캐시 값, 원장 · 우편 · 감사 · 원격 설정 레코드, 계정 · 길드 · 거래의 id 레코드)은 키트 프로토콜이 아니어서 이 단계에서 옮기지 않았습니다.
- **로그인 응답 쓰기 실패**(2026-10-17, 백로그 `A-16`): 계정 서버가 로그인 · 재접속 응답 몸을 쓰지 못하면 `kInternal` 로 답하고, 저장소 작업이 이미 만든 그 세션은 로그아웃 작업(`LoginService::logout`)으로 지웁니다.
  응답 몸을 저장소 작업 전에 쓰는 안은 기각했습니다. 몸이 싣는 세션 토큰 · 신원 · 시한은 그 작업이 만들기 때문입니다. 지금 실패를 부르는 값(원격 설정의 상점 주소)만 미리 확인하는 것은 같은 결함이 다른 필드로 다시 날 수 있어 고르지 않았습니다.
  지우는 작업의 완료는 짝이 되는 요청이 없어 버려지고, 지우기가 저장소 오류로 실패하면 세션은 시한까지 남습니다(클라이언트는 토큰을 받지 못했으므로 쓸 수 없습니다).

### 5-10. 전용 서버

- **전용 서버는 별도 실행 파일 `Server` 입니다**(2026-10-06). App 의 모드가 아닙니다. 기존 Shipping 프리셋은 Client 타깃이고, Dev 기본은 Game 타깃입니다. 언리얼의 `<Game>Server` 타깃과 같습니다.
- **서버 설정은 Shipping 에서도 디스크에서 읽고, 없으면 기동이 실패합니다**(2026-10-06). Windows 서비스 모드도 있습니다.
- **서버 전용 쿠킹은 하지 않고, 패키징할 때 타깃별로 에셋 종류를 뺍니다**(2026-10-06). 처음 빼는 종류는 텍스처, 셰이더 바이너리, 오디오입니다. 메시와 애니메이션은 충돌, 소켓, 히트박스, 루트 모션 때문에 남깁니다.
  패키징과 런타임이 같은 테이블(`Config/Engine/CookContract.json` 의 `target_excluded_asset_kinds`)을 읽습니다. 언리얼이 전용 서버에서 텍스처와 사운드 로드를 건너뛰는 것과 같은 목적입니다.
- **지형 레이어 가중치는 텍스처가 아닌 데이터 에셋(`.layerweights`)이고, 클라이언트와 전용 서버가 같은 파일을 CPU 로 읽습니다**(2026-10-16, 백로그 F-1 — 사용자가 "상용 엔진 기준 최선" 으로 위임).
  옛 스플랫은 비압축 RGBA8 DDS 였고 텍스처 종류라 서버 패키지에서 빠져, 서버의 레이어 질의(`computeLayerWeightsAt` — 발소리 · 표면 재질 · 이동 속도)가 말없이 레이어 0 이었습니다.
  상용 엔진은 가중치를 게임플레이 데이터로 서버에도 둡니다 — 언리얼 랜드스케이프는 전용 서버에서도 레이어 정보(물리 재질 판정)를 유지하고, 유니티 `TerrainData` 는 알파맵을 에셋 데이터로 담아
  GPU 텍스처를 읽을 때 만듭니다. 그래서 정본을 하나(`LayerWeightsData` — 매직 `SWLW` · 판 · 너비 · 높이 · 레이어 수 4 · 텍셀마다 4 바이트)로 두고, 그리는 쪽 GPU 텍스처는
  지형이 읽은 바이트로 지형마다 하나 만듭니다(`TextureCache::createCPUTexture` — 칠하기가 이미 쓰던 길). 버린 안: ① 쿠커가 DDS 에서 서버용 사본을 따로 뽑기 — 정본이 둘이 되고
  경로 규약(같은 이름 다른 확장자)에 기대며, 클라이언트는 여전히 DDS 포맷(BC 압축이면 CPU 가 못 읽음)에 묶입니다. ② 쿠킹 표에서 스플랫 DDS 만 예외로 남기기 — 서버가 텍스처 로더를 타고
  "텍스처는 서버에 없다" 는 표의 뜻이 깨집니다. 가중치에 밉이 필요 없고(옛 `Terrain_Weights` 규칙도 밉을 끔) 셰이더는 같은 RGBA8 을 받으므로 그림은 바뀌지 않습니다(쇼케이스 바이트 동일).
  폴백은 없습니다 — 경로를 정했는데 읽지 못하면(없음 · 형식 틀림 · 서버가 읽지 않는 `.dds`) 오류를 남기고 `reloadTerrain` 이 실패합니다. 경로가 비면 레이어를 칠하지 않는 지형입니다(데이터가 정한 것).
  서버 패키지에 남는지는 `TestServerPackExclusion.testServerPackKeepsTerrainData`, 서버 호스트의 질의는 `TerrainTest.DedicatedServerAnswersLayerQueries` 가 지킵니다.
- **App · Server 가 같이 쓰는 모듈 호스트는 최상위 폴더 `Source/ModuleHost` 입니다**(2026-10-10, 사용자 결정 — 옛 이름 `AppHost`, 같은 날 안의 클래스 `ModuleHost` 와 이름을 맞췄다). 실행 파일 폴더(`Source/App`) 안에 두고 정규식으로 골라 내던 것을 폴더째 나눴고,
  `CheckEngineLayers` 가 방향(App · Server → ModuleHost → Engine · RuntimeAPI)을 지킵니다. 언리얼이 실행 파일 진입점(`Launch`)과 모듈 관리자(`Core` 의 `FModuleManager`)를 다른 모듈에 두는 것과 같습니다.
  고정 스텝 시간 정책 `FixedTimestep` 은 모듈 호스트가 아니라 Core(`Core/Time`, `GameTimer` 옆)에 둡니다 — 언리얼도 고정 시간 값(`FApp::FixedDeltaTime`, Core 모듈)과 프레임 시간 계산(`UEngine::UpdateTimeAndHandleMaxTickRate`)을 모듈 관리자 밖에 둡니다.
  기본값은 Core 가 정하지 않고 만들 때 받습니다(App 이 `EngineConfig` 의 초기값을 넘긴다 — 출처 하나). 2026-10-13 에 `Engine/Config` 에서 옮겼습니다(엔진 의존이 기본값 하나뿐이었다).
- **`ModuleHost` 는 게임 모듈만 다루고, 에디터 몫은 App 의 파생 `EditorModuleHost`(`Source/App`)가 맡습니다**(2026-10-10). 전용 서버는 기반 클래스만 쓰고 모드 플래그(`_bDedicatedServer`)가 없습니다 — RHI 를 받았는지가 "디바이스가 있어야 게임을 만든다" 를 정합니다.
  에디터가 끼어드는 자리는 기반의 보호 가상 함수 다섯이고, 내리는 순서(`suspendModules`)는 기반 한 곳에 남습니다. 언리얼이 런타임 `UGameEngine` 과 `UEditorEngine` 을 파생으로 가르는 것과 같은 방향이며, 에디터 몫을 서버 바이너리에서 뺍니다.
- **에디터 뷰포트 격자는 카메라 높이로 1 · 10 · 100 m 간격을 고르고 경계에서 섞습니다**(2026-10-10). 언리얼 · 유니티의 에디터 격자와 같은 방식이며, 굵은 선(5 칸마다)은 월드 좌표로 정하고 가장자리는 흐립니다. 화면 공간 셰이더 격자(Unity 2019+ · Godot 4)는 렌더 패스를 하나 더 들여야 해 ImGui 선 그리기를 유지합니다.
- **`UserSettingsHost` 는 엔진으로 옮기지 않고 App 에 둡니다**(2026-10-10). 설정 정책(보류 · 적용 · 확인)은 이미 엔진의 `UserSettingsManager` 이고, 이 파일은 App 이 소유한 창과 `App::onResize` 의 스왑체인 경로에 화면 요청을 적용하는 창 소유자의 단계입니다.
  옮기면 창 접근과 리사이즈 순서가 두 곳으로 갈리고, 창이 없는 `Server` 는 쓸 일이 없습니다. 언리얼도 정책(`UGameUserSettings`)과 창 적용(창을 소유한 `UGameEngine` 의 해상도 변경)을 나눕니다.

- **느린 틱 트레이스는 Shipping 서버에 넣지 않고 Dev 구성 서버(`Ninja-Release`)에서 켭니다**(2026-10-11, 트레이스 내보내기 X5). 사용자 결정(내보내기 코드는 Shipping 에서 빠진다)을 그대로 두고,
  운영에서 시간축이 필요하면 최적화된 Dev 서버를 `-gv_traceSlowTickMs=N` 으로 띄웁니다 — 틱 본문이 N ms 를 넘으면 3 틱 뒤 링 버퍼(그 틱 앞뒤 63 프레임)를 저장하고, 30 초 쿨다운 · 프로세스당 20 파일 상한.
  Shipping 서버는 지표(`server_tick_seconds` 히스토그램 · 밀린 틱 경고)로 "느린 틱이 났다" 까지만 알립니다. 언리얼도 Insights 트레이스를 Shipping 에서 빼고 프로파일이 필요한 서버는 Test · Development 구성으로 띄우며, 유니티 프로파일러도 Development 빌드에만 있습니다.
- **여러 프로세스 트레이스는 첫 파일을 기준으로, 같은 호스트면 옮기지 않고 다른 호스트면 벽시계로 맞춥니다**(2026-10-11, X4). 같은 기계의 단조 시계(QPC · `CLOCK_MONOTONIC`)는 프로세스끼리 같아 정확하고,
  벽시계는 밀리초 해상도라 같은 기계에서 쓰면 오히려 틀립니다. 다른 기계는 시계 차이(NTP 수준)만큼 틀린 것을 줄 묶음 이름에 적습니다. Perfetto 도 여러 트레이스를 시계 동기화 표식으로 맞추고, 표식이 없으면 그대로 겹칩니다.
- **서버 디버깅의 방향**(2026-10-11, 사용자가 추천안을 받아들임). 여러 프로세스 재생(S1)을 먼저 하고, 한 프로세스 가상 서버(S8)는 다중 월드가 들어온 뒤에 합니다. 한 프로세스가 편하지만 엔진 구조 변경이 먼저 필요하기 때문입니다.
  디버그 채널은 읽기 전용과 개발 콘솔 명령 실행까지만 엽니다. 임의 프로퍼티 쓰기를 열면 에디터가 서버 권위를 우회하므로, 값을 바꿀 일은 개발 명령으로 게임이 정한 경로를 거칩니다.
  전용 창은 Multiplayer 창 하나에 탭으로 모읍니다(언리얼 Network Insights, 유니티 Multiplayer Play Mode 창 + Network Simulator). 디버그 채널과 원격 명령은 Dev 전용이고 루프백과 토큰을 요구하며, 서버 코드는 에디터에 의존하지 않습니다.
  남은 단위는 [서버 디버깅 계획](plans/ServerDebug.md)에 있습니다.
- **디버그 채널은 개발 명령을 나르는 한 줄 TCP 프로토콜이고, 새 읽기 기능은 개발 명령으로 더합니다**(2026-10-15, 서버 디버깅 S3). 요청은 `<토큰> <명령 한 줄>`, 응답은 `OK|ERR <바이트 수>` 줄과 본문입니다.
  I/O 스레드는 줄을 쌓기만 하고 게임 스레드의 `pump` 가 실행하므로 씬을 읽는 명령이 안전하고, 서버가 `pause` 로 멈춰 있어도 응답합니다(중단점에서 값을 보는 쓰임). 채널 전용 명령 체계를 따로 만들면 에디터 · 표준 입력 · 채널이 갈라져서,
  `scene.tree` · `object.dump` · `scene.poses` · `state.hash(es)` 를 `SW_DEV_COMMAND` 로 두어 콘솔 · 표준 입력 · 채널이 같은 명령을 씁니다. 채널이 받는 쓰기는 `teleport` · `tag.add` 같은 게임이 정한 개발 명령과 `pause` · `resume` · `step` 뿐입니다.
  토큰이 틀리면 그 연결에서는 아무것도 실행하지 않고 닫습니다. 언리얼의 `ServerExec`/원격 콘솔(`rcon` 류)과 같은 자리이고, 토큰 · 루프백 규칙은 운영 관리 채널(O4)이 그대로 씁니다.
- **어긋남 찾기의 해시는 씬 트랜스폼을 이름 순으로 접은 값이고, 이력은 서버가 `-gv_stateHashHistory` 일 때만 남깁니다**(2026-10-15, S7). 오브젝트 ID 는 프로세스마다 달라 넣지 않고, 이름이 같은 오브젝트는 값으로 정렬해 순회 순서를 지웁니다.
  꺼져 있으면 서버는 해시를 계산하지 않습니다. 키트별 상태(파괴 해시 · 롤백 상태 바이트)는 같은 이력에 다른 이름으로 더하는 확장입니다. 언리얼의 리플레이 체크섬 · 유니티 Netcode 의 `ReplicationCheck` 와 같은 목적이지만, 여기서는 틱을 맞춰 두 프로세스를 비교하는 도구가 먼저입니다.
- **권위 고스트는 서버가 실제로 가진 위치를 채널로 읽어 이름으로 맞춥니다**(2026-10-15, S6). 복제 키트가 마지막으로 알린 자세는 키트마다 다르고 공통 원천이 없어서, 서버의 `scene.poses` 를 고스트로 삼았습니다(복제 지연 · 보간 · 예측 오차가 모두 이 차이에 들어간다).
  언리얼 `p.NetShowCorrections` 가 서버가 보낸 보정 위치를 그리는 것과 같은 자리이고, 값은 서버의 진짜 위치라 "내 화면이 서버와 얼마나 다른가" 에 직접 답합니다. 디버그 드로우 큐는 에디터 씬 뷰의 ImGui 오버레이만 그려서 고스트도 거기에만 나오고, 시나리오 스크린샷(렌더 결과)에는 잡히지 않아 눈 확인은 못 했습니다(차이 숫자는 `net.ghost` 로 확인).
- **게임의 네트워크 역할은 게임 코드가 아니라 실행 인자가 정합니다**(2026-10-11, 서버 디버깅 S1). `GameInstanceBase` 가 `GameNetSession` 을 들고 `-gv_netListenPort`(서버 · 리슨 호스트) · `-gv_netConnectAddress`(클라이언트)로 엽니다 —
  시험 게임을 따로 세우지 않고 Empty 를 포함한 모든 게임이 서버 · 클라이언트로 뜹니다. 언리얼의 넷 드라이버(`?listen` · `open <주소>`)와 Godot 의 `SceneMultiplayer` 가 같은 자리이고, 장르별 복제는 키트가 세션의 라우터에 처리기를 답니다.
  리슨 호스트(App)는 기본이 꺼짐입니다. **전용 서버는 서버 설정의 게임 포트(`ServerConfig::_gamePort` · `_listenAddress`)를 늘 엽니다**(2026-10-17 고침, 언리얼 전용 서버와 같다) — 처음에는 전용 서버도 `-gv_netListenPort` 로만 열어
  Shipping 서버(테스트 변수가 없다)가 포트를 아예 열지 못했습니다. 설정은 `ServerServiceRegistry::findHostContext` 로 받고, 개발 빌드의 `-gv_netListenPort` 는 설정보다 우선합니다(에디터 멀티 프로세스 재생이 고른 포트).
  받는 주소는 `127.0.0.1`(루프백) · `0.0.0.0`(모든 인터페이스)만 받고, 다른 주소 · 샤드 2 개 이상은 오류로 열지 않습니다(대신 열지 않는다). 한 기계에서 서버를 여럿 띄우는 테스트는 게임 포트 0(운영체제가 고름)인 설정을 줍니다(`TestServerBoot.cpp` `runServer`).
- **개발 실행의 게임 UDP 소켓은 기본으로 127.0.0.1 에만 묶습니다**(2026-10-11, `gv_netLoopbackOnly` 기본 1). 모든 인터페이스(0.0.0.0)에 묶으면 Windows 방화벽이 실행 파일 경로마다 "허용" 창을 띄우는데, 워크트리 · 시험 실행 파일 · 에디터가 띄우는 서버가 많은 개발 환경에서는 매번 뜬다. 시험의 UDP 전송(`UDPNetTransport::open( port, bLoopbackOnly )`)도 전부 루프백이다. 다른 기계와 붙을 때만 `-gv_netLoopbackOnly=0` 을 주고, 서버 주소가 루프백이 아닌 클라이언트는 답을 받아야 하므로 값과 상관없이 모든 인터페이스에 묶는다. 언리얼 PIE 의 멀티 프로세스 재생도 루프백으로 붙는다. 운영 서버의 게임 포트는 `ServerConfig::_listenAddress`(기본 0.0.0.0)가 정한다.
- **전용 서버 로그 줄에는 틱 번호 `[tick=N]` 을 수준 꼬리표 뒤 · 문맥 꼬리표 앞에 붙입니다**(2026-10-11, S1 ②). 값은 Core `Logger` 의 프로세스 전역 틱 번호이고 서버가 틱마다 정합니다 — 네트워크 · 로더 스레드 줄에도 지금 틱이 붙습니다.
  언리얼은 모든 줄에 `GFrameCounter % 1000` 을 붙이지만, 서버 틱은 `pause` · `step` · 상태 줄의 번호와 맞춰 봐야 해 자르지 않습니다. 정하지 않은 프로세스(App · 테스트)의 줄은 바이트가 그대로라 줄 형식을 읽는 도구를 건드리지 않습니다.
- **에디터의 멀티 프로세스 재생은 자식 프로세스의 표준 입력으로 제어하고, 트레이스는 종료할 때 쓰게 합니다**(2026-10-11, S1). 클라이언트(App)도 `-gv_standardInputCommands=1` 이면 `quit` · 개발 명령을 받습니다 —
  Windows 의 Ctrl+Break 는 부모가 콘솔에 붙어 있어야 보낼 수 있어 에디터(창 프로그램)에서 쓸 수 없고, 강제 종료는 트레이스를 잃습니다. 트레이스는 `-gv_traceSaveOnExit=<경로>` 로 엔진 종료 첫머리에 쓰고(Stop 때 명령을 한 번 더 보내 기다리지 않는다),
  에디터가 프로세스 이름표(`Server` · `Client1`)를 달아 `mergeTraceJSON` 으로 합칩니다. 언리얼의 "Run Under One Process" 끔(자식 프로세스 PIE)과 같은 모양이고, 언리얼은 자식을 끌 때 프로세스를 죽이지만 여기서는 트레이스 때문에 정상 종료를 먼저 기다립니다(시한 15 초, 넘으면 강제 종료).

### 5-11. 에디터와 외부 공개

- **에디터 시나리오는 경계 전후 값을 이름으로 기억해 견준다(`Remember` · `sameAs` · `differsFrom`) — 단언마다 기대값을 손으로 적지 않는다**(2026-10-15, `AutomationRunner`).
  언리얼 Functional Test 는 C++/블루프린트 변수로, 유니티 Editor Test 는 C# 지역 변수로 앞 값을 들고 견준다(기억에 의존한 비교). 우리 시나리오는 데이터라 변수가 없었고,
  기억한 값을 글 그대로 견주는 단계를 더해 64 비트 상태 해시(`Editor.StateHash`)까지 정확히 견준다. 실행기가 엔진에 있어 플레이 · 모듈 리로드 · 백엔드 교체를 넘어 기억한다.
- **에디터 인스턴스를 다시 만들 때 선택 · 미저장 표시는 엔진 서비스(`EditSessionState`)에 맡겨 넘기고, 되돌리기 스택은 비우지 않는다**(2026-10-15).
  유니티는 도메인 리로드를 `SessionState` · 직렬화된 선택으로, 언리얼은 트랜잭션 버퍼가 핫 리로드를 넘는 것으로 같은 일을 한다(기억에 의존한 비교).
  백엔드 교체는 모듈 이미지를 내리지 않으므로 모듈 코드를 가진 되돌리기 명령도 남긴다(`setModuleImageKept`). 패널을 가리키는 명령은 `this` 대신 표지(shared_ptr)로 잡는다.
- **Apply to Prefab 은 같은 씬의 다른 인스턴스에 바로 퍼지고, 파일 쓰기와 인스턴스 갱신이 되돌리기 하나다**(2026-10-15, `PrefabCache::updateInstance`).
  유니티 Apply Overrides · 언리얼 블루프린트 컴파일 뒤 레벨 인스턴스 갱신과 같은 규칙이다(기억에 의존한 비교). 덮어쓴 값은 옛 원형과 견줘 뽑아 새 원형에 얹는다 —
  씬을 다시 읽을 때(`Scene::instantiate`)와 같은 식이라 저장 전후 결과가 같다.
- **지형은 같은 에셋을 다시 읽을 때 저장하지 않은 편집을 넘긴다**(2026-10-15, `TerrainComponent::reloadTerrain`). 언리얼 랜드스케이프는 편집이 PIE · 액터 이동에 사라지지 않는다.
  트랜스폼 · 프로퍼티 변경은 다시 읽기 안에서 편집을 들고 건너고(경로가 바뀌면 버린다), 컴포넌트를 새로 만드는 플레이 정지 복원은 플레이 스냅샷이 편집을 따로 싣는다.

- **메시 모디파이어 셰이딩 미리보기는 GPU 로 못 옮기는 스택이면 CPU 결과 정점 메시로 내려간다 — 와이어만 남기지 않는다**(2026-10-15, `MeshModifierSession::updateShadedPreview`).
  언리얼 모델링 도구 · 3ds Max 수정자 스택은 경로와 무관하게 셰이딩된 결과를 보인다(기억에 의존한 비교). CPU 쪽은 바뀔 때마다 새 메시를 끼워 렌더 스레드가 든 사본과 겹치지 않는다.

- **미리보기 씬(툴 창의 월드)은 씬마다 `FrameRenderer` 인스턴스 하나로 그린다 — 주 렌더러 안의 GPUScene 을 씬 키 맵으로 바꾸지 않는다**(2026-10-15, `FrameRendererScenePreview.cpp`).
  유니티 `PreviewRenderUtility` 가 숨은 씬을 따로 렌더하는 것과 같은 모양이다(기억에 의존한 비교). 주 렌더러의 핫 패스(컬링 슬롯 · 패스 상수 · 트랜지언트 · 활성 뷰)를 그대로 두어
  주 씬 그림이 바뀔 여지가 없다. 대가는 머티리얼 · 정점 풀 · PSO 캐시가 렌더러마다 따로인 것(메모리, 창을 처음 열 때의 PSO 생성)이고, 계획(`EditorMultiWorld.md` 결정 2)의
  "풀 공유" 는 미리보기 창이 여럿 동시에 열려 메모리가 문제로 재일 때 다시 본다. 미리보기 렌더러는 GPU 타임스탬프 · 프로파일 구간 · 렌더 타깃 목록을 건드리지 않는다(디바이스에 하나인 것을 주 렌더러가 쥔다).
- **툴 창의 Undo 는 창마다 스택이고, 포커스된 창이 자기 스택을 건다**(2026-10-15, `editor::setFocusedCommandStack` · `EditorPreviewPanel`). 오브젝트 Undo 명령은 기록할 때의 씬 키를 기억해
  활성 씬이 아닌 그 씬에서 되돌린다 — 창을 닫아 씬이 사라지면 명령은 아무것도 하지 않는다. 언리얼 · 유니티는 전역 스택 하나에 대상을 붙인다(계획 3 절).

- **확장 모듈(에디터 플러그인)은 등록 줄 `SW_EDITOR_SUBSYSTEM` 으로 에디터 서브시스템을 기여한다 — 함수 정적 싱글턴을 두지 않는다**(2026-10-14, `EditorExtensionSubsystems`).
  언리얼에서 플러그인의 `UEditorSubsystem` 파생을 에디터가 찾아 만드는 것과 같은 자리다. 확장은 에디터가 뜬 뒤에도 로드 · 언로드되므로 내장 서브시스템과 따로 두고,
  패널과 같은 장치(등록 목록 세대 · 모듈 언로드 직전 `releaseSubsystemsWithin`)로 만들고 내린다. 찾는 쪽은 `editor::findSubsystem<T>()`(`AdminToolSession` · `ParkLayoutPreview`).
  등록 줄은 UI 쪽 .cpp 에 둘 수 있다 — 세션 .cpp 를 에디터 없이 컴파일하는 테스트(EngineTest)가 있다.
- **렌더러 안(FrameRenderer 의 PSO 캐시 · 트랜지언트 풀 · 리드백 · 캔버스)도 서브시스템으로 만들지 않는다**(2026-10-14) — RHI 안(5-5)과 같은 이유다: 핫 패스의 타입 있는 멤버이고
  언리얼 렌더러도 명시 소유다. 모듈이 렌더 패스를 등록하는 창구(`G-1`)는 쓰는 모듈이 생길 때 만든다 — 지금 쓰는 모듈이 없고, 내보낼 것이 `FramePassContext` ·
  커맨드 리스트 · 트랜지언트 풀과 렌더 스레드 안전 계약까지라 소비자 없이 만들면 검증되지 않은 채 썩는다(언리얼 `FSceneViewExtension` 자리).
- **버그 리포트는 언리얼 `BugIt` 모양의 엔진 개발 명령입니다**(2026-10-10). 폴더 하나(zip 없음)에 info · 스크린샷 · 로그 사본 · 씬 사본 · 재현 명령을 두고, `bugitgo` 는 폴더를 주지 않으면 가장 최근 폴더입니다.
  입력 녹화는 담지 않습니다(전역 녹화기가 없다). 메모는 콘솔 `bugit <메모>` 로 붙이고, 에디터 커맨드는 메모 없이 바로 모읍니다(유니티 Bug Reporter 처럼 창을 따로 띄우지 않는다).

- **인스펙터의 "기본값과 다름" 은 컴포넌트 타입마다 만든 기본 인스턴스와 견줍니다**(2026-10-10, 언리얼 CDO — Class Default Object). 이 저장소는 초기값을
  생성자에 두어(AGENTS) 리플렉션 메타의 기본값 글이 대부분 비어 있으므로, 계획의 메타 글 비교로는 표시할 것이 거의 없습니다. 메타 글은 중첩 구조체처럼 인스턴스가 없을 때만 씁니다.
- **환경설정 · 단축키 · 모듈 창의 세부**(2026-10-10): 환경설정 값은 프로퍼티 글 형식(`formatPropertyText`)의 문자열로 저장합니다(형식마다 JSON 타입을 맞추지 않아도 왕복이 같다).
  단축키 키 이름은 낱말(`Comma` · `0`)이고 저장 파일은 `{ "id": [ "주 조합", "보조" ] }` 입니다. 모듈 창은 매니페스트를 고치고 Build 단추만 두며 다시 시작은 손으로 합니다(실행 인자를 이어받는 재시작은 하지 않았다).
- **에디터 폴더는 층이고 틀은 아래 · 구체는 위입니다**(2026-10-11, `CheckEditorLayers`). 패널 · 팝업 매니저는 인터페이스 옆(`Common/GUI`), 에셋 종류 동작 등록부는 `Workspace`,
  커맨드가 패널을 여는 일은 창구 `IEditorPanelHost`, UI 를 만지는 명령의 등록 줄은 그 UI 의 .cpp 입니다 — 언리얼에서 `FTabManager` 가 Slate 틀에, `IAssetTypeActions` 등록부가 AssetTools 에 있고
  편집기 모듈이 그 위에서 등록하는 것과 같습니다. 상태 막대 항목은 등록 줄(`SW_EDITOR_STATUS_BAR_ITEM`)로 더합니다(언리얼 상태 막대 확장).
  예외는 어느 층에서 include 해도 되는 잎 헤더(`prelude:`)와 모든 매니저를 만드는 조립 파일(`wiring:`)뿐입니다. 큰 패널은 `Panels/<패널>/`, 혼자인 패널은 영역 폴더(`Scene/` · `Animation/` · `Data/` · `Settings/`)에 둡니다.
- **ImGui 를 모르는 층(Common 의 Asset · Commands · Config · Localization · SourceControl)은 OBJECT 라이브러리 `EditorModel` 로 짓고 오브젝트를 EditorModule DLL 에 그대로 넣습니다**(2026-10-17, `sw_addModulePart`).
  정적 라이브러리나 별도 DLL 로 떼지 않습니다. 확장 모듈이 따로 링크하면 정적 상태가 두 벌이 되고 export 가 빠질 수 있습니다(언리얼도 모듈을 쪼개지 않고 모듈 안 의존만 가릅니다).
  vcpkg 가 모든 패키지 헤더를 한 include 자리에 두어 include 자리로는 imgui.h 를 숨길 수 없으므로, `IMGUI_USER_CONFIG` 에 `#error` 헤더를 주어 imgui.h 를 거치는 include 를 컴파일 오류로 만듭니다(린트 게이트가 아님).
- **패널과 커맨드의 `EditorContext::get()`(62 파일 224 곳)은 생성자 · 인자 주입으로 바꾸지 않습니다**(2026-10-11). 패널은 레지스트리가 인자 없이 만들고(`createEditorPanel<T>`),
  상용 엔진도 에디터 하위 시스템을 전역 창구로 줍니다(언리얼 `GEditor->GetEditorSubsystem<T>()`, 유니티 `Selection.activeObject` · `EditorWindow.GetWindow<T>()`).
  EditorTest 는 UI 매니저 때문에 `EditorContext` 를 만들 수 없으므로 컨텍스트를 인자로 바꿔도 테스트가 늘지 않습니다. 테스트할 판단이 생기면 그 함수만 필요한 서비스 하나(작업 공간, 선택)를 인자로 받습니다.
  `get()` 은 지금 에디터 범위의 주인을 돌려줍니다(범위의 첫 칸 — 프레임당 57 회 호출에 비교 한 번).
  `editor::findSubsystem<T>()` 는 에디터 범위(컨텍스트의 하위 객체)를 먼저 보고 없으면 호스트(엔진) 표를 봅니다. 언리얼이 `GEditor` 서브시스템과 `GEngine` 서브시스템을 범위로 가르는 것과 같은 순서입니다.
- **임포터(`Editor/Common/Asset`)를 라이브러리로 떼지 않습니다**(2026-10-11). 명령 줄 임포트(`App --import-*` · `--check-*`)는 App 이 에디터 모듈을 인스턴스 없이 로드해 C-ABI 진입점 `importEditorAssets` 를 부르고,
  쿠킹은 임포트 산출물만 읽으며, 전용 서버와 Shipping 은 임포트하지 않습니다. 상용 엔진도 에디터 바이너리를 머리 없이 돌립니다(언리얼 커맨드릿, 유니티 `-batchmode`, Godot `--headless --import`).
  `Common/Asset` 은 이미 ImGui 를 모르는 층 1 이라, 에디터 없는 빌드 팜이나 쿠킹 서버에서 임포트할 일이 생기면 그때 정적 라이브러리로 묶는 비용이 작습니다.
- **imgui-node-editor 의 vcpkg 수정은 업스트림에 PR 을 내지 않고 오버레이로 유지합니다**(2026-10-06). 외부에 공개하는 일은 사용자가 요청할 때만 합니다.
- **DPI 150 % 는 대체 테스트로 확인하고, 글자 래스터 선명도만 실제 모니터에서 확인합니다**(2026-10-06). 남은 확인은 [백로그](backlog/ui.md) `B-8` 에 있습니다.
- **에디터 보강의 결정 열다섯 개(D1 ~ D15)**(2026-10-06, 사용자 지시 "특별한 의도가 없으면 상용 엔진과 비교해 나은 쪽"으로 추천을 정함). 단위는 모두 적용했고 계획 문서는 지웠습니다(남은 로드맵은 [백로그](backlog/editor.md) `D3-1`).
  - **확장 지점**(D1 ~ D4): EditorModule 을 SHARED DLL 로 바꾸고 `SW_EDITOR_API` 로 내보냅니다. GameFramework · 키트의 에디터 확장은 `Source/GameFramework/PlugIn/Editor/<이름>/` 의 `GF_EditorPlugIn_<이름>`(2026-10-13 `Base/Editor` · 키트 안 `Editor/` 에서 옮김 — 런타임 층 `Base/` 밖의 별도 축이라 한 곳에 모읍니다), 게임의 것은 `SWGameEditor` 이고 매니페스트 종류는 `EditorExtension`(Dev 전용)입니다.
    등록부만 따로 떼지 않은 이유는, 확장이 쓸 위젯과 선택과 Undo 까지 결국 옮겨야 하기 때문입니다. 언리얼 에디터 모듈이 `UnrealEd` 를 그대로 링크하는 것과 같습니다.
    vcpkg imgui 는 정적 라이브러리라 DLL 마다 사본이 생기므로, CMake 가 확장마다 ImGui 컨텍스트 결속 소스를 만듭니다(Dear ImGui FAQ 의 DLL 지침). 확장의 메뉴와 단축키는 등록 줄 `SW_EDITOR_COMMAND` 로 더합니다(유니티 `[MenuItem]`).
  - **설정**(D5 ~ D7): 에디터 환경설정은 `Saved/Editor/EditorPreferences.json` 에 기본값과 다른 값만 두고, 섹션은 리플렉션 구조체 하나입니다(언리얼 `UDeveloperSettings`). 단축키 덮어쓰기는 `Saved/Editor/Shortcuts.json`(유니티 Shortcuts Manager)입니다.
    모듈 켜고 끄기는 프로젝트 매니페스트를 고치고 빌드와 재시작을 묻습니다. 실행 중 켜기는 없습니다(언리얼 Plugins 창의 Restart Now).
  - **디버그와 캡처**(D8 ~ D13): assert 무시 대화상자는 대화형 에디터 실행에서만 띄우고 자동 실행(`-unattended`, 시나리오, 프로파일)에서는 지금처럼 멈춥니다(언리얼 `ensure`). GPU 캡처는 네 백엔드를 모두 잡는 RenderDoc in-app API 이고 PIX 는 하지 않습니다.
    스크린샷 버튼은 PNG, 테스트용 `-gv_screenshot` 은 PPM 그대로입니다. 보기 모드는 Normals, Depth, Overdraw 를 더하고 Shader Complexity 는 하지 않습니다(머티리얼 그래프가 없어 의미가 약합니다).
    스레드 미니 타임라인은 프로파일러 패널 안에 두고 깊은 분석은 Tracy 로 합니다. 버그 리포트는 언리얼 `BugIt`/`BugItGo` 구조이고 zip 은 만들지 않습니다.
  - **씬 뷰 / 게임 뷰 분리**(V1, 2026-10-10): 에디터 카메라 뷰(Scene)와 게임 카메라 출력(Game)은 다른 패널이고 같은 영역의 탭이 기본이며, 재생 단추는 상단 툴바에 있습니다(유니티 Scene · Game · 툴바).
    보이는 패널의 뷰만 그리고, 엔진에는 게임 뷰를 주 출력으로 · 씬 뷰를 호스트 타깃 추가 뷰로 요청합니다(언리얼처럼 뷰포트마다 자기 뷰 패밀리 — 렌더러를 두 번 돌리지 않고 다중 뷰 하나로).
  - **씬 뷰 툴바는 떠 있는 오버레이 바**(2026-10-11, 사용자 요청): 유니티 씬 뷰 Overlays 처럼 바마다 끌어 옮기고 가장자리에 붙이고 접고 숨긴다. 언리얼의
    고정 뷰포트 툴바보다 좁은 씬 뷰에서 잘리지 않는다. 재생 툴바(상단)는 그대로 둔다 — 도킹 창으로 바꾸는 안은 사용자가 거둬들였다.
  - **선택 표시는 경계 상자**(N1, 2026-10-11): 고른 오브젝트는 강조 주황 경계 상자로 그린다(Godot 3D 선택 상자). 언리얼 · 유니티의 실루엣 외곽선은
    선택 오브젝트만의 스텐실 · id 버퍼가 네 백엔드에 다 있어야 해 미뤘다(지금 `postoutline` 은 깊이 불연속 외곽선이라 선택을 모른다).
  - **에디터의 통계 표시는 디버그 HUD 하나로**(2026-10-11, 사용자 요청): 씬 뷰 Stats 상자(FPS · 오브젝트 · 해상도)와 게임 뷰의 `DebugOverlayState` 줄을 지우고 HUD 섹션으로 바꿨다.
    언리얼은 에디터 뷰포트와 PIE 가 같은 `stat` 체계를 쓰고 켜기만 뷰포트마다다 — 같은 수치를 두 경로로 그리면 값 · 서식이 갈라진다. 게임 뷰는 런타임 HUD 를 그대로 보이고,
    씬 뷰는 런타임 UI 가 없으므로 같은 섹션 본문을 ImGui 로 그린다(뷰 해상도 줄만 씬 뷰 몫). 상태는 엔진 사용자 설정 한 곳이고 에디터 환경설정 파일에 다시 적지 않는다.
  - **Input Map Editor 는 아홉 탭 → 셋**(2026-10-11): 편집은 언리얼 Enhanced Input 처럼 한 화면(액션 · 바인딩 · 발화 규칙 · 충돌 · 글리프)에, 장치 · 그래프 · HUD · 명령 패턴은
    Debug 탭의 접는 절로, 녹화 · 주입은 한 탭으로 모았다. 시연용 단추(Hadoken 매크로 · 입력 버퍼 시험)는 지웠다 — 게임 코드가 쓰는 기능이고 편집기 기능이 아니다.
  - **프로파일러 캡처 파일은 패널 표의 창(최근 N 프레임) 글 파일 하나**(2026-10-11): 유니티 Profiler 의 .data · 언리얼 Insights 의 .utrace 같은 전체 기록은
    Tracy 캡처가 맡는다. 호출 트리는 Timeline 녹화를 접어 만든다(유니티 Hierarchy) — 따로 계측하지 않는다.
  - **렌더 타깃 창의 채널 · 깊이 · 픽셀 값은 CPU 스냅샷**(2026-10-11): 고를 때 · Refresh · Auto(30 UI 프레임)에 렌더 스레드가 타깃을 읽고(`readbackTexture2D`,
    깊이는 네 백엔드 모두 0..1 float32), 에디터가 RGBA8 로 바꿔 올린다. RenderDoc 텍스처 뷰어 · 유니티 Frame Debugger 처럼 멈춘 그림에서 값을 읽는다.
    언리얼 Buffer Visualization 처럼 셰이더로 매 프레임 그리는 안은 네 백엔드에 보기 셰이더와 픽셀 값 읽기 길을 따로 둬야 해 두지 않았다. 색 그대로(RGBA)는 GPU 텍스처를 실시간으로 보인다.
  - **플레이 옵션**(N6, 2026-10-11): 새 창 플레이는 엔진을 따로 띄운 프로세스(언리얼 Standalone Game)다. 언리얼의 "New Editor Window(PIE)" 는
    게임 뷰 패널을 떼어 띄우는 것(ImGui 멀티 뷰포트)으로 이미 되므로 따로 두지 않았다. 플레이 중 인스펙터 편집은 허용하고 Stop 이 되돌린다(유니티),
    남기기는 고른 오브젝트 단위의 Keep Simulation Changes(언리얼)다 — 프로퍼티 단위 남기기는 스냅샷 비교가 필요해 미뤘다.
  - **애니메이션 그래프 전이 편집은 오른쪽 인스펙터**(N8, 2026-10-11): 유니티 Animator 의 전이 인스펙터처럼 상태(이름 = 클립 · 반복)와 전이(조건 하나 · 블렌드)를
    캔버스 옆 칸에서 고친다. 언리얼의 전이 규칙 그래프는 데이터(`AnimationGraphLink`)가 비교 하나라 두지 않았다 — 조건이 여럿이 되면 그때 목록으로 늘린다.
  - **노드 그래프 틀 공개**(T3, 2026-10-10): `EditorNodeGraph` 를 내보내고 찾아 넣기 · 핀 타입 거절 · 문제 노드 표시를 틀에 둡니다(언리얼 `SGraphEditor`, 유니티 GraphView).
    확장 모듈은 imgui-node-editor 를 자기 사본으로 링크하고, 템플릿이 그 사본에도 지금 편집기를 겁니다 — ImGui 컨텍스트 결속기와 같은 까닭(vcpkg 정적 라이브러리).
    노드 그래프 라이브러리를 DLL 로 다시 짓는 길은 vcpkg 포트를 바꿔야 해 버렸습니다.
  - **편집 틀과 콘텐츠**(D14, D15): 공용 커브는 엔진 값 타입 `FloatCurve`(언리얼 `FRichCurve`, 유니티 `AnimationCurve`)이고 키트의 `GameCurve` 도 2026-10-13 에 지우고 `FloatCurve` 로 옮겼습니다(데이터는 `<Curve><Key …/></Curve>`, 읽기는 `GameDataXML::readFloatCurve` 한 곳). 콘텐츠 브라우저는 활성 게임 팩이 기본이고, 참조 찾기는 텍스트 에셋 역색인입니다(유니티 Find References 와 같은 텍스트 검색).
    적용(A1, 2026-10-10)에서 All packs 는 `game/` 루트 하나가 아니라 팩마다 루트 하나로 했습니다(언리얼 Content Browser 가 플러그인 콘텐츠를 루트마다 보이는 것과 같습니다).
    텍스처 썸네일(R9)은 썸네일 전용 축소본을 굽지 않고 엔진 텍스처 캐시의 텍스처를 그대로 ImGui 에 등록합니다. 언리얼은 썸네일을 패키지에 저장하지만, 우리 DDS 는 이미 밉이 있고 64 개 LRU 로 메모리가 묶입니다.
    에셋 이름 바꾸기와 옮기기(N3)는 언리얼처럼 리다이렉터를 남기지 않고 참조하는 글을 그 자리에서 고칩니다. 데이터에 옛 이름 호환을 두지 않는 결정(5-4)과 같은 방향이고, 지우기는 유니티 · 언리얼처럼 OS 휴지통입니다.
    역색인은 `Resource/` 가 바뀔 때 파일 단위 갱신 대신 통째로 다시 훑습니다.
  - **맵 검사**(T2, 2026-10-10): 언리얼 Map Check 처럼 창 하나와 상태줄 수, 맵을 열 때 오류가 있으면 알림입니다. 결과는 새 수집기가 아니라 로드 · 저장 · 편집 때 이미 모이는 `ValidationIssueLog` 를 그대로 씁니다.
    인스펙터 프로퍼티 줄로 스크롤하는 것은 넣지 않았습니다. 고르면 인스펙터가 그 오브젝트를 보이고, 프로퍼티 이름이 표에 있어 찾기 쉽습니다.
  - **패키징**(T4, 2026-10-10): 진입점은 `Scripts/dev/MakePackage.py` 하나이고 에디터 창은 그것을 새 프로세스로 띄웁니다(언리얼 Package Project 가 UAT `BuildCookRun` 을 띄우는 것과 같습니다).
    쿠킹은 Shipping 빌드가 이미 하므로 빌드를 할 때는 다시 쿠킹하지 않고, 빌드를 건너뛸 때만 `CookAssets.py` 를 부릅니다. 산출 검사는 정적(실행 파일, 다른 타깃 실행 파일 누출)이고, 패키지를 띄워 보는 검사는 넣지 않았습니다(창과 GPU 가 필요하다). 언리얼 Asset Registry 는 바뀐 파일만 다시 읽지만, 텍스트 에셋이 377 개라 Debug 에서도 워커로 1.1 초이고 파일 감시 사건을 놓칠 일이 없습니다.
  - **머티리얼 미리보기**(N10, 2026-10-11): 언리얼 · 유니티는 셰이더를 GPU 로 그린 구를 보이지만, 여기서는 다중 월드 툴 창(로드맵 9)이 올 때까지
    머티리얼 값(기본색 · 거칠기 · 금속성 · 방출)으로 CPU 에서 셈한 구를 ImGui 꼭짓점 색으로 그립니다. 지금 오프스크린 렌더 경로를 만들면 미리보기 월드가 없어
    편집 중인 씬에 숨은 오브젝트를 두거나 패스 바인딩 계약을 따로 맞춘 그리기를 네 백엔드에 넣어야 하고, 로드맵 9 에서 버릴 코드라 기각했습니다.
    셰이더 코드는 반영하지 않으므로 툴팁이 Apply to Selection 을 가리킵니다.
  - **시퀀서 키 트랙**(N9, 2026-10-11): 움직임은 구간 클립의 트랜스폼 한 벌이 아니라 키 트랙(트랜스폼 아홉 채널 · 숫자 프로퍼티 하나, 채널마다 `FloatCurve`, 시각 단위는 프레임)이 몰고,
    클립은 대상을 켜는 구간만 남습니다(유니티 Timeline 의 Activation 트랙). 키 단추는 언리얼 Sequencer 처럼 대상의 지금 상태를 모든 채널에 찍고, 채널 값을 고치면 그 채널에만 키가 생깁니다.
    카메라 컷은 구간 항목 종류이고 등록부의 컷 카메라가 게임 카메라 선택에서 가장 먼저입니다(언리얼 Camera Cut 트랙). 시퀀서를 닫을 때 미리보기 전 상태로 되돌리는 언리얼 Restore State 는 넣지 않았습니다.
  - **카탈로그 표 편집은 XML 원문의 해당 구간만 바꿉니다**(2026-10-13, `CatalogXMLText`). DOM 으로 읽어 다시 쓰면 데이터 파일의 주석과 들여쓰기가 사라지고,
    `XMLNode` 에는 속성 · 원소를 빼는 API 도 없기 때문입니다. 행은 리플렉션 구조체가 아니라 XML 원소이고, 저장하면 그 종류의 실제 로더로 다시 읽어 검증합니다(언리얼 DataTable · DataAsset 검증에 해당).
  - **엔진 데이터 종류의 레지스트리 이전과 Engine 열거자 이름 표 통합은 미룹니다**(`D3-1` ⑦, 2026-10-14). 레지스트리(`Engine/Resource`, 티어 5)는 위 티어(Scene · UI · Audio · Animation …)가 채울 수 있으므로
    "층 규칙 때문에 못 올린다" 는 걸림돌이 아닙니다. 미루는 까닭은 셋입니다. (1) `ResourceDataSchemaTest` 표(`kArrDataKind`)의 로더 상당수가 테스트 전용 판단(씬 만들기 · 위젯 트리 짓기 · 빈 그래프 거절)을 담고 있어,
    엔진 .cpp 로 옮기면 그 판단이 제품 코드로 새거나 테스트에 둘로 갈립니다. 옮기려면 약 40 개 엔진 .cpp 에 등록 줄과 로더를 나눠 넣어야 합니다. (2) 테스트 표는 앞 줄이 먼저 맞는 함수 판정(`isPrefab` 은 접미 둘)이고,
    레지스트리는 접두 · 포함 · 접미 하나씩에 "정확히 한 종류만 맞아야 한다" 는 규칙이라, 옮기면 두 종류가 한 파일에 맞는 겹침을 하나씩 다시 따져야 합니다. (3) `EnumNameTable` 은
    `Source/GameFramework/Base/Foundation/Data/EnumNameTable.h` 에 있어 아래 층인 Engine 이 쓸 수 없습니다 — 먼저 Core(예: `Core/String`)로 옮기고 GameFramework 사용처 약 10 곳의 include 를 고쳐야 합니다
    (손으로 둔 Engine 이름 배열은 14 개 파일: Telemetry 5 · `Localization/CultureInfo` · `Reflection/ReflectValue` · `UI/Debug/DebugHUD` 등). 사용처 개수는 grep 근사치입니다.
    대안은 영역 하나씩 단계로 옮기는 것입니다: 먼저 `EnumNameTable` 을 Core 로 옮기고 include 만 바꾼 뒤(동작 불변), 로더가 `ResourceDataKindUtil::loadCatalog` 인 줄(예: Audio)부터 `SW_RESOURCE_DATA_KIND` 로 옮기면서
    같은 커밋에서 테스트 표의 해당 줄을 지웁니다. 테스트 전용 로더가 필요한 종류(scene · prefab · uidocument · animgraph …)는 테스트 표에 남깁니다.
- **에디터 플러그인만 읽는 데이터는 `editor/` 팩에 두고, 종류는 플러그인 .cpp 의 `SW_RESOURCE_DATA_KIND` 로 등록합니다**(2026-10-15, 좌우 이름 표 `editor/character/default.mirrornames.xml`). 언리얼이 에디터 전용 콘텐츠를 `Engine/Content/Editor` 에 따로 두어 쿠킹에서 빼는 것과 같은 자리입니다.
- **무인(배타) 자동화 동안 ImGui 입력 거르개는 포커스 사건도 지웁니다**(2026-10-15, `907f4d467`). 포커스를 남기면 ImGui 가 눌린 키 · 단추를 모두 놓아 누르고 있는 자동화 입력(뷰포트 비행)이 끊깁니다. 다른 창이 전경을 가져가는 것은 사람의 장치 입력과 같은 바깥 간섭으로 봅니다(`editor.focusloss` 시나리오).
- **UI 미리보기 인스펙터는 검색창이 맨 위이고 슬롯 구역도 그 검색어로 거릅니다**(2026-10-15, `b7359358e`). 언리얼 UMG Details 는 검색창이 위에 있고 Slot 범주 행도 같은 검색에 걸리는 것으로 알고 있습니다. 자동화 표식은 잘렸는지(`_bClipped`)를 함께 기록하고 `EditorClick` · `EditorDrag` 는 잘린 표식을 누르지 않고 실패로 알립니다 — 잘린 자리를 누르면 다른 위젯이 받아 원인이 엉뚱한 기대 실패로 나타납니다.
- **새 에디터 인스턴스의 씬 뷰는 씬에 남은 에디터 카메라의 포즈를 이어받습니다**(2026-10-15, `666257e08` · `0fa1e0b35`). 백엔드 교체 · 에디터 리로드는 에디터 모듈을 새로 만들지만 카메라 오브젝트는 씬과 함께 넘어갑니다. 판정은 "처음 쓰는 카메라가 생성 자리(`EditorCamera::getSpawnPosition`)에 있지 않다" 이고, `camera.reset` 은 마지막 카메라를 기억한 채 포즈만 되돌립니다.
- **크래시 세션 ID 는 함수 지역 정적 초기화로 한 번 만들고, 크래시 핸들러 설치 때 미리 부릅니다**(2026-10-15, E-9). 언리얼 `FApp::GetSessionId` 처럼 시작 때 정해 두는 값입니다. 검사 후 채우는 정적 버퍼는 로그 워커와 크래시 보고 스레드가 동시에 처음 불러 덤프와 컨텍스트 파일의 세션 이름이 갈렸습니다(부하 40 병렬에서 3/280 → 0/480).
- **에디터 테마의 원본은 저장소의 토큰 JSON(`Resource/editor/theme/*.tokens.json`)이고 Figma 는 선택 입력입니다**(2026-10-15, `MakeEditorTheme.py`). 언리얼 에디터의 스타일은 Slate 스타일셋(코드)과 테마 JSON 이고, 유니티 UI Toolkit 은 USS 변수, Godot 은 Theme 리소스 파일입니다. 어느 엔진도 디자인 도구를 원본으로 삼지 않고 엔진이 읽는 파일이 원본입니다(기억에 의존한 비교). 원본이 Figma 면 인증이 없는 환경(CI, 다른 PC, 에이전트)에서 테마를 검사하거나 고칠 수 없습니다. 그래서 Figma 는 `import-figma` / `export-figma` 로 값만 주고받습니다.
- **Figma 와의 교환 형식은 W3C Design Tokens(DTCG) JSON 이고 Figma REST 변수 응답도 읽습니다**(2026-10-15, `MakeEditorTheme.py`). DTCG 는 `$value` / `$type` / 별칭 `{경로}` 가 정해진 표준이라 Tokens Studio, Style Dictionary 같은 도구가 읽고 씁니다. Figma 는 변수 JSON 가져오기를 기본 기능으로 두지 않고 플러그인에 맡깁니다(Figma 도움말 확인). 자체 형식을 만들면 그 도구들과 연결되지 않고, REST 응답만 쓰면 내보내기가 안 됩니다. 수 값은 단위 문자열 대신 `number` 로 씁니다. 실제 Figma 로 가져오는 시험은 하지 못했습니다.
- **에디터 스타일 린트(`CheckEditorStyleLiterals`)는 커밋이 추가한 줄만 검사하고 전체 훑기는 기준선(현재 171건) 이하만 허용합니다**(2026-10-15). 기존 위반 170여 건을 한 번에 토큰으로 옮기면 병합 충돌과 시각 회귀 위험이 큽니다. 그렇다고 기준선 없이 두면 새 위반이 쌓입니다. 같은 이유로 `CheckConditionParts` 도 새 줄 방식을 씁니다. 언리얼의 에디터 코드 스타일 검사도 변경된 줄을 대상으로 하는 방식이 흔하다고 알려져 있습니다(기억에 의존한 비교, 확인하지 못함). 옮길 때마다 기준선을 낮춥니다.
- **에디터 UI 캡처는 렌더 스레드가 그 프레임의 최종 ImGui 그리기 목록(모든 뷰포트 합본)을 오프스크린 타깃에 한 번 더 그려 읽습니다**(2026-10-15, `EditorUICapture`). 창 화면 복사(전경으로 올리기 · `BitBlt` · OS 창 캡처)는 가림 · 포커스 · 다른 창에 흔들리고 플로팅 창을 한 장에 담지 못합니다. 백버퍼 읽기(`blitTexture( 0, … )`)는 메인 창만 담습니다. 멀티 뷰포트의 목록은 모두 화면 좌표라 이어 붙이면 플로팅 창까지 한 장이 되고, 네 백엔드가 같은 ImGui 렌더러로 같은 목록을 그려 결과가 같습니다. 언리얼의 UI 포함 스크린샷(`FScreenshotRequest` · `HighResShot`)과 Slate 위젯 캡처가 위젯 트리를 다시 그려 얻는 것과 같은 방향입니다(기억에 의존한 비교). 단언은 그림 비교가 아니라 튼튼한 지표(마젠타 비율 · 스크롤바 없이 잘린 창 수)입니다.
- **테마 토큰 파일의 키는 리플렉션 멤버 이름이고, 실행 값(`EditorThemeConfig`)에는 기본값을 두지 않습니다**(2026-10-15). 읽기 코드가 따로 없고 모르는 키 · 빠진 색은 읽기 오류입니다. 값의 원본이 파일 하나라서 코드와 파일이 어긋날 수 없습니다. 사용자가 바꾼 강조색 · 둥글기만 설정(`EditorConfig`)에 저장하고(바꾸지 않았으면 -1), 나머지는 늘 토큰 값을 따릅니다. ImGui 기본 다크를 그대로 쓰던 Classic Dark 는 토큰으로 나타낼 수 없는 예외 경로라 없앴습니다.
- **캐릭터 편집 창의 결과는 데이터 프리셋 에셋(`data/appearance/presets/<id>.appearance.xml`, 루트 `<CharacterAppearance>`)으로 내보내고, 씬의 `CharacterAppearanceComponent` 는 지금처럼 프리셋 id 로 참조합니다**(2026-10-16).
  프리셋은 기준 프리셋을 부모로 두고 선택이 정한 칸만 적는다(유니티 프리팹 변형). 한 파일에 하나라 내보내기가 `presets.xml` 을 고쳐 쓰지 않고, 외형 데이터가 `presets.xml` 뒤에 그 폴더를 읽는다.
  내보내기는 실행 중인 게임의 `AppearanceDatabase` 에도 그 파일을 올린다(`loadPresetFile`, 같은 id 는 바꾼다). 쓰기(`CharacterAppearanceCatalog::writePresetToNode`)는 읽기 옆 한 자리다.
  언리얼 · 유니티는 프리셋을 에셋으로 두고 씬이 그 에셋을 가리킵니다. 이 엔진에서 그 자리는 게임 데이터 카탈로그의 항목이고 id 가 안정된 참조입니다(이름 해시 충돌은 데이터 로드가 막는다).
  컴포넌트에 프리셋 파일 경로 칸을 더하면 프리셋을 가리키는 길이 둘이 되고 부모 상속 · 검사(`AppearanceDatabase::finishLoad`)를 다시 짜야 합니다. 세이브 형식(`UserAppearancePresetStore`)은 플레이어 것이라 씬이 읽지 않습니다.
- **에셋 임포트 시나리오는 리소스 트리 안의 임시 폴더(`editor/automationtemp/`)로 가져오고, 시나리오가 처음과 끝에 그 폴더를 지웁니다**(2026-10-16, `asset.import`).
  가져온 에셋은 리소스 id 와 `.meta` 가 있어야 씬이 참조하므로 `Saved/Automation` 으로는 흐름을 잇지 못합니다. 폴더는 `.gitignore` 가 무시하고 명령이 그 폴더 밖을 거절해 저장소 리소스를 바꾸지 않습니다.
  언리얼 자동화 테스트가 임시 콘텐츠 폴더에 가져오고 끝에 지우는 것과 같은 방향입니다(기억에 의존한 비교).
- **프리팹을 만들 때 자식 프리팹 인스턴스는 중첩 프리팹 부품이 됩니다**(2026-10-16, `EditorInspectorCommands::adoptChildPrefabInstances`). 프리팹 본문은 루트 오브젝트와 중첩 인스턴스 노드뿐이라
  그대로 두면 자식 인스턴스가 프리팹에 실리지 않습니다. 유니티가 프리팹 인스턴스를 자식으로 둔 계층으로 프리팹을 만들면 중첩 프리팹이 되는 것과 같습니다. 기존 프리팹에 쓰는 Apply 는 되돌리기와 묶이지 않아 옮기지 않습니다.

### 5-12. 기능 대기열 결정 (2026-10-17)

백로그 `기능` 항목 50건을 작업 단위로 나누면서 정한 것입니다. 상용 엔진(언리얼 · 유니티 · Godot)의 같은 자리를 기준으로 사용자가 정했거나(사), 제가 유추하고 사용자가 승인했습니다(추). 뒤집을 때는 이 표를 고칩니다.

| 번호 | 결정 | 근거 | 출처 |
|---|---|---|---|
| D2-6.3 D2-5.5 | 머티리얼 텍스처 칸을 언리얼과 같은 16개로 늘린다. | 4개 한계가 지형 레이어 다섯 이상과 툰 텍스처를 막는다. 언리얼 머티리얼 샘플러 한도가 16이다. | 사 |
| D2-6.4 D2-10.2 | G버퍼를 확장해 셰이딩 모델 ID와 커스텀 데이터를 싣는다. 툰을 포워드 전용으로 두지 않는다. | 언리얼 디퍼드 방식. Roughness/Metallic 보기도 같은 확장으로 푼다. | 사 |
| D2-6.1 | 정점에 탄젠트를 넣고 임포트에서 MikkTSpace로 만든다. 옛 형식 리더는 두지 않고 에셋을 다시 쓴다. | 언리얼 · 유니티 · MToon 모두 정점 탄젠트를 쓴다. | 추 |
| D2-10.3 | Shaded Wireframe · Light Complexity · Mipmaps 보기 모드를 만든다. | 언리얼 · 유니티에 있는 디버그 보기다. | 사 |
| D3-7.4 | 엔진에 스카이 컴포넌트를 추가한다(하늘 그리기 + 환경광). 안개 · 시간대(D2-5.2)는 따로 둔다. | 언리얼 SkyAtmosphere · SkyLight, 유니티 Skybox, Godot Sky. | 사 |
| D2-11 | 파티클 · VFX 시뮬레이션은 CPU 와 GPU 를 둘 다 만들고 개발 빌드에서 `gv` 변수로 고른다. Shipping 은 GPU 로 고정하며 CPU 경로와 변수를 두지 않는다. | 학습용 엔진이라 두 방식을 비교해 보게 한다. 언리얼 Niagara 도 CPU · GPU 시뮬을 둔다. | 사 |
| D1-2.1 | 키트 투사체 · 근접 판정을 Box2D 센서로 옮기고 겹침 월드를 없앤다(뒤 순서). | 유니티 트리거 · Godot Area2D처럼 물리 월드 센서 하나로 통일한다. | 추 |
| D1-2.1 구현 | `BoxCollider2DComponent` 는 2D 씬의 키네마틱 센서 바디다. 막는 콜라이더도 센서(`PhysicsBodyDesc::_bOverlapOnly`, 이벤트 · 질의에서 트리거로 치지 않음)다. 연속 판정은 콜라이더가 지난 자리에서 지금 자리까지 `shapeCastAll` 로 쓸어 지나친 것을 시작 · 끝으로 알린다. 겹침은 고정 스텝에서만 난다. 무버 캐스트가 센서를 거르도록 센서가 아닌 셰이프 마스크에 전용 비트를 둔다. 타일맵 충돌은 정적 바디(강체를 막는다)다. | Box2D 3.1 은 키네마틱끼리 접촉을 만들지 않고 센서는 연속 충돌이 없다(문서가 캐스트를 권한다). 센서끼리는 서로를 본다(3.1.1 `sensor.c`). 유니티 트리거가 FixedUpdate 에서 나는 것과 같다. | 추 |
| D1-3.3 | 적이 `MoveTo`로 쫓는 게임을 내비메시로 옮긴다. `AbilityArena`부터. | 유니티 NavMeshAgent가 표준 경로다. | 추 |
| D1-5.3 | `PrefabInstanceRegistry` 기록은 되돌리기 이력의 수명에 맞춰 비운다. | 언리얼 · 유니티는 오버라이드 기록을 오브젝트 수명에 묶는다. | 추 |
| D4-3.3 | 명령형 게임의 오른쪽 스틱은 카메라 이동과 줌, 커서는 왼쪽 스틱. | 패드 RTS 관례. | 추 |
| D4-7.1 | 프리로드 고정은 요청이 참조를 쥐는 핸들 방식(핸들이 사라지면 해제). | 언리얼 FStreamableHandle, 유니티 Addressables 참조 카운트. | 추 |
| D4-8.7 | 양손 무기의 칸 점유는 엔진이 조회 API만 주고 막을지는 게임이 정한다. | 백로그 본문과 GAS 방식. | 추 |
| D4-9.1 | 중간 규모 대기열 순서: HZB 컬링, 데칼, SSR, HDR, 업스케일러, 프로파일러와 디버거, 비주얼 로거, DCC 내보내기. | 가치 대비 비용 순. | 추 |
| D4-13.2 | 의도 기록 · 재생의 정본 진입점은 콘솔 명령과 명령줄 인자, 에디터 단추는 그 위. | 언리얼 Replay(`demorec` · `demoplay`). | 추 |
| D4-15.1 | 말 모델은 외부 CC0 에셋을 받는다. | 저장소 규칙이 CC0만 넣는다. 출처 라이선스는 받을 때 확인한다. | 추 |
| D7-1.5 D7-2.2 D7-5.9 | 리눅스 확인은 CI 잡에만 맡긴다. WSL 검증은 사용자가 요청할 때만 한다. | WSL은 느려서 쓸 수 없다. | 사 |
| D7-2.1 | 서버 빌드에서 렌더러 · RHI 코드를 뺀다(뒤 순서). | 언리얼 Server 타깃, 유니티 Dedicated Server 빌드. | 추 |
| D7-5.4 D7-5.5 | 외부 로그인 · 푸시 · 영수증 제공자는 계약과 가짜까지만 둔다. | 외부 계정과 비용이 든다. 언리얼 Online Subsystem처럼 플랫폼 구현은 분리한다. | 추 |
| D7-3.3 | 프리셋은 사용자별 에디터 설정 파일에 저장한다(커밋하지 않음). | 언리얼 EditorPerProjectUserSettings, 유니티 UserSettings. | 추 |
| D7-3.6 | 리슨 호스트 줄은 프레임 번호를 쓰되 "프레임"이라고 표기한다. | 리슨 서버는 고정 틱이 없다. | 추 |
| F-1.1 | `_splatTexturePath`를 `_layerWeightsPath`류로 바꾸고 씬 XML을 다시 쓴다. | 옛 형식 리더를 두지 않는 규칙. | 추 |
| D5-2.1 D5-2.2 | 번역이 생기기 전에는 `ja`를 `stagedCultures`에 넣지 않는다. 아랍어 외 RTL은 요구가 생기면. | 언리얼은 번역된 문화권만 출시한다. | 추 |
| D5-3.3 | 일정 자리를 놓치면 가장 가까운 내비메시 지점으로 투영한다(허용 오차). | 유니티 SamplePosition, 언리얼 ProjectPointToNavigation. | 추 |
| D3-12.3 | Reset은 기본값으로, Revert는 마지막 저장 · 프리팹 원본으로 되돌린다. 뜻으로 구분해 둘 다 쓴다. | 언리얼 · 유니티의 같은 구분. | 추 |
| D3-4.2 | 무적(`cheat.god`)은 어빌리티 시스템의 태그 기반으로 넣는다. | 언리얼 CheatManager God와 GAS 태그. | 추 |
| D3-9.4 | 포토 모드는 게임 HUD를 기본으로 숨기고 토글을 둔다. | 언리얼 등 포토 모드 관례. | 추 |
| D3-9.2 | 시나리오 녹화는 실제 입력만 기록한다. 자동화 주입 입력은 기록하지 않는다. | 주입 입력을 기록하면 재생 때 되먹임이 생긴다. | 추 |
| D3-1.2 | 데이터 종류 레지스트리 이전은 D3-1.1(Core 이전)을 먼저 하고 결과를 보고 정한다. | 5-11의 단계 순서. | 추 |
| D3-10.3 | 월드 공간 워프를 별도 씬 오브젝트로 분리한다(뒤 순서). | 블렌더 변형자의 기준 오브젝트 방식. | 추 |
| D2-12 | 간접 조명 첫 구현은 옥타헤드럴 2D 아틀라스 프로브(큐브맵은 필요해지면 전환)로 시작한다. 라이트 프로브는 균일 격자 SH, 베이크 결과 `.probes`는 커밋한다. 금속도 · 거칠기 PBR을 도입한다(툰은 별도 경로 유지). 골든 이미지를 다시 뜬다. DX11 · GL은 DX12 · Vulkan과 최대한 같은 기능 수준으로 맞춘다(사용자 결정 2026-10-17). 백엔드 제약(DX11 엔진 텍스처 칸 4개, bindless 없음, 구조 버퍼와 간접 인자 겸용 불가)은 기능을 빼서가 아니라 백엔드 쪽 우회(칸 재배치, 복사 단계)로 푼다. | 큐브 바인딩은 네 백엔드가 모두 거부하고 칸 지도를 이미 16칸 결정으로 다시 짠다. CI에 GPU가 없다(내비메시 선례). 유니티 APV 방향. | 추 |
| D7-8 | 예측은 서버 권위 + 내 폰만 재시뮬레이션 + 허용 오차 비교로 한다. 규모는 64인 이하, 30 Hz 유지, 관심 영역 거름은 `MMOReplicator`의 격자를 Core 부품으로 빼서 두 키트가 함께 쓴다. 처음 연결할 게임은 Shooter3D. 월드 전체 롤백(물리 객체 예측)은 그것을 원하는 게임이 생길 때만 한다. | 언리얼 CharacterMovement SavedMove, Overwatch 이동 예측, Fish-Net. 플랫폼 간 비트 결정성은 보장되지 않는다. | 추 |
| D2-11 | VFX 첫 모듈 형식은 고정 모듈 + 값 커브(`FloatCurve`)로 시작하고 다형 목록 · 그래프는 뒤로 미룬다. 서버는 시뮬레이션하지 않는다(외형 전용). 2D 픽셀 퍼펙트 입자는 P1에 넣지 않는다. | 설계 검토 권고. 입자 수 목표는 P1 측정 뒤에 정한다. | 추 |
| 리소스 | 애니메이션 비용 측정용으로 합성 리그 생성기(`Scripts/dev`)를 만든다. 본 수(20 · 60 · 150 · 300), 위상(인간형 · 네 발 · 다리 여럿 · 긴 척추), 클립 수, 모프 수를 매개변수로 glTF 리그와 합성 클립을 낸다. 눈에 보이는 품질용 리소스(CC0)와 비용 측정용 데이터를 나눈다. | 실제 에셋 페이지에서는 본 수가 확인되지 않고 라이선스도 불확실하다. 절차 생성 도구가 이 저장소의 관례다. | 사 |
| 리소스 | 합성 리그는 생성기(`Scripts/dev/MakeSyntheticRig.py`, 결정적)가 원본이다. 임의 인자의 결과는 커밋하지 않고 명령으로 다시 만든다. 테스트 · 벤치가 읽는 대표 넷(본 20 · 150 biped, 60 quadruped, 300 multileg, 클립 둘 · 모프 둘, 합 953 KB)만 쿠킹에서 빠지는 원본 폴더 `Resource/game/empty/rigs_raw/` 에 커밋하고, PythonTest 가 생성기 출력과 커밋한 바이트를 견준다. 런타임 결과(.mesh)는 커밋하지 않고 테스트가 임포터로 그 자리에서 읽는다. | 한도 1 MB 안이다. .mesh 는 인덱스 없는 삼각형 목록이라 몇 배 크다(2026-10-17). | 추 |
| 리소스 | Quaternius 에셋은 쓰지 않는다. 팩 페이지는 CC0라 하지만 같은 사이트 `license.html`이 에셋 재배포를 제한해서, 공개 저장소에 넣는 것이 재배포일 수 있다. 한글 글꼴은 OFL이라 넣지 않고 OS 글꼴 대체를 유지한다. | 공개 저장소 포함은 재배포일 수 있다. 확인을 기다리지 않고 닫는다(2026-10-17). | 사 |
| 리소스 | 픽셀 아트(16 px 세트, `engine/textures_raw/pixel/`)는 밉 없이 비압축 R8G8B8A8 sRGB 로 굽는다(`Pixel_Art` 임포트 규칙, `texture-pixel-art` 검사). `texture-limits` 의 2 의 거듭제곱은 밉이 있는 텍스처에만 요구한다(`mipped_power_of_two`) — 2 의 거듭제곱은 밉 사슬의 요구라, 밉 없는 UI 이미지 · 아틀라스를 제외 목록으로 하나씩 빼던 것을 규칙 자체로 바꿨다. | 유니티 픽셀 아트 권장(Filter Point · Compression None · 밉 끔), 언리얼 Paper2D 의 2D 픽셀 아트 설정과 같다. BC 는 4x4 블록이 이웃 색을 섞고 변이 4 의 배수여야 해 918x203 같은 아틀라스를 못 담는다. 도입 13.7 MB(원본 0.5 + DDS 13.2, 2026-10-17). | 추 |
| D2-13 | 빛 컬링은 2D 타일 대신 3D 클러스터 하나로 하고(16×9×24 지수 슬라이스에서 시작), 빛 목록 형식 `LightGrid`를 먼저 고정한다. 생산자는 CPU로 먼저 만들고 GPU 컴퓨트 생산자도 만든다(개발 빌드는 `gv`로 고르고 Shipping은 GPU 고정). 목표는 카메라당 1024개(현재 상한), 측정 뒤 조정. 칸 밴드 확장은 D2-6.3의 16칸 지도 재작성과 한 번에 한다. "빛이 없으면 키라이트" 셰이더 폴백은 없앤다 — 3D 씬은 방향광이나 스카이를 데이터로 갖고 없으면 어둡게 그린다. 빛 순회는 커서 API(`swBeginLights` · `swNextLight`) 하나로 모은다. 첫 단계(S0)는 폴백 제거 + 커서 API이며 골든 이미지가 바이트까지 같아야 한다. | 2D 타일은 렌더 그래프에 컴퓨트 패스 상태가 없고 반투명 · 파티클 · 프로브를 못 덮는다. 컬링을 넣으면 폴백이 빈 클러스터마다 켜진다. 학습용이라 CPU · GPU 두 경로를 둔다(D2-11과 같은 방침). | 추 |
| D1-7 | 월드는 Scene을 유지하고 셀을 활성 씬에 더해 올린다(`World` 타입은 필요가 확인되면). 편집은 한 씬으로 하고 쿠킹 때 위치와 격자 설정으로 셀 파일을 나눈다(셀은 계산, 항상 올라 있을 것만 "공간 로드 안 함" 표시). 셀을 넘는 참조는 같은 셀이나 항상 올라 있는 오브젝트만 허용하고 그 밖은 쿠킹 오류로 멈춘다(조용히 null로 바꾸지 않는다). 서버도 플레이어 주변 셀만 스트리밍한다. 첫 대상은 WildWest. HLOD는 메시 LOD 뒤. 선행으로 정점 풀 부분 갱신(C-43)을 먼저 한다. | 언리얼 World Partition의 런타임 격자와 같은 자리. 셀별 매니저는 셀 사이 물리가 끊긴다. | 추 |
| C-41 | Linux · GPU 검증은 (1) CI 상태를 세션 시작에 보이게, (2) 셰이더 전 백엔드 확인, (3) 요구 백엔드를 정책 데이터로 받아 건너뛰는 대신 실패하는 형식, (4) 소프트웨어 렌더러 라벨(lavapipe+xvfb, WARP) 순서로 한다. GPU 러너(Team 조직 전환, 분당 요금)와 자체 호스트 러너는 하지 않는다. 소프트웨어 렌더러 전용 골든은 두지 않는다(5-2 유지). WSL 검증은 사용자가 요청할 때만 한다. | 이번 결함 넷은 모두 GPU 없이 잡히는 결함이었고 원인은 CI가 죽어도 보이지 않은 것이다. 자체 호스트 러너는 공개 저장소에서 포크 PR 위험이 있다. | 추 |
