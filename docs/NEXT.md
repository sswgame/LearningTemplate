# 다음 세션 인계 — 구조 개편

> 질문: 지금 어디까지 왔고, 구조 개편을 어떤 순서로 이어 가는가?
> 독자: 이어서 일하는 세션(에이전트와 사람).
> 결론: Ninja-Debug(활성 게임 Empty, 모듈 11 개)는 오류 0 으로 빌드된다. 용어 · 약어 · 폴더 정리는 끝났고, Engine 폴더의 역방향 include 는 190 → 50 이다. 남은 것은 A2(씬이 기능을 모르게) · A3(낮은 층 파일 읽기를 인터페이스로) → B(링크 단위 분할) → C(키트를 플러그인 묶음으로, Tools 를 Source/Programs 로) 순서다.

## 1. 지금 상태 (2026-10-29)

- **빌드**: `cmake --preset Ninja-Debug` + `cmake --build --preset Ninja-Debug` 오류 0. 모든 게임 · 키트(`Ninja-Release`)는 이 세션 끝에 돌렸다 — 결과는 아래 4 절.
- **기본 프리셋**: Ninja-Debug 는 활성 게임과 그 게임이 쓰는 키트만 짓는다(76 → 11 모듈). 전체는 Ninja-Release(`SW_GAMES=ALL`, `SW_BUILD_UNUSED_KITS=ON`).
- **층 게이트**: `py -3 -m Scripts engine-layers`(커밋 훅도 부른다). 층 표는 `Scripts/lint/EngineLayers.py` 의 `kLayer` 하나, 남은 역방향 50 줄은 `Scripts/lint/rules/EngineLayersBaseline.txt`(래칫 — 고쳐서 줄면 `--update-baseline`, 늘리지 않는다).
- **도구**: `move-source`(파일 · 폴더 이동 + 모든 참조 · include 순서 · clang-format), `rename-symbol`(낱말 경계, `--regex` 로 이름 일부), `acronym-case`(약어 대문자, 바깥 이름은 `Scripts/dev/AcronymExternalNames.txt`), `engine-layers`. 규칙 문서(`docs/agents` · `04_CodingGuidelines`)는 이름 도구가 고치지 않는다(나쁜 예를 일부러 든다).
- **Windows 대소문자**: git 이 대소문자를 무시하므로 대소문자만 바꾼 경로는 `git rm --cached` + `git add` 로 다시 올려야 한다(`acronym-case` 커밋에서 한 방법).

## 2. 이번 세션에 정한 규칙

| 규칙 | 근거 |
|---|---|
| Asset = 콘텐츠(`Engine/Asset`, `AssetID` · `AssetPaths` · `AssetIO`, 데이터 폴더 `Content/`), Resource = GPU · RHI 자원만(`RHIRenderResource`, `Renderer/Frame/Resource`) | 언리얼 Content · AssetManager · FRenderResource |
| Pack(`.pack`, `PackManager` · `PackReader` · `PackWriter`) | 확장자 · 폴더와 같은 철자 |
| 자동화 감시 값은 Watch(`Engine/Automation/Watch`, `SW_AUTOMATION_WATCH`, `*Watches.cpp`). Probe 는 라이트 · 리플렉션 프로브와 물리 sweep · 픽셀 검사에만 | 디버거 감시식 |
| 씬 컨테이너는 Scene(`SceneSettings` · `SceneStreamingSubsystem` · `StreamingCell*`). World 는 월드 공간 뜻에만 | 엔진 용어 하나 |
| 기능 폴더가 자기 컴포넌트를 든다(`Physics/Component` · `Animation/Component` · `Navigation/Component` · `Audio/Component` · `TileMap/Component`). `Object` 는 컴포넌트 모델 코어와 렌더 컴포넌트(서버도 씬을 읽어야 하므로 Renderer 로 보내지 않는다) | Unity 패키지 · Godot 모듈, 언리얼 UStaticMeshComponent 는 Engine |
| 자동화는 `Engine/Automation/{Watch,Scenario,Runner}`, `SW_ENABLE_AUTOMATION=OFF` 는 `Runner/` 폴더째 뺀다 | 언리얼 AutomationWorker · Controller |
| GameFramework 의 `Base/Character`(옛 Actor), `Plugin`(옛 PlugIn, 모듈 `GF_EditorPlugin_*`) | Actor 클래스가 없었다 |
| 약어 대문자(`GPU` · `ID` · `JSON` · `VFX` …), 첫 낱말은 소문자(`gpuTime`) | NamingCpp Acronyms |
| 금지 동사 정리: `getOrCreate*`(옛 findOrAdd), `addPasses`(옛 setupPasses), `findResolved` | NamingCpp |
| 게임 모듈의 수명을 소유하는 쪽은 Manager: `GameModuleManager` · `EditorModuleManager`, 라이브러리 `Source/ModuleManager`(옛 ModuleHost) | 언리얼 FModuleManager, NamingCpp 접미사 표 |

