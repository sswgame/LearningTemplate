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
| **출력 매개변수 (Out Param)** | `out` + PascalCase / 포인터는 `pOut`, 이중 포인터는 `ppOut` | `outConfig`, `pOutBuffer`, `ppOutObject`, `outListItem` |

### 변수 및 자료구조 특수 접두/접미어
- **포인터(Pointer)**: `p` 접두어 (`_pObject`, `pMember`) / 이중 포인터는 `pp` (`_ppMember`, `ppMember`) — 삼중 포인터 이상(`ppp`, `_ppp`, `***`)은 구조적 결함이므로 엄격히 금지
- **배열(Array)**: 고정 크기 배열은 `arr` 접두어 (`arr`, `_arr`)
- **리스트/벡터(Vector/List)**: 가변 크기 배열은 `list` 접두어 (`list`, `_list`) — `List` 접미어 사용 금지 (단, `byte` 단어가 포함된 바이트 버퍼 `vector<uint8>`, `vector<int8>`, `vector<utf8>` 등은 `list` 접두어 생략: `_bytes`, `_rawBytes`, `bytes`, `outBytes`, `pOutBytes`, `pOutBuffer`)
- **맵(Map/Dict)**: 연관 컨테이너는 `map` 접두어 (`map`, `_map`)
- **셋(Set)**: 고유값 컨테이너는 `unique` 접두어 (`unique`, `_unique`, 예: `_uniqueIds`, `_uniqueTags`) — 유일하게 복수형 허용
- **단수형 명명 규칙 (unique 제외 복수형 금지)**: `unique` 접두어를 제외한 모든 컨테이너(`list`, `map`, `arr` 등) 및 매개변수는 **복수형(`...s`)이 엄격히 금지되며 단수형(Singular)만 사용**해야 합니다 (예: `_listActor`, `_listItem`, `_mapIdToName`, `outListItem`, `outListHandle`. 단, `unique` 접두어(`_uniqueIds`, `outUniqueIds`) 및 원시 바이트 데이터(`_bytes`, `bytes`, `outBytes`)는 예외).
- **출력 매개변수(Out Parameter)**: 기본적으로 `out` 접두어로 시작하며 컨테이너 접두어는 `out` 뒤에 위치함. 단, 포인터(`p`/`pp`)의 경우에만 예외적으로 `p`/`pp`가 `out` 앞에 위치함:
  - 일반 출력: `out` + PascalCase (`outConfig`, `outResult`, `outX`, `outSpawnX`)
  - 단일 포인터 출력 (예외적 p 선행): `pOut` (`pOutBuffer`, `pOutApi`, `pOutMatrix`, `pOutResult`), 입출력 포인터는 `pInOut` (`pInOutSize`)
  - 이중 포인터 출력 (예외적 pp 선행): `ppOut` (`ppOutBuffer`, `ppOutObject`, `ppOutInstance`), 입출력 이중 포인터는 `ppInOut`
  - 가변 컨테이너 출력: `outList` (`outListItem`, `outListBuffer`, `outListHandle`) — `listOut`, `out...List` 및 복수형(`outListItems`) 사용 금지 (`byte` 단어가 포함된 바이트 벡터는 `outBytes`, `outRawBytes` 등 `list` 생략 가능)
  - 맵 컨테이너 출력: `outMap` (`outMapData`, `outMapLookup`) — `mapOut` 사용 금지
  - 고유 셋 출력: `outUnique` (`outUniqueIds`, `outUniqueTags`) — `uniqueOut` 사용 금지 (`unique`는 복수형 허용)
  - 고정 배열 출력: `outArr` (`outArrBuffer`) — `arrOut` 사용 금지
  - 입출력 겸용(In/Out): `inout` / `pInOut` / `ppInOut` 접두어 사용 (`inoutSkeleton`, `pInOutSize`)

### DLL Export / Import (API) 매크로 규칙
- `SW_API`: **Engine.dll**의 심볼 노출 및 참조 (`SW_EXPORTS` 정의에 반응)
- `SW_MODULE_API`: **동적 모듈 플러그인(Editor.dll, Demo.dll, RHI 백엔드 등)**의 진입점 C-ABI 노출 (`SW_MODULE_EXPORTS`에 반응)
- `SW_GF_API`: **GameFramework.dll** 클래스 심볼 노출 및 참조 (`SW_GF_EXPORTS`에 반응)
- `SW_GAMESERVICE_API`: RuntimeAPI GameService 로케이터(`bindGameService` / `getRawService`)용 매크로

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
- 비트 필드(bit field) 플래그(예: `uint8 _bFlag : 1;`)는 `true`/`false` 대신 `SW_TRUE`(1) / `SW_FALSE`(0)를 사용하여 대입 및 비교합니다.
- 생성자에서 멤버를 초기화할 때는 선언 순서대로 정렬해야 하며, 중괄호 `{}` 초기화를 사용하세요. 한 줄에 1개 멤버씩 초기화하며 다음 줄에 `,`로 시작합니다.
- 범위(Range) 비교 시 변수를 안쪽(중간)에 위치하도록 작성하여 수학적 범위 표기법($min \le value \ \&\&\ value \le max$)을 따릅니다 (`kMin <= value && value <= kMax`).
- 비트 패딩(Byte Padding) 낭비가 발생하지 않도록 변수 선언 순서를 최적화하세요.

### 헬퍼 Util vs Internal
1. 여러 번역 단위가 공유하는 헬퍼는 `XxxUtil` 정적 구조체 헤더로 선언합니다 (`Internal` 이름을 붙이지 않음).
2. 단일 `.cpp` 내에서만 사용하는 헬퍼는 클래스 구현과 분리된 별도 `namespace sw { namespace { struct FooInternal; } }` 블록에 배치합니다.

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
