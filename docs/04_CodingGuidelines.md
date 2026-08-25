# 📝 Coding Guidelines (코딩 규칙 및 컨벤션)

SW Engine 프로젝트에 기여하거나 새로운 게임 모듈을 작성할 때 반드시 지켜야 하는 코딩 스타일과 네이밍 규칙입니다.
엔진의 통일성과 유지보수성을 위해 매우 엄격하게 적용됩니다.

---

## 1. C++ 네이밍 규칙

| 대상 | 규칙 | 예시 |
| :--- | :--- | :--- |
| **네임스페이스** | `sw` (+ 하위) | `sw`, `sw::editor` |
| **클래스 / 구조체 / enum** | `PascalCase` | `ImGuiEditor`, `TaskManager` |
| **인터페이스** | `I` + `PascalCase` | `IEditor`, `IRHIDevice` |
| **멤버 / 일반 함수** | `camelCase` | `initialize()`, `getRootFolderPath()` |
| **멤버 변수** | `_camelCase` | `_bInitialized`, `_gameRenderTarget` |
| **지역 변수** | `camelCase` | `consolasPath`, `deltaTime` |
| **상수** | `k` + `PascalCase` | `kMaxPathSize`, `kFontSize` |
| **전역 변수** | `gv_` + `camelCase` | `gv_rhiBackend`, `gv_enableVSync` |
| **정적(static) 변수** | `s_` / private은 `_s_` | `s_activeWindow`, `_s_nextObjectId` |
| **매크로** | `SW_SCREAMING_CASE` | `SW_API`, `SW_LOG_INFO` |

### 변수 및 자료구조 특수 접두/접미어
- **포인터(Pointer)**: `p` 접두어 (`_pObject`, `pMember`) / 이중 포인터는 `pp`
- **배열(Array)**: 고정 크기 배열은 `arr` 접두어
- **리스트/벡터(Vector/List)**: 가변 크기 배열은 `list` 접미어 (`childList`, `vertexList`)
- **맵(Map/Dict)**: 연관 컨테이너는 `map` 접두어 (`mapTypeInfos`)
- **셋(Set)**: 고유값 컨테이너는 `unique` 접두어

## 2. C++ 구조 및 작성 규칙

### 헤더 선언 순서
헤더 파일 선언부는 읽기 쉽도록 다음 순서를 엄격히 준수합니다.
1. `public` 멤버 변수
2. 함수: 생성자/소멸자(`ctor`/`dtor`) → `initialize`/`shutdown` → `process` → `getter`/`setter`
3. `private` 함수 (멤버 변수와 구분하여 별도 접근 지정자 선언)
4. `private` 멤버 변수 (클래스 맨 아래 위치)

### `#include` 규칙
1. 헤더(`.h`)에서는 전방 선언(Forward Declaration)을 우선시하며, 불가능할 때만 include 합니다.
2. 헤더(`.h`)에서 ThirdParty 헤더를 직접 include 하지 마세요.
3. 소스(`.cpp`) 인클루드 순서:
   - `#include "pch.h"` (이후 빈 줄)
   - 매칭되는 헤더 (`#include "MyClass.h"`)
   - 같은 Relative Scope 내의 파일들 (빈 줄)
   - 다른 Relative Scope 내의 파일들 (빈 줄)
   - 시스템/OS 전용 헤더 (`Core/Common/StdHeaders.h` 권장) (빈 줄)
   - 외부 ThirdParty 헤더

### 분기문 및 초기화 규칙
- 부울(bool) 타입이 아닌 포인터 등은 명시적으로 `== nullptr` 혹은 `== false` 로 비교하세요. `!_bValid` 보다는 `_bValid == false` 를 권장합니다.
- 생성자에서 멤버를 초기화할 때는 선언 순서대로 정렬해야 하며, 중괄호 `{}` 초기화를 사용하세요.
- 비트 패딩(Byte Padding) 낭비가 발생하지 않도록 변수 선언 순서를 최적화하세요.

## 3. CMake 및 빌드 규칙

| 대상 | 규칙 | 예시 |
| :--- | :--- | :--- |
| Feature option (`-D`) | `SW_*` + `option()` | `SW_ENABLE_PCH`, `SW_USE_VCPKG` |
| 함수 / 매크로 | `sw_camelCase` | `sw_addLibrary` |
| 타겟 프로퍼티 / Compile Def | `SW_SCREAMING_CASE` | `SW_PLATFORM_WINDOWS` |
| 제품 타겟 이름 | `PascalCase` | `App`, `Core`, `SWGame` |

## 4. Python 스크립트 규칙
- **공개 함수**: `camelCase` (`setupEnvironment`)
- **비공개 헬퍼**: `camelCaseInternal` (`safeCallInternal`)
- **모듈 상수**: `kPascalCase` 또는 `_kPascalCase`
- **모듈 / 파일명**: `PascalCase.py` (`SetupEnvironment.py`)

---
[◀ 이전: 핫리로드 및 ABI 가이드](03_LiveReload_and_ABI.md) | [🏠 위키 홈으로 돌아가기](../README.md)
