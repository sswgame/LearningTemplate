# 엔진 분할 계획

`Engine.dll` 하나를 티어 경계로 나누고, 그 전에 의존 방향의 약한 고리를 푼다. 폴더 의존은 이미 DAG 이고 `CheckEngineLayers.py` 가 지킨다.
막힌 곳은 링크 단위가 하나라는 점과, 컴포넌트 모델(`Object`)이 기능 폴더 다섯에 묶여 있다는 점이다.

## 측정한 현재 상태 (2026-10-11, `Source/Engine` 의 `#include "Engine/…"` 수 — 기능 캐시 등록 반전 · `SceneNavigation` 이동 뒤)

| 방향 | include 수 | 파일 수 | 대표 헤더 |
|---|---|---|---|
| Object → Graphics | 53 | 22 | `Mesh.h` · `GPULight.h` · `ShaderBindingSlots.h` · `MaterialInstance.h` |
| Object → Physics | 36 | 19 | `IPhysicsScene.h` · `PhysicsTypes.h` · `PhysicsWorld.h` |
| Object → Animation | 46 | 23 | `Skeleton.h` · `Pose.h` · `AnimClip.h` |
| Object → Audio | 11 | 8 | `AudioSpatial.h` · `AudioEngine.h` |
| Object → Navigation | 8 | 4 | `NavMeshGeometry.h` · `INavMesh.h` — 모두 `Component/Navigation`(기능 쪽) |
| Scene → Graphics | 3 | 2 | `MaterialCache.h` · `IRHIDevice.h` · `ShaderBindingSlots.h` |

- `Object` 안에서 기능 의존이 몰린 곳은 `Component/{2D, 3D, Audio, Navigation, Physics}`(기능 컴포넌트)와 `GameObject/`(Graphics 10 · Physics 7 · Audio 2)다.
  `GameObject` 가 기능 폴더를 직접 아는 것이 진짜 문제다 — 컴포넌트 모델의 코어가 렌더 · 물리 · 내비를 include 한다.
- `Engine/` 은 소스 559개 · 헤더 포함 약 1,160개, Debug `Engine.dll` 은 약 18 MB 다. 재링크 시간은 아직 측정하지 않았다(아래 0 단계).
- 폴더 그래프는 사이클 0 이고 게이트 티어 표는 계산값과 같다(2026-10-11). `Resource` 의 기능 캐시 배선 예외는 등록 반전(`SW_BUILT_IN_ASSET_CACHE`)으로 없어졌다 — 남은 배선 예외는 `ReflectGenerated.h` 하나다.
- Core 층 정리와 Engine 폴더 재배치(1 ~ 7)는 끝났다([Core README](../../Source/Core/README.md) · 결정 기록).

## 단계

### 0. 기준 측정 (먼저)
- Engine 안에서 `Graphics` 한 파일 · `UI` 한 파일 · `Object` 한 파일을 고쳤을 때 Release 증분 빌드 시간(컴파일 + 재링크)을 3회씩 측정한다. 표는 [검증과 측정](../08_Verification.md) 형식.
- 분할 뒤 같은 세 경우와 비교한다. 이득이 재링크 시간에서 나오지 않으면 3 단계(DLL 승격)는 하지 않는다.

### 0-3. Engine 폴더 재배치 — 남은 판단
옛 경로와 옮기지 않기로 한 것은 [결정 기록](../09_Decisions.md) 2 · 4 절, 티어 표는 `py -3 Scripts/lint/report/RunEngineLayerGraph.py --apply` 가 게이트 표와 `Source/Engine/README.md` 티어 표를 함께 맞춘다(이동 도구는 끝나서 지웠다).

- `Utility` 루트 10 파일(`CommandStack` · `DebugOverlayState` · `GameAutoplay` · `GameTimeScale` · `KeyValueFile`)은 이름만 보고 두었다. 후보는 `CommandStack` → `Scene`(에디터 실행 취소),
  `KeyValueFile` → `Serialization/Format`, `GameTimeScale` · `GameAutoplay` → `Config`, `DebugOverlayState` → `Profiling` 이다.
- `Character/AnimNotify`(처리기 · 표 · 컴포넌트)는 `CharacterHit` · `SocketSetComponent` 를 써서 `Object/Animation` 으로 내릴 수 없다 — 알림 계약만 `Animation/Notify` 로 모았다.
- `Test/EngineTest/Graphics` 의 렌더러 테스트(`TestGpuScene` · `TestRenderGraph` …)은 아직 `Graphics` 폴더에 있다(`Test/EngineTest/Renderer` 로 옮길 후보).

**Engine 에 남는 이유가 있는 폴더(2026-10-10 사용 현황 조사):** 에디터는 `GameFramework` 를 include 할 수 없다(`CheckEngineLayers`: Editor 금지 목록) — 그래서 에디터가 쓰는 폴더(`Dialogue` · `Sequencer` · `Destruction` · `Environment` · `Automation` · `UI` · `Animation` · `Scene` …)는 Engine 이 아니면 둘 곳이 없다. 서버 실행 파일도 `GameFramework` 를 모르므로 `Observability`(서버가 `MetricRegistry` · `OpsHTTPEndpoint` 를 쓴다)도 남는다.
`Character` 는 `Destruction`(에디터가 씀)이 쓰고 `Spatial` 은 `Character/Fit` 이 쓰므로 남는다. `Telemetry` 는 `EngineLoop` · App · 게임이, `Compression` 은 Core 인터페이스의 코덱 공급자라 Core 가 서드파티를 몰라야 하는 규칙 때문에 남는다.

