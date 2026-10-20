# 구조 단순화 계획 (2026-10-20)

> 상태: 설계 · 선행: 없음 · 영역: engine · 옛번호: 없음 · 진행: 3/21

상용 엔진(언리얼 · 유니티 · Godot) 비교에서 나온 구조 단순화 정책 네 개를 한 문서로 묶는다. 네 정책 모두 "같은 사실을 두 곳에 적어 두어서, 한쪽만 바뀌면 조용히 어긋난다"는 같은 근본 원인에서 나왔다.
순서는 필수 설정(HC) → 팩 쿠킹(PK) → 에셋 종류 등록(AT) → 텍스트 형식(TX)이다. PK 가 끝나면 쿠커 쪽 파이썬 소비자가 사라져 AT 의 접미사 테이블 정리가 싸지고, AT 의 등록 줄 하나가 있으면 TX 의 형식 변환이 종류별 로더 하나를 바꾸는 일이 된다.
결정의 배경은 [결정 기록](../09_Decisions.md) 5-4 "유지보수성을 위한 데이터화"(폴백 금지), 5-12 의 `구조` 행 둘, 2절 "에셋 로더 등록제" 보류(이 문서가 재개한다)다.

## 1. 근본 원인

1. **값이 없을 때 대신 쓸 값을 읽는 코드가 가진다.** `ensureConfig` 가 파일이 없으면 `T{}` 를 만들고, 게임 인스턴스는 `gamesettings.xml` 이 없어도 이어 가며, 서버는 Dev 에서만 내장 기본값으로 돈다. 필수와 선택이 호출 서명에 없으므로 호출부마다 폴백을 고를 수 있다.
2. **쿠커가 엔진이 아닌 도구 언어로 쓰였다.** 팩을 파이썬이 쓰는 까닭은 코드에 적혀 있다 — "파이썬 표준 라이브러리에 `zlib` 이 들어 있기 때문"(`ZlibCompressionCodec.h`). 그 결과 팩 레이아웃(`PackFormat.json` 에서 C++ 헤더와 파이썬 `struct.pack` 이 따로 나옴), 셰이더 스탬프 수집 규칙, 팩 증분 기록(`.packrec`)이 C++ 과 파이썬에 두 벌이다.
3. **에셋 종류가 늘 때마다 테이블을 하나씩 더 만들었다.** 등록 한 줄에서 파생하지 않고 종류마다 따로 적는다(5.1절).
4. **텍스트 형식을 필요할 때마다 새로 골랐다.** 에셋 XML 과 설정 JSON 이 있는데도 `.meta` · 에디터 상태는 `key=value`(`KeyValueFile`)이고, JSON 에셋 19 종이 따로 있다(6.1절).

## 2. 순서와 DataDriven 과의 경계

[DataDriven](DataDriven.md) 은 구현중이며 이 문서와 다음처럼 겹친다. 겹치는 단위는 그 문서의 이름으로 가리키고 여기에 다시 쓰지 않는다.

| DataDriven 단위 | 이 문서와의 관계 |
|---|---|
| D8 `LoadOutcome { Loaded, Absent, Failed }` | HC1 이 필수 설정에서 `Absent` 를 오류로 다룬다. `ConfigReadResult::Missing` 은 `Absent` 와 같은 값이라, D8 이 먼저 들어가면 그 열거를 쓰고 HC1 이 먼저면 D8 이 `ConfigReadResult` 를 이 이름으로 흡수한다. |
| D9 폴백 인자를 받는 읽기 함수 삭제 | `KeyValueFile` 은 TX 가 통째로 지우므로 D9 의 `KeyValueFile` 부분(`getInt` · `getFloat` · `getBool`)은 하지 않는다. D9 가 먼저 끝나면 TX1 · TX2 는 이미 `readRequired` · `readOptional` 로 바뀐 호출부를 옮긴다. |
| 7.5 절 P1 경로 상수를 `EngineDefaultAssets` 로 | HC3 의 UI 규칙 필드("비면 엔진 기본")가 쓰는 엔진 기본 경로다. P1 이 먼저면 HC3 은 그 항목을 읽기만 한다. |
| 7.5 절 P4 테스트가 원본을 거치게 함 | HC1 · HC4 의 테스트 정리가 같은 파일(`Test/EngineTest/Config/`)을 만진다. 같은 작업자에게 맡긴다. |
| W5 도구(`Scripts/common/Config.py` 가 `{}` 를 돌려주는 곳) | PK4 가 `CookAssets.py` 를 지우면 `readOptionalConfig` 의 쿠커 쪽 소비자가 사라진다. W5 는 남은 호출부만 본다. |

[IncrementalCook](IncrementalCook.md)(끝)의 `CookStore` · `CookJob` 이 PK2 의 증분 판정을 맡는다. 새 판정 코드를 만들지 않는다.
동시 빌드는 둘까지다. AT1 은 `AssetFormat.h` 를, HC3 은 `GameConfig.h` 를 바꿔 여러 모듈을 다시 빌드하므로 단독으로 돌린다.

## 3. 필수 설정 — 폴백을 지운다 (HC)

### 3.1 현재 구조

- `ConfigManager::ensureConfig<T>`(`Source/Engine/Config/ConfigManager.h`)는 설정 하나를 차례로 찾는다. Dev 는 디스크 파일, 없으면 생성 JSON(`shipping_host::k…JSON`), 그것도 없으면 `T{}` C++ 기본값(175-182 행, 경고 "using cpp defaults")이다. Shipping 은 디스크를 보지 않고 생성 JSON 만 쓴다.
- 호출은 세 곳이다. `EngineConfig` · `GameConfig`(`ConfigManager.cpp` 200 · 204 행)와 `ServerConfig`(`ServerApp.cpp` 293 행). 서버는 Shipping 에서 파일이 없으면 멈추지만 Dev 에서는 "using built-in defaults (Dev only)" 로 `ensureConfig` 를 다시 불러 `T{}` 로 돈다.
- `EngineConfig` 는 `T{}` 가 범위 밖이라 우연히 멈춘다(코드 주석이 그렇게 적는다). `GameConfig` 는 `T{}` 가 통과해 `_packRoot` 가 빈 채로 선다. 테스트(`TestConfigManager.cpp` 143-149 행)가 이것을 정상 동작으로 못박고, `ConfigReadResult::Missing` 의 문서 주석이 "부르는 쪽이 기본값을 쓴다"이다. 폴백이 열거의 뜻에 들어 있다.
- 게임 하나의 설정이 두 파일이다. `Config/Game/<게임>.json`(`GameConfig` 필드 11 개 — 팩 루트, 창 제목, 렌더 파이프라인, 사용자 설정 · 텔레메트리 스키마, UI 규칙 네 개, 일시정지 메뉴 켬, 사용자 설정 기본값)과 팩의 `data/gamesettings.xml`(`GameSettings` — 시작 맵, 입력 맵, 로딩 화면 등)이다. 게임 11 개 모두 둘 다 있다. 팩 루트를 뺀 필드를 읽는 줄은 소스에 약 58 개다(이름이 겹치는 다른 구조체 포함). 경계는 [설정 문서](../07_Configuration.md) 32-33 · 42 행의 문장("팩을 마운트하기 전에 알아야 하면 프리셋")으로만 정해져 있다.
- `GameInstanceBase::initialize` 는 `gamesettings.xml` 읽기가 실패해도 "using defaults" 추적 로그만 남기고 이어 간다(`GameInstanceBase.cpp` 163-164 행). 두 번째 폴백이다. 그 위 161-162 행은 `_packRoot` 가 비어 있으면 건너뛴다.

