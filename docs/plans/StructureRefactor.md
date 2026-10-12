# 구조 리팩토링 계획

구조 문제를 단위로 나눈 계획입니다. 단위마다 파일 소유자가 하나라 병렬로 진행할 수 있고, 진행 방식은 [작업 방식](../11_Workflow.md)을 따릅니다.
R1(엔진 루프에서 자동화 빼기), R2(에디터 그리기 인계 상태 기계), R3(레지스트리 통일)는 끝났고 R4 는 파일 일부가 남았습니다. 하지 않기로 한 것은 [결정 기록](../09_Decisions.md) 2절에 있습니다.

## 단위

### R4. 자체 포맷 로더를 리플렉션으로 (파일마다 한 단위)
- **남은 후보**: `SpriteClipAsset` · `SequenceAsset` · `Skeleton` · `AnimationGraphAsset` · `DialogueGraphAsset` · `LocalizationDocuments` · `CultureInfo`.
- **판정을 먼저 한다**: 적지 않은 값의 의미가 필요한가(부모 상속, "없음" 과 "0" 구분)? 그렇다면 옮기지 않는다. 옮기지 않기로 한 것과 그 이유는 [결정 기록](../09_Decisions.md) 5-4.
- **방법**: 구조체에 `REFLECT` · `PROPERTY`, `ConfigManager::readConfigJSON` 으로 읽기(모르는 키 · 읽지 못한 값 · 범위는 거기서 오류), 의미 검사만 직접. 데이터 파일은 키를 `_이름` 규약으로 다시 쓰고(옛 이름 별칭 없음, 결정 기록 5-4) 설정 카탈로그를 `typeName` 방식으로 바꿔 문서를 생성한다.
- **검증**: 그 로더의 기존 테스트, `ResourceDataSchemaTest`, 설정 · 리소스 스키마 게이트. 파일 하나가 한 단위라 서로 충돌하지 않는다.

### R5. `EngineLoop` 추가 분해 (= S13) — 끝남
- 2026-10-12 에 측정 없이 옮겼다(사용자 결정). 결과와 남은 측정은 아래 묶음 3 "S13 ~ S17".

### R6. 빌드 속도 5 · 6 단계
- [빌드 속도](BuildSpeed.md) 5 단계(유니티 범위 확대)는 공유 설치가 조용할 때 교차 3 쌍 + 배치 4 · 8 · 16 을 잰다. **다른 빌드가 없을 때만** 한다(측정이 오염된다).

### R7. 시나리오 체인 후속
- 내용은 [백로그](../06_Backlog.md) 1-4 의 "에디터 체인에 남은 것" ②③ 과 "백엔드 교체 단계" 가 원본이다. `ImGuiEditor` 는 만지지 않고 패널 파일만 다룬다.

### R8. 폴더 · 파일 구조 감사
- **결론**: 기계적으로 합치지 않는다. 25 줄 이하 파일은 97 개 / 2,932 개이고, 큰 클래스를 나눈 가족 대부분이 파일마다 120 줄이 넘으며 작은 조각(`EditorAssetTypeFocus.cpp` 36 줄)은 의존을 분리한 것이다.
  합치면 층 규칙(`CheckEngineLayers`)과 파일 분할 기준(그래픽스 구조 재편)을 해친다.
- **한다면**: 감사 보고서 스크립트(같은 접두의 `.cpp` 가족과 줄 수)를 `Scripts/lint/report/` 에 두고, 120 줄 이하 조각이 의존 분리가 아닌 가족만 사람이 본다. 지금 후보는 `RenderView.cpp`(13 줄, 함수 하나) 정도다.

## 병렬 배치

| 묶음 | 단위 | 서로 겹치는 파일 |
|---|---|---|
| A | R4 의 파일들, R7 의 패널 초기화 | 없음(각자 소유) |
| B | R5 | `EngineLoop.*` |
| C (조용한 PC) | R6 | 빌드 설정 |

적용 담당은 묶음 안에서 파일이 겹치지 않는 단위를 한꺼번에 받아 적용한다. 단위마다 커밋은 따로 두고(문제점 → 해결방안 → 결과), 검증은 단위의 검증 줄을 모두 통과해야 main 에 넣는다.

