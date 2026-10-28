# 다음 세션 인계 — 구조 개편

> 질문: 지금 어디까지 왔고, 구조 개편을 어떤 순서로 이어 가는가?
> 독자: 이어서 일하는 세션(에이전트와 사람).
> 결론: 확장성과 빌드 시간의 근본 원인은 Engine 안 폴더 19 개가 한 순환 덩어리라는 것이다(층 단위로만 DAG). 먼저 빌드를 세우고, Engine 폴더 그래프를 DAG 로 만든 뒤(A), 그 선을 따라 링크 단위로 나누고(B), 키트를 플러그인 묶음으로 모은다(C). 이름 정리는 옮기는 단계에서 같이 한다(D).

2026-10-29 에 옛 계획 문서(`docs/plans`)를 모두 지웠다. 코딩 규칙은 `docs/agents/` 와 `docs/04_CodingGuidelines.md` 만 남았다.

## 0. 먼저 — 첫 빌드

코드는 10-28 묶음 이후 한 번도 컴파일되지 않았다. 구조 개편의 행동 변경(A2 부터)은 초록 빌드 위에서만 한다 — 원래 있던 오류와 개편 오류가 섞이면 가르지 못한다. 순수 이동(A1)은 include 린트 + configure 로 확인하고 들어가도 된다.

- `cmake --preset Ninja-Debug` 는 선다(활성 게임 `Empty`, 모듈 11 개). 다음은 `cmake --build --preset Ninja-Debug` 로 오류를 묶음별로 잡는다.
- 모든 게임 · 키트 컴파일 확인은 `Ninja-Release`(`SW_GAMES=ALL`, `SW_BUILD_UNUSED_KITS=ON`).
- 없는 스크립트를 부르는 곳: 에디터 에셋 검증(`Scripts/qa/ValidateAssets.py`), 프로젝트 설정 창(`GenerateConfigReference.py`, 옛 `ConfigReference.json`), 아이콘 폰트(`GenerateEditorIcons.py`), `BuildLayout.cmake` · `BuildCaches.json` 의 `RunBuildBaseline.py`. 새로 쓰거나 호출 쪽을 지운다.
- `Source` 주석 21 곳이 지운 `docs/plans/*` 를 가리킨다 — 그 파일을 고칠 때 같이 걷는다.

## 1. 측정 (2026-10-29)

| 항목 | 값 | 뜻 |
|---|---|---|
| Engine 폴더 의존 | 37 폴더 중 19 개가 한 강결합 요소(SCC) | 기능 하나를 빼거나 갈아 끼울 수 없고 모듈로 나눌 수 없다 |
| 거꾸로 올라가는 include | 190 개 / 31 쌍. 129 개가 `Object` → Animation 47 · Physics 41 · Navigation 26 · Audio 12 · TileMap 3 | `Object` 가 컴포넌트 모델 코어와 기능 컴포넌트를 함께 든다 |
| `Scene.cpp` | `ScenePhysics` · `SceneNavigation` · `SceneAudio` · `MeshComponent` · `DirectionalLightComponent` 를 직접 부른다 | 씬이 기능을 하드코딩한다(언리얼은 `UWorldSubsystem` 등록) |
| 그 밖의 역방향 | Serialization · Config · Telemetry · Bootstrap · Profiling → Resource(`ResourceID` · `ResourcePaths` · `ResourceIO`), Module → Scene · Audio · Input · Object, Reflection → Object · Localization, Window → Graphics(`IRenderSurface`), Graphics → Physics, Resource → Physics | 낮은 층이 쓰는 타입이 높은 폴더에 있다 |
| 헤더 무게 | TU 당 프로젝트 헤더 평균 86, `Engine/pch.h` 전이 55 개, `Component.h` fan-in 449 TU | 헤더는 무겁지 않다 — 빌드 시간의 주범이 아니다 |
| 빌드 구성(10-22 전체 Dev 빌드, CPU 합) | 컴파일 약 9,100 s(Engine 847 TU 평균 3.1 s 가 최대), 리플렉션 코드젠 모듈 66 개 합 171 s, 링크 · lib 약 1,000 s | 넓이(TU · DLL 수)가 비용이다 |
| 기본 프리셋 | 고쳤다: 76 → 11 모듈, 컴파일 대상 2,893 → 2,252 | 일상 빌드는 활성 게임과 그 키트만 |

측정 스크립트(폴더 SCC · 역방향 include · fan-in · ninja 로그 분해)는 세션 스크래치에 있었다 — 다시 쓸 때는 `Scripts/dev/` 에 하나로 두고 A5 게이트가 같은 코드를 쓴다.

## 2. 목표 구조 (상용 엔진 비교)

언리얼은 `Core → CoreUObject → Engine → Renderer` 와 `RHI` · `PhysicsCore` · `AudioMixer` · `NavigationSystem` 을 따로 둔 모듈 DAG 이고, 기능 컴포넌트는 그 기능 모듈 안에 있다. Godot 은 `core → servers → scene` 이고 물리 · 오디오가 서버다. 여기서의 목표:

```
Engine/  (폴더 = 장래 링크 단위, 위가 아래만 include)
  Foundation   Common · Curve · Compression · Reflection · Serialization · Config · Module · Profiling · Observability · Telemetry · Console · Bootstrap
               + 리소스 식별자 · 경로 · IO(Resource 에서 내린다)
  Asset        Resource(캐시 · 쿠킹) · Text · Localization
  Platform     Window · Input
  RHI          Graphics
  World        Object(컴포넌트 모델 코어만) · Scene(씬 서브시스템 등록으로 기능을 모른다)
  Features     Physics · Audio · Animation · Navigation · TileMap · Vfx · Sequencer · Dialogue · Environment · Destruction · Character · UI · Net · UserSettings
               각자 자기 컴포넌트(`<기능>/Component/`)와 씬 서브시스템을 든다
  Renderer     Renderer + 렌더 컴포넌트(Mesh · Light · Camera · Sprite)
  Automation   Probe(최하위 등록표) · Scenario · Runner
```

