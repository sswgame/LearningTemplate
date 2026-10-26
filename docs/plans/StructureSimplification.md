# 구조 단순화 계획 (2026-10-20)

> 상태: 측정대기 · 선행: 없음 · 영역: engine · 옛번호: 없음 · 진행: 21/21

**남은 확인(통합에서 확인 · 측정):** 매니페스트 id `all-presets-compile`, `ctest-nogpu`, `ctest-lint`, `cook-pack-timing-release`, `dedicated-server-pack-exclusion`, `dev-meta-scan-time` — 각 단위 표의 "남은 것". 확인이 끝나면 통합 담당이 이 문서를 닫는다.

상용 엔진 비교에서 나온 구조 단순화 정책 네 개를 한 문서로 묶는다. 모두 "같은 사실을 두 곳에 적어 두어서, 한쪽만 바뀌면 조용히 어긋난다"는 같은 근본 원인에서 나왔다.
순서는 필수 설정(HC) → 팩 쿠킹(PK) → 에셋 종류 등록(AT) → 텍스트 형식(TX)이다. PK 가 끝나면 쿠커 쪽 파이썬 소비자가 사라져 AT 의 접미사 테이블 정리가 싸지고, AT 의 등록 줄 하나가 있으면 TX 의 변환이 종류별 로더 하나를 바꾸는 일이 된다.
배경은 [결정 기록](../09_Decisions.md) 5-4 "유지보수성을 위한 데이터화"(폴백 금지), 5-12 의 `구조` 행 둘, 2절 "에셋 로더 등록제" 보류(이 문서가 재개한다)다.

## 1. 근본 원인

1. **값이 없을 때 대신 쓸 값을 읽는 코드가 가진다.** `ensureConfig` 가 파일이 없으면 `T{}` 를 만들고, 게임 인스턴스는 `gamesettings.xml` 이 없어도 이어 가며, 서버는 Dev 에서만 내장 기본값으로 돈다. 필수와 선택이 호출 서명에 없다.
2. **쿠커가 엔진이 아닌 도구 언어로 쓰였다.** 파이썬이 팩을 쓰는 까닭은 표준 `zlib` 때문이었고, 그 결과 팩 레이아웃 · 셰이더 스탬프 수집 규칙 · 팩 증분 기록이 C++ 과 파이썬에 두 벌이다.
3. **에셋 종류가 늘 때마다 테이블을 하나씩 더 만들었다**(5.1절).
4. **텍스트 형식을 필요할 때마다 새로 골랐다.** XML 에셋 · JSON 설정이 있는데도 `.meta` · 에디터 상태는 `key=value`(`KeyValueFile`)이고 JSON 에셋 19 종이 따로 있다.

## 2. 순서와 DataDriven 과의 경계

[DataDriven](DataDriven.md) 과 겹치는 단위는 그 문서의 이름으로 가리킨다.

| DataDriven 단위 | 관계 |
|---|---|
| D8 `LoadOutcome { Loaded, Absent, Failed }` | HC1 이 필수 설정의 `Absent` 를 오류로 다룬다. `ConfigReadResult::Missing` 은 `Absent` 와 같은 값 |
| D9 폴백 인자 읽기 함수 삭제 | `KeyValueFile` 은 TX 가 통째로 지우므로 D9 의 `KeyValueFile` 부분은 하지 않는다 |
| 7.5 절 P1 `EngineDefaultAssets` | HC3 의 "비면 엔진 기본" 필드가 쓰는 경로 |
| 7.5 절 P4 / W5 | HC1 · HC4 테스트 정리는 같은 파일을 만지므로 같은 작업자. PK4 가 `CookAssets.py` 를 지우면 `readOptionalConfig` 의 쿠커 소비자가 사라진다 |

증분 판정은 `CookStore` · `CookJob`([결정 기록](../09_Decisions.md) 5-2)이 맡는다. 동시 빌드는 둘까지이고 AT1(`AssetFormat.h`) · HC3(`GameConfig.h`)은 단독으로 돌린다.

## 3. 필수 설정 — 폴백을 지운다 (HC)

**끝**(HC1 ~ HC4, 마지막 `wt/hc4done`). 설계 결정만 남긴다.

### 3.1 현재 구조