### 3.2 해법

- **출처를 서명에 둔다.** `ensureConfig<T>( path, generated )` 를 `loadHostConfig<T>( ConfigSource )` 로 바꾼다. `ConfigSource` 는 `disk( path )` 와 `baked( json )` 두 값뿐이고 빌드 구성이 하나를 고른다(Shipping 은 `baked`, 그 밖은 `disk`). "디스크에 없으면 생성 JSON" 이라는 연결이 호출 서명에서 사라진다.
- **필수와 선택을 함수 이름으로 가른다.** `loadHostConfig` 는 파일이 없으면 `Absent` 가 아니라 오류이고 `[[nodiscard]]` 결과로 기동을 멈춘다. 없어도 되는 설정은 `loadOptionalConfig<T>` 가 `Absent` 를 돌려준다(D8 의 `LoadOutcome`). `T{}` 를 호출부가 만들 수 없다. `ServerApp` 의 Dev 분기는 지운다 — 개발자도 서버 설정 파일 없이 서버를 띄우지 않는다.
- **호스트 프리셋은 팩을 찾는 값 하나다.** `GameConfig` 는 `PROPERTY( Required ) DataPath _packRoot` 만 가진다(`DataPath` 는 읽기 전에 쓰면 Debug 어서션이 나므로 `T{}` 로는 쓸 수 있는 값이 안 된다). 나머지 열 필드는 팩의 `gamesettings.xml` 로 옮긴다. 값의 원본은 팩의 한 파일이다.
- **엔진이 읽는 필드는 엔진이 소유한 구조체가 읽는다.** `GameSettings` 는 GameFramework 에 있어 엔진이 읽을 수 없다. `gamesettings.xml` 에 `<Engine>` 요소를 두고, 엔진 소유 리플렉션 구조체(`PackEngineSettings`)가 그 요소만 읽는다. `GameSettings` 는 `<Engine>` 요소를 건너뛴다(지금도 "표준 칸 · `<custom>` · `<Defaults>` 밖의 원소는 로드 오류"라 이 요소를 명시적으로 허용해야 한다). 요소마다 읽는 계층이 하나라서 같은 필드를 두 곳이 읽지 않는다.
- **"비면 엔진 기본" 필드는 선택 필드로 만든다.** UI 규칙 네 개(`_uiScaleSettings` 등)과 렌더 파이프라인은 빈 문자열이 "엔진 기본"을 뜻한다. `Optional<DataPath>` 로 바꾸고, 엔진 기본 경로는 `EngineDefaultAssets`(P1)가 가진다. 없음이 형식에 보인다.
- **창 제목은 마운트 뒤에 정한다.** 창은 숨긴 채 만들어지고(`App.cpp` 의 `showWindow( false )`) 이미 `IWindow::setTitle` 이 있다. 팩 설정을 읽은 뒤 제목을 설정한다. 그 전에는 제목이 필요한 코드가 없는지를 HC3 의 첫 확인으로 한다.
- 스키마 필드의 기본값(`PROPERTY` 초기값이 있고 파일이 키를 생략하는 것)은 이 문서가 건드리지 않는다. 그것은 DataDriven 2절의 규칙(기본값은 Def 의 초기값, 필수는 `Required`)이다. 이 문서가 지우는 것은 **파일 전체가 없을 때** 구조체 전체를 `T{}` 로 대신하는 길이다.

### 3.3 대안과 기각 이유

- **`GameSettings` 를 Engine 으로 옮겨 구조체 하나로 합친다.** 기각한다. 시작 맵 · 입력 맵 · 로딩 화면 · 다국어를 GameFramework 의 `GameInstanceBase` 가 적용하고, 구조체를 옮기면 `SW_GF_API` 와 키트 소비자(`bindScopedService( &_bootstrap._data )`)까지 옮겨야 한다. 요소를 가르는 쪽이 한 파일, 한 소유자를 지키면서 이동이 작다. 합쳐서 얻는 것이 크다고 확인되면 다시 본다.
- **엔진 필드를 `gamesettings.xml` 이 아닌 두 번째 팩 파일로 둔다.** 기각한다. "원본 하나"의 반대이고 11 개 팩에 파일이 하나씩 느는 것뿐이다.
- **`T{}` 대체를 경고에서 오류 로그로 올린다.** 기각한다. 로그는 폴백을 막지 못한다(근본 원인 1). 서명에서 해당 경로를 없앤다.
- 언리얼은 `Base*.ini` 위에 `Default*.ini` 를 덮는 계층형이라 파일이 없어도 위 계층 값이 선다 [기억]. 이 저장소는 계층이 아니라 필수 파일 하나이므로 그 방식이 맞지 않는다. 유니티 `ProjectSettings/*.asset` · Godot `project.godot` 도 파일이 없으면 프로젝트가 열리지 않는다 [기억].

### 3.4 단위

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| HC4 | **거의 끝(`wt/hc4` · `wt/hc4b`): `ConfigCatalog.py` 의 `PackEngineSettings` 항목과 `loadHostConfig` 문구, `Config/README.md` 의 폴백 · `ensureConfig` 문장, 생성 문서, `Config.py` 의 `loadToolchainConfig` 가 `{}` 대신 `None`, `GenerateShippingHostDefaults.py` 가 `_packRoot` 하나뿐인 프리셋만 굽도록 검사, `CheckGamePresets.py` 의 스키마 · 텔레메트리 경로 검사를 팩 `gamesettings.xml` 의 `<Engine>` 쪽으로 옮김(프리셋의 다른 키도 위반), CMake `VcpkgPortsToolchain.cmake` · `ThirdParty/dxc` 의 오류 무시 `string(JSON ERROR_VARIABLE)` 를 `sw_readOptionalJsonValue` 로. `FileScan.py`(읽지 못하면 멈추거나 위반으로 알림) · `AssetValidation`(`return []` 는 위반 없음이고 XML 파싱 실패는 `xml_wellformed` 가 보고)은 같은 모양이 아니라 그대로 둔다(W5 끝).** 남은 것: Shipping 구성 컴파일(C++ 빌드)로 `ShippingGameDefaults.h` 가 새 검사를 지나는지 확인. | `Scripts/generate/`, `Scripts/lint/gate/`, `Scripts/common/ConfigCatalog.py`, `docs/07_Configuration.md`, `Config/README.md`, `docs/Config/`(생성물) | HC3 | `-L lint`, 생성 문서 점검, Shipping 구성 컴파일 | 없음 |