## 옮길지 판정하는 기준 (이득만 보지 않는다)

기존 구조로 옮기는 일은 줄 수가 줄어서가 아니라 **앞으로 고칠 곳이 줄어서** 한다. 후보마다 아래를 본 뒤 하나라도 걸리면 옮기지 않고 이유를 남긴다.

- **반쪽 이전 금지**: 한 클래스 안에 옛 방식과 새 방식이 섞이면 읽는 사람이 더 헷갈린다(예: UserSettings 공급자만 옮기고 적용기는 남기는 것). 통째로 옮기거나 통째로 두고, 필요한 API 가 공용 구조에 없으면 그 API 를 먼저 더할지 따로 판단한다.
- **훅 · 인터페이스는 쓰는 곳이 둘 이상일 때**: 쓰는 곳이 하나뿐인 일반화(새 훅 · 새 문맥 필드)는 호출 시점 순서를 숨기고 읽는 비용만 늘린다. 순서에 뜻이 있으면(씬 틱 뒤 · UI 앞 등) 훅 뒤로 숨기지 않고 호출 자리에 두고 이유를 주석으로 적는다.
- **동작이 같으면 코드를 늘리지 않는다**: 한도 없는 대기에 시한 · 경고를 붙이는 식으로 상수와 분기만 늘고 동작이 같은 변경(핫 경로 포함)은 하지 않는다.
- **의미가 조금씩 다른 복붙은 합치지 않는다**: 조건이 콜백 · 인자로 새는 공용 함수는 복붙 넷보다 읽기 어렵다.
- **옮기면 검증 한 줄이 따라온다**: 공용 구조가 주는 규칙(중복 거절 · 리로드 대체 · 순서)을 지키는지 보는 단언을 그 자리의 테스트에 둔다.

## SOLID 는 반드시 지킨다 (성능을 해치지 않는 선에서)

사용자 결정(2026-10-12): 위 기준보다 앞서는 원칙이다. 리팩토링 후보는 아래로도 판정한다. 성능은 **측정으로** 해치지 않음을 보이거나, 핫 경로가 아님(프레임당 한두 번 · 목록 순회 한 번 수준)을 코드로 보인다.

- **S**: 클래스 하나가 한 가지 이유로만 바뀐다. `EngineLoop` 처럼 일이 몰린 클래스는 나누는 쪽이 기본이다(R5 는 측정 후).
- **O**: 새 기능을 더할 때 기존 클래스를 고치지 않는다. 같은 모양의 `if`/`switch` 분기나 직접 호출이 늘어나는 자리는 레지스트리 · 관찰자 뒤로 뺀다.
- **L**: 인터페이스 구현이 계약을 좁히지 않는다. 관찰자 훅의 기본 동작은 중립(빈 함수 · `kRunning`)이다.
- **I**: 인터페이스는 쓰는 쪽이 필요한 만큼만 둔다. 한 훅에 서로 무관한 일을 얹지 않는다(훅이 둘이면 둘로).
- **D**: 위 티어가 구체 클래스를 직접 알지 않는다. 엔진 루프는 텔레메트리 · 자동화 · 교체기를 구체 타입이 아니라 관찰자 인터페이스로만 안다.

위 "옮길지 판정하는 기준"의 **훅은 쓰는 곳이 둘 이상일 때** 항목은 SOLID 와 부딪히면 SOLID 가 이긴다(구체 의존을 남기는 쪽이 더 큰 부채다). 순서가 뜻이 있으면 훅 이름과 주석에 순서를 적는다.

## SOLID 감사 결과와 후속 단위 (2026-10-12, 읽기 전용 감사 셋 · 31건)

증거(줄 수 · include 수 · 분기 수)는 감사 때 `D:\w\refac` 에서 다시 확인했다. L 위반은 Box2D 관절 하나뿐이고 I 는 약하다 — 이 저장소의 약점은 **S(큰 클래스)와 O/D(직접 호출 · 종류별 분기)**다.
성능 조건: 핫 경로(프레임당 수천 번)에는 가상 호출을 넣지 않는다. 뽑은 클래스는 값 멤버로 두고 비가상으로 직접 부른다. 핫 경로를 건드린 단위는 이동 전후 측정 한 줄을 남긴다.