(해소됨) `ensureConfig<T>` 가 디스크 → 생성 JSON → `T{}` 순으로 폴백했고, `GameInstanceBase::initialize` 도 `gamesettings.xml` 읽기 실패를 "using defaults" 로 넘겼다. 게임 설정은 `Config/Game/<게임>.json`(`GameConfig`)과 팩의 `data/gamesettings.xml`(`GameSettings`) 두 파일이었다.

### 3.2 해법

- 출처는 서명에 둔다: `loadHostConfig<T>( ConfigSource )`(`disk( path )` · `baked( json )`, 빌드 구성이 고름). 파일이 없으면 오류, `[[nodiscard]]` 결과로 기동을 멈춘다. 없어도 되는 설정은 `loadOptionalConfig<T>` 가 `Absent` 를 돌려준다. `ServerApp` 의 Dev 분기는 지웠다.
- 호스트 프리셋 `GameConfig` 는 `PROPERTY( Required ) DataPath _packRoot` 하나다. 나머지 필드는 팩의 `gamesettings.xml` 로 옮겼다.
- 엔진이 읽는 필드는 `gamesettings.xml` 의 `<Engine>` 요소를 엔진 소유 구조체 `PackEngineSettings` 가 읽는다(`GameSettings` 는 그 요소를 건너뜀). 요소마다 읽는 계층이 하나다.
- "비면 엔진 기본" 필드(UI 규칙 · 렌더 파이프라인)는 `Optional<DataPath>`, 창 제목은 마운트 뒤 `IWindow::setTitle`.
- 스키마 필드의 기본값은 건드리지 않는다(DataDriven 2절). 지우는 것은 **파일 전체가 없을 때** 구조체를 `T{}` 로 대신하는 길이다.

### 3.3 대안과 기각 이유

- `GameSettings` 를 Engine 으로 옮겨 합치기 — 기각(GameFramework 의 `GameInstanceBase` 가 적용하고 `SW_GF_API` · 키트 소비자까지 옮겨야 한다). 요소를 가르는 쪽이 이동이 작다.
- 엔진 필드를 두 번째 팩 파일로 — 기각("원본 하나"의 반대).
- `T{}` 대체를 경고에서 오류 로그로 — 기각(로그는 폴백을 막지 못한다, 서명에서 경로를 없앤다).
- 언리얼은 `Base*.ini` 위에 `Default*.ini` 를 덮는 계층형이라 맞지 않고, 유니티 · Godot 은 파일이 없으면 프로젝트가 열리지 않는다 [기억].

### 3.4 단위

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| HC4 | **끝(`wt/hc4done`).** `ConfigCatalog.py` · `Config/README.md` · 생성 문서 · `Config.py` 의 `loadToolchainConfig`(`None`) · `GenerateShippingHostDefaults.py`(`_packRoot` 하나뿐인 프리셋만) · `CheckGamePresets.py`(스키마 · 텔레메트리 검사를 `<Engine>` 쪽으로) · CMake 의 오류 무시 `string(JSON ERROR_VARIABLE)` 를 `sw_readOptionalJsonValue` 로. `FileScan.py` · `AssetValidation` 은 같은 모양이 아니라 둔다(W5 끝). Shipping 컴파일은 C++ 무변경이라 생략 | `Scripts/generate/`, `Scripts/lint/gate/`, `Scripts/common/ConfigCatalog.py`, `docs/07_Configuration.md`, `Config/README.md`, `docs/Config/` | HC3 | `-L lint`, 생성 문서 점검 | 없음 |

### 3.5 위험

- Dev 에서 `Config/` 없이 실행하면 이제 멈춘다 — `AllTests` 전체로 이 동작에 기댄 테스트 · 도구를 확인한다.
- 에디터 설정 패널(`TestProjectSettings.cpp`)이 `Config/Game/*.json` 을 `GameConfig` 로 편집한다 — 편집 대상이 `<Engine>` 요소로 가는지 정하고 `ux-reviewer` 캡처로 본다.
- `RHISubsystem.cpp` 가 `IWindow` 없을 때 창을 만드는 두 번째 경로도 같은 설정 시점으로 맞춘다. Shipping 서버는 팩을 못 읽으면 이전보다 일찍 멈춘다(의도).

## 4. 팩 쿠킹을 C++ 로 옮긴다 (PK)