단위가 끝날 때 고칠 문서: `docs/07_Configuration.md` 32-33 · 42 행(프리셋 경계), `Config/README.md`("기본값을 쓰는 것은 파일이 없을 때뿐" 문단), `GameConfig.h` 머리 주석, 결정 기록 5-4 의 해당 항목.

### 3.5 위험

- **Dev 의 작업 디렉터리.** 지금은 `Config/` 가 없는 곳에서 실행해도 생성 JSON 으로 선다. HC1 뒤에는 멈춘다. 테스트 · 도구 중에 이 동작에 기댄 것이 있는지를 `AllTests` 전체로 확인한다(개별 테스트 이름을 미리 아는 것은 없다).
- **에디터 설정 패널이 `Config/Game/*.json` 을 `GameConfig` 로 편집한다**(`TestProjectSettings.cpp` 39-61 행). HC3 에서 편집 대상이 팩 파일의 `<Engine>` 요소로 옮겨 가는지, 패널에서 필드를 빼는지를 먼저 정한다. 이 패널의 변경은 `ux-reviewer` 캡처로 본다.
- **창 제목 시점.** 마운트 전에 제목이 필요한 호출이 있으면 HC3 에서 멈춘다(`RHISubsystem.cpp` 65 행이 `IWindow` 가 없을 때 창을 만드는 두 번째 경로다). 그 경로도 같은 설정 시점으로 맞춘다.
- **Shipping 서버.** `kGameConfigJSON` 이 팩 루트만 생성하게 되므로, 팩을 읽지 못하면 이전보다 일찍 멈춘다. 의도한 변화다.

## 4. 팩 쿠킹을 C++ 로 옮긴다 (PK)

### 4.1 현재 구조

`Scripts/generate/CookAssets.py`(877 행)는 쿠킹 세 가지를 한 파일에서 부른다. 씬 · 프리팹 · GUID 레지스트리 · VAT · 내비메시는 `App --cook-scenes`, 셰이더는 `App --cook-shaders` 로 이미 C++ 이고, **파이썬이 직접 하는 것은 아래다.**

| 파이썬이 하는 일 | 위치 | C++ 의 같은 일 |
|---|---|---|
| 팩 바이트 쓰기(`PackWriter.buildInternal`, `struct.pack` 은 `PackFormatSpec` 을 거침) | 472-620 행 | 리더 `ResourcePakReader`, 레이아웃 헤더 `ResourcePakTypes.h`. `PackFormat.json` 에서 헤더(`GeneratePackFormat.py`)와 파이썬 객체(`Scripts/common/PackFormat.py`)가 따로 나옴. 테스트도 세 번째 쓰기(`ResourcePakTestUtil::createPackFile`)를 가짐 |
| 압축(zlib `Z_FIXED`, LZ4/Zstd 는 pip 모듈) | 120-180 행 | `ZlibCompressionCodec` · `Lz4CompressionCodec` · `ZstdCompressionCodec`(압축 가능) |
| 셰이더 스탬프 판정(`verifyShaderCookInternal`, 수집 규칙 247-251 행) | 223-352 행 | `ShaderCookStamp.cpp` 의 `collectStampedSources`(76 행). 규칙이 두 언어에 있고 `CheckShaderStamps.py` 가 파이썬 쪽을 따로 검사함 |
| 팩에 넣을 파일 고르기(`shouldIncludeFile` — 전역 제외 폴더 · 패턴, 규칙별 제외, 빌드 타깃 제외) | 360-413 행 | 타깃 제외는 `CookContract` X-macro 로 런타임(`ResourcePaths::setHostTarget`)이 이미 같은 테이블을 읽음 |
| 증분 기록(`PackRecord` · `.packrec`, 키 = 해시) | 431-470 행 | `CookStore` · `CookJob` · `Saved/CookRecord/*.cookrec`([IncrementalCook](IncrementalCook.md)) |
| 팩 이름과 게임 목록(`listPackNameOfGame`, 프리셋의 `_packRoot` 를 파이썬이 읽음) | 701-790 행 | `GameSelection` |

소비자는 `Scripts/dev/MakePackage.py`(`CookAssets.py --all` 호출), CMake 의 `CookAssets` 타깃(`CMake/Engine/AssetAndToolTargets.cmake` 73 행), 린트 게이트 `CheckShaderStamps.py`(커밋 훅 패턴 포함)다. `PackConfig.json` 의 설명은 "LZ4 는 pip 의 lz4, Zstd 는 pip 의 zstandard 가 있어야 쿠킹된다"고 적는다. 압축 코덱 선택이 도구 설치 상태에 매인다.

### 4.2 해법

- **쿠킹은 엔진 언어 한 곳에서 한다.** 언리얼 `-run=cook` · `UnrealPak`, 유니티 빌드 파이프라인, Godot 내보내기 모두 엔진 쪽 언어로 쓰고 에디터 · 엔진 코드가 직접 부른다 [기억]. 이 저장소도 이미 셰이더 · 씬이 그렇다. 파이썬 스크립트는 `App` 을 찾아 부르는 얇은 호출자만 남기거나 사라진다.
- **팩 형식은 C++ 헤더가 원본이다.** 레이아웃은 데이터가 아니라 형식이다(DataDriven 의 "코드는 형식만 가진다"는 값에 관한 말이고 레이아웃은 형식 자체다). `ResourcePakTypes.h` 의 구조체를 손으로 쓴 원본으로 두고 `static_assert`( `sizeof` · `offsetof` )로 레이아웃을 고정한다. `PackFormat.json` · `GeneratePackFormat.py` · `Scripts/common/PackFormat.py` 는 지운다. 팩을 읽고 쓰는 쪽이 같은 구조체를 쓰므로 오프셋이 어긋난 "빈 팩" 결함을 낼 수 없다.
- **쓰기는 `PackWriter` 하나다.** 엔진의 `Engine/Resource/Pack/` 에 `ResourcePakWriter` 를 두고 쿠커와 테스트(`ResourcePakTestUtil`)가 함께 쓴다. 세 번째 쓰기를 지운다.
- **스탬프 판정은 `ShaderCookStamp` 한 곳이다.** `verify` 를 C++ 에 더하고 `App --verify-shaders`(종료 코드와 문제 목록)로 부른다. 수집 규칙이 한 언어에만 있으므로 "두 언어가 같은 규칙인지"를 보는 `CheckShaderStamps` 게이트가 필요 없어진다. 대신 기존 `ShaderCookStampTest`(`Test/EngineTest/Graphics/TestShader.cpp`)에 `verify` 케이스를 더해 판정을 본다.
- **증분 판정은 `CookStore` 다.** 팩 하나를 `CookJob` 으로 만들고 입력(파일 경로 · 내용 해시 · 설정)을 기록한다. `.packrec` 형식과 `kPackRecordFormatVersion` 은 지운다.
- **팩 설정은 설정 JSON 이다.** `PackConfig.json` 은 리플렉션 `IConfig`(`PackConfig`, 필수 필드는 `Required`)로 읽는다. 파이썬 `config[...]` 의 `KeyError` 가 하던 "없으면 멈춤"을 형식이 한다.

