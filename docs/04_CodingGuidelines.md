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

### 함수 이름 어휘 — 한 개념에 이름 하나

같은 일을 하는 함수가 두 이름을 갖는 것은 규칙이 없어서가 아니라 **아무도 세지 않아서**다. 이 저장소는
실제로 `queryAABB` 와 `queryAabb`, `alloc*` 과 `allocate*`, `setup*` 과 `initialize*` 를 동시에 갖고 있었다.
읽는 사람은 어느 쪽이 맞는지 알 수 없고, 다음 사람은 방금 본 쪽을 따라 쓴다. 그렇게 갈라진다.
아래 네 규칙 중 앞의 셋은 `Scripts/lint/gate/CheckFunctionVocabulary.py` 가 강제한다.

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

**이름은 `hashed_string` 하나로 받는다.** 문자열 리터럴은 암묵 변환된다(`isActionDown( "Jump" )`). 포인터·`string_view`·
`string` 은 explicit 이라 동적 텍스트를 intern 하는 자리는 호출부에 `hashed_string( text )` 로 보인다. 같은 이름에 `string_view`
판을 나란히 두지 않는다 — 매개변수 수가 같으면 리터럴 호출이 모호하다(린트 `NamePair`). intern 하면 안 되는 조회(키가 아닐
수 있는 텍스트)는 이름을 따로 갖는다: `findStringByText( string_view )`. `setX()` 와 짝인 게터는 `getX()`/`isX()` 이지 맨이름
`x()` 가 아니다(린트 `BareGetter`).
| GPU 리소스 수명 | `initRhi` / `updateRhi` / `releaseRhi` / `forgetRhi` / `isRhiValid` | 그 밖의 모든 것 |

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

### 분기문 및 초기화 규칙
- 본문이 한 줄인 `if` 는 중괄호를 생략합니다. `else` / `else if` 가 붙은 사슬은 **모든 갈래가 한 줄일 때만** 생략하고, 한 갈래라도 여러 줄이면 전부 중괄호를 유지합니다.
- 반복문(`for` / `while` / `do`)은 본문이 한 줄이어도 **항상 중괄호를 유지**합니다. `Scripts/lint/fixer/FormatBranchBraces.py` 가 `if` 계열만 정리하며, clang-format 의 `RemoveBracesLLVM` 은 반복문까지 벗겨내므로 쓰지 않습니다.
- `switch` 의 `case` / `default` 는 본문이 **두 문장 이상이면 중괄호를 씌우고**, 한 문장이면 씌우지 않습니다. `break;` 도 한 문장으로 세므로 `case A:` 아래에 문장 하나와 `break;` 가 오면 중괄호를 씌우며, `case A: return X;` 는 그대로 둡니다. `break;` 는 중괄호 **안**에 둡니다. 본문에 전처리기 지시문이 끼어 있으면 건드리지 않습니다 — 본문의 끝이 글자만으로 정해지지 않아 여는 중괄호와 닫는 중괄호가 `#if` 의 반대편에 놓일 수 있습니다. 같은 스크립트가 자동 정리하며, clang-format 의 `InsertBraces` 는 case 라벨을 보지 않아 이 규칙을 표현하지 못합니다.
- 부울(bool) 타입이 아닌 포인터 등은 명시적으로 `== nullptr` 혹은 `== false` 로 비교하세요. `!_bValid` 보다는 `_bValid == false` 를 권장합니다.
- 비트 필드(bit field) 플래그(예: `uint8 _bFlag : 1;`)는 `true`/`false` 대신 `SW_TRUE`(1) / `SW_FALSE`(0)를 사용하여 대입 및 비교합니다.
- 생성자에서 멤버를 초기화할 때는 선언 순서대로 정렬해야 하며, 중괄호 `{}` 초기화를 사용하세요. 한 줄에 1개 멤버씩 초기화하며 다음 줄에 `,`로 시작합니다.
- 범위(Range) 비교 시 변수를 안쪽(중간)에 위치하도록 작성하여 수학적 범위 표기법($min \le value \ \&\&\ value \le max$)을 따릅니다 (`kMin <= value && value <= kMax`).
- 비트 패딩(Byte Padding) 낭비가 발생하지 않도록 변수 선언 순서를 최적화하세요.

### 헬퍼 Util vs Internal
1. 여러 번역 단위가 공유하는 헬퍼는 `XxxUtil` 정적 구조체 헤더로 선언합니다 (`Internal` 이름을 붙이지 않음).
2. 단일 `.cpp` 내에서만 사용하는 헬퍼는 클래스 구현과 분리된 별도 `namespace sw { namespace { struct FooInternal; } }` 블록에 배치합니다.

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

---
[◀ 이전: 핫리로드 및 ABI 가이드](03_LiveReload_and_ABI.md) | [🏠 위키 홈으로 돌아가기](../README.md)