## 3. 남은 일 (순서대로)

| 단계 | 내용 | 확인 |
|---|---|---|
| A2 | 씬이 기능을 모르게 — 이미 있는 `SceneServiceRegistry`(언리얼 UWorldSubsystem 자리)로 옮긴다: ① `Scene::update` 끝의 `SceneAudio::update` 호출을 늦은 프레임 단계 서비스로 ② `SceneNavigation::setSourcePath` 를 `ISceneService` 의 씬 로드 훅으로 ③ `SceneManager` 의 물리 고정 스텝 정책 ④ `SceneDebugDraw` 의 물리 · 내비 그리기를 서비스별 `drawDebug` 로. 기준선의 `Scene -> Physics/Navigation/Audio/Animation` 15 줄이 사라진다 | 씬 열기 · 플레이 · 디버그 그리기 |
| A3 | 낮은 층의 파일 읽기: Serialization · Config · Telemetry · Bootstrap · Profiling 이 `AssetIO` · `AssetPaths` · `AssetID` 를 부른다(약 24 줄). 언리얼 `IPlatformFile` 체인처럼 낮은 층(0~1)에 읽기 인터페이스를 두고 Asset 이 구현을 등록한다. `AssetID.h` 는 Core 만 쓰므로 낮은 층으로 내릴 수 있다. 그 밖에 `Object -> Animation/Sprite`(스프라이트 클립), `Graphics -> Physics`(`MeshCache` 의 셰이프 캐시 무효화 → 콜백), `Asset -> Physics`, `Telemetry -> UserSettings`, `Bootstrap -> RHIRenderResource`, `Reflection -> Localization` | `engine-layers` 기준선 0 |
| B | 층을 따라 Engine 을 링크 단위로 나눈다(Foundation · RHI · World · 기능별 · Renderer). 서버의 렌더러 빼기(`SW_UNBUILT_ENGINE_FOLDERS` · `SW_WITH_CLIENT_CODE`)가 모듈 하나 빼기로 바뀐다 | 전 프리셋, 증분 빌드 시간(`.ninja_log`) |
| C | 키트 폴더 하나에 `Runtime/` · `Server/` · `Editor/`(지금 에디터 확장은 `GameFramework/Plugin/Editor/<키트>` 로 떨어져 있다), `Source/Programs/ReflectionParser` · `Source/Programs/OnlineLoadBot` → `Source/Programs/` | 전 프리셋 |

그 밖에:
- 없는 스크립트를 부르는 곳: 에디터 에셋 검증(`Scripts/qa/ValidateAssets.py`), 프로젝트 설정 창(`GenerateConfigReference.py`), 아이콘 폰트(`GenerateEditorIcons.py`), `BuildLayout.cmake` · `BuildCaches.json` 의 `RunBuildBaseline.py`.
- `Source` 주석 약 21 곳이 지운 `docs/plans/*` 를 가리킨다. 주석의 '리소스' 낱말도 콘텐츠 뜻이면 '에셋' 으로.
- 셰이더는 이름이 바뀌었으니(약어 · Content 경로) 다시 쿠킹해야 한다(`App --cook-shaders`). 이 세션은 쿠킹 · 실행을 하지 않았다.
- 테스트 · 시나리오 틀은 아직 없다(옛 것은 10-28 에 지웠다).

## 4. 이 세션 끝의 Ninja-Release 결과

Ninja-Release(모든 게임 12 · 모듈 76, 3,077 단계) 오류 0 — ModuleHost 이름 바꾸기 전 상태에서. 그 뒤 Ninja-Debug 오류 0.