### 4.3 대안과 기각 이유

- **파이썬 쿠커를 두고 팩 레이아웃만 C++ 와 공유한다(지금 구조).** 기각한다. 생성 헤더와 파이썬 객체 두 가지가 이미 있고, 스탬프 규칙은 생성으로도 못 합쳤다. 언어가 둘이면 규칙이 둘이다.
- **C++ 쓰기를 파이썬 확장 모듈로 노출한다.** 기각한다. 빌드 산출물이 하나 늘고 쿠커가 파이썬에서 불린다는 사실만 남는다. `App` 의 헤드리스 모드가 이미 같은 경로다(`HeadlessSubsystem.cpp` 107-139 행의 `--cook-scenes`).
- **셰이더 스탬프 검사를 파이썬에 남기고 규칙만 C++ 가 생성한 JSON 으로 공유한다.** 기각한다. 규칙이 "어느 파일이 의존인가"라는 코드(include 해석)이고 데이터로 줄지 않는다.

### 4.4 단위

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| PK1 | 팩 쓰기 `ResourcePakWriter` 를 만들고 `ResourcePakTypes.h` 를 레이아웃 원본으로 굳힙니다(`static_assert`). 테스트의 `createPackFile` 을 이 쓰기로 바꿉니다. 먼저 기준을 남깁니다 — 파이썬이 쓴 13 개 팩(`engine` · `common` · 게임 11)의 바이트 | `Engine/Resource/Pack/`, `Test/EngineTest/` | 없음 | 팩 읽기 · 쓰기 왕복 테스트, 파이썬 팩과 같은 입력의 바이트 비교(다르면 CRC 와 리더 왕복으로 같은 내용임을 확인하고 이유를 적음) | 없음 |
| PK2 | `PackCooker` 와 `App --cook-packs` 를 만듭니다. `PackConfig`(리플렉션 설정)를 읽고, 파일 선택(전역 · 규칙별 제외, 셰이더 원본 제외, 빌드 타깃 제외, 스테이징 병합, 레지스트리 항목)을 파이썬과 같은 결과로 옮기고, 증분 판정은 `CookStore`/`CookJob` 에 맡깁니다. 압축은 `ICompressionCodec` 로 하고 `deflateStrategy` 계약이 리더의 요구인지 코덱 옵션인지 확인해 둡니다 | `Engine/Resource/Cook/`, `Engine/Resource/Pack/`, `App/`, `Config/Engine/PackConfig.json` | PK1 | 13 개 팩이 파이썬 결과와 같은 항목 목록 · 내용인지 비교, 무변경 재쿠킹이 쓰지 않는지, `-L nogpu` | 있습니다. 파이썬의 전체 쿠킹 · 무변경 재쿠킹 시간을 먼저 적고, Release 에서 같은 두 경우를 3회 이상 비교합니다 |
| PK3 | 셰이더 스탬프 판정을 C++ 한 곳으로 합니다. `ShaderCookStamp::verify` 와 `App --verify-shaders` 를 더하고 `CheckShaderStamps` 게이트를 C++ 테스트로 바꿉니다. 커밋 훅은 셰이더 파일을 고친 커밋에서만 빌드된 `App` 을 부르고, 없으면 안내와 함께 실패합니다(건너뛰지 않음) | `Engine/Graphics/Shader/Compile/`, `App/`, `Scripts/lint/gate/`, `Scripts/lint/`(커밋 훅 패턴) | 없음 | `ShaderCookStampTest`, 스탬프가 틀린 커밋 시도, `-L lint` | 없음 |
| PK4 | 호출부를 옮기고 파이썬을 지웁니다. `MakePackage.py` · CMake `CookAssets` 타깃이 `App --cook-all` 류 한 호출을 부르게 하고, `CookAssets.py` · `PackFormat.py` · `GeneratePackFormat.py` · `PackFormat.json` · 관련 파이썬 테스트(`TestPackIncremental.py` 등)를 지웁니다. `CookContract.json` 의 파이썬 소비자도 정리합니다 | `Scripts/generate/`, `Scripts/common/`, `Scripts/dev/`, `CMake/Engine/`, `Test/PythonTest/`, `Config/Engine/PackFormat.json` | PK2, PK3 | 전 프리셋 컴파일, `MakePackage` 로 Shipping 패키지 한 벌, `-L lint`, `-L nogpu` | 있습니다. 전체 패키징 시간 전후 |

단위가 끝날 때 고칠 문서: `Scripts/generate/README.md`, `Scripts/docs/Commands.md` · `TrapsBuild.md`, `CMake/README.md`, 결정 기록 5-2 의 쿠킹 항목, `Config/README.md`(압축 설명), `ZlibCompressionCodec.h` 주석("파이썬 표준 라이브러리" 문장), `ResourcePakReader.cpp` 368 행 오류 문구("CookAssets.py" → 새 명령).

### 4.5 위험

- **팩 바이트가 달라진다.** 코덱 구현이 같은 zlib 이라도 압축 결과가 파이썬과 다를 수 있다. 팩 키가 바뀌어 모든 팩이 한 번 다시 쓰인다. PK1 의 비교로 이유를 적어 두고, 항목 목록 · 내용 · CRC 가 같으면 받아들인다.
- **셰이더 스탬프 검사가 빌드된 `App` 을 요구한다.** 커밋 훅은 지금 빌드 없이 돈다. 영향은 `Resource/*/shaders/*` 를 고치는 커밋에 한정되고, 그 사람은 셰이더를 `App` 으로 쿠킹하므로 `App` 이 이미 있다. 셰이더를 쿠킹하지 않는 리소스 작업자는 커밋된 바이너리를 받아 쓰므로 영향이 없다(커밋 유지 결정, 5-12 `구조` 행). "App 이 없으면 건너뛴다"는 두지 않는다.
- **헤드리스 `App` 이 자기 팩을 읽는 순환.** 쿠킹은 느슨한 트리를 읽어야 한다. `--cook-scenes` 가 이미 같은 방식이다.
- **파이썬 쪽 숨은 소비자.** `listPackNameOfGame` 을 쓰는 곳이 `MakePackage.py` 외에 있는지 PK4 에서 `grep` 으로 확인한다.

