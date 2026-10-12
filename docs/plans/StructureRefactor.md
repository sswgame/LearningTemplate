# 구조 리팩토링 계획

2026-10-12 세션에서 드러난 구조 문제를 단위로 나눈 계획입니다. 단위마다 파일 소유자가 하나라 병렬로 진행할 수 있습니다. 진행 방식은 [작업 방식](../11_Workflow.md)을 따릅니다 —
작업 에이전트는 제안서(워크트리의 패치)만 만들고 빌드하지 않으며, 적용 담당 하나가 적용 · 빌드 · 검증 · push 를 합니다. 동시에 빌드하는 작업은 최대 둘입니다.

## 이번 세션이 남긴 근거

| 발견 | 증거 | 단위 |
|---|---|---|
| 엔진 루프가 자동화(명령줄 `-scenario`, 단계 전환)를 직접 안다 | `EngineLoop.cpp` 1,686 줄 · include 102 개. 자동화 필드 · 호출이 루프에 있었다(세션 분리로 필드 하나 + 호출 다섯으로 줄였지만 생성은 여전히 루프) | R1 |
| 에디터 렌더 · UI 스레드의 in-flight 인계가 리로드에서 깨진다 | Vulkan `THREADING ERROR: VkCommandPool … "RenderThread" and "GameThread"`, GL `RenderThread` 의 `read of 0x28`. 원인은 `abandonPendingDraw` 가 게임 모듈 리로드마다 불렸던 것 | R2 |
| 손으로 짠 레지스트리가 남아 있다 | 탐침 레지스트리는 `RegistrationList` 가 아니라서 리로드 때 이름 중복으로 등록이 사라졌다(고침). 같은 모양이 `ISceneService.cpp` · `BuiltInAssetCache.cpp` · `GameAutoplay.cpp` 에 있다 | R3 |
| JSON 을 손으로 걷는 자체 포맷 로더가 있다 | 모듈 매니페스트 · `AnimationLODSettings` · `MemoryBudgetFile` 은 리플렉션으로 옮겼다. 남은 후보 아홉 | R4 |
| `EngineLoop` 에 일이 몰려 있다 | 셸 입력 · 단축키 · 백엔드 교체 · 초상화 렌더 · 프로파일 세션이 한 클래스 | R5 |
| 작은 파일은 많지 않고 분할은 대체로 책임 기준이다 | 25 줄 이하 97 개 / 2,932 개. 큰 클래스를 나눈 가족 대부분이 파일마다 120 줄이 넘고, 작은 조각(`EditorAssetTypeFocus.cpp` 36 줄)은 의존을 분리한 것이다 | R8 |

## 단위

### R1. 엔진 루프에서 자동화를 뺀다 (프레임 관찰자 훅)
- **왜**: 언리얼은 자동화가 모듈이고 엔진 루프는 일반 틱만 부르며, 유니티도 PlayerLoop 훅으로 붙는다. 루프가 `-scenario` 를 읽고 세션을 만드는 것은 자동화를 엔진 코어로 끌어올리는 결합이다.
  시퀀서 · AI 같은 다른 구동기가 같은 자리를 쓸 수 있어야 한다.
- **무엇**: `Engine/EngineFrameObserver.h` 에 `IEngineFrameObserver`(`onFrameBegin` · `onFrameEnd` → 종료 코드 · `onWindowClosed` → 종료 코드 · `onBuildRenderPacket`)와 문맥(입력 · 렌더 스레드가 쓴 스크린샷 수)을 둔다.
  `EngineLoop` 는 관찰자 목록만 알고(`addFrameObserver`), `AutomationSession` 이 그것을 구현한다. 세션 생성(명령줄 `-scenario` · 렌더 캡처 켜기)은 런처(`App`)로 옮긴다. 서버는 `-scenario` 를 쓰지 않는다.
- **파일**: `EngineLoop.*`, 새 `EngineFrameObserver.h`, `AutomationSession.*`, `App.cpp` · `App.h`, `Test/EngineTest/Automation/TestAutomationSession.cpp`.
- **검증**: `AutomationSessionTest`, `AppScenarioTest.ChainedScenariosShareOneProcess`, 에디터 케이스 DX12, 린트 `CheckEngineLayers`. `EngineLoop.cpp` 에 `Automation` 이 한 번도 나오지 않는다.
- **순서**: 가장 먼저(R5 가 같은 파일을 만진다).

### R2. 에디터 그리기 인계(in-flight)를 작은 상태 기계로 뽑는다
- **왜**: UI 스레드 · 렌더 스레드 · 리로드 · 종료가 `_publishedDrawSlot` · `_inFlightDrawSlot` 두 원자 값으로 약속을 주고받는데, 약속의 주인이 코드 곳곳에 흩어져 있다. 이번에 리로드가 약속을 깨서 두 백엔드가 서로 다른 모양으로 죽었다.
  GPU 없이는 확인할 방법이 없었다.
- **무엇**: `DrawSnapshotHandoff`(게시 · 렌더 시작 · 렌더 끝 · 포기)를 `Editor/Common/Backend/` 에 두고 불변식을 문서화한다(렌더 스레드가 읽는 슬롯은 UI 스레드가 쓰지 않는다 · 포기는 인스턴스를 없앨 때만). `ImGuiEditor` 는 그것을 쓴다.
- **검증**: nogpu 단위 테스트(스레드 둘이 임의 순서로 게시 · 렌더 · 포기를 부를 때 겹침이 없는지), Vulkan · GL 에디터 체인 3 회 반복에서 `[Error]` 0 건.
- **파일**: `ImGuiEditor.*`, `EditorModuleHost.cpp`, 새 헤더 · 테스트. R1 과 겹치지 않는다.

