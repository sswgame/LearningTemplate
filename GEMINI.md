# SW Engine (게임 엔진 템플릿) 개발 가이드 및 규칙

이 문서는 SW Engine 프로젝트의 구조, 빌드 규칙, C++/CMake/Python 네이밍 컨벤션 및 아키텍처 제약 사항을 정의한 AI 및 개발자용 지침입니다.

---

## 1. 프로젝트 개요 및 아키텍처

- **목적**: C++ 기반 게임 및 에디터 엔진 템플릿 (CMake + Ninja + Clang 기반 초고속 빌드).
- **빌드 모드**:
  - **Dev (개발)**: 에디터 툴 포함, 디버깅 및 **모듈 핫리로드(LiveReload)** 지원 (DLL 동적 로드).
  - **Shipping (배포)**: 에디터 코드 제외, 최고 성능을 위해 단일 실행 파일로 정적 링크(STATIC).
- **주요 링킹 및 아키텍처 규칙**:
  - `Source/Core`: 기초 유틸리티 STATIC 라이브러리. `Engine`과 OBJECT 공유 컴파일.
  - `Source/Engine`: 오브젝트, 그래픽스(RHI), 물리, 씬 등 핵심 엔진 (Dev 모드에서 SHARED DLL).
  - `Source/RuntimeAPI`: App ↔ Editor/Game 모듈 간의 순수 C-ABI 통신 규약 (Header-Only `INTERFACE` 라이브러리로, 구현체 없이 계약 기능만 담당).
  - `Source/App`: 순수 진입점 프로그램(exe). Engine + RuntimeAPI만 링크. 내부적으로 `EngineLoop`를 통해 메인 루프를 돌고, `ModuleHost`를 통해 핫리로드 시 게임 모듈의 상태(State) 직렬화 및 비동기 태스크 펜싱 등을 관리하는 얇은 런처(Thin Launcher)입니다.
  - `Source/Editor`: 개발 모드 전용 에디터 모듈.
  - `Source/GameFramework`: 장르별 공통 프레임워크 및 키트.
  - `Source/Games`: 실제 게임 로직 (`Demo`, `Empty` 등). `SW_ACTIVE_GAME` 변수로 빌드 대상 게임 선택.
- **DLL Export / Import (API) 매크로 규칙**:
  - `SW_API`: **Engine.dll**의 심볼을 노출하거나 참조할 때 사용합니다. (`SW_EXPORTS` 매크로에 반응)
  - `SW_MODULE_API`: **동적 모듈 플러그인(Editor.dll, Demo.dll, RHI 백엔드 등)**의 진입점(C-ABI Entry Point)을 노출할 때 공통으로 사용하는 매크로입니다. (`SW_MODULE_EXPORTS`에 반응)
  - `SW_GAMESERVICE_API`: **GameFramework.dll** 같이 다른 모듈들이 참조하여 링크(`.lib`)하는 공용 모듈의 심볼을 노출할 때 사용합니다. (`SW_GF_EXPORTS`에 반응)

---

## 2. 빠른 시작 및 빌드 워크플로우

1. **환경 설정**:
   ```bash
   # Windows PowerShell: py -3 Scripts/setup/SetupEnvironment.py
   # Windows PowerShell: py -3 Scripts/setup/SetupVcpkg.py --install
   # macOS/Linux: python3 Scripts/setup/SetupEnvironment.py
   # macOS/Linux: python3 Scripts/setup/SetupVcpkg.py --install
   ```
2. **CMake 구성**:
   ```bash
   cmake --preset Ninja-Debug
   ```
   *(sccache 사용 시 `-DSW_USE_SCCACHE=ON` 권장)*
3. **빌드**:
   ```bash
   cmake --build --preset Ninja-Debug
   ```
   - 바이너리 출력 위치: `build/Ninja-Debug/Bin`
   - 컴파일 데이터베이스: `build/Ninja-Debug/compile_commands.json`

---

## 3. 네이밍 컨벤션 및 코딩 규칙 (Strict Conventions)