## 5. 에셋 종류를 등록 줄 하나에서 파생한다 (AT)

이 절은 결정 기록 2절 "에셋 로더 등록제(종류가 대여섯이 되면 — UE `UFactory`)" 보류를 **다시 연다.** 조건이던 대여섯 종을 넘어 지금 20 종 이상이다.

### 5.1 현재 구조

한 종류의 사실이 여섯 곳에 따로 적힌다. 2026-10-20 에 센 값이다.

| 곳 | 무엇을 적나 | 규모 |
|---|---|---|
| `AssetKind`(`Engine/Resource/AssetFormat.h` 19-28 행) | 쿠킹 · 포맷 이관의 종류 열거, `AssetFormatVersions` 상수 | 6 개(Material · MaterialInstance · RenderPipeline · RenderPass · Prefab · Scene) |
| `Config/Engine/CookContract.json` | `cook_suffixes`(저작 소스 ↔ 쿠킹본, 종류), `asset_kinds`(Texture · ShaderBinary · Audio — 위와 다른 종류 목록), `target_excluded_asset_kinds` | 7 + 3 + 1 줄. 파이썬과 C++ X-macro 둘이 읽음 |
| `SW_RESOURCE_DATA_KIND`(`ResourceDataKind.h`) | 카탈로그 파일의 경로 규칙(접두 · 포함 · 접미)과 로더 함수 | 29 줄 |
| `IAssetCache` 와 `SW_BUILT_IN_ASSET_CACHE` | 경로 키 에셋 캐시, `getAssetKindName()` 문자열 | 등록 24 곳 |
| `EditorAssetType`(`Editor/Common/Workspace/EditorAssetType.{h,cpp}`) | 열거 + 종류 정보 테이블(`kArrKindInfo` 20 줄) + 경로 판정 규칙(`kArrAssetMatch` 28 줄) + 확장자 배열 약 25 줄 | 사용 188 줄, 29 파일 |
| `kExtension` 상수 | 타입마다 자기 헤더에 접미사 | 36 개 |

한 종류를 더하면 이 중 몇 곳을 손으로 고쳐야 하는지가 기억에 달려 있다. `IAssetCache.h` 머리 주석이 이미 "목록이 여럿이면 한쪽만 늘어난다"고 적는다. 접미사 우선순위(`.scene.xml` 이 `.scene` 보다 먼저)도 JSON 순서와 `kArrAssetMatch` 순서 두 곳이 따로 정한다.

### 5.2 해법

- **종류 정의는 `AssetTypeDef` 하나다.** 접미사 목록, 레이블, 저작 소스인지 쿠킹본인지(쿠킹 역할), 팩에서 뺄 수 있는 그룹(Texture · ShaderBinary · Audio 에 해당하는 값), 이관 버전 상수를 한 구조체에 둔다. 각 타입 헤더의 `kExtension` 은 이 정의의 접미사로 대신하고, 캐시의 `.cpp` 에 있는 `SW_BUILT_IN_ASSET_CACHE` 줄이 `SW_ASSET_TYPE( Type, def )` 한 줄로 바뀐다 — 캐시 등록이 곧 종류 등록이다.
- **파생하는 쪽은 레지스트리를 순회한다.** 쿠커(`AssetCookPath`), 접미사 우선순위(접미사 길이가 긴 것이 먼저라는 규칙을 레지스트리가 정렬로 가진다 — JSON 순서에 기대지 않는다), 팩 제외 그룹, 이관 키(`AssetKind`), 에디터의 경로 판정이 모두 같은 목록을 읽는다. 종류를 하나 더하는 일은 등록 줄 하나다.
- **카탈로그 종류(`ResourceDataKind`)는 같은 레지스트리에 들어올 수 있는지 AT4 에서 가른다.** 접미사 규칙이 아니라 경로 규칙(`game/` 접두 + `/data/appearance/` 포함 + `.xml` 접미 등)이라 형태가 다르다. 한 구조체에 "접미사 규칙 또는 경로 규칙"을 두는 것이 두 레지스트리를 두는 것보다 단순하면 합친다. 아니면 이 문서는 카탈로그를 따로 둔 이유를 적고 끝낸다.
- **에디터는 표시 정보만 가진다.** 아이콘 · 색 · 패널 제목 · 임포터는 Editor 에만 있어야 하므로 `EditorAssetTypeInfo` 는 남기되, 접미사 · 캐시 이름 · 핫 리로드 경로는 엔진 레지스트리에서 읽고 표시 정보는 레이블 키로 짝짓는다. 열거 `EditorAssetType` 은 레이블 핸들로 바뀐다. 엔진 종류가 에디터 표시 정보를 빠뜨리는 것은 `AssetTypeCoverageTest` 가 막는다(표시 없음을 명시한 종류는 허용). 형식으로 막지 못하는 틈은 에디터가 엔진 헤더를 모르는 경계뿐이라 테스트를 둔다.
- **`CookContract.json` 은 줄어든다.** PK4 뒤에는 파이썬 소비자가 없으므로 `cook_suffixes` · `asset_kinds` 는 레지스트리로 옮겨 지우고, `rhi_backends`(CMake 가 필요) · `default_rhi_backend` · 타깃별 제외 그룹 이름(`target_excluded_asset_kinds`, 레지스트리의 그룹 이름을 가리키며 모르는 이름은 로드 오류)만 남는다.

### 5.3 대안과 기각 이유

- **지금처럼 두고 새 종류를 등록할 때 체크리스트를 문서화한다.** 기각한다. 체크리스트는 게이트보다도 약하다.
- **에디터의 `EditorAssetType` 만 정리한다.** 기각한다. 에디터 테이블이 접미사 사본을 갖는 것이 근본 원인이므로(`AssetCookPath::isCookableSource( path, kind )` 주석이 이미 이 문제를 적는다), 에디터 쪽만 고치면 엔진 쪽 사본 둘이 남는다.
- **언리얼의 `UFactory`/`UAssetDefinition` 처럼 임포트와 표시까지 한 등록에 넣는다.** 일부만 가져온다. 언리얼 `UAssetDefinition` 은 에디터 모듈에 있고 `UClass` 가 종류 키다 [기억]. 여기서는 엔진 레지스트리를 원본으로 두고 에디터 표시 정보만 바깥에 둔다.

