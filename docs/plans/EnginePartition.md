# 엔진 분할 계획

> 상태: 측정대기 · 선행: 없음 · 영역: engine · 옛번호: C-1 · 진행: 6/6

**남은 확인(통합에서 확인):** 매니페스트 id `engine-partition-e3-tests`, `engine-partition-e3-reflection-tests`, `engine-partition-e3-memorytag-tests`, `engine-partition-e3-cube-bench`(6.4 절). 3 단계(링크 분할)는 하지 않기로 했다. 확인이 끝나면 통합 담당이 이 문서를 닫는다.

`Engine.dll` 하나를 티어 경계로 나누고 그 전에 의존 방향의 약한 고리를 풉니다. 폴더 의존은 이미 DAG 이고 `CheckEngineLayers.py` 가 지킵니다. 막힌 곳은 링크 단위가 하나라는 점이었고, 컴포넌트 모델 코어(`GameObject/` 등)가 기능 폴더를 include 하던 것은 0 으로 줄어 게이트가 지킵니다(2 단계, E3 · E4). 폴더 사이 include 수는 `py -3 Scripts/lint/report/RunEngineLayerGraph.py` 로 봅니다(`--apply` 가 게이트 표와 `Source/Engine/` 티어 표를 함께 맞춘다). Engine 에 남겨 두기로 한 폴더는 [결정 기록](../09_Decisions.md) 2절에 있습니다.

## 0. 기준 측정

2026-10-16 Debug 측정: `Engine.dll` 링크는 1.9 s 이고 `.cpp` 하나를 고친 증분은 임포트 라이브러리 갱신(`sw_keepImportLibraryIfUnchanged`)으로 5 s 였다. 빌드 시간만 보면 DLL 승격의 근거가 없다([결정 기록](../09_Decisions.md) 3절). 이득이 재링크 시간에서 나오지 않으면 3 단계는 하지 않는다(이후 재측정은 [ArchitectureSeams](ArchitectureSeams.md) 0 단계).

## 0-3. Engine 폴더 재배치의 남은 판단

`Character/AnimationNotify`(처리기, 테이블, 컴포넌트)는 `CharacterHit` · `SocketSetComponent` 를 써서 `Object/SceneAnimation` 으로 내릴 수 없다. 알림 계약은 `Animation/Notify` 에, 컴포넌트 모델 코어가 값으로 드는 알림 종류(`AnimationNotifyPhase`)는 `Common/` 에 있다.

## 2. 컴포넌트 모델을 코어와 기능으로 가르기 (끝)

코어(`Object/GameObject` · `Object/Prefab` · `Object/Component` 바로 아래)가 기능 폴더(Graphics · Physics · Navigation · Audio · Animation · TileMap)를 include 하는 줄은 0 이고 `CheckEngineLayers` 의 코어 규칙(`coreFolder` · `coreBannedLayer`, 예외 키 `core:`)이 지킨다. 물리 · 오디오 · 내비는 씬 서비스(`ISceneService`)이고 씬 위의 애니메이션은 `Object/SceneAnimation` 이다. Object 폴더 **안의** 방향도 0 이다(코어 금지 목록에 `Object/Component/2D` · `3D` · `Audio` · `Navigation` · `Physics` 와 `Object/SceneAnimation`, 게이트는 폴더 접두로 본다). `PrimitiveRegistry` 는 코어 기반 `ScenePrimitive` · `ScenePrimitiveBatch`(`Object/GameObject/Registry/ScenePrimitive.h`)만 안다. 들어오는 타입이 메시 계열뿐이라는 불변식은 기반의 private 생성자와 friend 가 지키고 메시가 필요한 곳(`GPUSceneBuilder`, `PixelPerfectCameraComponent`)이 `static_cast` 한다.

## 3. 링크 단위로 승격 (하지 않음)

근거가 없어 하지 않는다. 아래는 다른 이유(예: 서버 타깃이 Render 층을 링크하지 않는 것)가 생길 때 다시 볼 설계 메모다.
- 먼저 `OBJECT` 라이브러리로 나눈다(DLL 은 하나, 위험 낮음). 층은 Foundation(티어 0 ~ 2), Data(3 ~ 4), Render(5, Renderer, Environment), World(6 ~ 7 코어), Features(기능 컴포넌트), Tools(UI, Telemetry, Automation, Module), 루트(Engine).
- 이득이 확인되면 Dev 만 층마다 DLL 로 올린다. 내보내기 매크로는 `SW_API` 하나를 유지하되 각 DLL 의 `SW_EXPORTS` 를 층 전체에 건다.
- 리플렉션 등록기를 보존하려고 `sw_linkWholeArchive` 를 층 라이브러리마다 건다(Shipping 정적 링크는 지금과 같다). 서버 타깃은 Render 층을 링크하지 않는다.