### 묶음 1 — 끝남 (2026-10-12)
S1(UI 화면 스위치 등록) · S2(자동화 엔진 단계 등록) · S4(`RHIBackendInfo` 표) · S6(`supportsJoint`) · S7(새 에셋 템플릿 표) · S9(`registerTexture` 삭제, ABI v5)를 옮겼다. 검증: Debug 경고 0, 린트 63/63, `-L nogpu` 49/49, `-L hostgpu` 7/7.
**옮기지 않은 것(판정 기준에 걸림)**:
- **S3**(서버 `pause`/`resume`/`step` → 개발 명령): Shipping 이 `quit`/`stop`/`status`/`help` 를 직접 처리해야 해 한 함수에 두 방식이 섞이고(반쪽 이전), 쓰는 곳이 `ServerApp` 하나이며, 에디터가 같은 이름 `pause`·`stop`·`step` 을 이미 개발 명령(플레이 세션 뜻)으로 등록해 전역 레지스트리에서 충돌한다. 두 `quit` 은 뜻이 달라(표준 입력 루프 bool ↔ `ShutdownSignal`) 합치지 않는다.
- **S5 의 실행 함수 칸**: `bSkipEffect` 만 표 비트(`kSkipsWithoutPostProcess`)로 옮겼다. 전용 실행 switch 9 case 를 표로 옮기면 쿠커 · 검증이 읽는 데이터가 `FrameRenderer` 에 의존하고(D 위반) 새 패스를 더할 때 고칠 곳도 줄지 않는다.
- **S8**(`IProfilerTab`): 탭들이 패널 상태(`_scopeHistory` · 타임라인 · 불러온 트레이스)를 나눠 써서 탭만 떼면 반쪽 이전이고, 함수 표만 두면 고칠 곳 수가 같다. "`ProfilerPanel`(1243 줄)을 탭 상태 객체와 `ProfilerTabContext` 로 나누기" 를 묶음 3 후보로 올린다.
**후속**: `UIAutomationSteps::ensureLinked` 호출(`UISystem::initialize`)은 Engine 이 통째 링크라 불필요해 보인다. `Joint2DComponent` 는 Cone 을 고르면 `createJoint` 를 프레임마다 다시 시도해 오류 로그를 반복한다.

### 묶음 2 — 끝남 (2026-10-12)
S10(`SceneEngineServices` 주입) · S11(`BugItInputs` 주입, `makeInputs` / `capture`) · S12(에디터 프레임 단계 등록부 `EditorFrameStep` + 호출처 없던 `IEditor` 가상 둘 삭제)를 옮겼다. 검증: Debug 경고 0, 린트 63/63, `-L nogpu` 49/49, `-L hostgpu` 7/7.
**옮기지 않은 것**:
- **S10 의 인자 없는 생성자**(`Scene( name )` · `GameObjectManager()` · `SceneManager()`): 호출처가 테스트 · 쿠커 · 프리팹 원형 등 약 90 곳과 500 곳이다. 이 생성자들은 만들 때 `makeFromBoundEngine()` 으로 묶음을 한 번 읽고 클래스 안 접근은 모두 묶음을 거치므로 반쪽 이전이 아니다(`_pEngineOverride` 와 같은 "주입, 없으면 기본"). 범위 밖에 같은 패턴이 남았다: `FacialAnimationComponent` · `WidgetComponent` · `UISystem` · `FontSystem` 의 `getBoundEngineServices()`, `SceneCooker` · `SceneNavigationCooker` · `PortraitRenderer` 의 인자 없는 `Scene` 생성.
- **S12 의 프레임 앞 정리 · 프레임 열기 · 프레임 닫기**: ImGui 순서와 렌더 스레드 인계(`DrawSnapshotHandoff`)가 위치를 정하는 셸의 프레임 계약이라 `updateUI` 에 두고 이유를 주석으로 적었다.
**측정할 것**: S12 의 `GT.Editor.updateUI` p50/p99 전후(Release · `-EnableEditor -dx12 -gv_profileFrames=600` · 3 회, 전의 `commandsAndWatchers + panels` 대 후의 `frameSteps`, VSync 에 붙지 않았는지 먼저).

