# ReflectionParser — 리플렉션 코드 생성기

> **[🏠 위키 홈으로 돌아가기](../../README.md)** | **[📖 문서 지도](../../docs/02_DocumentMap.md)**

## 이것은 무엇이고 왜 있나

빌드 중에 실행되는 콘솔 도구입니다. 엔진과 게임 헤더를 libclang 으로 파싱해 `REFLECT`, `PROPERTY`, `FUNCTION`, `ENUM` 표시가 달린 선언을 찾고 타입 설명을 등록하는 C++ 소스(`*.gen.cpp`, `*.gen.h`)를 생성합니다(언리얼의 UnrealHeaderTool 에 해당).
씬 로드, 에디터 인스펙터, 핫 리로드, `addComponentByName` 이 이 등록 코드에 의존하므로 Engine 보다 먼저 빌드되고, 순환을 피하려고 `Core` 만 링크합니다. 헤더에 표시를 다는 방법과 런타임 쪽은 `Source/Engine/Reflection/` 의 헤더 주석에 있고 이 문서는 헤더가 `.gen.cpp` 가 되기까지를 다룹니다.

## 머릿속 그림

```mermaid
flowchart TD
  CLI["main<br/>인자, 설정, 필드 목록, 템플릿 로드"] --> Fresh{"증분 판정<br/>스탬프가 최신인가"}
  Fresh -->|예| Skip[건너뜀]
  Fresh -->|아니오| Scan{"키워드가 있나"}
  Scan -->|없음| Empty["빈 .gen.cpp"]
  Scan -->|있음| Clang["libclang 파싱<br/>헤더 여럿은 한 번역 단위로"]
  Clang --> Visit["AST 순회<br/>AstVisitor"]
  Visit --> Apply["어노테이션 적용<br/>AnnotationApply"]
  Apply --> DTO["헤더별 수집 결과<br/>ParsedHeader"]
  DTO --> Emit["코드 생성<br/>CodeGenerator + Templates"]
  Emit --> Write["내용이 다를 때만 쓰기<br/>스탬프 갱신"]
```

- **스탬프.** 산출물마다 `<이름>.gen.cpp.stamp` 가 있고 다음 실행은 산출물이 아니라 이 파일의 시각으로 다시 만들지 판단합니다.
- **수집 결과(DTO).** 헤더 하나마다 `ParsedHeader`(`ParsedReflection.h`)에 타입, 프로퍼티, 함수, 이벤트, 열거형이 모입니다. 코드 생성기는 이 구조만 읽고 libclang 을 모릅니다.
- **어노테이션 필드 목록.** `PROPERTY( Min = 0 )` 의 `Min` 같은 토큰 하나는 `PredefinedAnnotationField.xxx` 의 한 줄이고 그 한 줄이 DTO 에 넣는 법, `.gen.cpp` 에 쓰는 법, 검증을 모두 정합니다.
- **철자 목록.** `Source/Core/Predefined/AnnotationMeta.txt` 가 토큰 철자를 필드에 연결합니다(`flag.ReadOnly = ReadOnly, readOnly`). 이 파일에 없는 토큰을 만나면 그 헤더의 생성이 실패합니다.

## 따라 해 보기 — 생성기가 무엇을 읽었는지 보기

"이 프로퍼티가 왜 인스펙터에 안 나오지?" 는 생성기가 헤더에서 무엇을 읽었는지부터 보면 대부분 풀립니다. 빌드가 넘기는 인자 그대로 헤더 하나만 넘기고 `--dump` 를 붙입니다(저장소 루트에서).

### 생성기가 무엇을 읽었는지 보기

```bash
build/Ninja-Debug/BuildTools/ReflectionParser.exe --dump --input Source/Engine/Object/Component/CameraComponent.h \
  --output <임시 폴더> --include Source --annotation-meta Source/Core/Predefined/AnnotationMeta.txt \
  --builtins Source/Engine/Reflection/ReflectBuiltins.xxx --emit-templates Tools/ReflectionParser/Templates
```

`--dump` 는 스탬프가 최신이어도 다시 파싱해 헤더마다 읽은 내용을 표준 출력에 찍습니다(출력 폴더에 실제 `.gen.cpp` · 스탬프도 생기므로 빌드의 생성 폴더가 아니라 임시 폴더를 준다).

