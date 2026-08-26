# RuntimeAPI (런타임 API)

엔진(Engine)과 게임/에디터(DLL) 사이를 이어주는 **순수 Header-Only (INTERFACE) 인터페이스와 핸들 모음**입니다. CMake 타겟은 하나(`RuntimeAPI`)이며, 헤더는 역할별로 폴더를 나눕니다.

## 왜 필요한가요?
엔진은 핫리로드(LiveReload)를 지원하기 때문에, `App` 실행 파일이 `Editor`나 `Game` 모듈의 구체적인 C++ 클래스에 강하게 결합(정적 링크)되면 안 됩니다.
이 폴더의 API는 C-ABI 경계를 지켜주며, 엔진이 모듈 내부 클래스를 몰라도 함수 테이블로 로직을 호출할 수 있게 합니다. 이 모듈에는 **.cpp 구현체가 단 하나도 존재하지 않으며**, 완전히 추상적인 계약(Contract) 역할만 수행합니다.

## 폴더

| 폴더 | 헤더 | 누가 include | 용도 |
|------|------|--------------|------|
| **ABI/** | `RuntimeHandles.h`, `GameAPI.h`, `EditorAPI.h`, `EditorUIContext.h` | App, 모듈, 테스트 | 호스트 ↔ 모듈 함수 테이블·핸들. `EditorUIContext`는 Dev 동일 프로세스 공유 타입이며 POD/ABI 동결 대상이 아님 |
| **Service/** | `ModuleService.h`, `GameService.h`, `EditorService.h` | 게임은 `GameService.h`만, 에디터/App은 `EditorService.h` | 서비스 로케이터. 에디터 전용 id는 `EditorService.h`에만 있음 |
| **Export/** | `GameModuleExports.h`, `EditorModuleExports.h` | 모듈 `.cpp`만 | `SW_IMPLEMENT_*_MODULE` 매크로. `Memory.h`와 로케이터 bind를 끌어옴 |
| **(루트)** | `PluginAPI.h` | Engine, App, GameFramework | Engine이 export하는 모듈 타입 등록 API. C-ABI 테이블이 아님 |

- Game 모듈은 `ABI/GameAPI.h`, `Service/GameService.h`, `Export/GameModuleExports.h`만 include 한다 (`EditorUIContext` / `EditorService.h` 금지).
- 모듈 구현 `.cpp`는 `Export/*ModuleExports.h`만 있으면 테이블 채우기 매크로까지 포함된다.

## 핵심 규칙
- **구현체 없음**: 뼈대(인터페이스 선언)와 타입만 둔다. 동작 코드는 `Engine`, `Editor`, `SWGame` 쪽에 구현한다.
- **ABI 버전**: `GameAPI` 또는 `EditorAPI`의 구조체 레이아웃이나 함수 시그니처를 변경할 때는 해당 `k*APIAbiVersion`을 올린다. 호스트와 모듈은 같은 버전이어야 하며, 다르면 로드를 거부한다.
