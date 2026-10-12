# 엔진 분할 계획

`Engine.dll` 하나를 티어 경계로 나누고, 그 전에 의존 방향의 약한 고리를 풉니다. 폴더 의존은 이미 DAG 이고 `CheckEngineLayers.py` 가 지킵니다.
막힌 곳은 링크 단위가 하나라는 점과, 컴포넌트 모델(`Object`)이 기능 폴더 다섯(Graphics, Physics, Animation, Audio, Navigation)에 묶여 있다는 점입니다.
특히 `GameObject/` 가 기능 폴더를 직접 include 하는 것이 문제입니다. 컴포넌트 모델의 코어가 렌더, 물리, 내비를 알게 됩니다.
폴더 사이 include 수는 `py -3 Scripts/lint/report/RunEngineLayerGraph.py` 로 봅니다. Engine 에 남겨 두기로 한 폴더는 [결정 기록](../09_Decisions.md) 2절에 있습니다.

## 0. 기준 측정 (먼저)

- Engine 안에서 `Graphics` 한 파일, `UI` 한 파일, `Object` 한 파일을 고쳤을 때 Release 증분 빌드 시간(컴파일 + 재링크)을 3 회씩 잽니다. 표는 [검증과 측정](../08_Verification.md) 형식입니다.
- 분할 뒤 같은 세 경우와 비교합니다. 이득이 재링크 시간에서 나오지 않으면 3 단계(DLL 승격)는 하지 않습니다.

## 0-3. Engine 폴더 재배치의 남은 판단

티어 표는 `py -3 Scripts/lint/report/RunEngineLayerGraph.py --apply` 가 게이트 표와 `Source/Engine/README.md` 티어 표를 함께 맞춥니다.

- `Utility` 루트 파일(`CommandStack` · `DebugOverlayState` · `GameAutoplay` · `GameTimeScale` · `KeyValueFile`)은 이름만 보고 두었습니다. 후보는 `CommandStack` → `Scene`(에디터 실행 취소),
  `KeyValueFile` → `Serialization/Format`, `GameTimeScale` · `GameAutoplay` → `Config`, `DebugOverlayState` → `Profiling` 입니다.
- `Character/AnimationNotify`(처리기, 테이블, 컴포넌트)는 `CharacterHit` · `SocketSetComponent` 를 써서 `Object/Animation` 으로 내릴 수 없습니다. 알림 계약만 `Animation/Notify` 로 모았습니다.
- `Test/EngineTest/Graphics` 의 렌더러 테스트(`TestGpuScene` · `TestRenderGraph` 등)는 아직 `Graphics` 폴더에 있습니다(`Test/EngineTest/Renderer` 로 옮길 후보).

## 2. 컴포넌트 모델을 코어와 기능으로 가르기 (중간 위험, 가장 큰 효과)

- `Object` 를 둘로 나눕니다. **코어**는 `GameObject` · `Prefab` · `Component` 기반 클래스 · 리플렉션 훅이고, **기능 컴포넌트**는 `Component/{2D,3D,Audio,Navigation,Physics}` · `Object/Animation` 입니다.
- 씬마다 하나인 기능 서비스는 씬 서비스 레지스트리(`ISceneService` · `SW_SCENE_SERVICE`)로 만듭니다. `SceneNavigation` 이 첫 예이고 `GameObject/` 는 그 타입을 모릅니다.
  다음은 `ScenePhysics` · `SceneAudio` 입니다. `SceneAudio::update` 는 카메라가 필요해 프레임 단계 목록 밖에 있으므로 단계 하나를 더하거나 `Scene::tick` 쪽 진입점이 필요합니다.
- 코어는 `Graphics` · `Physics` · `Navigation` · `Audio` 를 include 하지 않습니다. `GameObject` 의 기능 의존은 기능 컴포넌트가 코어에 등록하는 방향으로 뒤집습니다.
  Transform 변경 알림, 바운드 질의, 씬 등록 모두 `GameObject` 가 인터페이스(`ISceneListener` 형태)로 듣습니다.
- 기능 컴포넌트는 코어 위, 각자의 기능 폴더 아래에 둡니다. 폴더 이동은 `git mv` 후 코드젠을 다시 만듭니다(헤더를 옮기면 `.gen.cpp` 가 낡는다).
- 확인: `RunEngineLayerGraph.py` 에서 코어 폴더가 기능 폴더를 가리키는 화살표가 0 입니다.

## 3. 링크 단위로 승격

- 먼저 `OBJECT` 라이브러리로 나눕니다(DLL 은 하나, 위험 낮음). 층은 Foundation(티어 0 ~ 2), Data(3 ~ 4), Render(5, Renderer, Environment), World(6 ~ 7 코어), Features(기능 컴포넌트), Tools(UI, Telemetry, Automation, Module), 루트(EngineLoop)입니다.
- 0 단계 측정으로 이득이 확인되면 Dev 만 층마다 DLL 로 올립니다. 내보내기 매크로는 층별로 늘리지 않고 `SW_API` 하나를 유지하되, 각 DLL 의 `SW_EXPORTS` 를 층 전체에 겁니다.
- 리플렉션 등록기를 보존하려고 `sw_linkWholeArchive` 를 층 라이브러리마다 겁니다(Shipping 정적 링크는 지금과 같다). 서버 타깃은 Render 층을 링크하지 않습니다.

## 4. 월드를 값으로

- `World` 가 씬, 물리 월드, 내비, GPUScene 빌더, 틱 설정을 소유하고 `EngineLoop` 는 월드 목록을 돕니다. 프리팹 격리, 머티리얼 미리보기, 에디터 툴 창, 서버의 방이 이 위에 섭니다(백로그 "다중 월드").
- 2 단계 뒤에 합니다. 월드가 코어 위의 소유자가 되려면 코어가 기능을 모르는 상태여야 합니다.

## 5. 모듈 확장 진입점과 상태 규칙

- 모듈이 서비스, 렌더 패스, 레지스트리를 등록하는 공식 진입점을 둡니다. 리로드되는 모듈의 변경 가능한 정적 변수를 막는 게이트(허용 목록 + 이유)를 두고,
  `GameDataCache` · `SQLDriverRegistry` · `LocalStoreFactory` 의 등록 테이블을 서비스로 올립니다.

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