```text
== Source/Engine/Object/Component/CameraComponent.h
REFLECT sw::CameraComponent : sw::SceneComponent  [component factory]
  PROPERTY _fovY : float32  Min=0.100000  Max=3.140000  Units=rad
  ...
ENUM sw::CameraRole : unsigned char
  Game = 0
  ...
```

`[component factory]` 가 없으면 이름으로 그 컴포넌트를 만들 수 없습니다. 프로퍼티가 목록에 없으면 `PROPERTY()` 가 빠졌거나 다른 선언에 붙은 것으로 읽은 것이고, 범위와 단위가 기대와 다르면 토큰 철자를 `AnnotationMeta.txt` 와 비교합니다.

| 인자 | 뜻 |
|---|---|
| `--input <header.h>` | 파싱할 헤더. 여러 번 줄 수 있습니다 |
| `--output <dir>` | `.gen.cpp`, `.gen.h`, 스탬프를 쓸 폴더. 필수입니다 |
| `--include <dir>` | clang include 경로. 여러 번 줄 수 있습니다 |
| `--builtins <ReflectBuiltins.xxx>` | 빌트인 타입 목록. 없으면 경고만 합니다 |
| `--annotation-meta <AnnotationMeta.txt>` | 토큰 철자 목록. 필수입니다 |
| `--emit-templates <dir>` | `Templates/*.tpl` 폴더. 필수입니다 |
| `--source-root <dir>` | 모듈 판별에 쓸 상대 경로의 기준 |
| `--emit-builtins-gen <file.cpp>` | `ReflectBuiltins.gen.cpp` 만 쓰고 끝냅니다 |
| `--depfile <file.d>` | 입력이 include 한 헤더를 Makefile 형식 의존 파일로 씁니다 |
| `--dump` | 최신이어도 다시 파싱하고 읽은 내용을 찍습니다 |
| `--module-local` | 대상(실행 파일, 게임 모듈)의 타입이 모듈 경계를 넘지 않습니다. 서버 버스 본문 타입의 내보내기 검사를 하지 않습니다 |
| `--help`, `-h` | 사용법을 찍고 0 으로 끝납니다 |

## 작동 원리

### 빌드에 들어가는 방법

CMake 의 `sw_addReflectionStep`(`cmake/Engine/ReflectionCodeGen.cmake`)이 타깃마다 구성 단계에서 `REFLECT(`, `ENUM(`, `REFLECT_CONTAINER(` 로 시작하는 줄이 있는 헤더를 모으고, 빌드 단계에서 그 목록으로 생성기를 한 번 실행해 `.gen.cpp` 를 타깃 소스에 넣습니다.
생성기가 쓴 의존 파일(`<출력>/ReflectionParser.d`)을 `DEPFILE` 로 받으므로 리플렉션 대상이 아닌 헤더가 바뀌어도 이 단계가 다시 돕니다. LLVM 이 없으면 `SW_REQUIRE_REFLECTION=ON`(기본)에서 구성이 실패하고 `OFF` 면 경고 후 건너뜁니다(LLVM 은 `py -3 Scripts/setup/SetupEnvironment.py`).

### 증분 판정

`GeneratedFiles.cpp` 의 `IncrementalCheck` 가 입력 헤더마다 판단합니다. 스탬프의 시각이 입력 헤더, 그 헤더가 include 한 프로젝트 헤더, 생성기 실행 파일, 빌트인 목록, 철자 목록, 템플릿, 설정 파일보다 새롭고 스탬프에 적힌 원본 경로가 지금 입력과 같으면 건너뜁니다(`git mv` 는 파일 시각을 바꾸지 않아 경로를 비교한다).
산출물은 내용이 같으면 다시 쓰지 않습니다(그 파일을 include 하는 TU 가 모두 다시 컴파일되기 때문). 그래서 다시 만들었는지는 산출물이 아니라 스탬프로 봅니다.

### 헤더 여럿을 한 번역 단위로 묶기

헤더가 둘 이상이면 입력들을 차례로 `#include` 하는 가상 파일(`ReflectionParser.batch.cpp`)로 묶어 한 번에 파싱합니다(`ReflectionPipeline::parseBatch` — 파싱 시간 대부분이 공통 include 에 든다). 선언은 적힌 파일의 것으로 세고 매크로 선언은 매크로가 전개된 위치의 파일입니다(`AstVisitor::findTargetIndex`).
한 헤더의 **어노테이션 오류**는 그 헤더만 실패시키고, **C++ 오류**로 배치 전체가 파싱되지 않으면 헤더마다 따로 병렬로 다시 파싱해 어느 헤더의 오류인지 알려 줍니다. 헤더가 하나면 묶지 않습니다.