### 4.1 현재 구조

(해소됨) `Scripts/generate/CookAssets.py` 가 팩 바이트 쓰기 · 압축 · 셰이더 스탬프 판정 · 팩 파일 고르기 · 증분 기록(`.packrec`) · 팩 이름 목록을 파이썬으로 했고, 팩 레이아웃은 `PackFormat.json` 에서 헤더와 파이썬 객체가 따로 나왔으며 테스트에 세 번째 쓰기가 있었다. 씬 · 프리팹 · 셰이더는 이미 `App --cook-*` 였다.

### 4.2 해법

- 쿠킹은 엔진 언어 한 곳에서 한다(언리얼 `-run=cook` · 유니티 · Godot 모두 엔진 쪽 언어 [기억]).
- 팩 형식 원본은 손으로 쓴 `ResourcePakTypes.h`(`static_assert` 로 레이아웃 고정). `PackFormat.json` · `GeneratePackFormat.py` · `Scripts/common/PackFormat.py` 는 지웠다.
- 쓰기는 `Engine/Resource/Pack/` 의 `ResourcePakWriter` 하나(쿠커와 `ResourcePakTestUtil` 공용).
- 스탬프 판정은 `ShaderCooker::verifyCookedShaders` 한 곳(`App --verify-shaders`), `CheckShaderStamps` 게이트는 필요 없어졌다.
- 증분 판정은 `CookStore` 작업 하나, `.packrec` 은 지웠다. `PackConfig.json` 은 리플렉션 `IConfig`(모든 칸 `Required`).

### 4.3 대안과 기각 이유

파이썬 쿠커 + 공유 레이아웃(지금 구조), C++ 쓰기를 파이썬 확장 모듈로 노출, 스탬프 규칙을 JSON 으로 공유 — 모두 기각(언어가 둘이면 규칙이 둘이고, 규칙이 include 해석이라는 코드라 데이터로 줄지 않는다).

### 4.4 단위

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| PK 검증 | **코드는 끝(`wt/structpk`, 빌드 없이 썼다).** PK1 `ResourcePakWriter` · `ResourcePakTypes.h`. PK2 `PackCooker` · `PackConfig` · `App --cook-packs` · `--cook-all` · `--pack-dir`, 타깃 제외는 `ResourcePaths::isExcludedForTarget`(런타임과 같은 표), 증분은 `CookStore` 작업 하나(출력이 기록 바이트면 쓰지 않게 `CookStore` 를 고침). PK3 `verifyCookedShaders`(도구 지문 포함) · `App --verify-shaders`, `CheckShaderStamps` 삭제, `ShaderCookStampTest` 두 케이스, 커밋 훅은 `Resource/*/shaders/` 를 고친 커밋에서 `App --verify-shaders` 를 부르고 App 이 없으면 실패. PK4 `CookAssets.py` · `PackFormat.py` · `GeneratePackFormat.py` · `PackFormat.json` · `CheckCookContract` 삭제, CMake `CookAssets` · `MakePackage` 가 `--cook-all` 을 부름, 팩 이름 목록은 `MakePackage` 로. **남은 것**: 전 프리셋 컴파일 · `-L nogpu` · `-L lint`, 파이썬이 쓴 13 개 팩과 항목 목록 · 내용 비교(빈 항목 처리가 다를 수 있다 — 파이썬 zlib 8 바이트, 지금 0 바이트), 무변경 재쿠킹이 쓰지 않는지, 쿠킹 · 패키징 시간 Release 3회, `MakePackage` 로 Shipping 패키지 한 벌 | 위 넷 | HC 와 무관 | 위 남은 것 | 있습니다(쿠킹 · 무변경 재쿠킹 · 패키징 시간). 파이썬 기준 시간은 옛 커밋(`85df5a61d^`)에서 잰다 |

### 4.5 위험

- 코덱이 같은 zlib 이라도 압축 결과가 달라 팩 키가 바뀌고 모든 팩이 한 번 다시 쓰인다. 항목 목록 · 내용 · CRC 가 같으면 받아들인다.
- 셰이더 스탬프 검사가 빌드된 `App` 을 요구한다 — `Resource/*/shaders/*` 를 고치는 커밋에 한정되고 "App 이 없으면 건너뛴다"는 두지 않는다(커밋 유지 결정, 5-12 `구조` 행).
- `listPackNameOfGame` 의 다른 소비자가 없는지 `grep` 으로 확인한다.

