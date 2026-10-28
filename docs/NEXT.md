# 다음 세션 인계 — 구조 개편

> 질문: 지금 어디까지 왔고, 다음 세션은 무엇부터 하는가?
> 독자: 이어서 일하는 세션(에이전트와 사람).
> 결론: Ninja-Debug · Ninja-Release(모든 게임 12 · 모듈 76) 모두 오류 0 이다. 용어 · 약어 · 폴더 · 다중 상속 정리의 큰 묶음은 끝났다. 다음은 3 절의 전수 감사 목록(정적 전용 타입 393 · 평평한 대형 폴더 · 모호한 폴더 · 파일 이름)을 4 절의 규칙대로 처리하고, 그 뒤 A3(낮은 층 파일 읽기) → B(Engine 링크 분할) → C(키트에 에디터 확장 모으기)다.

## 1. 지금 상태 (2026-10-29)

- **빌드**: `cmake --build --preset Ninja-Debug`(활성 게임 Empty, 모듈 11) · `Ninja-Release`(전체) 오류 0. 셰이더 재쿠킹 · 실행 확인은 하지 않았다(이름이 바뀐 셰이더가 있다 — `App --cook-shaders`).
- **층 게이트** `py -3 -m Scripts engine-layers`(커밋 훅): 표는 `Scripts/lint/EngineLayers.py` 의 `kLayer`, 남은 역방향 35 줄은 `Scripts/lint/rules/EngineLayersBaseline.txt`(줄이기만).
- **도구**: `move-source`(이동 + 모든 참조 · include 순서 · clang-format), `rename-symbol`(`--regex` 로 이름 일부), `acronym-case`(바깥 이름 목록 `Scripts/dev/AcronymExternalNames.txt`), `engine-layers`, 구조 감사 `py -3 Scripts/dev/StructureAudit.py`. 이름 도구는 규칙 문서(`docs/agents` · `04_CodingGuidelines`)를 고치지 않는다.
- **조심할 것**: Windows git 은 대소문자만 다른 이름 바꾸기를 못 본다(`git rm --cached` + `git add`). `git reset --hard` 전에는 미커밋 변경을 패치로 보관한다(이 세션에서 사용자의 `NEXT.md` 편집을 한 번 잃었다). 단독 낱말(`Image` · `Resource`)을 일괄 치환하지 않는다 — 바깥 API(DirectX::Image) · 다른 뜻 폴더(`Renderer/Frame/Resource`)를 깬다.

## 2. 이번 세션에 정한 규칙

| 규칙 | 근거 |
|---|---|
| Asset = 콘텐츠(`Engine/Asset`, 데이터 폴더 `Content/`), Resource = GPU · RHI 자원만 | 언리얼 Content · AssetManager · FRenderResource |
| Pack(`.pack`) · Watch(자동화 감시 값, Probe 는 라이트 · 리플렉션 프로브만) · Scene(씬 컨테이너, World 는 월드 공간 뜻만) | 같은 단어 한 뜻 |
| Module 은 동적으로 올리는 코드 단위만. 렌더러 확장점은 `RenderExtension` · `Extension*`(`Renderer/Extension`), 모듈 로더는 `IModuleLoader` · `ModuleLoadStage` · `ModuleUnloadStage` | 언리얼 SceneViewExtension |
| 모듈 문맥의 Image → Library(`ModuleLibraryUtil` · `DeferredLibraryUnloadQueue`). Image 는 그림 · 텍스처만 | |
| 엔진 범위 셋: Core(`Engine/CoreServices`, 옛 Bootstrap) → Service → Startup. 게임의 팩 설정은 `GamePackConfig` | Bootstrap · Startup 이 같은 뜻이었다 |
| 수명을 소유하면 Manager: `GameModuleManager` · `EditorModuleManager`, 라이브러리 `Source/ModuleManager`(모듈 일만, 핫 리로드는 `LiveReload/` 폴더째 Shipping 제외). 호스트 기능은 `HostService`(`Engine/HostServices.h`) | 언리얼 FModuleManager |
| 기능 폴더가 자기 컴포넌트 · 씬 서비스를 든다. 씬은 기능을 모른다(`ISceneService::onSceneLoading` · `applyPreviewPolicy` · `drawDebug`) | 언리얼 UWorldSubsystem |
| 다중 상속: 구현 상속 · 믹스인은 합성(`SubscriptionList` 멤버, 언로드 처리기 등록 + 구독, 콜백 하나짜리 인터페이스는 델리게이트), 서비스 인터페이스는 서브시스템을 상속(`IModuleLoader : ISubsystem`). 순수 인터페이스 여러 개 구현(ISP) · 바깥 API 요구는 허용 | |
| 폴더: 한 기능의 하위는 하위 폴더(`Core/Log/Sink`, `RHI/DX/DX11`, `RHI/<백엔드>/Module`, `Automation/{Watch,Scenario,Runner}`). 프로그램은 `Source/Programs`, `Tools/` 는 내려받는 외부 도구만 | 언리얼 Source/Programs |
| 약어는 대문자(`GPU` · `ID` · `VFX`), 첫 낱말은 소문자(`gpuTime`) | NamingCpp |

## 3. 전수 감사 — 남은 같은 모양의 문제 (2026-10-29 측정)

다시 재려면 `py -3 Scripts/dev/StructureAudit.py`(아래 항목을 같은 기준으로 다시 낸다).