### 3.1 CMake (빌드 설정)
| 대상 | 규칙 | 예시 |
| :--- | :--- | :--- |
| Feature option (`-D`) | `SW_*` + `option()` | `SW_ENABLE_PCH`, `SW_USE_VCPKG` |
| 함수 / 매크로 | `sw_camelCase` | `sw_addLibrary` |
| 타겟 프로퍼티 / compile def | `SW_SCREAMING` | `SW_PLATFORM_WINDOWS` |
| 제품 타겟 이름 | `PascalCase` | `App`, `Core`, `SWGame` |

### 3.2 C++ 코드 컨벤션
| 대상 | 규칙 | 예시 |
| :--- | :--- | :--- |
| 네임스페이스 | `sw` (+ 하위) | `sw`, `sw::editor` |
| 클래스 / 구조체 / enum | `PascalCase` | `ImGuiEditor`, `TaskManager` |
| 인터페이스 | `I` + `PascalCase` | `IEditor`, `IRHIDevice` |
| 멤버 함수 / 일반 함수 | `camelCase` | `initialize()`, `getRootFolderPath()` |
| 멤버 변수 | `_camelCase` | `_bInitialized`, `_gameRenderTarget` |
| 지역 변수 | `camelCase` | `consolasPath`, `deltaTime` |
| 상수 | `k` + `PascalCase` | `kMaxPathSize`, `kFontSize` |
| 전역 변수 | `gv_` + `camelCase` | `gv_rhiBackend`, `gv_enableVSync` |
| 정적(static) 변수 | `s_` / private은 `_s_` | `s_activeWindow`, `_s_nextObjectId` |
| 매크로 | `SW_SCREAMING` | `SW_API`, `SW_LOG_INFO` |

#### C++ 헤더 선언 순서
헤더 파일 작성 시 아래 순서를 엄격히 준수합니다:
1. `public` 멤버 변수
2. 함수: 생성자/소멸자(`ctor`/`dtor`) → `initialize`/`shutdown` → `process` → `getter`/`setter`
3. `private` 함수 (멤버 변수와 구분하여 별도 접근 지정자 선언)
4. `private` 멤버 변수 (클래스 맨 아래 위치)

### 3.3 Python 스크립트 컨벤션
| 대상 | 규칙 | 예시 |
| :--- | :--- | :--- |
| 공개 함수 | `camelCase` | `setupEnvironment`, `setupVcpkg` |
| 비공개 헬퍼 | `camelCaseInternal` | `safeCallInternal`, `exeNameInternal` |
| 모듈 상수 | `k` / `_k` + `PascalCase` | `kPfb1Magic` |
| 모듈 / 파일명 | `PascalCase.py` | `SetupEnvironment.py` |
| JSON config 키 | `snake_case` | `"dxc_dll_path"`, `"vcpkg_root"` |

### C++ include 규칙
1. Header(.h)에서는 forward declaration을 기본적으로하고, 불가능한 경우 인클루드한다.
2. Header(.h)에서는 ThirdParty의 헤더를 직접 인클루드하지 않는다.
3. .cpp에서는 다음 규칙으로 include 한다.
   - #include "pch.h"(이후 한 줄 띄운다.)
   - #include [매칭되는 .h의 헤더]
   - #include [매칭되는 헤더와 같은 Relative Scope의 파일들]

   Source 폴더 기준에서 제일 상위의 폴더 구분이 달라질때마다 한 줄씩 띄어 구분한다.
   - #include [매칭되는 헤더와 다른 Relative Scope의 파일들]
      이 때 파일 스코프가 달라지므로 한 줄을 띄어 구분한다.

   - 아래는 전부 <>를 이용하여 Include 한다.
   - #include [System Include] (기본적으로는 "StdHeaders.h"를 사용하여 한 곳에 모은 것을 이용한다)
      이 때도 한 줄을 띄어 구분한다.
   - #include [OS Specific Include] (기본적으로는 "PlatformOsHeaders.h"를 사용하여 한 곳에 모은 것을 이용한다)
      이 때도 한 줄을 띄어 구분한다.
   - #include [Project Global Include]
      이 때도 한 줄을 띄어 구분한다.
   - #include [ThirdParty Include]
4. 기본적으로 #include는 띄어진 상태에서 정렬하여야하지만, 순서가 반드시 보장된 경우에 한해서 예외를 둔다 (예: #include <imgui.h> 등)