### 묶음 3 — S(큰 클래스 나누기) — 끝남 (S13 ~ S22 전부, 2026-10-12)
SOLID 계획 단위는 S1 ~ S22 가 모두 끝났다(옮기지 않은 것은 각 묶음의 판정). 남은 후보는 아래 "다음 후보" 한 줄뿐이다.

**S13 ~ S17 끝남 (2026-10-12, 핫 경로를 측정 없이 옮김 — 사용자 결정, 측정은 사용자가 따로 한다)**: 모두 비가상 값 멤버 · 직접 호출, 루프 본문 · 호출 순서 그대로.
- S13 `EngineLoop.cpp` 1722 → 1440 줄: `ShellInputActions`(셸 InputMap) · `RenderPacketBuilder`(패킷 상태 여섯) · `SceneDebugDraw`(물리 · 내비 디버그 선, 정적) · `PortraitBatchUtil`(초상화 일괄). 전역 변수 정의 셋이 쓰는 파일로 옮겨 설정 문서를 다시 생성했다. 백엔드 교체 · `pollShaderReloadHotkey` · `MaterialCache::initializePending` · 관찰자 루프는 수명 계약 · 순서가 뜻이라 `tick` 에 남겼다.
- S14 `UISystem.cpp` 1580 → 1032 줄: `UIInputRouter` · `UIThemeController` · `UIOffscreenScreens`(오프스크린 레이아웃 · 그리기 컨텍스트를 화면마다가 아니라 프레임에 한 번 만든다, 값은 같다). 일시정지 메뉴 열기 · 탐색 방식 포커스 복원 · 모든 화면 스타일 재적용 루프는 화면 스택을 알아야 해 `UISystem` 에 남겼다.
- S15 `FractureComponentBase.cpp` 1811 → 1660 줄: `FractureAnchorUtil`(앵커 판정) · `FractureNetworkSync`(상태 해시 · 자세 목표 대기열 · 스냅샷, 바이트 배치 유지) · `FractureRenderUnits`. 그룹 재구성 · 물리 어댑터(약 440 줄)는 반쪽 이전이라 남겼다. 자세 목표 대기열은 자기 잠금(`_driveMutex`)을 쓴다.
- S16 `FrameRenderer.h` 985 → 927 줄: `GPUPassTimeline` · `PresentCaptureTarget` · `RenderTargetImageFile`. 트랜지언트 계획은 파이프라인 · TAA · 그림자 · 활성 뷰를 함께 읽어 남겼다. **동작 변화**: 엔진 서비스가 묶이지 않은 실행은 GPU 계측이 꺼진다(전에는 단언).
- S17 `GPUSceneBuilder.cpp` 1644 → 1339 줄: `GPUTransparentSorter` · `GPUMaterialRegistry` · `GPUSkinPaletteCollector`. 배치 키 대표 맵 · 정렬 키를 채우는 루프 · `DrawCandidate` 를 읽는 것은 빌더에 남겼다.
- **측정할 것(사용자)**: `GT.GPUScene.build`(S17), `GT.Editor.updateUI`(S12), 프레임당 `EngineLoop.tick` · `UISystem` · `FrameRenderer` 구간 전후. Release · `-gv_profileFrames` · 3 회, VSync 에 붙지 않았는지 먼저. `tick` 은 이제 `engine::getDebugDrawQueue()` 를 매 프레임 부른다(비가상 호출 셋 증가).
- **다음 후보**: "`ProfilerPanel`(1243 줄)을 탭 상태 객체와 `ProfilerTabContext` 로 나누기"(S8 에서 넘어옴).