## 5. 에셋 종류를 등록 줄 하나에서 파생한다 (AT)

결정 기록 2절 "에셋 로더 등록제" 보류를 다시 연다(조건이던 대여섯 종을 넘어 20 종 이상).

### 5.1 현재 구조

(해소됨) 한 종류의 사실이 `AssetKind` 열거 · `CookContract.json`(`cook_suffixes` · `asset_kinds` · `target_excluded_asset_kinds`) · `SW_RESOURCE_DATA_KIND` · `IAssetCache`/`SW_BUILT_IN_ASSET_CACHE` · `EditorAssetType`(열거 · 정보 테이블 · 경로 판정 · 확장자 배열) · 타입별 `kExtension`(36 개) 여섯 곳에 따로 적혔다.

### 5.2 해법

- 종류 정의는 `AssetTypeDef` 하나(접미사 목록, 레이블, 쿠킹 역할, 팩 제외 그룹, 이관 버전). `kExtension` 은 이 정의의 접미사를 가리킨다.
- 쿠커 · 접미사 우선순위(긴 접미사 먼저, 레지스트리가 정렬로 가짐) · 팩 제외 그룹 · 이관 키 · 에디터 경로 판정이 모두 같은 레지스트리를 읽는다.
- 에디터는 표시 정보(아이콘 · 색 · 패널 제목 · 임포터)만 가진다. 엔진 종류와의 짝은 `AssetMatchRow::_engineTypeName`, 표시 정보 누락은 `AssetTypeCoverageTest` 가 막는다.
- `CookContract.json` 은 `rhi_backends` · `default_rhi_backend` · 타깃별 제외 그룹 이름만 남는다.

### 5.3 대안과 기각 이유

- 체크리스트 문서화 — 기각(게이트보다 약하다).
- 에디터 `EditorAssetType` 만 정리 — 기각(엔진 쪽 사본 둘이 남는다).
- 카탈로그 종류(`ResourceDataKind`)를 같은 레지스트리에 합침(AT4) — 기각(2026-10-22). 카탈로그는 경로 규칙(접두 · 포함 · 접미)과 스키마 검사 로더를 들고 Resource 층(티어 5)에서 등록하는데, 에셋 종류 표는 접미사만 보는 티어 0 표라 한 구조체에 넣으면 칸이 절반 비고 층이 섞인다. 겹치던 접미사 글자는 `VfxEffectDef::kExtension` 을 가리키게 고쳤다.
- 언리얼 `UFactory`/`UAssetDefinition` 처럼 임포트 · 표시까지 한 등록 — 일부만(엔진 레지스트리가 원본, 에디터 표시 정보는 바깥).

### 5.4 단위

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| AT 검증 | **AT1~AT5 코드는 끝(`wt/structpk`, 빌드 없이 썼다).** AT1 `AssetTypeDef` · `AssetTypeRegistry` · `SW_ASSET_TYPE`(`Engine/Common/`, 티어 0), 접미사는 길이 내림차순 · 같으면 등록 순, 겹친 이름 · 접미사는 등록하지 않고 문제로 남김. `AssetKind` · `AssetFormatVersions` 삭제(판은 `_formatVersion`, 이관 키는 종류 이름). 씬 · 프리팹 · 머티리얼 · 머티리얼 인스턴스 이전. `cook_suffixes` 삭제. AT2 · AT3 `kExtension` 가진 엔진 타입 33 종 등록(캐시 등록과 종류 등록은 나란히). AT5 제외 묶음(`_excludeGroup`) · Texture · Audio · ShaderBinary 종류, `asset_kinds` 삭제, `target_excluded_asset_kinds` 는 묶음 이름만(`SW_TARGET_EXCLUDED_GROUP_TABLE`), `ResourcePaths` 제외가 종류 표로 판정. **남은 것**: 전 프리셋 컴파일, `AssetTypeRegistryTest` · `ResourceHostTargetTest` · 씬 · 프리팹 쿠킹 왕복, 전용 서버 팩이 텍스처 · 셰이더 바이너리 · 오디오를 빼는지, `-L lint` · `-L nogpu` | 위 폴더 | PK | 위 남은 것 | 없음 |

