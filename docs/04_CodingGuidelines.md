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

### 전역 변수는 두 종류
- 에디터에서 바꿀 런타임 설정은 `SW_GLOBAL_VARIABLE_*` 로 선언합니다.
- 벤치 · 자동화 · 진단 스위치는 `SW_TEST_GLOBAL_VARIABLE_*` 로 선언합니다. `-gv_*` 로는 그대로 정할 수 있지만 에디터 패널 · 프리셋에 보이지 않고
  **Shipping 에는 등록되지 않습니다.** 스크립트가 배포 실행 파일을 그 변수로 몰아야 할 때만 마지막 인자로 `SW_KEEP_IN_SHIPPING` 을 줍니다
  (`gv_profileFrames`, `gv_screenshot*`, `gv_crashTest`, `gv_bench*`).
- `extern` 은 같은 인자의 `SW_EXTERN_…` 판으로 씁니다. 어긋나면 `CheckGlobalVariableKinds.py` 가 막습니다(`Core/GlobalVariable/GlobalVariableManager.h`).

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

### 함수 이름 어휘 — 한 개념에 이름 하나

같은 일을 하는 함수가 두 이름(`queryAABB` 와 `queryAabb`, `alloc*` 과 `allocate*`)을 가지면 읽는 사람은 어느 쪽이 맞는지 알 수 없고,
다음 사람은 방금 본 쪽을 따라 쓴다. 그렇게 갈라진다. `Scripts/lint/gate/CheckFunctionVocabulary.py` 가 헤더 선언에서 다섯 가지를 강제한다 —
두문자어 달리기(`AcronymRun`), 금지 동사(`BannedVerb`), `check*` 술어(`CheckVerb`), `string_view`/`hashed_string` 이름 쌍(`NamePair`),
맨이름 게터(`BareGetter`). 아래 4) `on*` 규칙과 축약어 규칙은 리뷰가 지킨다.

**1) 두문자어는 camelCase 낱말 하나다.** `initRhi`, `queryAabb`, `bindComputeUav`, `exportGameApi`,
`updateUi`, `isValidUtf8`, `parseUint64`. 대문자가 연달아 붙으면 낱말 경계가 사라진다 — `getRHIFormatBlockInfo`
는 `RHIF` 에서 눈이 멈춘다. **타입 이름은 대상이 아니다**(`IRHIDevice` · `AABB` · `TagID` 는 그대로다).
규칙이 보는 것은 camelCase 식별자뿐이다.

**2) 한 개념에 동사 하나.**

| 개념 | 쓰는 동사 | 쓰지 않는 것 |
| :--- | :--- | :--- |
| 객체를 살리고 내린다 | `initialize` / `shutdown` | `setup`, `startup`, `cleanup`, `teardown` |
| 메모리·슬롯을 내주고 돌려받는다 | `allocate` / `free` | `alloc`, `dealloc`, `dispose` |
| 새 값을 만들어 돌려준다 | `create`(소유) · `make`(값) | `build`, `construct`, `generate` |
| 찾는다 | `get`(반드시 있다) · `find`(없을 수 있다) | `fetch`, `retrieve`, `lookup`, `obtain` |
| 입력에서 값을 셈한다 | `compute` | `calculate`, `calc` |
| GPU 리소스 수명 | `initRhi` / `updateRhi` / `releaseRhi` / `forgetRhi` / `isRhiValid` | 그 밖의 모든 것 |

**이름은 `hashed_string` 하나로 받는다.** 문자열 리터럴은 암묵 변환된다(`isActionDown( "Jump" )`). 포인터·`string_view`·
`string` 은 explicit 이라 동적 텍스트를 intern 하는 자리는 호출부에 `hashed_string( text )` 로 보인다. 같은 이름에 `string_view`
판을 나란히 두지 않는다 — 매개변수 수가 같으면 리터럴 호출이 모호하다(린트 `NamePair`). intern 하면 안 되는 조회(키가 아닐
수 있는 텍스트)는 이름을 따로 갖는다: `findStringByText( string_view )`. `setX()` 와 짝인 게터는 `getX()`/`isX()` 이지 맨이름
`x()` 가 아니다(린트 `BareGetter`). `hashed_string` 은 언리얼 `FName` 규칙이다 — `==` · `getHash()` 는 대소문자를 무시하고,
`c_str()` 은 적은 철자 그대로다("이름이 바뀌었나" 는 `isEqual( other, NameCase::CaseSensitive )`). `operator<` 는 없다 — 사람 · 파일이
보는 정렬은 `HashedStringLexicalLess`, 찾기용은 `HashedStringFastLess`(intern 순서 — 실행마다 다르다).