### 5.4 단위

동시 빌드는 둘까지다. AT1 은 `AssetFormat.h` 를 바꿔 모든 모듈을 다시 빌드한다.

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| AT1 | `AssetTypeDef` · 레지스트리 · `SW_ASSET_TYPE` 를 만들고 `AssetKind` 6 종(Material · MaterialInstance · RenderPipeline · RenderPass · Prefab · Scene)을 옮깁니다. `AssetFormatVersions`, `registerXMLMigrator` 의 키, `AssetCookPath` 를 레지스트리 파생으로 바꾸고 `cook_suffixes` 를 레지스트리로 옮겨 접미사 정렬 규칙을 코드 한 곳에 둡니다 | `Engine/Resource/`, `Engine/Scene/`, `Engine/Graphics/Material/`, `Engine/Object/`, `Config/Engine/CookContract.json`, `Scripts/generate/GenerateCookContract.py`, `Scripts/common/CookContract.py` | PK4 | 전 프리셋 컴파일, 씬 · 프리팹 쿠킹 왕복, `AssetTypeRegistryTest`(접미사 중복 없음 · 정렬 우선순위), `-L lint`, `-L nogpu` | 없음 |
| AT2 | 캐시 24 곳과 `kExtension` 36 개 중 애니메이션 · 캐릭터 · 오디오 · 파쇄 · 대화 · 시퀀서 계열을 `SW_ASSET_TYPE` 로 옮기고 `kExtension` 상수를 등록 정의의 접미사로 대신합니다 | `Engine/Animation/`, `Engine/Character/`, `Engine/Audio/`, `Engine/Destruction/`, `Engine/Dialogue/`, `Engine/Sequencer/` | AT1 | 전 프리셋 컴파일, 해당 폴더 테스트, `-L nogpu` | 없음 |
| AT3 | 나머지 그래픽스 · 환경 · 지역화 · 내비 · 타일맵 · VFX · 키트 계열을 같은 방식으로 옮깁니다 | `Engine/Graphics/`, `Engine/Environment/`, `Engine/Localization/`, `Engine/Navigation/`, `Engine/TileMap/`, `Engine/Vfx/`, `GameFramework/Base/` | AT1 | 전 프리셋 컴파일, 해당 폴더 테스트, `-L nogpu` | 없음 |
| AT4 | `SW_RESOURCE_DATA_KIND` 29 줄을 같은 레지스트리에 합칠 수 있는지 가르고, 합칠 수 있으면 합칩니다. 못 합치면 이유를 5.3절에 적고 카탈로그는 현 구조로 둡니다 | `Engine/Resource/`, `Engine/UI/`, `Engine/Vfx/`, `Editor/Panels/Catalog/`, `GameFramework/Base/` | AT2, AT3 | 스키마 테스트(`ResourceDataSchemaTest`), 카탈로그 패널 테스트, `-L nogpu` | 없음 |
| AT5 | `CookContract.json` 의 `asset_kinds` · `cook_suffixes` 를 지우고 타깃별 제외를 레지스트리 그룹 이름 참조로 바꿉니다. `ResourcePaths::setHostTarget` 이 레지스트리에서 읽게 합니다 | `Config/Engine/`, `Engine/Resource/`, `Scripts/generate/GenerateCookContract.py`, `Scripts/lint/gate/CheckCookContract.py`, `Scripts/common/` | AT1, PK4 | 전용 서버 팩이 텍스처 · 셰이더 바이너리 · 오디오를 빼는지 확인, `-L lint`, `-L nogpu` | 없음 |
| AT6 | 에디터를 레지스트리로 옮깁니다. `EditorAssetType` 열거를 레이블 핸들로 바꾸고 `kArrAssetMatch` · 접미사 배열을 지웁니다. `AssetTypeCoverageTest` 를 더합니다 | `Editor/Common/Workspace/`, `Editor/Common/Commands/Asset/`, `Editor/Common/GUI/`, `Editor/Panels/` | AT2, AT3 | 에디터 테스트, 콘텐츠 브라우저 · 핫 리로드 시나리오, `ux-reviewer` 캡처 | 없음 |

단위가 끝날 때 고칠 문서: 결정 기록 2절의 로더 등록제 문장(이 문서가 열면서 이미 고친다), `IAssetCache.h` 머리 주석, `docs/Config/` 생성물, `CookContract.json` 의 `$schema_note`.

### 5.5 위험

- **정적 등록이 링크에서 빠진다.** `ConfigManager.cpp` 에 "리플렉션 등록자가 링크에서 빠졌다"는 검사가 있을 만큼 알려진 위험이다. `SW_BUILT_IN_ASSET_CACHE` · `SW_RESOURCE_DATA_KIND` 가 같은 정적 등록자를 이미 쓰므로 새 위험은 아니다. `AssetTypeRegistryTest` 가 접미사 상수를 쓰는 모든 타입이 등록되어 있는지 본다.
- **에디터 188 줄 29 파일.** AT6 은 충돌이 많다. 에디터 다른 단위(`EditorCharacterEditor` 등)와 같은 시간대에 병합하지 않는다.
- **접미사 판정 순서.** 지금은 `kArrAssetMatch` 의 순서가 우선순위다(첫 일치). 레지스트리는 정렬 규칙(긴 접미사 우선, 같으면 등록 순)으로 대신하며, 순서에 기댄 기존 규칙(예: `.png` 가 Texture 와 SpriteClip 이미지 양쪽)은 AT6 에서 케이스로 옮긴다.
- **모듈 언로드.** 키트 DLL 이 등록한 종류는 모듈을 내릴 때 빠져야 한다. `ResourceDataKindRegistrar` 가 이미 하는 일과 같은 등록자 패턴을 따른다.

## 6. 텍스트 형식을 역할마다 하나로 (TX)

### 6.1 현재 구조

텍스트 파서가 세 개다. XML(에셋), JSON(설정과 에셋 일부), `KeyValueFile`(`key=value`).

