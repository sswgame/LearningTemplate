# 엔진 분할 계획

> 상태: 구현대기 · 선행: 없음 · 영역: engine · 옛번호: C-1 · 진행: 2/6

`Engine.dll` 하나를 티어 경계로 나누고, 그 전에 의존 방향의 약한 고리를 풉니다. 폴더 의존은 이미 DAG 이고 `CheckEngineLayers.py` 가 지킵니다.
막힌 곳은 링크 단위가 하나라는 점입니다. 컴포넌트 모델 코어(`GameObject/` 등)가 기능 폴더를 직접 include 하던 것은 0 으로 줄었고 게이트가 지킵니다(2 단계).
남은 것은 Object 폴더 안에서 코어가 기능 컴포넌트를 아는 방향입니다.
폴더 사이 include 수는 `py -3 Scripts/lint/report/RunEngineLayerGraph.py` 로 봅니다. Engine 에 남겨 두기로 한 폴더는 [결정 기록](../09_Decisions.md) 2절에 있습니다.

## 0. 기준 측정 (먼저)

- Engine 안에서 `Graphics` 한 파일, `UI` 한 파일, `Object` 한 파일을 고쳤을 때 Release 증분 빌드 시간(컴파일 + 재링크)을 3 회씩 잽니다. 표는 [검증과 측정](../08_Verification.md) 형식입니다.
- 분할 뒤 같은 세 경우와 비교합니다. 이득이 재링크 시간에서 나오지 않으면 3 단계(DLL 승격)는 하지 않습니다.
- 2026-10-16 Debug 측정: `Engine.dll` 링크는 1.9 s 이고 `.cpp` 하나를 고친 증분은 임포트 라이브러리 갱신(`sw_keepImportLibraryIfUnchanged`)으로 5 s 가 됐습니다. 빌드 시간만 보면 DLL 승격의 근거가 없습니다([결정 기록](../09_Decisions.md) 3절).

## 0-3. Engine 폴더 재배치의 남은 판단

티어 표는 `py -3 Scripts/lint/report/RunEngineLayerGraph.py --apply` 가 게이트 표와 `Source/Engine/` 티어 표를 함께 맞춥니다.

- `Character/AnimationNotify`(처리기, 테이블, 컴포넌트)는 `CharacterHit` · `SocketSetComponent` 를 써서 `Object/SceneAnimation` 으로 내릴 수 없습니다. 알림 계약은 `Animation/Notify` 에,
  컴포넌트 모델 코어가 값으로 드는 알림 종류(`AnimationNotifyPhase`)는 `Common/` 에 있습니다.

## 2. 컴포넌트 모델을 코어와 기능으로 가르기 (중간 위험, 가장 큰 효과)

코어(`Object/GameObject` · `Object/Prefab` · `Object/Component` 바로 아래)가 기능 폴더(Graphics · Physics · Navigation · Audio · Animation · TileMap)를 include 하는 줄은 0 이고,
`CheckEngineLayers` 의 코어 규칙(`coreFolder` · `coreBannedLayer`, 예외 키 `core:`)이 지킵니다. 물리 · 오디오 · 내비는 씬 서비스(`ISceneService`)이고, 씬 위의 애니메이션은 `Object/SceneAnimation` 입니다.
남은 일은 Object 폴더 **안의** 방향입니다.

- 코어가 기능 컴포넌트 폴더(`Object/Component/3D`)를 아직 include 합니다. `PrimitiveRegistry` 가 `MeshComponent` · `MeshInstanceBatch` 를 압니다(애니메이션은 씬 서비스로 옮겼습니다).
  3 단계에서 Object 를 링크 단위 둘로 가르려면 이 방향도 뒤집어야 합니다. 규칙은 코어 금지 목록에 `Object/Component/*` 하위 폴더를 더하는 식으로 걸 수 있습니다.
- 확인: `RunEngineLayerGraph.py` 에서 코어 폴더가 기능 폴더를 가리키는 화살표가 0 입니다.
- 2026-10-18 에 센 남은 줄은 둘입니다. 작업 단위는 6절 E3 · E4 입니다.

| 파일 | include | 쓰임 |
|---|---|---|
| `GameObject/Registry/PrimitiveRegistry.cpp:8` | `Component/3D/MeshComponent.h` | 레지스트리가 `MeshComponent*` 를 보관하고 `kInvalidPrimitiveIndex` 와 프리미티브 번호 접근자를 씁니다 |
| `GameObject/Registry/PrimitiveRegistry.cpp:9` | `Component/3D/MeshInstanceBatch.h` | 인스턴스 배치의 `_pRegistry` 와 항목 번호를 씁니다 |