**3) 술어는 질문처럼 읽힌다.** `is` / `has` / `was` / `can` / `should` 로 시작하거나 3인칭 동사를 쓴다
(`supportsX`, `usesX`, `requiresX`, `matchesX`, `allowsX`, `overlapsX`). **`check*` 는 술어가 아니다** —
bool 을 돌려주면 `is*`/`has*` 이고, void 로 단언하면 `assert*` 다. `setX()` 와 짝인 게터는 `getX()` /
`isX()` 이지 맨이름 `x()` 가 아니다.

**4) `on*` 은 "일어났다" 는 알림이다.** 핸들러를 **등록**하는 함수가 아니다. 등록은 `register*` /
`unregister*` 다 — `GameStrings::onLanguageChanged` 가 핸들 값을 돌려주고 있던 것이 이 규칙이 생긴 이유다.

**축약어는 이 저장소의 타입 이름이 줄여 쓸 때만 쓴다.** `XmlNode::attr()` 은 옆에 있는 타입이
`XmlAttribute` 라서 틀렸고(`attribute()` 로 고쳤다), `TagQueryExpr::…Expr` 과 `ShaderEngineCbMember` 의
`…Cb…` 는 타입이 같은 약어를 들고 있으므로 맞다.

### DLL Export / Import (API) 매크로 규칙
- `SW_API`: **Engine.dll**의 심볼 노출 및 참조 (`SW_EXPORTS` 정의에 반응)
- `SW_MODULE_API`: **동적 모듈 플러그인(EditorModule.dll, SWGame.dll, RHI 백엔드 등)**의 진입점 C-ABI 노출 (`SW_MODULE_EXPORTS`에 반응)
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
4. 플랫폼마다 다른 include 는 무조건 include 들 **뒤의 `#if SW_PLATFORM_WINDOWS / #elif … / #endif` 사슬 하나**에 둡니다. 갈래 안에서는 프로젝트 헤더, 빈 줄,
   시스템 헤더 순입니다. 시스템 헤더만 따로 같은 조건의 블록을 하나 더 열지 않습니다 — `CheckIncludeOrder.py` 가 같은 조건 계열의 include 블록 둘을 위반으로 냅니다.