1. **정적 함수만 있는 타입 393 개**(Util · Internal 접미사 아님). 함수만 있는데 이름이 인스턴스처럼 읽힌다 — `EditorPlaySession`(정적 37), `EditorWidgets`(34), `EditorSceneCommands`(26), `EditorChrome`(23), `EditorAssetTypeRegistry`(23), `BinarySerializer`(17), `Ledger`(14), `MonsterRules`(10) … 상위 폴더: `Editor/Viewport` · `Editor/Common/{GUI,Workspace,Asset,Widgets,Commands/*}` · `Engine/{Audio,Reflection,Serialization/Format,Scene,Text}` · `GameFramework/Base/{Gameplay/Appearance,Online/Ledger}`.
2. **평평한 대형 폴더**: `Editor/Common/Workspace`(35 단위, 31 개가 `Editor*`) · `Editor/Common/GUI`(32, 30 개 `Editor*`) · `Core/Container`(40) · `Engine/Reflection`(26) · `Engine/Audio`(25) · `Engine/Graphics/RHI`(21) · `Engine/Destruction`(`Fracture*` 10 개 묶음) · `GameFramework/Base/Gameplay/Appearance`(21).
3. **모호한 폴더 이름 25 개**: `Core/Common` · `Editor/Common` · `Engine/Common` · `Engine/Serialization/{Base,Object}` · `Engine/UI/Base` · `GameFramework/Base` · `GameFramework/Plugin/Editor/Base` · `Editor/Panels/Data` · `GameFramework/Base/Foundation/Data` · 온라인 키트의 `Shared` 11 개 · `Engine/Graphics/RHI/Support`.
4. **모호한 파일 이름 19 개**: `*Data`(`FrameData` · `HeightfieldData` …) · `*Info`(`CultureInfo` · `RHIBackendInfo` …) · `*Base`(`GameInstanceBase` · `FractureComponentBase`) · `Engine/Common/Common.h` · `ReflectionCore`.
5. **에디터 접두어 반복**: `Editor/` 아래 타입 · 파일이 거의 모두 `Editor*` — 네임스페이스(`sw::editor`)와 폴더가 이미 말해 준다.

## 4. 처리 규칙 (3 절에 적용)

- 정적 함수만 있는 타입: 여러 번역 단위가 쓰면 `XxxUtil`(Helpers 규칙), 실제로 상태(숨은 전역)를 감싼 정면이면 `XxxRegistry` · `XxxService` 처럼 역할 이름. 이름만 인스턴스인 것(`EditorPlaySession`)은 `EditorPlaySessionUtil` 이나 실제 인스턴스로.
- 평평한 폴더: 첫 낱말 묶음 4 개 이상이면 하위 폴더(`Destruction/Fracture/`). 부모 이름을 반복하는 접두어는 하위 폴더로 옮길 때 같이 떼는 것을 검토한다(타입 이름은 전역 유일해야 하므로 `sw::editor` 안에서만).
- 모호한 폴더: 무엇이 들었는지로 — `Common` 은 가장 낮은 층 공용 형식이면 `Foundation`, 아니면 내용별로 흩는다. 온라인 키트 `Shared` 는 클라이언트 · 서버가 같이 쓰는 프로토콜이라 `Protocol` 또는 키트 이름 중 하나로 통일.
- 파일 이름: 주 타입 이름과 같게. `*Data` · `*Info` 는 Def · Desc · Settings 등 NamingCpp 의 타입 접미사 표로.
- 한 묶음마다 `move-source` / `rename-symbol` → Ninja-Debug → 커밋, 묶음 몇 개마다 Ninja-Release.

## 5. 그 다음

| 단계 | 내용 |
|---|---|
| A3 | 낮은 층의 파일 읽기: Serialization · Config · Telemetry · CoreServices · Profiling → `AssetIO` · `AssetPaths` · `AssetID`(약 24 줄). 언리얼 `IPlatformFile` 처럼 낮은 층에 읽기 인터페이스, Asset 이 구현 등록. `Object → Animation/Sprite` · `Graphics → Physics`(MeshCache) · `Telemetry → UserSettings` · `CoreServices → RHIRenderResource` · `Reflection → Localization`. 목표: 기준선 0 |
| B | 층을 따라 Engine 을 링크 단위로(Foundation · RHI · World · 기능별 · Renderer). 서버의 렌더러 빼기가 모듈 하나 빼기로 |
| C | 키트 폴더에 에디터 확장 모으기(`GameFramework/Plugin/Editor/<X>` → 키트 `Editor/`, `sw_addKit` 이 이미 `Editor/CMakeLists.txt` 를 부른다). 온라인 키트는 `Runtime/` · `Server/` 옆에 `Editor/` 를 둔다 |
| 남은 처리기 인터페이스 | 프로토콜 인터페이스(`INetMessageHandler` · `IStreamHandler` · `IStreamEndpointListener`)는 규칙상 둔다. `IEngineFrameObserver` · `IWindowTitleTarget` 은 등록 + 델리게이트 후보 |
| 그 밖 | 없는 스크립트를 부르는 곳(에셋 검증 `Scripts/qa/ValidateAssets.py` · 설정 색인 · 아이콘 폰트 · `RunBuildBaseline.py`), 지운 `docs/plans/*` 를 가리키는 주석, 셰이더 재쿠킹 · 실행 확인 |