- **`KeyValueFile`**(`Core/File/KeyValueFile.{h,cpp}`) 소비자는 다섯이다. `.meta` 167 개(`AssetDatabase.cpp` 307 · 446 행 — `guid` · `sourcePath` · `imported` 세 줄), 에디터 패널 가시성(`EditorDockLayout` 의 `_windowsIniPath`, `EditorLayoutStore` 의 가시성 파일), 뷰포트 오버레이 상태(`EditorViewportOverlays`), 사용자 외형 프리셋(`UserAppearancePresetStore` — 플레이어 데이터), 퍼즈 대상(`LoaderFuzzTargets`). 에디터의 `EditorAssetType::Data` 확장자 목록에도 `.ini` · `.kv` 가 있다. DataDriven D1 이 이미 `KeyValueFile::readRequired` · `readOptional` 을 더했으므로 폴백 인자 쪽 정리(D9)도 이 파일을 포함한다.
- **JSON 에셋.** `Resource/` 의 `.json` 은 69 개(약 1.3 MB)다. 확장자 종류는 `kExtension` 상수로 19 종(`.skeleton` · `.rig` · `.bonelod` · `.bonemask` · `.animgraph` · `.retarget` · `.facial` · `.visemes` · `.sprite` · `.dialogue` · `.seq` · `.vertexanimation` · `.avatar` · `.tokens` · `.cultures` · `.strings` · `.translation` · `.locproject` · `.tm`)이고 나머지는 이름 있는 설정성 파일이다. 같은 역할인데 XML 에셋(`.material` · `.vfx.xml` · `.tilemap.xml` · `.audioevents.xml` · `.pairedanim.xml`)과 JSON 에셋이 섞여 있고, 프리팹은 `.prefab.xml` 과 `.prefab.json` 둘이 쿠킹 입력이다(`cook_suffixes`).
- 결정 기록 5-12 의 `리소스 C-44` 행은 도구가 `Resource/` 에 두는 출처 목록(`*.sources.json`, 가장 큰 파일 309 KB)을 JSON 으로 통일했다. 그 결정은 도구 데이터 쪽 이야기라 이 문서와 충돌하지 않는다.

### 6.2 해법

- **역할 규칙.** 저작자가 편집하고 리소스 id · GUID 로 참조되며 쿠킹 대상인 데이터는 XML, 시작할 때 `ConfigManager` 가 읽는 호스트 · 도구 설정과 사용자 상태(에디터 상태, 플레이어 설정)는 JSON 이다. 역할이 모호한 파일(`tokens` · `lipsync` · `animationlod` 등)은 TX4 에서 하나씩 가르고 결과를 테이블로 남긴다.
- **형식은 로더 서명이 막는다.** AT 의 `AssetTypeDef` 에 형식 필드를 두지 않고 에셋 로더가 `XMLDocument` 만 받게 한다. JSON 읽기는 `ConfigManager`/`IConfig` 에서만 닿는다. 에셋 로더가 `JSONValue` 를 손으로 읽는 경로 자체가 서명에 없다. 형식으로 못 막는 틈(에셋 폴더의 코드가 `JSONSerializer` 를 직접 부르는 것)만 마지막에 린트 게이트로 둔다.
- **`.meta` 는 XML 이다**(에셋 곁의 에셋 메타). `<AssetMeta guid="…" sourcePath="…" imported="0"/>` 한 요소를 리플렉션 구조체가 읽고, `guid` · `sourcePath` 는 `Required` 다.
- **에디터 · 사용자 상태는 JSON 이다.** 리플렉션 구조체 + `JSONSerializer` 로 읽고 쓴다. `Saved/Editor/` 의 파일은 git 이 무시하는 사용자 상태라 옛 형식 리더를 두지 않고 새 형식으로 다시 쓴다(결정 기록의 "옛 형식 리더를 두지 않는다"). 플레이어 외형 프리셋은 출시 전 데이터라 같은 처리를 한다 — 확인이 필요하면 8절.
- **외부 규격은 건드리지 않는다.** `.po` · `.xlf`(번역 교환), glTF · VRM, ImGui 의 `imgui.ini` 는 엔진 형식이 아니다.
- **옛 형식 리더를 두지 않는다.** 변환은 일회성 스크립트로 모두 다시 쓰고 스크립트는 지운다. 임포터가 쓰는 JSON(`.skeleton.json` · `.vertexanimation.json` 등)은 임포터가 XML 을 쓰게 바꾸고 커밋된 결과를 다시 임포트한다.

### 6.3 대안과 기각 이유

- **`KeyValueFile` 만 지우고 JSON 에셋은 둔다.** 기각한다. 에셋 로더마다 JSON/XML 을 고를 수 있는 구조가 남는다(근본 원인 4). 로더 서명으로 막으려면 JSON 에셋을 옮겨야 한다.
- **모든 것을 JSON 으로 통일한다.** 기각한다. 에셋 XML 은 리플렉션 직렬화 · 마이그레이션 · 씬 프리팹 참조가 이미 XML 로 서 있다(`AssetFormat.h`: "저작 기본 포맷은 XML 입니다").
- **`.meta` 를 JSON 으로 한다.** 기각한다. `.meta` 는 에셋 곁에 놓이고 에셋과 함께 참조되는 쪽이라 에셋의 역할을 따른다. 유니티 `.meta` 는 YAML 로 에셋과 같은 계열이고 Godot `.import` 는 설정 텍스트다 [기억] — 둘 다 에셋 형식과 따로 쓰지는 않는다.