### AST 순회와 소스 토큰

애노테이션은 선언의 `AnnotateAttr` 에서 읽는 것이 기본이고 없을 때만 소스 글자를 거꾸로 훑습니다(`//` · `/* */` 주석 안은 건너뛰므로 헤더 주석의 `REFLECT()` 예시는 표시로 읽히지 않는다). 토큰을 필드에 넣는 일은 `AnnotationApply` · `AnnotationFields` 가 합니다. libclang 타입에 없는 정보는 소스 토큰에서 읽습니다.

- **기본 인자.** 인자 커서 토큰에서 괄호 밖 첫 `=` 뒤를 글자 그대로 `FunctionParameterInfo::_defaultValue` 에 넣고 실행 중에 인자 타입으로 변환합니다.
- **C 고정 배열.** `int32 _arr[4]` 는 `std::array` 와 같은 고정 길이 시퀀스(`ArrayWrapper`)입니다. 원소가 컨테이너인 C 배열은 오류입니다.
- **이벤트.** `PROPERTY()` 필드의 정규 타입이 `sw::MulticastDelegate<` 로 시작하면 프로퍼티가 아니라 이벤트(`ParsedEventInfo`)입니다. 인자 타입은 정규 타입이 아니라 적힌 형태의 템플릿 인자에서 꺼내고 생성 코드는 오프셋 · 이름 · 인자 목록만 적습니다(연결 · 호출은 `ReflectEventOpsOf<decltype(필드)>`).

### 생성되는 것

헤더 `MeshComponent.h` 가 리플렉션 대상이면 다음이 생깁니다.

- `MeshComponent.gen.cpp` — 타입마다 `Registrar<T>`(프로퍼티의 이름 · 위치 · 메타데이터를 담은 `TypeInfo` 등록, `REFLECT_BODY()` 가 있으면 `StaticType()` 정의). 고정 배치형(B) 코덱도 여기 붙습니다(`FixedCodecGenerator`): 적격 타입은 `fixedWrite` · `fixedRead` · `fixedEquals` · `computeFixedSchemaHash` 와 `info._pFixedCodec` 등록 줄이 생기고 적격이 아니면 이유만 등록하며 `// FixedSchema:` 줄이 정규 문자열(또는 이유)입니다.
  열거자 값이 `0..N-1` 로 빈틈없으면 이름 목록 `info._listDenseName` 과 열거자마다의 `static_assert` 가 붙어 `getEnumName` 이 이 목록을 봅니다(목록은 `EnumInfo` 가 값으로 가진다 — 모듈 정적 배열을 가리키면 핫 리로드 해제와 겹친다).
- `FixedSchemas.gen.txt` — 타깃마다 하나, 스키마 해시가 왜 바뀌었는지 보는 진단 파일(빌드는 읽지 않음). `MeshComponent.gen.h` — `ENUM( Flags )` 전방 선언과 `IsBitFlagEnum` 특수화. `*.gen.cpp.stamp` — 증분 판정용.
- `FlagOps.gen.h` — 타깃마다 하나, 그 타깃의 `.gen.h` 를 모아 모든 소스에 강제 include 됩니다. 원본 헤더를 include 하지 않습니다(그러면 다른 헤더의 include 누락이 가려진다). 비트 연산자는 `Core/Common/EnumUtil.h` 의 제네릭을 씁니다.

### 설정 파일

clang 인자, SDK 상대 경로, 출력 확장자, 조정값은 `Config/Environment/parser_config.defaults.json` 한 곳에 있습니다(`parser_args.default` · `platform.*` · `extra` · `force_include`(기본 `Core/CoreMinimal.h`), `paths`, `clang_flags`, `emit`, `tuning`).
로컬 `parser_config.json` 은 `SetupEnvironment.py` 가 기본값과 합쳐 갱신하며 기계마다 다른 키(`paths.*`, `parser_args.extra`, `parser_args.force_include`)만 받고, LLVM · MSVC 절대 경로는 `toolchain_config.json` 에 있습니다.
타깃 매크로(`SW_PLATFORM_*`, `SW_X64`, `SW_ARM64`, `SW_COMPILER_CLANG`)는 설정에 적지 않고 생성기가 자기를 빌드할 때의 매크로를 넘깁니다(`ParserConfig::load`, `Core/Common/TargetMacroCheck.h` 가 libclang 내장 매크로와 비교). 매크로 이름 · 인자 철자 · 템플릿 이름 같은 C++ 쪽 이름은 `ParserDefines.h` 에 있습니다.