키트: `GameFramework/Kits/<묶음>/<키트>` 의 런타임 · 서버 모듈과 `GameFramework/PlugIn/Editor/<키트>` 의 에디터 모듈이 다른 나무에 있다. 언리얼 플러그인(`.uplugin` 하나에 Runtime · Editor 모듈)처럼 키트 폴더 하나에 `Runtime/` · `Server/` · `Editor/` 를 모은다. `Tools/ReflectionParser` · `Tools/OnlineLoadBot` 은 언리얼 `Source/Programs` 처럼 `Source/Programs/` 로.

## 3. 단계

단계마다 코드 → 린트 → 커밋, 묶음 끝에 빌드 한 번(원칙 2). 옮기는 단계는 기계적 include 재작성 스크립트로 하고 `py -3 -m Scripts format-includes --files …` 로 순서를 맞춘다.

| 단계 | 내용 | 확인 |
|---|---|---|
| A1 | 기능 컴포넌트를 기능 폴더로: `Object/Component/{Physics,Navigation,Audio}` → `<기능>/Component`, `Object/SceneAnimation` → `Animation/Scene`, 3D 의 스켈레탈 · 얼굴 · 리타깃 → `Animation/Component`, 2D 의 `TileMapRendererComponent` → `TileMap` | include 린트, configure, 역방향 수 재측정 |
| A2 | `Scene` 이 기능을 모르게: 씬 서브시스템 등록(언리얼 `UWorldSubsystem`) — `Scene` 은 등록된 서브시스템을 단계별로 돌리고, Physics · Navigation · Audio 가 스스로 등록. 메시 · 방향광 조회는 Renderer 쪽 서브시스템으로 | 첫 빌드 뒤. 씬 열기 · 플레이 |
| A3 | 낮은 층이 쓰는 타입 내리기: `ResourceID` · `ResourcePaths` · `ResourceIO` → Foundation, `IRenderSurface` → Window 와 Graphics 사이의 계약, `PhysicsMeshShapeCache` · Reflection → Object 두 줄 · Module → Scene/Audio/Input/Object 를 등록 · 인터페이스로 | 역방향 0 |
| A4 | 렌더 컴포넌트(Mesh · Light · Camera · Sprite · 2D 조명)를 Renderer 쪽으로, `Object` 는 코어만 | 역방향 0, SCC 없음 |
| A5 | 게이트 하나: Engine 폴더 순위 표 + 역방향 include 검사(형식으로 막지 못하는 틈이라 게이트, 원칙 1). `Scripts/lint` 에 두고 `lint` 가 부른다 | 린트 자가 확인 |
| B | DAG 를 따라 링크 단위로 나눈다: Foundation · RHI · World · 기능별 · Renderer. 서버 빌드의 "렌더러 빼기" 정규식(`SW_UNBUILT_ENGINE_FOLDERS` · `SW_WITH_CLIENT_CODE`)이 모듈 하나 빼기로 바뀐다. 증분 빌드는 고친 단위만 다시 링크 | 전 프리셋 구성 · 빌드, Dev 기동 시간 |
| C | 키트를 플러그인 묶음으로(`Runtime/` · `Server/` · `Editor/`), 프로그램을 `Source/Programs/` 로 | 전 프리셋 |
| D | 옮기는 폴더마다 이름 정리: `findOrAdd*` → `getOrCreate*`(약 70 곳, 충돌은 `findOrAddNode` → `getOrCreateNode` 하나가 기존 이름 2 곳과 겹친다), `setupPasses` → `addPasses`, `lookupResolved` → `findResolved`, `ModuleUnloadListener` 의 `add/removeListener` → `register/unregister`. 예외(도메인 용어): io_uring `setupRing`, Recast `alloc` 재정의, 관리 명령 `lookup`, 오디오 `addListener`, 복제 관심 영역 `removeObserver` | include 린트, 빌드 |

빌드 시간 쪽 남은 것: 첫 초록 빌드 뒤 `.ninja_log` 로 콜드 · 증분(헤더 하나, `.cpp` 하나)을 재고, Debug 링크에 GHASH(`/clang:-gcodeview-ghash` + `/DEBUG:GHASH`)를 A/B 로 잰다. B 의 분할 효과도 같은 방법으로 잰다.

## 4. 이번 세션에 끝낸 것

- `RuntimeAPI` → `Editor` 역방향 제거(`EditorModuleExports.h` 를 `Editor/` 로).
- 테스트가 없어 소비자가 없는 `sw_addEditorExtensionModel` 과 호출 5 곳 제거.
- `ReflectionSubsystem` → `Engine/Reflection`, `Engine/Utility` 해체(`FloatCurve` → `Engine/Curve`, `EditSessionState` → `Engine/Module`, `EngineFrameObserver` → `Engine/`).
- 자동화를 `Engine/Automation/{Probe,Scenario,Runner}` 로. `SW_ENABLE_AUTOMATION=OFF` 는 파일 이름 14 개 정규식 대신 `Runner/` 폴더째 뺀다.
- 기본 개발 프리셋을 활성 게임과 그 키트만 짓게.