## 4. 월드를 값으로

`World` 가 씬, 물리 월드, 내비, GPUScene 빌더, 틱 설정을 소유하고 `Engine` 는 월드 목록을 돈다. 프리팹 격리, 머티리얼 미리보기, 에디터 툴 창, 서버의 방이 이 위에 선다(EditorMultiWorld). 코어가 기능을 모르는 상태(2 단계) 뒤에, EditorMultiWorld 를 시작할 때 따로 설계한다.

## 5. 모듈 확장 진입점과 상태 규칙

모듈이 서비스, 렌더 패스, 레지스트리를 등록하는 공식 진입점을 두고 리로드되는 모듈의 변경 가능한 정적 변수를 막는 게이트(허용 목록 + 이유)를 둔다. `SQLDriverRegistry` · `LocalStoreFactory` 는 E6 에서 옮겼고(정적 등록 객체 + 모듈 언로드 리스너인 표, [결정 기록](../09_Decisions.md) 5-8), `GameDataCache` 는 `GameDataCacheRegistry` 와 `AssetManager::onModuleUnloading` 이 같은 길을 이미 타 남은 것이 없다.

## 건드리지 않는 것

키트 DLL 병합(핫 리로드 단위와 키트별 켜기 · 끄기가 깨진다), 임포터의 위치(에디터 모듈의 일 — `EditorModuleHost::importAssetsWithEditorModule`).

## 6. 작업 단위 (2026-10-18 설계)

### 6.1 근본 원인 · 해법

코어가 기능 객체를 값으로 소유하는 것은 등록할 진입점이 없던 때의 구조가 남은 것이다. 물리 · 내비 · 오디오 · 애니메이션(`AnimationSystem`)은 씬 서비스(`SW_SCENE_SERVICE( 타입, 프레임 단계 )`)로 옮겼고 프리미티브 레지스트리만 이 진입점을 거치지 않았다. 해법은 **프리미티브 개념을 코어의 데이터 슬롯으로** 두는 것이다 — 코어(`Object/GameObject/Registry/`)에 가상 함수 없는 기반 `ScenePrimitive`(프리미티브 번호 하나)와 `ScenePrimitiveBatch`(레지스트리 포인터와 항목 수)를 두고 `MeshComponent` 와 `MeshInstanceBatch` 가 상속한다. 트랜스폼 전파가 레지스트리에 더티를 알리는 핫 패스는 가상 호출 없이 슬롯 번호로 간다. 게이트(`CheckEngineLayers.toml` 의 `coreBannedLayer`)는 마지막이다(include 는 형식이 아니라 폴더 방향은 include 그래프로만 보인다).

### 6.3 대안과 기각 이유

- **프리미티브 레지스트리를 `Object/Component/3D` 로 옮기고 트랜스폼 계층이 리스너 인터페이스로 알리는 구조**: 더티 트랜스폼마다 가상 호출이 하나 늘고 큐브 20,000 개 벤치의 `GT.Frame` 이 그 경로를 지난다. 측정 없이 핫 패스에 간접 호출을 넣지 않는다(언리얼은 가상 `OnUpdateTransform`, Godot 는 `NOTIFICATION_TRANSFORM_CHANGED` [기억]).
- **`MeshComponent` 를 코어로 내리기**: 메시가 머티리얼과 LOD 를 통해 그래픽스를 알아 코어가 그래픽스를 다시 include 한다.
- **3 단계를 지금 하기 / 4 단계를 이 묶음에 넣기**: 링크 시간 이득이 없었고, 월드는 규모가 커 EditorMultiWorld 때 따로 설계한다.

### 6.4 단위

E3 · E4 는 구현이 들어갔다(`wt/enginepart`, 빌드 미확인). `MeshComponent` 의 리플렉션 부모가 `ScenePrimitive`(`REFLECT( Abstract )`)로 바뀌어 모든 모듈이 다시 빌드된다. 게이트 `CheckEngineLayers` 는 통과한다(코어 → 기능 0 줄).

| 단위 | 남은 확인 | 측정 |
|---|---|---|
| E3 | 매니페스트 id `engine-partition-e3-tests`(`GPUSceneTest`), `engine-partition-e3-reflection-tests`(부모 사슬과 상속 프로퍼티), `engine-partition-e3-memorytag-tests`(인스턴스 배치). 전체 빌드와 네 백엔드 × 대표 셋은 묶음의 `debug-full-build` · `app-scenarios-release` 가 돈다 | 매니페스트 id `engine-partition-e3-cube-bench` — 큐브 20,000 개 벤치의 `GT.Frame` p50 · p99 를 Release, VSync 끔, 3 ~ 5 판으로 전후 비교 |

### 6.5 사용자 결정

없습니다. 3 단계와 4 단계를 미루는 판단은 결정 기록에 있습니다.