### 5.5 위험

- 정적 등록이 링크에서 빠질 위험 — 기존 등록자와 같은 패턴이고 `AssetTypeRegistryTest` 가 접미사 상수를 쓰는 모든 타입이 등록됐는지 본다.
- 에디터 188 줄 29 파일이라 충돌이 많다 — 다른 에디터 단위(`EditorCharacterEditor` 등)와 같은 시간대에 병합하지 않는다.
- 접미사 판정 순서는 정렬 규칙(긴 접미사 우선)으로 대신하며 순서에 기댄 규칙(`.png` 가 Texture 와 SpriteClip 이미지 양쪽)은 케이스로 옮긴다. 키트 DLL 이 등록한 종류는 모듈 언로드 때 빠져야 한다(`ResourceDataKindRegistrar` 와 같은 패턴).

## 6. 텍스트 형식을 역할마다 하나로 (TX)

### 6.1 현재 구조

(해소됨) 파서가 XML · JSON · `KeyValueFile` 셋이었다. `KeyValueFile` 소비자는 `.meta` 167 개 · 에디터 패널 가시성 · 뷰포트 오버레이 · 사용자 외형 프리셋 · 퍼즈 대상이었고, `Resource/` 의 `.json` 69 개(JSON 에셋 확장자 19 종)와 XML 에셋이 같은 역할로 섞여 있었다. 도구 출처 목록 `*.sources.json`(결정 기록 5-12 `리소스 C-44`)은 도구 데이터라 충돌하지 않는다.

### 6.2 해법

- **역할 규칙**: 저작자가 편집하고 리소스 id · GUID 로 참조되며 쿠킹 대상이면 XML, 시작 때 `ConfigManager` 가 읽는 호스트 · 도구 설정과 사용자 상태는 JSON.
- **형식은 등록이 막는다**(TX7): `AssetTypeRegistry::isSuffixFormatAllowed` 가 `.json` 접미사는 쿠킹 입력(`CookSource`, 지금은 `.prefab.json` 하나)으로만 받는다(`AssetTypeRegistryTest.JSONSuffixIsOnlyACookSource`). 에셋 로더는 `XMLDocument` 만 받고 JSON 읽기는 `ConfigManager`/`IConfig` 에서만 닿는다. 에셋 폴더 코드가 `JSONSerializer` 를 직접 부르는 틈만 린트 게이트로 둔다.
- `.meta` 는 XML(`<AssetMeta guid sourcePath imported/>`, `guid` · `sourcePath` `Required`). 에디터 · 사용자 상태는 리플렉션 + JSON, 옛 형식 리더는 두지 않고 다시 쓴다.
- 외부 규격(`.po` · `.xlf` · glTF · VRM · `imgui.ini`)은 건드리지 않는다. 임포터가 쓰는 JSON 은 XML 을 쓰게 바꾸고 커밋된 결과를 다시 임포트한다.

### 6.3 대안과 기각 이유

`KeyValueFile` 만 지우고 JSON 에셋 유지(로더가 형식을 고를 수 있는 구조가 남는다), 전부 JSON(에셋 XML 은 직렬화 · 마이그레이션 · 참조가 이미 XML 로 선다), `.meta` 를 JSON(에셋 곁에 놓이는 에셋의 일부라 에셋 형식을 따른다 — 유니티 `.meta` 는 YAML [기억]) — 모두 기각.

### 6.4 단위

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| TX 검증 | **TX1~TX4 는 끝(`wt/structpk`, 빌드 없이 썼다).** TX1 `AssetMeta`(REFLECT)를 `toXML` · `parseXML` 로만 읽고 씀 — `AssetDatabase` 와 모델 임포터 공용. `loadMetaFile` 은 `AssetMetaReadResult`(없음 · 깨짐 구분), 깨진 `.meta` 를 새 GUID 로 덮어쓰지 않는다. `.meta` 167 개를 다시 썼고 GUID 동일을 확인했다. TX2 `EditorPanelVisibility`(windows.json) · `EditorViewportOverlayFile` · `UserAppearancePresetFile`(판 3, 옛 판 리더 삭제)을 리플렉션 JSON 으로. TX3 `KeyValueFile` · 퍼즈 대상 · 케이스 삭제(남은 무관한 두 케이스는 `TestStringTableAndGameConfig.cpp`). TX4 아래 6.5절. **남은 것**: 전 프리셋 컴파일, 에셋 레지스트리 생성 결과 비교 · GUID 복구 · 임포트 테스트, 에디터 레이아웃 저장 · 복원, 외형 프리셋 왕복, `-L lint` · `-L nogpu`, Dev 기동 `.meta` 스캔 시간 전후 | 위 폴더 | AT 검증 | 위 남은 것 | 있습니다(`.meta` 스캔 시간) |