### 2. 컴포넌트 모델을 코어와 기능으로 가르기 (중간 위험, 가장 큰 효과)
- `Object` 를 둘로: **코어**(`GameObject` · `Prefab` · `Component` 기반 클래스 · 리플렉션 훅)와 **기능 컴포넌트**(`Component/{2D,3D,Audio,Navigation,Physics}` · `Object/Animation`).
- `SceneNavigation` 은 `Object/Component/Navigation` 으로 옮겼고 `GameObjectManager` 는 전방 선언 + `unique_ptr` 로 든다 — `GameObject/` 의 Navigation include 는 0 이다.
  남은 것: `GameObjectManager.cpp` 가 아직 그것을 만들고 틱에서 부른다(씬 시스템 등록으로 뒤집을 차례). `ScenePhysics` · `SceneAudio` 도 같은 모양이다.
- 코어는 `Graphics` · `Physics` · `Navigation` · `Audio` 를 include 하지 않는다. `GameObject` 의 기능 의존 19 건(위 줄)은 기능 컴포넌트가 코어에 등록하는 방향으로 뒤집는다:
  Transform 변경 알림 · 바운드 질의 · 씬 등록 모두 `GameObject` 가 인터페이스(`ISceneListener` 꼴)로 듣는다.
- 기능 컴포넌트는 코어 위, 각자의 기능 폴더 아래에 둔다. 폴더 이동은 `git mv` 후 코드젠을 다시 만든다(헤더 이동은 .gen.cpp 를 썩힌다 — 메모리 `moved-header-stale-codegen`).
- 확인: `RunEngineLayerGraph.py` 에서 코어 폴더가 기능 폴더를 가리키는 화살표가 0.

### 3. 링크 단위로 승격
- 먼저 `OBJECT` 라이브러리로 나눈다(DLL 은 하나, 위험 낮음). 층: Foundation(티어 0~2) · Data(3~4) · Render(5, Renderer, Environment) · World(6~7 코어) · Features(기능 컴포넌트) · Tools(UI · Telemetry · Automation · Module) · 루트(EngineLoop).
- 0 단계 측정으로 이득이 확인되면 Dev 만 층마다 DLL 로 올린다. 내보내기 매크로는 층별로 늘리지 말고 `SW_API` 하나를 유지하되 각 DLL 의 `SW_EXPORTS` 를 층 전체에 건다.
- 리플렉션 등록기 보존: `sw_linkWholeArchive` 를 층 라이브러리마다 건다(Shipping 정적 링크는 지금과 같다). 서버 타깃은 Render 층을 링크하지 않는다.

### 4. 월드를 값으로
- `World` 가 씬 · 물리 월드 · 내비 · GPUScene 빌더 · 틱 설정을 소유하고 `EngineLoop` 는 월드 목록을 돈다. 프리팹 격리 · 머티리얼 미리보기 · 에디터 툴 창 · 서버의 방이 이 위에 선다(백로그 "다중 월드").
- 2 단계 뒤에 한다 — 월드가 코어 위의 소유자가 되려면 코어가 기능을 모르는 상태여야 한다.

### 5. 모듈 확장 창구와 상태 규칙
- 모듈이 서비스 · 렌더 패스 · 레지스트리를 등록하는 공식 창구. 리로드되는 모듈의 변경 가능한 정적 변수를 막는 게이트(허용 목록 + 이유)와, `GameDataCache` · `SQLDriverRegistry` · `LocalStoreFactory` 의 등록 표를 서비스로 올리는 일.

## 건드리지 않는 것
- 키트 DLL 병합(핫 리로드 단위와 키트별 켜기 · 끄기가 깨진다). 산출물 폴더 정리는 별도 진행.
- 임포터의 위치 — 에디터 모듈의 일이다(`EditorModuleHost::importAssetsWithEditorModule`).

## 순서와 위험

| 단계 | 규모 | 위험 | 확인 |
|---|---|---|---|
| 0 측정 | 작음 | 없음 | 증분 시간 표 |
| 2 코어 / 기능 가르기 | 중간 | 중간 | 코어 → 기능 화살표 0 · EngineTest · 씬 로드 · 에디터 시나리오 |
| 3 링크 분할 | 중간 | 낮음(OBJECT) / 중간(DLL) | 네 백엔드 × Dev · Shipping · Server 빌드 |
| 4 월드 | 큼 | 높음 | 프리팹 격리 · 서버 방 시나리오 |
| 5 모듈 창구 | 중간 | 낮음 | 게이트 + 핫 리로드 테스트 |

각 단계는 독립 커밋 묶음이고, 단계가 끝날 때마다 전 프리셋 컴파일과 `ctest -L lint · nogpu` 를 돈다(검증 보류 지시가 풀린 뒤).