### 분기문 및 초기화 규칙
- 본문이 한 줄인 `if` 는 중괄호를 생략합니다. `else` / `else if` 가 붙은 사슬은 **모든 갈래가 한 줄일 때만** 생략하고, 한 갈래라도 여러 줄이면 전부 중괄호를 유지합니다.
- 반복문(`for` / `while` / `do`)은 본문이 한 줄이어도 **항상 중괄호를 유지**합니다. `Scripts/lint/fixer/FormatBranchBraces.py` 가 `if` 계열만 정리하며, clang-format 의 `RemoveBracesLLVM` 은 반복문까지 벗겨내므로 쓰지 않습니다.
- enum 을 `switch` 할 때 **모든 열거자를 다루면 `default:` 를 두지 않습니다**(LLVM 코딩 표준). 그래야 열거자를 늘리고 `case` 를 빠뜨리면 `-Werror=switch` 가 빌드를 세웁니다. 다 다뤘는데 `default:` 가 있으면 그 검사가 꺼지므로 그것 자체가 오류입니다(`-Werror=covered-switch-default`). 일부 열거자만 다루는 switch 는 `default:` 를 쓰고 나머지를 나열하지 않습니다(`-Wswitch-enum` · `-Wswitch-default` 는 끕니다). 범위 밖 값은 들어오는 자리(역직렬화가 모르는 열거자를 거절)에서 막고, 모든 `case` 에서 반환하는 함수는 switch 뒤에 폴백을 반환합니다. 파일마다 `#pragma` 로 switch 경고를 바꾸지 않습니다.
- `switch` 의 `case` / `default` 는 본문이 **두 문장 이상이면 중괄호를 씌우고**, 한 문장이면 씌우지 않습니다. `break;` 도 한 문장으로 세므로 `case A:` 아래에 문장 하나와 `break;` 가 오면 중괄호를 씌우며, `case A: return X;` 는 그대로 둡니다. `break;` 는 중괄호 **안**에 둡니다. 본문에 전처리기 지시문이 끼어 있으면 건드리지 않습니다 — 본문의 끝이 글자만으로 정해지지 않아 여는 중괄호와 닫는 중괄호가 `#if` 의 반대편에 놓일 수 있습니다. 같은 스크립트가 자동 정리하며, clang-format 의 `InsertBraces` 는 case 라벨을 보지 않아 이 규칙을 표현하지 못합니다.
- 부울(bool) 타입이 아닌 포인터 등은 명시적으로 `== nullptr` 혹은 `== false` 로 비교하세요. `!_bValid` 보다는 `_bValid == false` 를 권장합니다.
- `uint8` 불리언 멤버(`_b*` — 비트필드 `uint8 _bFlag : 1;` 이든 아니든)는 `true`/`false` · 맨 `1`/`0` 대신 `SW_TRUE` / `SW_FALSE` 로 대입 · 비교합니다(`_bFlag = SW_TRUE;`, `if (_bFlag == SW_FALSE)`). 맨 `1` 은 개수로 읽히고, 매크로는 상태라고 말합니다. 진짜 `bool` 멤버는 `true`/`false` 그대로입니다. `CheckCodeConventions.py` 의 `Style/BitfieldBoolean`(전체 스캔 전용)이 검사합니다.
- 생성자가 있으면 필드는 헤더가 아니라 생성자에서 초기화합니다. 초기값은 **한 곳에만** 둡니다 — 두 곳에 쓰면 어느 쪽이 이기는지(생성자) 가려지고 한쪽만 고치게 됩니다(`Style/HeaderMemberInitializer`, 전체 스캔 전용). 헤더 기본값이 유일한 자리인 셋은 예외입니다: 기본 생성자가 `= default` 이거나 헤더 인라인인 클래스, 위임 생성자(`: Self( ... )` 는 멤버 초기화를 가질 수 없다), 생성자가 아예 없는 타입.
- 생성자에서 멤버를 초기화할 때는 선언 순서대로 정렬해야 하며, 중괄호 `{}` 초기화를 사용하세요(반복자 쌍만은 소괄호 — `_listValue( list.begin(), list.end() )`. 중괄호면 `initializer_list` 생성자가 골라져 반복자 둘이 원소로 담긴다, `Style/IteratorPairBraces`). 한 줄에 1개 멤버씩 초기화하며 다음 줄에 `,`로 시작합니다.
- 기본 초기화가 값을 정하지 않는 필드(정수 · 실수 · `bool` · 열거형 · 포인터 · 그 배열 · `atomic<스칼라>` · 비트필드)는 값이 **어딘가에** 있어야 합니다. 초기화 목록을 가진 생성자(복사 · 이동 생성자 포함)는 그 필드를 전부 목록에 두거나, 그 필드에 헤더 기본값이 있어야 합니다. 기본 생성자가 `= default` 인 클래스는 헤더 기본값을 주고, 비트필드는 C++17 에서 헤더 기본값을 가질 수 없으니 생성자를 씁니다. `-Wreorder-ctor` 와 `Style/ConstructorOrder` 는 목록에 **있는** 필드의 순서만 봅니다 — 빠진 필드는 쓰레기 값으로 시작하고 아무도 알아채지 못합니다. 버퍼로 쓰는 바이트 배열(`utf8 _arrStaticBuffer[N]`)은 예외입니다. `CheckCodeConventions.py` 의 `Style/ConstructorInitializesEveryField`(전체 스캔 전용)가 검사하며, 글자로 판정할 수 없는 타입(구조체 · 뜻이 둘인 별칭)은 추측하지 않고 건너뜁니다.
- 범위(Range) 비교 시 변수를 안쪽(중간)에 위치하도록 작성하여 수학적 범위 표기법($min \le value \ \&\&\ value \le max$)을 따릅니다 (`kMin <= value && value <= kMax`).
- 비트 패딩(Byte Padding) 낭비가 발생하지 않도록 변수 선언 순서를 최적화하세요.
- `if` 초기화문(`if ( auto x = ...; x )`)을 쓰지 않습니다. 뜻이 분명하지 않거나 세 부분 이상인 조건은 지역 변수로 이름을 붙인 뒤 비교합니다.
- `auto` 는 반복자 · 구조적 바인딩 · 그만큼 복잡한 타입에만 씁니다. 람다는 성능상 이득이 있을 때만 씁니다. `const` 는 성능을 해치지 않는 한 붙일 수 있는 곳에 붙입니다.
- 버퍼 · 경로 크기를 숫자로 적지 않습니다(`char buf[64]`, `fixed_string<256>`, `StringBuilder<32>`). `constant` 네임스페이스(`Core/Common/Defines.h`)의 `kMaxBuffer16` ~ `kMaxBuffer8192`, 파일 경로는 `kMaxPathSize` 를 씁니다.
- 타입은 `Types.h` 의 별칭을 쓰고, 쓸 수 있는 Core · Engine 기능이 있으면 STL · 시스템 기능보다 그것을 씁니다.

