# 구조 리팩토링 계획

구조 문제를 단위로 나눈 계획입니다. 단위마다 파일 소유자가 하나라 병렬로 진행할 수 있고, 진행 방식은 [작업 방식](../11_Workflow.md)을 따릅니다.
R1(엔진 루프에서 자동화 빼기), R2(에디터 그리기 인계 상태 기계), R3(레지스트리 통일)는 끝났고 R4 는 파일 일부가 남았습니다. 하지 않기로 한 것은 [결정 기록](../09_Decisions.md) 2절에 있습니다.

## 단위

### R4. 자체 포맷 로더를 리플렉션으로 (파일마다 한 단위)
- **남은 후보**: `SpriteClipAsset` · `SequenceAsset` · `Skeleton` · `AnimGraphAsset` · `DialogueGraphAsset` · `LocalizationDocuments` · `CultureInfo`.
- **판정을 먼저 한다**: 적지 않은 값의 의미가 필요한가(부모 상속, "없음" 과 "0" 구분)? 그렇다면 옮기지 않는다. 옮기지 않기로 한 것과 그 이유는 [결정 기록](../09_Decisions.md) 5-4.
- **방법**: 구조체에 `REFLECT` · `PROPERTY`, `ConfigManager::readConfigJSON` 으로 읽기(모르는 키 · 읽지 못한 값 · 범위는 거기서 오류), 의미 검사만 직접. 데이터 파일은 키를 `_이름` 규약으로 다시 쓰고(옛 이름 별칭 없음, 결정 기록 5-4) 설정 카탈로그를 `typeName` 방식으로 바꿔 문서를 생성한다.
- **검증**: 그 로더의 기존 테스트, `ResourceDataSchemaTest`, 설정 · 리소스 스키마 게이트. 파일 하나가 한 단위라 서로 충돌하지 않는다.

### R5. `EngineLoop` 추가 분해 (= S13)
- **왜**: 셸 입력 · 단축키 · 백엔드 교체 · 초상화 렌더 · 프로파일 세션이 한 클래스에 몰려 있다(`EngineLoop.cpp` 약 1,700 줄 · include 약 100 개).
- **먼저 잰다**: `-ftime-trace` 로 `EngineLoop.cpp` 헤더 파싱 · 증분 시간([빌드 속도](BuildSpeed.md) 6 단계). 숫자가 분해를 정당화하지 않으면 하지 않는다.
- **후보**: 셸 입력 · 단축키(`updateShellActions` · `createShellInputMap` · `pollShaderReloadHotkey` · `wasDebugActionTriggered`), 백엔드 교체(`applyPendingBackendChange` · `rebindSceneAfterDeviceRecreate`), 초상화 렌더.

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

### 묶음 3 — S(큰 클래스 나누기) — 하나씩, 핫 경로는 측정 후
| 단위 | 대상(줄 수) | 방향 | 핫 경로 |
|---|---|---|---|
| S13 (= R5) | `EngineLoop.cpp` `tick` 285 줄 · include 102 | `RenderPacketBuilder` · `DebugOverlayDriver` 를 값 멤버로. 나머지는 관찰자 훅 | 프레임당 1 회 — 측정 |
| S14 | `UISystem.cpp` 1620 | `UIInputRouter` · `UIThemeController` · `UIOffscreenScreens` | 프레임당 1 회 |
| S15 | `FractureComponentBase.cpp` 1811 | 네트워크 동기 · 앵커 풀이 · 렌더 유닛 멤버 객체 | 프레임당, 객체 분리만 |
| S16 | `FrameRenderer.h` 985 · 멤버 127 | GPU 타임라인 · 리드백 · 트랜지언트를 값 멤버 클래스로, 프로파일러는 초기화 때 주입 | 비핫, 이동 뒤 측정 |
| S17 | `GPUSceneBuilder.cpp` 1644 | 투명 정렬 · 머티리얼 레지스트리 · 스킨 팔레트를 비가상 클래스로 | **핫** — 클래스 이동만, 내부 루프 그대로, 측정 필수 |
| S18 | `EditorViewportClient.cpp` 1179 | 카메라 · 피킹 · 기즈모 · 오버레이 · 드롭 | 비핫 |
| S19 | `ContentBrowserPanel.cpp` 1530 | 파일 작업 · 참조 결과 · 임포트 대기열 · 탐색 이력 | 비핫 |
| S20 | `LiveReloadManager.cpp` 1317 | 그래프 계산 · 섀도 복사 · 지연 언로드 큐 | 비핫 |
| S21 | `EditorAssetCommands.cpp` 817 · `UserSettingsManager.cpp` 1061 · `SceneNavigation.cpp` 1285 · `App.cpp` 866 · `EditorModuleHost.cpp` 445 | 각 파일의 감사 표 방향 | 비핫 |
| S22 | `IRHIDevice.h` 가상 37 중 14 가 빈 기본 | 선택 능력 인터페이스(`IRHITimestampProvider` 등)로 — **`IRHICommandList` 는 건드리지 않는다** | 비핫 |

### 핫 경로라 유지하는 것
핫 경로라 제안하지 않는 것과 의도된 예외는 [결정 기록](../09_Decisions.md) 2절에 있습니다.

### 순서 제안
묶음 1 은 병렬로(파일이 서로 겹치지 않는다). 묶음 2 는 S10 → S11 → S12. 묶음 3 은 S13 을 먼저(측정 + `IEngineFrameObserver` 위에서), 큰 클래스 하나가 한 단위이고 단위마다 린트 + 해당 테스트 + 핫 경로면 측정으로 닫는다.