### 6.4 단위

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| TX1 | `.meta` 167 개를 XML 로 다시 쓰고 `AssetDatabase` 읽기 · 쓰기를 리플렉션 구조체(`AssetMeta`)로 바꿉니다. GUID 가 한 개도 바뀌지 않는지 `assetregistry` 를 다시 만들어 비교합니다 | `Engine/Resource/`, `Resource/**/*.meta`, `Editor/Common/Asset/`(`.meta` 를 쓰는 임포터), `Test/EngineTest/Resource/` | AT1 | 에셋 레지스트리 생성 결과가 변환 전과 같음, GUID 복구 테스트, 임포트 테스트, `-L nogpu` | 있습니다. Dev 기동의 느슨한 트리 `.meta` 스캔 시간 전후(파일 열기 수는 같고 파싱 비용만 다름) |
| TX2 | 에디터 · 사용자 상태(패널 가시성, 뷰포트 오버레이, 외형 프리셋)를 리플렉션 JSON 으로 옮깁니다 | `Editor/Common/GUI/`, `Editor/Common/Workspace/`, `Editor/Viewport/`, `GameFramework/Base/Gameplay/Appearance/`, `Test/EditorTest/` | 없음 | 에디터 레이아웃 저장 · 복원 테스트, 외형 프리셋 왕복 테스트 | 없음 |
| TX3 | `KeyValueFile` · 퍼즈 대상 · `TestKeyValueFile` · `TestRequiredOptionalRead` 의 해당 케이스를 지우고 `EditorAssetType::Data` 확장자에서 `.ini` · `.kv` 를 뺍니다 | `Core/File/`, `Test/EngineTest/`, `Editor/Common/Workspace/` | TX1, TX2 | 전 프리셋 컴파일, `-L lint`, `-L nogpu` | 없음 |
| TX4 | JSON 파일 69 개와 확장자 19 종을 역할로 분류한 테이블을 이 문서(6.5절)에 남기고, 변환 스크립트 한 개를 만듭니다(변환 뒤 지움). 설정으로 남을 파일은 `ConfigManager`/`IConfig` 경로로만 읽히게 정리합니다 | `Resource/`, `Scripts/dev/`, 이 문서 | 없음 | 분류 테이블 검토 | 없음 |
| TX5 | 애니메이션 · 캐릭터 계열 JSON(`.skeleton` 10, `.rig` 7, `.vertexanimation` 5, `.bonelod` 5, `.animgraph` 4, `.sprite` 3, `.bonemask` 2, `.retarget` · `.facial` · `.visemes` 각 1, `clips`)을 XML 로 바꿉니다. 임포터 출력도 XML 로 바꾸고 커밋된 결과를 다시 임포트합니다 | `Engine/Animation/`, `Engine/Character/`, `Editor/Common/Asset/`, `Resource/**` | AT2, TX4 | 해당 폴더 테스트, 임포트 테스트, 애니메이션 대표 셋 실행, `-L nogpu` | 있습니다. 가장 큰 파일(`avatarsample_d.skeleton.json` 46 KB 등) 로드 시간 전후 |
| TX6 | 지역화 계열 JSON(`.strings` · `.translation` · `.locproject` · `.cultures` · `.tm`)을 XML 로 바꿉니다. `.po` · `.xlf` 교환 가져오기 · 내보내기는 그대로 둡니다 | `Engine/Localization/`, `Editor/Common/Localization/`, `Resource/editor/localization/`, `Resource/game/*/data/localization/` | AT3, TX4 | 지역화 테스트, 교환 왕복 테스트, 에디터 글 편집 시나리오 | 있습니다. `editor.strings.json`(174 KB) · `ko.translation.json`(136 KB) 로드 시간 전후 |
| TX7 | 남은 에셋 JSON(`.dialogue` · `.seq` · `.avatar` · `.tokens` 중 TX4 가 에셋으로 분류한 것)을 XML 로 바꾸고, 에셋 로더가 `XMLDocument` 만 받게 하는 서명을 굳힙니다. 에셋 폴더에서 `JSONSerializer` 호출을 막는 린트는 서명이 못 막는 틈이 남는 경우에만 둡니다 | `Engine/Dialogue/`, `Engine/Sequencer/`, `Editor/Common/Config/`, `Scripts/lint/` | AT2, AT3, TX4 | 대화 · 시퀀서 테스트, `-L lint`, `-L nogpu` | 없음 |

단위가 끝날 때 고칠 문서: [설정 문서](../07_Configuration.md)의 형식 문단, `AssetFormat.h` 머리 주석("JSON 은 도구 · 설정을 주고받는 데 씁니다"), 결정 기록 5-4 의 형식 항목, DataDriven 7.2절의 `KeyValueFile` 문장(D9 가 이 문서를 가리키게).

### 6.5 JSON 분류 (TX4 가 채운다)

TX4 가 `Resource/` 의 `.json` 69 개를 아래 열로 채운다. 지금까지 확인된 것은 출처 목록(`*.sources.json` 2개, 도구 데이터)이 설정 쪽이라는 것뿐이다.

| 파일 또는 확장자 | 개수 | 역할 | 처리 |
|---|---|---|---|
| (TX4 에서 채움) | | | |

### 6.6 위험

- **데이터 재작성량.** `.meta` 167 + JSON 69 = 236 개를 다시 쓴다. GUID 보존이 가장 크다(바뀌면 씬 참조가 깨진다). TX1 의 비교가 그 검사다.
- **XML 이 JSON 보다 크고 느리다.** 지역화 문서는 130 ~ 300 KB 로 가장 크다. 런타임은 쿠킹본이 아니라 원본을 읽는 문서가 있는지(느슨한 트리 Dev 에서만인지 팩에서도인지)를 TX6 에서 먼저 확인하고 측정한다. 측정이 나쁘면 해당 종류만 쿠킹본(바이너리)을 둔다.
- **임포터 출력 재생성.** 커밋된 임포트 결과(`.skeleton.json` 등)를 다시 만들면 임포트 스탬프가 모두 바뀌고 diff 가 크다. 종류별 단위로 나눠 한 번에 한 종류씩 한다.
- **D9 와 겹침.** 2절 테이블대로다.

## 7. 하지 않는 것

- **쿠킹된 셰이더 바이너리를 커밋하지 않는 것** — 계속 커밋한다(결정 기록 5-12 `구조` 행, 2026-10-20). 셰이더를 쿠킹하지 않는 리소스 작업자가 받자마자 쓰기 위해서다. PK3 의 스탬프 검사는 이 커밋된 바이너리가 소스와 맞는지 본다.
- **백엔드 등급제**(DX12 · Vulkan 만 전 기능) — 하지 않는다(같은 행). 네 백엔드를 같은 기능으로 유지한다.
- 외부 형식(`.po` · `.xlf` · glTF · VRM · `imgui.ini`)과 쿠킹 산출물 `assetregistry.txt`(사람이 편집하지 않는 쿠커 출력)는 6절의 대상이 아니다. 후자는 TX 가 끝난 뒤에도 남는 텍스트 줄 형식이라 필요하면 후보로 올린다.

## 8. 사용자 결정

정해졌다.

1. **네 정책을 한다**(사용자, 2026-10-20). 필수 설정(폴백 삭제와 설정 출처 하나), 팩 쿠킹의 C++ 이전, 에셋 종류의 등록 줄 하나 파생, 텍스트 형식의 역할별 하나와 `KeyValueFile` 삭제. 순서는 HC → PK → AT → TX.
2. **쿠킹된 셰이더 바이너리는 계속 커밋한다**(사용자, 2026-10-20, 결정 기록 5-12). 계획에 넣지 않는다.
3. **백엔드 등급제는 하지 않는다**(사용자, 2026-10-20, 결정 기록 5-12). 계획에 넣지 않는다.

아직 묻지 않았고 권고안으로 진행하는 것. 뒤집을 때는 해당 단위 전에 알린다.

- HC3 의 엔진 필드는 `GameSettings` 를 옮기지 않고 `<Engine>` 요소(엔진 소유 구조체)로 둔다(3.3절).
- TX2 의 플레이어 외형 프리셋은 옛 형식 리더 없이 새 JSON 으로 다시 쓴다(출시 전 데이터이므로).
- PK3 의 커밋 훅은 빌드된 `App` 이 없으면 셰이더 파일을 고친 커밋을 막는다.