## 확장하는 법

### 새 어노테이션 필드 추가하기

`PROPERTY( MyFlag )` 같은 토큰을 새로 받으려면 네 곳을 고칩니다.

1. `PredefinedAnnotationField.xxx` 에 한 줄. 멤버에 그대로 넣으면 `REGISTER_ANNOTATION_FIELD( Property, ReadOnly, _bReadOnly, Runtime )`, 함수로 변환하면 `REGISTER_ANNOTATION_FIELD_FN( Property, AssetType, _assetType, applyAssetType, Runtime )`.
   마지막 열은 출력 위치입니다(`Editor` 는 `#if !defined( SW_SHIPPING )` 안, `Runtime` 은 그 밖, `Manual` 은 생성기가 손으로 씀).
2. `ParsedReflection.h` 의 DTO 에 멤버를 추가합니다(멤버 타입이 값의 종류를 정한다: bool 비트필드, `string`, `float32`, `FunctionNetRole`, `ParsedRangeExpr`(`Min` · `Max`), `uint32`(`MaxCount`)).
3. 출력 위치가 `Editor` 나 `Runtime` 이면 `Source/Engine/Reflection/ReflectionMetadata.h` 에 **같은 이름**의 멤버를 추가합니다(`ReflectionTypes.h` 는 메타데이터를 상자로만 들어 키를 더해도 그 헤더를 읽는 TU 가 다시 컴파일되지 않는다).
4. `Source/Core/Predefined/AnnotationMeta.txt` 에 철자를 추가합니다(`flag.MyFlag = MyFlag, myFlag` — 한 줄이 단독 토큰 `X` 와 `X = true` 를 함께 등록).

1 · 2 · 4번이 서로 맞지 않으면 생성기가 시작할 때 멈춥니다(`AnnotationFields::validateBindings`). 이미 있는 필드에 철자만 더 받으려면 4번만 고치고, 4번을 고쳤으면 `py -3 Scripts/generate/GenerateReflectionAnnotationKeys.py` 로 편집기용 키 목록도 다시 만듭니다.

### 출력 형식 바꾸기

생성 코드의 골격은 `Templates/*.tpl` 에 있고 `CodeGenerator.cpp` 가 그 사이를 채웁니다. 형식을 바꿀 때는 C++ 보다 템플릿을 먼저 봅니다. 생성기를 고쳤으면 아래 "함정과 주의" 마지막 항목대로 산출물을 바이트 비교합니다.

## 함정과 주의

생성기가 스스로 막는 것(둘째 · 가상 베이스, 범위 필드가 뒤에 있음, float32 로 정확히 못 나타내는 정수 경계, 못 찾은 `Min` · `Max` · `MaxCount` 상수, 서버 버스 본문 타입의 내보내기 매크로 누락, 같은 모듈 안 같은 이름 헤더)는 헤더 경로 · 줄과 함께 생성 오류이므로 따로 적지 않습니다.