### 플랫폼 · 아키텍처 · 컴파일러는 SW_ 매크로로 묻기
- 플랫폼 · 아키텍처 · 컴파일러는 CMake 가 판정해 정의하는 매크로로만 묻습니다. 컴파일러 내장 매크로(`_WIN32` · `__linux__` · `_MSC_VER` ·
  `__clang__` · `__GNUC__` · `_M_X64` · `__x86_64__` · `__aarch64__` …)를 직접 읽지 않습니다 — 언리얼의 `PLATFORM_COMPILER_CLANG` ·
  `PLATFORM_CPU_X86_FAMILY` 와 같은 방식입니다.

  | 묻는 것 | 매크로 | 정의하는 곳 |
  | :--- | :--- | :--- |
  | 플랫폼 | `SW_PLATFORM_WINDOWS` · `SW_PLATFORM_LINUX`(macOS 는 지원하지 않는다) | `cmake/Modules/Platform/` |
  | 아키텍처 | `SW_X64` · `SW_ARM64` | `cmake/Modules/Architecture/` (컴파일러가 겨냥하는 아키텍처로 판정) |
  | 컴파일러 | `SW_COMPILER_CLANG`(clang-cl 포함) · `SW_COMPILER_MSVC`(cl.exe) · `SW_COMPILER_GCC` | `cmake/Modules/Compiler/` |

- clang-cl 은 `__clang__` 과 `_MSC_VER` 를 둘 다 정의합니다. 그래서 "MSVC 확장(`__forceinline` · `__declspec` · MS intrinsic ·
  `__FUNCSIG__`)을 쓸 수 있는가" 는 `SW_COMPILER_MSVC` 가 아니라 `SW_PLATFORM_WINDOWS` 로 묻습니다. Windows 는 MS ABI 툴체인
  (cl · clang-cl)으로만 짓습니다. "Clang 또는 GCC 의 `__builtin_*`" 는 `SW_COMPILER_CLANG || SW_COMPILER_GCC` 입니다.
- 엔진은 64 비트(x64 · arm64)만 짓습니다. 32 비트 갈래(`_M_IX86` · `__i386__` · `__arm__`)는 두지 않습니다.
- 내장 매크로를 읽는 곳은 `Source/Core/Common/TargetMacroCheck.h` 하나뿐입니다. CMake 판정이 실제 컴파일러와 어긋나거나 매크로가
  빠지면 그 헤더의 `#error` 로 빌드가 섭니다. `CheckTargetMacros.py` 가 Source · Test · Tools/ReflectionParser 에서 검사합니다.

### 이미 잡아 둔 메모리에 객체 만들기
- placement new 는 `sw_placement_new( pMemory ) T( ... )` 로 씁니다(`Core/Memory/Memory.h`). 맨 `new ( pMemory ) T( ... )` 는 쓰지 않습니다.
- 매크로는 주소를 `void*` 로 바꾸는 캐스트를 드러냅니다. T 가 포인터 타입이면(`vector<char*>` 등) `char**` → `void*` 같은 변환이 조용히 일어나기 때문입니다. 또 표기가 하나뿐이어야 매크로 한 곳만 고쳐도 전체에 반영됩니다.
- `CheckCodeConventions.py` 가 `Style/PlacementNew` 로 검사합니다.

### 힙 할당은 sw 할당자로
- 맨 `new T` · `new T[n]` 은 쓰지 않습니다. 객체는 `sw_new T( ... )` · `make_unique<T>`, 배열은 `sw_new_array<T>( n )` + `sw_delete_array( p, n )` · `make_unique<T[]>( n )`(원소 소멸자가 없는 타입) · `vector<T>` 로 잡습니다. CRT `new` 는 메모리 태그와 누수 검사에 보이지 않습니다. `CheckCodeConventions.py` 가 `Style/RawNew` 로 검사합니다.

