# RuntimeAPI (런타임 API)

엔진(Engine)과 게임/에디터(DLL) 사이를 이어주는 **순수 Header-Only (INTERFACE) 인터페이스와 핸들 모음**입니다. CMake 타겟은 하나(`RuntimeAPI`)이며, 헤더는 역할별로 폴더를 나눕니다.

## 왜 필요한가요?
엔진은 핫리로드(LiveReload)를 지원하기 때문에, `App` 실행 파일이 `Editor`나 `Game` 모듈의 구체적인 C++ 클래스에 강하게 결합(정적 링크)되면 안 됩니다.
이 폴더의 API는 C-ABI 경계를 지켜주며, 엔진이 모듈 내부 클래스를 몰라도 함수 테이블로 로직을 호출할 수 있게 합니다. 이 모듈에는 **.cpp 구현체가 단 하나도 존재하지 않으며**, 완전히 추상적인 계약(Contract) 역할만 수행합니다.

## 폴더

| 폴더 | 헤더 | 누가 include | 용도 |
|------|------|--------------|------|
| **ABI/** | `RuntimeHandles.h`, `GameAPI.h`, `EditorAPI.h`, `ModuleAbi.h` | App, 모듈, 테스트 | 호스트 ↔ 모듈 C-ABI 함수 테이블·불투명 핸들, 모듈 ABI 버전 · 스탬프(App 과 모듈이 같은 헤더로 빌드됐는지 로드 때 대조) |
| **Service/** | `ModuleService.h`, `EngineServiceList.xxx`, `HostServiceList.xxx`, `ServiceListColumns.h`, `IModuleCompiler.h` | App, 모듈 | 호스트 ↔ 모듈 C-ABI 단일 통합 서비스 테이블 (`GameService.h`, `EditorService.h`는 각 모듈에 위치). `ServiceListColumns.h` 는 서비스 목록의 낱말 칸을 값으로 바꾸는 매크로(아래). `IModuleCompiler` 는 에디터 안 백그라운드 컴파일러 서비스 — C-ABI 가 아니라 C++ 가상 함수 테이블이다 |
| **Export/** | `GameModuleExports.h`, `EditorModuleExports.h`, `ModuleForwardUtil.h` | 모듈 `.cpp`만 | `SW_IMPLEMENT_*_MODULE` 매크로. `Memory.h`와 로케이터 bind를 끌어옴. `ModuleForwardUtil` 은 불투명 핸들 → 구현 인스턴스 전달(널 검사 한 곳) |

- Game 모듈은 `ABI/GameAPI.h`, `GameFramework/Base/Framework/GameService.h`, `Export/GameModuleExports.h`만 include 한다.
- Editor 모듈은 `ABI/EditorAPI.h`, `Editor/Common/Workspace/EditorService.h`, `Export/EditorModuleExports.h`를 include 한다.
- 모듈 구현 `.cpp`는 `Export/*ModuleExports.h`만 있으면 테이블 export 매크로까지 포함된다.

## C-ABI 경계

- 모듈이 내보내는 진입점은 `extern "C"` + `SW_MODULE_API` 함수뿐이다 — `get<Game|Editor>ModuleAbiVersion` · `get…ModuleAbiStamp` ·
  `export<Game|Editor>Api`(함수 포인터 표 `GameAPI` · `EditorAPI` 를 채운다). 에디터 모듈은 헤드리스 원본 임포트 진입점
  `importEditorAssets( kind, checkOnly )`(`kImportEditorAssetsSymbol`, 종류는 `EditorImportKind` — `App --import-textures` · `--import-models` 와 `--check-*`)도 내보낸다.
- 호스트는 표를 받기 전에 버전 · 스탬프(`ABI/ModuleAbi.h` 의 `kModuleAbiVersion` · `kModuleAbiStamp`)를 대조하고, 다르면 그 이미지를 쓰지 않는다.
  주의: `GameAPI` · `EditorAPI` 에 항목을 더하거나 순서를 바꾸면 `kModuleAbiVersion` 을 올리고 스탬프를 고친다.
- 경계를 넘는 객체는 불투명 핸들(`ABI/RuntimeHandles.h` — `WindowHandle` · `RHIDeviceHandle` · `EditorHandle` · `GameHandle` · `TextureHandle`)이다.
- export 매크로는 서로 바꿔 쓰지 않는다: `SW_API`(Engine.dll 심볼) · `SW_MODULE_API`(모듈의 C-ABI 진입점) · `SW_GF_API`(GameFramework.dll 클래스) ·
  `SW_GAMESERVICE_API`(GameService 로케이터).

## 서비스 표

`ModuleService` 는 서비스 id(`internal::ModuleServiceId`) 순서의 포인터 배열 하나다. id 는 엔진 서비스 목록(`RuntimeAPI/Service/EngineServiceList.xxx`)과
호스트 서비스 목록(`Service/HostServiceList.xxx`)에서 생성된다. 호스트가 모듈 인스턴스를 만들 때 채워 넘기며(`ModuleHost` 가 엔진 칸은 `engine::fillModuleServices` 로, 호스트 칸은 직접),
게임 모듈에는 `GameVisible` 칸만 채운다(나머지는 nullptr). 두 목록 모두 RuntimeAPI 에 있다 — 서비스 id 가 계약이라서다. 엔진 목록의 타입 이름은 전방 선언만 만들고, member · getter · requirement · creator 칸은 Engine 만 읽는다.

목록의 칸은 숫자가 아니라 **낱말**이다 — `requirement`(`Required` · `Optional`), `visibility`(`GameVisible` · `HostOnly`),
`creator`(`EngineCreated` · `HostCreated`, 엔진 목록만). 값이 필요한 자리(`if constexpr` 등)는 `ServiceListColumns.h` 의
`SW_SERVICE_IS_REQUIRED` · `SW_SERVICE_IS_GAME_VISIBLE` · `SW_SERVICE_IS_ENGINE_CREATED` 로 바꾼다. 모르는 낱말(오타)은 정의되지 않은
매크로 이름이 되어 컴파일 오류다.

## 핵심 규칙
- **구현체 없음**: 뼈대(인터페이스 선언)와 타입만 둔다. 동작 코드는 `Engine`, `Editor`, `SWGame` 쪽에 구현한다
  (`Export/` 의 매크로 본문은 모듈의 `.cpp` 안에서 펼쳐진다).
- **Engine · App 을 include 하지 않음**: RuntimeAPI 의 헤더는 `Core/` 와 RuntimeAPI 자신만 include 한다(`Export/` 의 모듈 매크로가 모듈 쪽 로케이터 — `Editor/` · `GameFramework/` — 를 끌어오는 것은 그 본문이 모듈 `.cpp` 에서 펼쳐지는 계약이라 예외). `CheckEngineLayers.py` 가 막는다.

## 함정 · 계약

- **서비스 표(`EngineServiceList.xxx`)는 RuntimeAPI(`Service/`)에 있고 Engine 이 include 한다** — 서비스 id 가 호스트 ↔ 모듈 계약이라서다. 타입 이름은 전방 선언만 만든다. RuntimeAPI 는 Engine · App 헤더를 include 하지 않는다(`CheckEngineLayers`). 표를 id 표와 바인딩 표로 나누지 않는다(목록 둘이 된다).