## 3. 링크 단위로 승격 (하지 않음)

근거가 없어 하지 않습니다. 2026-10-16 Debug 측정(PC `SW`)에서 `Engine.dll` 링크는 1.9 s 뿐이었고 증분 비용은 임포트 라이브러리 갱신이었습니다([결정 기록](../09_Decisions.md) 3절). 아래는 다른 이유(예: 서버 타깃이 Render 층을 링크하지 않는 것)가 생길 때 다시 볼 설계 메모입니다.

- 먼저 `OBJECT` 라이브러리로 나눕니다(DLL 은 하나, 위험 낮음). 층은 Foundation(티어 0 ~ 2), Data(3 ~ 4), Render(5, Renderer, Environment), World(6 ~ 7 코어), Features(기능 컴포넌트), Tools(UI, Telemetry, Automation, Module), 루트(Engine)입니다.
- 0 단계 측정으로 이득이 확인되면 Dev 만 층마다 DLL 로 올립니다. 내보내기 매크로는 층별로 늘리지 않고 `SW_API` 하나를 유지하되, 각 DLL 의 `SW_EXPORTS` 를 층 전체에 겁니다.
- 리플렉션 등록기를 보존하려고 `sw_linkWholeArchive` 를 층 라이브러리마다 겁니다(Shipping 정적 링크는 지금과 같다). 서버 타깃은 Render 층을 링크하지 않습니다.

## 4. 월드를 값으로

- `World` 가 씬, 물리 월드, 내비, GPUScene 빌더, 틱 설정을 소유하고 `Engine` 는 월드 목록을 돕니다. 프리팹 격리, 머티리얼 미리보기, 에디터 툴 창, 서버의 방이 이 위에 섭니다(EditorMultiWorld).
- 2 단계 뒤에 합니다. 월드가 코어 위의 소유자가 되려면 코어가 기능을 모르는 상태여야 합니다.

## 5. 모듈 확장 진입점과 상태 규칙

- 모듈이 서비스, 렌더 패스, 레지스트리를 등록하는 공식 진입점을 둡니다. 리로드되는 모듈의 변경 가능한 정적 변수를 막는 게이트(허용 목록 + 이유)를 두고,
  `SQLDriverRegistry` · `LocalStoreFactory` 는 E6 에서 옮겼습니다(정적 등록 객체 + 모듈 언로드 리스너인 표, [결정 기록](../09_Decisions.md) 5-8). `GameDataCache` 가 남았습니다.

## 건드리지 않는 것

- 키트 DLL 병합(핫 리로드 단위와 키트별 켜기 · 끄기가 깨진다). 산출물 폴더 정리는 별도로 진행합니다.
- 임포터의 위치. 에디터 모듈의 일입니다(`EditorModuleHost::importAssetsWithEditorModule`).

## 순서와 위험

| 단계 | 규모 | 위험 | 확인 |
|---|---|---|---|
| 0 측정 | 작음 | 없음 | 증분 시간 표 |
| 2 코어 / 기능 가르기 | 중간 | 중간 | 코어 → 기능 화살표 0, EngineTest, 씬 로드, 에디터 시나리오 |
| 3 링크 분할 | 중간 | 낮음(OBJECT) / 중간(DLL) | 네 백엔드 × Dev · Shipping · Server 빌드 |
| 4 월드 | 큼 | 높음 | 프리팹 격리, 서버 방 시나리오 |
| 5 모듈 진입점 | 중간 | 낮음 | 게이트 + 핫 리로드 테스트 |

각 단계는 독립 커밋 묶음이고, 단계가 끝날 때마다 전 프리셋 컴파일과 `ctest -L lint · nogpu` 를 돌립니다.

## 6. 작업 단위 (2026-10-18 설계)

### 6.1 근본 원인

코어가 기능 객체를 값으로 소유하는 것은, 그 객체를 등록할 진입점이 없던 때의 구조가 남은 것입니다.
물리와 내비게이션과 오디오와 애니메이션(`AnimationSystem`)은 씬 서비스(`SW_SCENE_SERVICE( 타입, 프레임 단계 )`)로 옮겼고, 매니저는 레지스트리만 봅니다.
프리미티브 레지스트리만 이 진입점을 거치지 않습니다. 그래서 같은 구조로 옮기면 코어가 기능 폴더를 include 할 이유가 없어집니다.
게이트에 금지 폴더를 더하는 것은 그 뒤에 하는 마지막 단계입니다.