### 오브젝트 · 컴포넌트 참조 — 빌리기는 포인터, 보관은 핸들
- 내가 소유하지 않은 `GameObject` / `Component` 를 가리키는 `T*` 는 **지금 부른 함수 안에서만**(길어야 이번 프레임) 씁니다.
- 프레임을 넘겨 드는 참조(멤버 · 선택 목록 · 되돌리기 기록)는 `GameObjectHandle` / `ComponentHandle`(`Core/Container`)로 들고, 쓸 때마다 소유 매니저의 `resolveGameObject` / `resolveComponent` 로 풉니다. 대상이 사라졌으면 nullptr 입니다.
- 핸들은 다시 쓰지 않는 id 라 이름을 바꿔도 끊기지 않습니다. 에디터 되돌리기 · 플레이 세션 복원 · 핫 리로드는 오브젝트를 원래 id 로 되살립니다.
- objectId 는 프로세스 전체에서 겹치지 않지만, 핸들은 그 오브젝트를 가진 `GameObjectManager`(씬)로만 풉니다.
- 오브젝트 모델이 스스로 관리하는 구조 링크(소유자 · 씬 계층 · 등록부)는 생포인터 그대로 둡니다.
- 이름으로 가리키는 참조는 두지 않습니다 — 이름을 바꾸면 끊기고, 같은 이름의 새 오브젝트가 생기면 조용히 그쪽을 가리킵니다.
- 저장된 상태도 다른 오브젝트를 이름으로 가리키지 않습니다. 부모 참조는 부모의 id 이고, 배치의 모든 오브젝트를 읽은 뒤 `ObjectStateBatch` 가 풉니다.
  오브젝트 상태를 읽는 새 경로는 배치(`ObjectLoadContext::_pBatch` + `finish()`)를 지납니다 — 매니저가 이름을 고유하게 바꾸므로 이름으로 찾으면 동명이인에 떨어집니다.

### 헬퍼 Util vs Internal
1. 여러 번역 단위가 공유하는 헬퍼는 `XxxUtil` 정적 구조체 헤더로 선언합니다 (`Internal` 이름을 붙이지 않음).
2. 단일 `.cpp` 내에서만 사용하는 헬퍼는 클래스 구현과 분리된 별도 `namespace sw { namespace { struct FooInternal; } }` 블록에 배치합니다.
3. `Internal` 헬퍼 이름은 **클래스가 아니라 번역 단위**를 따릅니다. 유니티 빌드(`SW_ENABLE_UNITY_BUILD`, `CI-*` 프리셋)는 `.cpp` 여럿을 한 번역 단위로
   묶고 익명 네임스페이스는 번역 단위마다만 이름을 숨기므로, 한 클래스를 여러 `.cpp` 로 나눈 뒤 헬퍼마다 클래스 이름을 붙이면 재정의 오류가 납니다
   (`VulkanRHIResourcePipeline.cpp` 는 `VulkanRHIResourcePipelineInternal`). `CheckCodeConventions.py` 의 `Naming/DuplicateInternalHelper`(전체 스캔 전용)가 검사합니다.

### 익명 네임스페이스는 파일당 하나, 스코프 최상단에

1. `.cpp` 의 익명 네임스페이스는 **최대 하나**이고, 그것을 감싸는 네임스페이스의 맨 위에 둔다
   (`SW_LOG_CALLER` 가 있으면 그 바로 아래). 번역 단위 지역인 것 — 헬퍼 구조체 · 상수 · `s_*` 상태 ·
   free 함수 — 은 전부 그 안에 넣는다.
2. **free `static` 함수를 쓰지 않는다.** 익명 네임스페이스가 이미 내부 링키지를 주므로, 안으로 옮길 때
   `static` 키워드는 뗀다.
3. **위로 올리면 빌드가 깨질 수 있다.** 블록은 자신이 쓰지 않는 것들 위에만 놓을 수 있다. 참조하는
   타입 정의·상수·`s_*` 변수를 지나쳐 올리면 컴파일 오류다 — 옮긴 뒤 반드시 빌드한다. 그 의존 대상이
   자신도 번역 단위 지역(파일 스코프 `static`)이면 **블록 안으로 같이** 옮긴다.