- **생성 파일을 손으로 고치지 마세요.** 다음 실행에서 덮어씁니다. 헤더의 표시, `Templates/`, `AnnotationMeta.txt`, `PredefinedAnnotationField.xxx` 중 하나를 고칩니다.
- **모듈 판별 규칙은 `--source-root` 기준 상대 경로로 맞추세요.** `parser_config` 의 `parsing.module_rules` 와 `default_module` 을 절대 경로로 맞추면 상위 폴더 이름에 걸려 다른 모듈로 분류됩니다.
- **설정 파일이 깨지면 파서가 멈춥니다**(`ParserConfig::load`). 기본값으로 대신 돌지 않고 파일이 없는 것만 선택입니다.
- **`REFLECT_BODY()` 안에 주석을 넣지 마세요.** 매크로 본문의 주석 줄에 줄 이음(`\`)이 빠지면 전처리가 깨집니다.
- **생성기에게 메모리 레이아웃을 재게 하지 마세요.** 생성기의 clang 인자에는 `SW_DEBUG` 와 `_DEBUG` 가 없어 Debug 레이아웃을 모릅니다. 비트필드 위치는 실행 중에 `PropertyInfo::resolveBitField` 가 찾습니다.
- **`PROPERTY` 의 선언 순서 · 이름 · `Min` · `Max` · `Step` · `MaxCount` 는 고정 배치형 와이어 형식입니다.** 바꾸면 스키마 해시가 바뀌어 다른 빌드의 바이트는 `SchemaMismatch` 로 거절됩니다. 명세는 [고정 배치형 코덱 계약](../../Source/Engine/Serialization/Fixed/README.md)입니다.
  같은 상한을 코드도 쓰면 `MaxCount` · `Min` · `Max` 에 상수 이름을 적습니다(`MaxCount = kMaxSlotCount`, `Max = RollbackSession::kMaxPrediction`, 앞에 `-` 가능 — `AstVisitor::resolveMaxCountName` · `resolveRangeConstants` 가 찾고 `_` 로 시작하는 이름은 필드 이름이라 풀지 않는다. `Step` 은 숫자만).
- **`REFLECT` 타입은 비가상 베이스를 하나까지만 둡니다.** `castTo` 가 리플렉션 부모 사슬로 파생을 판단해 `reinterpret_cast` 로 내려가므로 부모가 offset 0 에 있지 않으면 잘못된 주소가 됩니다.
- **서버 버스 본문 타입(`kBusVersion` 이 있는 `REFLECT` 타입)은 모듈 내보내기 매크로를 답니다.** 다른 모듈의 `ServerBusTopic<T>` 가 `T::StaticType()` 을 부르므로 없으면 서버 키트 링크에서야 드러납니다(`SW_API` 는 파서에서 비워 Engine 타입에는 맞지 않는다). 실행 파일과 게임 모듈은 CMake 가 `--module-local`(`sw_addReflectionStep( MODULE_LOCAL )`)을 넘겨 검사하지 않습니다.
- **모르는 토큰은 빌드를 멈춥니다.** 에디터 힌트는 `Meta = "Color"`, 단위는 `Units = m` 으로 적고, 목록에 있는 단위를 `Meta = "Units=m"` 으로 적거나 따옴표 없는 값에 공백(`Units = m / s`)을 넣으면 거절합니다(`Units = "m/s"`).
- **선언이 어느 헤더의 것인지는 `findTargetIndex` 로만 판단하세요.** `clang_Location_isFromMainFile` 은 매크로 위치에 늘 "아니다"라고 답합니다.
- **타입 별칭과 컨테이너는 정해진 방법으로만 풀립니다.** 별칭은 `resolvePropertyAlias` · `getBaseClassDeclaration` 이 벗기고 컨테이너 규칙은 바깥 템플릿 이름이 **같을 때만** 맞습니다(`ContainerTypeMap.cpp`).
- **리눅스에서 파일 시각의 부호에 주의하세요.** libstdc++ 의 파일 시계에서는 지금 시각이 음수이므로 시각을 부호 없는 64비트로 읽습니다.
- **`Bin` 의 옛 생성기를 돌리지 마세요.** 실행 파일은 `build/<프리셋>/BuildTools/ReflectionParser.exe` 입니다. Shipping 빌드의 생성기는 Info 로그를 찍지 않으므로 사용법과 `--dump` 결과는 표준 출력으로 씁니다.
- **생성기를 고쳤으면 생성물을 바이트 단위로 비교하세요.** 고치기 전과 후의 생성기로 같은 입력의 산출물(리플렉션 타깃 전부와 `ReflectBuiltins.gen.cpp`)을 모두 만들어 `diff -r` 결과가 비어 있어야 합니다(테스트는 생성 코드의 모양을 일부만 본다).

## 더 볼 곳

- [Tools](../README.md) — 다른 호스트 도구
- 진입점: `ReflectionParser.cpp`(main) · `ReflectionPipeline.cpp` · `GeneratedFiles.cpp` · `AstVisitor.cpp` · `CodeGenerator.cpp` · `ParserOptions.cpp`(인자 정의 `kArrOptionRow` · `kArrSwitchRow`), 연동은 `cmake/Engine/ReflectionCodeGen.cmake`