예를 들면 다음과 같다
""" <기존 후>
#include "Engine/Object/Component.h"

#include "Core/Log/Logger.h"
#include "Engine/ECS/Registry.h"
#include "Engine/Object/ComponentDefaults.h"
#include "Engine/Object/GameObject.h"
#include "Engine/Reflection/ReflectionCore.h"
#include <algorithm>
#include <utility>
"""
""" <수정 후> // 여기선 정렬까진 하진 않았으나 정렬도 해야한다.
#include "pch.h"

#include "Engine/Object/Component.h"
#include "Engine/Object/ComponentDefaults.h"
#include "Engine/Object/GameObject.h"
#include "Engine/ECS/Registry.h"
#include "Engine/Reflection/ReflectionCore.h"

#include "Core/Log/Logger.h"
#include "Core/Common/StdHeaders.h"
"""
### 자료형 표현 규칙
1. Types.h의 자료형 표현 규칙을 이용하여 자료형을 사용한다.

### 프로젝트 기능 사용 규칙
1. STL이나 System 기능보단 Core와 Engine에 있는 기능을 우선적으로 사용하며, 불가능할 시 STL/System 기능을 사용하는 것을 고려한다.

### 변수 초기화 규칙
1. 생성자에서 초기화 시 선언 순서로 정렬되어야 한다
2. 생성자가 선언되어 있는 경우 Header(.h)에서 초기화하지 말고 생성자에서 초기화해야한다.
2. ()대신 {}를 이용하여 초기화되어야 한다.
3. : 이후에 변수는 하나씩만 와야하며 다음 줄에 ,로 시작하여 초기화가 되어야한다.
4. 비트 패딩을 이용할 수 있으면 해야하며, 바이트 패딩이 최소화되도록 순서를 맞춘다.

### 분기문 규칙
1. 비교할 때는 bool 자료형이고 true가 아니면 명시적으로 !로 비교하지말고, nullptr/false/값비교를 명시한다..
    ```cpp
    //BAD
    if (!_bValid) 
      return;
    //GOOD
    if (_bValid == false) 
      return;
    ```
2. if문 안의 코드가 1줄인 경우 괄호를 생략한다.
   - 단 else/else if문과 존재한다면, 해당 블록들도 1줄이 아니면 괄호를 사용한다.
3. if문 초기화는 사용하지 않는다.
4. 조건이 여러 개일 때, 뜻이 불명확해보이거나, 3개 이상이면 지역변수로 조건 내용을 정의하여 사용한다.

### auto 사용 규칙
1. iterator 또는 structured binding과 같이 표현이 복잡한 경우에만 사용한다.

## 변수 명명 규칙
1. raw 포인터 접근인 경우 접두어로 p를 붙인다.
   - 멤버 변수인 경우 _pMember 형식이 된다.
   - 로컬 변수인 경우 pMember 형식이 된다.
   - 이중 포인터인 경우 pp가 된다.
   - 삼중 포인터 이상은 없어야한다. (있다면 구조를 의심해야한다)
2. 연관 컨테이너인 경우, map 접두어를 붙인다.
3. 고정 크기 배열인 경우([],array 등인 경우) arr 접두어를 붙인다.
4. 가변 크기 배열 또는 리스트(vector, list, deque 등)인 경우 list 접미어를 붙인다.
5. 키 또는 값이 유일함이 보장되는 컨테이너(set/unordered_set 등)은 unique 접두어를 붙인다.
6. loop문에서 i,j,k 등을 의미를 알 수 없는 인덱싱 변수 명칭은 금한다. (최소 index)
7. 변수명은 의미가 명확하게 기술한다. 예) EditorTable* et 같이 쓰면 안된다. EditorTable* editorTable로 기술한다.

### 코드 스타일 규칙
1. 코드 스타일은 .clang-format을 준수한다.

### 람다 사용 규칙
1. 람다의 사용은 성능상의 이점이 있지 않는 한 자제한다.

### const 사용 규칙
1. 성능상 이점이 있는 경우가 아니면 const를 붙일 수 있는 곳은 다 붙인다.