4. 정당한 예외 넷 (전부 이유가 있다):
   - 상호 배타적인 전처리 분기(`#if SW_PLATFORM_WINDOWS` / `#elif SW_PLATFORM_LINUX`)의 블록들은
     실제 번역 단위에는 하나뿐이다. 합치지 않는다.
   - `REFLECT` 코드젠 타입은 익명 네임스페이스로 못 옮긴다 — 생성된 `.gen.cpp` 가 한정 이름
     (`sw::MockMeshComponent`)으로 참조한다.
   - `SW_GLOBAL_VARIABLE_*` 은 `extern` 을 붙여 **외부 링키지를 의도**하므로 밖에 둔다.
   - `main` 과 헤더에 선언된 함수는 네임스페이스 스코프에 그대로 둔다.
5. **익명 네임스페이스 바로 안의 상수 이름은 다른 `.cpp` 와 겹치지 않게 한다.** 유니티 빌드(`CI-*`)는 `.cpp` 를 한 번역 단위로
   묶으므로 두 파일의 `constexpr int32 kLimit` 이 재정의로 충돌한다. 그 TU 의 `XxxInternal` 구조체 안 `static constexpr` 로 옮기거나
   이 파일에만 있는 이름을 쓴다. 구조체 · 함수 안의 상수는 상관없다(`Naming/DuplicateAnonymousConstant`, 전체 스캔에서만).


### 리소스 에셋
- `Resource/` 아래 파일 · 폴더 이름은 전부 소문자(`[a-z0-9_.-]+`)입니다(문서 `README.md` 만 예외). `CheckResourceCasing.py` 와 커밋 훅이 검사합니다.
- 런타임은 텍스처를 DDS 로만 읽습니다. 런타임 텍스처 폴더(`textures/`)에는 `.dds` 와 데이터(`.sprite.json` · `.meta`)만 두고, 원본 이미지(PNG · JPG · TGA …)는
  같은 상대 경로의 `textures_raw/` 에 두어 `App --bake-textures` 로 굽습니다(에디터는 핫 리로드 때도 굽습니다). DDS 는 `textures_raw/bake.stamp` 와
  같이 커밋합니다. 원본 · 굽기 규칙 · DDS 가 어긋나면 `TextureBakeStampTest`(와 `App --check-textures`)가 집니다. 아무도 참조하지 않는 원본은
  옮기지 말고 지웁니다. `CheckTextureFolders.py` 가 검사합니다.

## 3. CMake 및 빌드 규칙

| 대상 | 규칙 | 예시 |
| :--- | :--- | :--- |
| Feature option (`-D`) | `SW_*` + `option()` | `SW_ENABLE_PCH`, `SW_USE_VCPKG` |
| 함수 / 매크로 | `sw_camelCase` | `sw_configurePch`, `sw_addGameModule` |
| 타겟 프로퍼티 / Compile Def | `SW_SCREAMING_CASE` | `SW_PLATFORM_WINDOWS` |
| 제품 타겟 이름 | `PascalCase` | `App`, `Core`, `SWGame` |

## 4. Python 스크립트 규칙
- **공개 함수**: `camelCase` (`setupEnvironment`)
- **비공개 헬퍼**: `camelCaseInternal` (`safeCallInternal`)
- **모듈 상수**: `kPascalCase` 또는 `_kPascalCase`
- **모듈 / 파일명**: `PascalCase.py` (`SetupEnvironment.py`)

## 5. HLSL 셰이더 규칙
셰이더도 HLSL 이 표현할 수 있는 데까지 **C++ 규칙을 그대로** 따른다. `Scripts/lint/gate/CheckShaderConventions.py` 가
`Resource/` 아래 모든 `.hlsl` · `.hlsli` 에 이 절을 적용한다(CI 게이트 · 커밋 훅).