### R3. 손으로 짠 레지스트리를 `RegistrationList` 로 통일
- **대상**: `ISceneService.cpp` · `BuiltInAssetCache.cpp` · `GameAutoplay.cpp`(정적 벡터 + 직접 빼기). 이름이 있는 것은 이름 검사와 리로드 대체(`RegistrationReloadScope`)를 얻는다.
- **검증**: 레지스트리마다 "리로드 이미지가 같은 이름을 올려도 등록이 남는다" 테스트 한 줄, 기존 테스트.
- **파일**: 위 셋과 그 테스트. 병렬로 진행한다.

### R4. 자체 포맷 로더를 리플렉션으로 (파일마다 한 단위)
- **후보**: `SpriteClipAsset` · `SequenceAsset` · `FacialRig` · `LipSync` · `Skeleton` · `AnimGraphAsset` · `DialogueGraphAsset` · `LocalizationDocuments` · `CultureInfo`.
- **판정을 먼저 한다**(결정 기록 규칙): 적지 않은 값의 의미가 필요한가(부모 상속 · "없음" 과 "0" 구분)? 그렇다면 옮기지 않는다(`TextureImportConfig` · `ModelImportConfig` 가 그렇다).
  동적 문서(`CrashReportService`)와 외부 형식(JWT · Chrome 트레이스 · VRM)도 제외한다.
- **방법**: 구조체에 `REFLECT` · `PROPERTY`, `ConfigManager::readConfigJSON` 으로 읽기(모르는 키 · 읽지 못한 값 · 범위는 거기서 오류), 의미 검사만 직접. 데이터 파일은 키를 `_이름` 규약으로 다시 쓰고(옛 이름 별칭 없음, 결정 기록 5-4) 설정 카탈로그를 `typeName` 방식으로 바꿔 문서를 생성한다.
- **검증**: 그 로더의 기존 테스트, `ResourceDataSchemaTest`, 설정 · 리소스 스키마 게이트. 파일 하나가 한 단위라 서로 충돌하지 않는다.

### R5. `EngineLoop` 추가 분해
- **먼저 잰다**: `-ftime-trace` 로 `EngineLoop.cpp` 헤더 파싱 · 증분 시간([빌드 속도](BuildSpeed.md) 6 단계). 숫자가 분해를 정당화하지 않으면 하지 않는다.
- **후보**: 셸 입력 · 단축키(`updateShellActions` · `createShellInputMap` · `pollShaderReloadHotkey` · `wasDebugActionTriggered`), 백엔드 교체(`applyPendingBackendChange` · `rebindSceneAfterDeviceRecreate`), 초상화 렌더.
- **순서**: R1 뒤.

### R6. 빌드 속도 5 · 6 단계
- [빌드 속도](BuildSpeed.md) 5 단계(유니티 범위 확대)는 공유 설치가 조용할 때 교차 3 쌍 + 배치 4 · 8 · 16 을 잰다. **다른 빌드가 없을 때만** 한다(측정이 오염된다).

### R7. 시나리오 체인 후속
- 백엔드 교체 단계(`<SwapBackend>`)로 한 프로세스가 네 백엔드를 건너게 한다.
- `isolated` 로 뺀 시나리오 중 상태가 남는 것(그래프 노드 · 프로파일러 타임라인 · 타일맵 · 시퀀서 · 썸네일 캐시)을 패널 쪽 초기화로 체인에 합친다. R2 와 `ImGuiEditor` 를 같이 만지지 않게 패널 파일만 다룬다.
- 체인 사이 초기화(프레임 50 개 ≈ 체인 시간의 20 %)를 "씬이 열렸는지" 탐침으로 줄인다.

### R8. 폴더 · 파일 구조 감사
- **결론**: 기계적으로 합치지 않는다. 분할은 대체로 책임(의존 분리 · 큰 클래스)이고, 합치면 층 규칙(`CheckEngineLayers`)과 파일 분할 기준(그래픽스 구조 재편)을 해친다.
- **한다면**: 감사 보고서 스크립트(같은 접두의 `.cpp` 가족과 줄 수)를 `Scripts/lint/report/` 에 두고, 120 줄 이하 조각이 의존 분리가 아닌 가족만 사람이 본다. 지금 후보는 `RenderView.cpp`(13 줄, 함수 하나) 정도다.

## 병렬 배치

| 묶음 | 단위 | 서로 겹치는 파일 |
|---|---|---|
| A (먼저) | R1 | `EngineLoop.*`, `App.*` |
| B (A 와 병렬) | R2, R3, R4 의 파일들, R7 의 패널 초기화 | 없음(각자 소유) |
| C (A 뒤) | R5 | `EngineLoop.*` |
| D (조용한 PC) | R6 | 빌드 설정 |

적용 담당은 묶음 안에서 파일이 겹치지 않는 제안서를 한꺼번에 받아 적용한다. 단위마다 커밋은 따로 두고(문제점 → 해결방안 → 결과), 검증은 단위의 검증 줄을 모두 통과해야 main 에 넣는다.

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