### 6.5 JSON 분류 (TX4 가 채운다)

`Resource/` 의 `.json` 69 개(2026-10-22 직접 센 값).

| 파일 또는 확장자 | 개수 | 역할 | 처리 |
|---|---|---|---|
| `.skeleton` · `.rig` · `.vertexanimation` · `.bonelod` · `.animgraph` · `.sprite` · `.bonemask` · `.retarget` · `.facial` · `.visemes` | 39 | 에셋(임포터 산출물 · 저작 데이터, 팩에 든다) | XML (TX5) |
| `engine/animation/` 의 `animationcrowd.json` · `animationlod.json` · `lipsync.json` | 3 | 에셋(고정 리소스 경로로 읽는 조정 표, 팩에 든다) | XML (TX5) |
| `.strings` · `.translation` · `.locproject` · `.cultures` · `.tm` | 13 | 에셋(지역화 문서) | XML (TX6). `.po` · `.xlf` 교환은 그대로 |
| `.dialogue` · `.seq` · `.avatar`(VRM 임포터 출력도) · `common/sql/*/migrations.json` | 1 · 1 · 1 · 2 | 에셋 | XML (TX7) |
| `editor/theme/*.tokens.json` | 5 | 에디터 표시 데이터(팩에 들지 않음) | JSON 유지. 읽기 경로가 `ConfigManager` 의 엄격한 JSON 읽기인지 TX7 에서 확인 |
| `*.sources.json` · `models_raw/**/*.clips.json` · `engine/automation/allowedlogs.json` | 2 · 1 · 1 | 도구 데이터 | JSON 유지 |

변환 스크립트는 로더를 XML 로 바꾸는 단위(TX5~TX7)가 종류마다 하나씩 만들고 변환 뒤 지운다 — 범용 변환기는 두지 않는다.

### 6.6 위험

- 재작성량이 `.meta` 167 + JSON 69 = 236 개다. GUID 보존이 가장 크다(TX1 비교가 검사).
- 지역화 문서는 XML 이 130 ~ 300 KB 로 크다 — 팩에서도 원본을 읽는지 TX6 에서 먼저 확인하고 측정한다. 나쁘면 그 종류만 쿠킹본(바이너리)을 둔다.
- 커밋된 임포트 결과를 다시 만들면 임포트 스탬프가 모두 바뀌고 diff 가 크다 — 종류별로 한 번에 한 종류씩.

## 7. 하지 않는 것

- 쿠킹된 셰이더 바이너리를 커밋하지 않는 것 — 계속 커밋한다(5-12 `구조` 행, 2026-10-20). PK3 스탬프 검사가 소스와 맞는지 본다.
- 백엔드 등급제(DX12 · Vulkan 만 전 기능) — 하지 않는다(같은 행). 네 백엔드를 같은 기능으로 유지한다.
- 외부 형식과 쿠킹 산출물 `assetregistry.txt` 는 6절의 대상이 아니다.

## 8. 사용자 결정

정해졌다: 네 정책을 한다(HC → PK → AT → TX, 2026-10-20), 셰이더 바이너리는 계속 커밋, 백엔드 등급제는 하지 않는다(결정 기록 5-12).

권고안으로 진행한 것(뒤집을 때는 해당 단위 전에 알린다): HC3 의 엔진 필드는 `<Engine>` 요소로 둔다(3.3절). TX2 외형 프리셋은 옛 형식 리더 없이 새 JSON 으로(구현함, 판 3). PK3 커밋 훅은 빌드된 `App` 이 없으면 `Resource/*/shaders/` 를 고친 커밋을 막는다(구현함, 쿠킹 표만 고친 커밋은 `ShaderCookStampTest` 가 본다).