### 6.2 해법

- **프리미티브 개념은 코어의 데이터 슬롯입니다.** 코어(`Object/GameObject/Registry/`)에 가상 함수 없는 기반 `ScenePrimitive`(프리미티브 번호 하나)와 `ScenePrimitiveBatch`(레지스트리 포인터와 항목 수)를 둡니다.
  `MeshComponent` 와 `MeshInstanceBatch` 가 이 기반을 상속하고, `PrimitiveRegistry` 는 기반 타입만 압니다. 트랜스폼 전파가 레지스트리에 더티를 알리는 핫 패스는 지금처럼 가상 호출 없이 슬롯 번호로 갑니다.
  `MeshComponent*` 가 필요한 곳(`GPUSceneBuilder`, `PixelPerfectCameraComponent`)은 기능 층 위라서 그쪽에서 `static_cast` 합니다. 레지스트리에 들어오는 타입이 메시 계열뿐이라는 불변식은 등록 함수의 시그니처가 지킵니다.
- **게이트는 마지막입니다.** 위 두 단계로 줄이 0 이 된 뒤 `CheckEngineLayers.toml` 의 `coreBannedLayer` 에 `Object/Component/2D` · `3D` · `Audio` · `Navigation` · `Physics` 와 `Object/SceneAnimation` 을 더합니다.
  형식으로 막을 수 없는 이유는 include 가 형식이 아니기 때문입니다. 폴더 방향은 include 그래프로만 보입니다.

### 6.3 대안과 기각 이유

- **프리미티브 레지스트리를 `Object/Component/3D` 로 옮기고, 트랜스폼 계층이 리스너 인터페이스로 알리는 구조.** 기각합니다. 더티 트랜스폼마다 가상 호출이 하나 늘고, 큐브 20,000 개 벤치의 `GT.Frame` 이 그 경로를 지납니다.
  측정 없이 핫 패스에 간접 호출을 넣지 않습니다. 언리얼은 `USceneComponent` 가 가상 `OnUpdateTransform` 으로 `UPrimitiveComponent` 에 알리지만, 둘 다 같은 Engine 모듈 안에 있습니다 [기억].
  Godot 는 노드가 `NOTIFICATION_TRANSFORM_CHANGED` 를 받아 `RenderingServer` 에 인스턴스 트랜스폼을 넘깁니다 [기억]. 여기서는 개념만 코어에 두고 데이터 슬롯으로 연결하는 쪽이 비용이 없습니다.
- **`MeshComponent` 를 코어로 내리는 구조.** 기각합니다. 메시는 머티리얼과 LOD 를 통해 그래픽스를 알고, 그러면 코어가 그래픽스를 다시 include 합니다.
- **3 단계(OBJECT 라이브러리, DLL 승격)를 지금 하는 것.** 하지 않습니다. 0 단계 측정에서 링크 시간 이득이 없었습니다([결정 기록](../09_Decisions.md) 3절). 다른 이유가 생길 때만 합니다.
- **4 단계(월드를 값으로)를 이 묶음에 넣는 것.** 하지 않습니다. 코어가 기능을 모르는 상태(E4)가 선행 조건이고 규모가 큽니다. EditorMultiWorld 를 시작할 때 따로 설계합니다.

### 6.4 단위

동시 빌드는 둘까지입니다. E3 은 `GameObjectManager.h` 를 바꿔 모든 모듈을 다시 빌드합니다.

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| E3 | 프리미티브 개념을 코어 기반 타입으로 바꿉니다 | `Engine/Object/GameObject/Registry/`, `Engine/Object/Component/3D/Mesh*`, `Engine/Object/Component/2D/SpriteInstanceBatch.cpp`, `Engine/Environment/{Foliage,Terrain,Water}/`, `Engine/Renderer/Scene/GPUSceneBuilder.cpp` | 없음 | `TestGPUScene`, 인스턴스 배치 테스트, 네 백엔드 × 대표 셋 | 필요합니다. 큐브 20,000 개 벤치의 `GT.Frame` p50 · p99 를 Release, VSync 끔, 3 ~ 5 판으로 전후 비교합니다 |
| E4 | 코어 금지 목록에 기능 컴포넌트 폴더를 더합니다 | `Scripts/lint/rules/CheckEngineLayers.toml` | E3 | `-L lint`, `RunEngineLayerGraph.py` 의 코어 → 기능 화살표 0 | 없음 |

### 6.5 사용자 결정

없습니다. 3 단계와 4 단계를 미루는 판단은 이미 결정 기록에 있습니다.