**끝남 (2026-10-12)**: S18(`EditorViewportClient` 1179 → 243 줄: 카메라 · 피킹 · 기즈모 · 캔버스 오버레이 · 에셋 드롭 값 멤버), S19(`ContentBrowserPanel` 멤버 39 → 29: 파일 작업 · 참조 검색 · 임포트 대기열 · 탐색 이력, 이력 · 이름 바꾸기 · 임포트 대기열 테스트), S20(`LiveReloadManager` 1317 → 965 줄: `ModuleDependencyGraph` · `ModuleShadowCopy` · `DeferredImageUnloadQueue`, DLL 없는 테스트).
**옮기지 않은 것**: S19 의 모달 · 결과 창 플래그와 이름 입력 버퍼(ImGui 팝업 시점이라 작업 클래스가 ImGui 순서를 알게 된다), S20 의 `verifyModuleBindings` · `isBoundTo`(모든 컨텍스트 핸들을 함께 보는 검사라 떼면 반쪽 이전), `ModuleShadowCopy` 를 인스턴스로 만들기(번호가 프로세스 전역이어야 섀도 폴더 이름이 겹치지 않는다).
S18 의 새 클래스는 ImGui 에 의존해 단위 테스트가 없다 — 에디터 자체 테스트 · `AppScenarioTest` 에디터 케이스가 본다.

**S21 · S22 끝남 (2026-10-12, Debug 빌드까지만 확인 — 테스트는 사용자가 따로 맡는다)**:
- S21: `EditorAssetCommands` 817 → 157 줄(`EditorSceneFileCommands` · `EditorPrefabIsolationCommands` · `EditorFolderListing`, 호출처 클래스 이름 변경), `UserSettingsManager` 1061 → 922 줄(`UserSettingsFile`), `SceneNavigation` 1285 → 565 줄(`NavGeometryCollector` · `NavTileRebaker` · `NavAgentCrowdSync` · `NavDebugView` · `NavMeshRuntime.h`), `App.cpp` 866 → 509 줄(`AppHeadlessTasks` · `AppAssertDialog` · `DevConsoleHost` · `ModuleReloadDriver`), `EditorModuleHost` 445 → 389 줄(헤드리스 구현은 `EditorModuleHostHeadless.cpp`).
- S22: `IRHIDevice` 의 선택 능력 둘을 `IRHITimestampProvider`(네 백엔드) · `IRHIContextThreadBinding`(DX11 · GL)로 뽑았다. 질의는 비가상 `findTimestampProvider()` 멤버 읽기라 핫 경로 비용이 없다. RHI 모듈 ABI 30 → 31.
- **옮기지 않은 것**: `UserSettingsManager` 의 키 바인딩 겹침 · 품질 번들 맞추기(`setPendingAt` 상태 기계와 떨어지지 않아 반쪽 이전) · `applyToTarget` 의 전역 변수 쓰기(호출처 하나), `SceneNavigation` 의 에이전트 루프 · 런타임 찾기 · 설정 로드, `App` 의 `updateShellActions` · 모듈 카탈로그 해석 · 헤드리스 static 두 함수의 클래스 이전(`EditorModuleHost::…` 이름을 문서가 가리킨다), `IRHIDevice` 의 `supportsNativeBindlessSampling` · `applyVSyncInternal` · 비어 있지 않은 공용 기본 구현.
- **테스트 전이라 먼저 볼 곳**: (Debug 전체 빌드 · 링크는 통과) SmokeTest 소스 목록에 `EditorModuleHostHeadless.cpp` 가 없는 점, 새 `SW_API` 클래스), `-L nogpu`(`NavMesh*` · `UserSettingsFileTest` · `AppHeadlessTasksTest` · `EditorFolderListing` 테스트), Shipping(단일 백엔드 정적 링크, `SW_PROFILE_COMPILED` 꺼짐), Linux(GLX 컨텍스트 · 다중 상속 경고), `OpenGLRHICommandList.cpp`(compile_commands 에 없어 문법 검사도 못 했다), 에디터 실기동(Present 훅), `assertdialog.scenario.xml`, `-gv_reloadGameAtFrame`, `-gv_standardInputCommands`.

### 핫 경로라 유지하는 것
핫 경로라 제안하지 않는 것과 의도된 예외는 [결정 기록](../09_Decisions.md) 2절에 있습니다.

### 순서 제안
끝났다. 처음 계획은 핫 경로(S13 · S14 · S17)를 측정으로 먼저 정당화하는 것이었으나 사용자 결정으로 이동만 먼저 하고 측정은 뒤로 미뤘다 — 측정이 나쁘게 나오면 그 단위를 되돌리거나 훅을 합친다.