| 대상 | 규칙 | 예시 |
| :--- | :--- | :--- |
| 공유 헤더(`.hlsli`) 함수 | `sw` + camelCase — FXC 에 네임스페이스가 없어 `sw::` 대신 | `swLoadInstance`, `swComputeWorldNormal`, `swLoadRwTexture2D` |
| 한 파일(`.hlsl`) 함수 | camelCase, 접두어 없음 | `isVisible`, `hashSeed` |
| 진입점 | `VSMain` · `PSMain` · `CSMain` 그대로 (컴파일러 · 파이프라인 XML 이 문자열로 부른다) | |
| 공유 헤더 타입 | `Sw` + PascalCase, `_t` 꼬리 없음 | `SwInstanceData`, `SwMaterialData`, `SwRootConstantsData` |
| 한 파일 타입 | PascalCase, `Sw` 없음. C++ 구조체를 비추면 C++ 타입 이름 그대로 | `PSInput`, `GpuBatchInfo`, `RHIDrawIndirectCommand` |
| 필드 | camelCase. C++ 멤버를 비추면 그 이름에서 `_` 를 뺀 것 | `startVertexLocation`(`_startVertexLocation`), `position`, `worldPosition` |
| 지역 변수 · 매개변수 | camelCase, 읽히는 이름 | `vertexId`, `instance`, `planeIndex` |
| `out` · `inout` 매개변수 | `out` · `inout` 접두어 | `outPosition`, `outNeighborDepth` |
| 고정 배열 지역 변수 | `arr` + 단수 명사 | `arrOffset[4]`, `arrPosition[6]` |
| `groupshared` | `s_` (배열은 `s_arr`) | `s_arrKey[]`, `s_arrId[]` |
| 파일 수준 `static const` | `kPascalCase` | `kInvalidIndex`, `kPi` |
| `#define` | `SW_` + 대문자. 리소스 · 함수를 매크로로 별칭하지 않는다 | `SW_MATERIAL_BEGIN`, `SW_SORT_MAX_ELEMENTS` |
| include 가드 | `SW_<도메인>_<파일>_HLSLI` | `SW_ENGINE_POSTBLOOM_HLSLI` |
| 전역 리소스 · cbuffer 멤버 | `g_` + PascalCase — **C++ 가 이름으로 묶는다** | `g_ViewProj`, `g_SwInstances`, `g_SceneDepthIndex` |

- **C++ 함수 이름 규칙이 그대로 적용된다.** 약어는 한 단어(`RW` → `Rw`, `ID` → `Id`), 줄임말은 풀어 쓴다(`Tex` → `Texture`,
  `Cmp` → `Comparison`, `pos` → `position`, `nrm` → `normal`, `col` → `color`, `vid` → `vertexId`). 입력에서 값을 구하는 함수는
  `…Of` 꼬리가 아니라 `compute…` 다(`SwWorldNormalOf` → `swComputeWorldNormal`). GPU 접근 동사는 버퍼 · RW 텍스처 원소에
  `load` / `store`, 텍스처 읽기에 `sample` / `gather` 다.
- **읽히는 이름.** 한 글자 · `i` `j` `k` · `idx` `inst` `ao` `dtid` 같은 줄임말을 쓰지 않는다. HLSL 키워드(`sample`, `point`,
  `line`, `linear`, `texture`, `sampler`, `vector`, `matrix` …)를 이름으로 쓰지 않는다. 지역 `const` 는 C++ 처럼 `kPascalCase` 도 된다.
- **문자열로 묶인 이름은 스타일 때문에 바꾸지 않는다.** 전역 · cbuffer 멤버(`g_*` — `shaderslot::resname` · `cbname`,
  `PassConstantNames`, `g_<이름>Index` 레지스트리 규약), cbuffer 이름(`PassCB` · `CullParams` · `SwRootConstants` …), 시맨틱,
  밖에서 넣는 define(`DX11` · `SW_PASS_*` · `MATERIAL_*` …), `bindingslots.hlsli` 의 매크로(C++ 가 같은 파일을 include 한다),
  머티리얼 구조체 필드(`.material` 이 이름으로 채운다), 셰이더 파일 이름이 그렇다. 그래서 `g_*` 에는 컨테이너 · 단수 · 줄임말
  규칙을 적용하지 않는다. 규칙에서 빼야 하는 이름은 게이트의 `kStringBoundName` 에 **이유와 함께** 적는다(지금은 진입점 셋).
- **문자열로 묶인 이름을 바꾸려면 같은 커밋에서 C++ 도 바꾼다.** `ShaderBindingContract::validate` 는 계약 표에 없는 리플렉션
  이름을 조용히 건너뛰어서, 셰이더 쪽만 이름을 바꾸면 그 리소스의 검사가 아무 말 없이 꺼진다.
  `ShaderBindingContractTest.EveryBoundNameIsInBakedReflection` 이 반대 방향(C++ 가 아는 이름이 구운 `reflection.manifest`
  에 있는가)을 봐서 그 개명을 실패로 만든다.

---
[◀ 이전: 핫리로드 및 ABI 가이드](03_LiveReload_and_ABI.md) | [🏠 위키 홈으로 돌아가기](../README.md)
