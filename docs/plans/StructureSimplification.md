# 구조 단순화 계획 (2026-10-20)

> 상태: 측정대기 · 선행: 없음 · 영역: engine · 옛번호: 없음 · 진행: 21/21

**남은 확인(통합에서 확인 · 측정):** 매니페스트 id `all-presets-compile`, `ctest-nogpu`, `ctest-lint`, `cook-pack-timing-release`, `dedicated-server-pack-exclusion`, `dev-meta-scan-time` — 각 단위 표의 "남은 것". 확인이 끝나면 통합 담당이 이 문서를 닫는다.

상용 엔진 비교에서 나온 구조 단순화 정책 네 개(필수 설정 HC → 팩 쿠킹 PK → 에셋 종류 등록 AT → 텍스트 형식 TX)입니다. 모두 "같은 사실을 두 곳에 적어 두어 한쪽만 바뀌면 조용히 어긋난다"는 같은 원인에서 나왔고, 코드는 모두 들어갔으며 남은 것은 검증 · 측정뿐입니다. 배경은 [결정 기록](../09_Decisions.md) 5-4(폴백 금지), 5-12 의 `구조` 행 둘, 2절 "에셋 로더 등록제" 보류(AT 가 재개)입니다.

## 1. 근본 원인

1. 값이 없을 때 대신 쓸 값을 읽는 코드가 가졌다(`ensureConfig` 의 `T{}`, `gamesettings.xml` 없이 이어 가기, Dev 서버 내장 기본값).
2. 쿠커가 엔진이 아닌 도구 언어(파이썬)로 쓰여 팩 레이아웃 · 셰이더 스탬프 수집 규칙 · 증분 기록이 두 벌이었다.
3. 에셋 종류가 늘 때마다 테이블을 하나씩 더 만들었다.
4. 텍스트 형식을 필요할 때마다 새로 골랐다(`.meta` · 에디터 상태는 `key=value`, JSON 에셋 19 종이 XML 에셋과 섞임).

## 2. 순서와 DataDriven 과의 경계

[DataDriven](DataDriven.md) D8(`LoadOutcome`)은 HC1 이 쓰고, D9 의 `KeyValueFile` 부분은 TX 가 통째로 지워 하지 않으며, 7.5 절 P1(`EngineDefaultAssets`)은 HC3 의 "비면 엔진 기본" 필드가 씁니다. 증분 판정은 `CookStore` · `CookJob`([결정 기록](../09_Decisions.md) 5-2)이 맡습니다. 동시 빌드는 둘까지이고 AT1(`AssetFormat.h`) · HC3(`GameConfig.h`)은 단독으로 돌립니다.

## 3. 필수 설정 — 폴백을 지운다 (HC)

**끝**(HC1 ~ HC4). 결정만 남깁니다.

- 출처는 서명에 둡니다: `loadHostConfig<T>( ConfigSource )`(`disk` · `baked`, 빌드 구성이 고름)는 파일이 없으면 `[[nodiscard]]` 오류로 기동을 멈추고, 없어도 되는 설정은 `loadOptionalConfig<T>` 가 `Absent` 를 돌려줍니다. `ServerApp` 의 Dev 분기는 지웠습니다.
- 호스트 프리셋 `GameConfig` 는 `PROPERTY( Required ) DataPath _packRoot` 하나이고 나머지는 팩의 `gamesettings.xml` 로 옮겼습니다. 엔진이 읽는 필드는 `<Engine>` 요소를 엔진 소유 구조체 `PackEngineSettings` 가 읽습니다(`GameSettings` 는 그 요소를 건너뜀). "비면 엔진 기본" 필드는 `Optional<DataPath>`, 창 제목은 마운트 뒤 `IWindow::setTitle`.
- 스키마 필드의 기본값은 건드리지 않습니다(DataDriven 2절). 지운 것은 파일 전체가 없을 때 구조체를 `T{}` 로 대신하는 길입니다.
- 기각: `GameSettings` 를 Engine 으로 옮겨 합치기(GameFramework 의 `GameInstanceBase` 가 적용해 이동이 큼), 엔진 필드를 두 번째 팩 파일로(원본 둘), `T{}` 대체를 오류 로그로 올리기(로그는 폴백을 못 막음). 언리얼의 계층형 `Default*.ini` 는 맞지 않다 [기억].

### 3.4 단위

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| HC4 | **끝.** `ConfigCatalog.py` · `Config/README.md` · 생성 문서 · `Config.py` 의 `loadToolchainConfig`(`None`) · `GenerateShippingHostDefaults.py`(`_packRoot` 하나뿐인 프리셋만) · `CheckGamePresets.py` · CMake 의 오류 무시 `string(JSON ERROR_VARIABLE)` 를 `sw_readOptionalJsonValue` 로 | `Scripts/generate/`, `Scripts/lint/gate/`, `docs/07_Configuration.md`, `docs/Config/` | HC3 | `-L lint` | 없음 |

### 3.5 위험

Dev 에서 `Config/` 없이 실행하면 이제 멈춥니다(`AllTests` 로 이 동작에 기댄 테스트 · 도구 확인). 에디터 설정 패널(`TestProjectSettings.cpp`)은 `Config/Game/*.json` 을 `GameConfig` 로 편집하므로 편집 대상이 `<Engine>` 요소로 가는지 `ux-reviewer` 캡처로 봅니다. `RHISubsystem.cpp` 의 `IWindow` 없을 때 창을 만드는 경로도 같은 설정 시점으로 맞춥니다.

## 4. 팩 쿠킹을 C++ 로 옮긴다 (PK)

결정: 쿠킹은 엔진 언어 한 곳에서 합니다. 팩 형식 원본은 손으로 쓴 `ResourcePakTypes.h`(`static_assert` 로 레이아웃 고정, `PackFormat.json` · `GeneratePackFormat.py` · `PackFormat.py` 삭제), 쓰기는 `ResourcePakWriter` 하나(쿠커와 `ResourcePakTestUtil` 공용), 스탬프 판정은 `ShaderCooker::verifyCookedShaders`(`App --verify-shaders`, `CheckShaderStamps` 삭제), 증분은 `CookStore` 작업 하나(`.packrec` 삭제), 팩 설정은 리플렉션 `IConfig`(`PackConfig`, 모든 칸 `Required`). 기각: 파이썬 쿠커 + 공유 레이아웃(규칙이 둘), 파이썬 확장 모듈로 노출(`App` 헤드리스 모드가 이미 같은 경로), 스탬프 규칙을 JSON 으로 공유(include 해석이라 데이터로 안 줄어듦).

### 4.4 단위

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| PK 검증 | **코드는 끝(PK1 `ResourcePakWriter` · `ResourcePakTypes.h`, PK2 `PackCooker` · `App --cook-packs` · `--cook-all` · `--pack-dir`, PK3 `App --verify-shaders` + 커밋 훅(`Resource/*/shaders/` 를 고친 커밋은 App 이 없으면 실패), PK4 파이썬 쿠커 · 대조 게이트 삭제와 CMake `CookAssets` · `MakePackage` 이전).** **남은 것**: 전 프리셋 컴파일 · `-L nogpu` · `-L lint`, 파이썬이 쓴 13 개 팩과 항목 목록 · 내용 비교(빈 항목 처리가 다를 수 있다 — 파이썬 zlib 8 바이트, 지금 0 바이트), 무변경 재쿠킹이 쓰지 않는지, 쿠킹 · 패키징 시간 Release 3회, `MakePackage` 로 Shipping 패키지 한 벌 | `Scripts/generate/`, `Source/Engine/Resource/Pack/`, `CMake/` | HC 와 무관 | 위 남은 것 | 있습니다(쿠킹 · 무변경 재쿠킹 · 패키징 시간). 파이썬 기준 시간은 옛 커밋(`85df5a61d^`)에서 잰다 |

### 4.5 위험

압축 결과가 파이썬과 달라 팩 키가 바뀌고 모든 팩이 한 번 다시 쓰입니다(항목 목록 · 내용 · CRC 가 같으면 받아들임). 셰이더 스탬프 검사는 빌드된 `App` 을 요구하며 "없으면 건너뛴다"는 두지 않습니다(셰이더 바이너리 커밋 유지 결정). `listPackNameOfGame` 의 다른 소비자가 없는지 `grep` 으로 확인합니다.

## 5. 에셋 종류를 등록 줄 하나에서 파생한다 (AT)

결정 기록 2절 "에셋 로더 등록제" 보류를 다시 엽니다(조건이던 대여섯 종을 넘어 20 종 이상). 종류 정의는 `AssetTypeDef` 하나(접미사 목록, 레이블, 쿠킹 역할, 팩 제외 그룹, 이관 버전)이고 `kExtension` 은 그 접미사를 가리킵니다. 쿠커 · 접미사 우선순위(긴 접미사 먼저, 레지스트리가 정렬) · 팩 제외 그룹 · 이관 키 · 에디터 경로 판정이 모두 같은 레지스트리를 읽고, 에디터는 표시 정보(아이콘 · 색 · 제목 · 임포터)만 가지며 엔진 종류와는 `AssetMatchRow::_engineTypeName` 으로 짝짓고 누락은 `AssetTypeCoverageTest` 가 막습니다. `CookContract.json` 은 `rhi_backends` · `default_rhi_backend` · 타깃별 제외 그룹 이름만 남습니다.

### 5.3 대안과 기각 이유

체크리스트 문서화(게이트보다 약함), 에디터 `EditorAssetType` 만 정리(엔진 쪽 사본이 남음), **카탈로그 종류(`ResourceDataKind`)를 같은 레지스트리에 합치기(AT4) — 기각(2026-10-22)**: 카탈로그는 경로 규칙(접두 · 포함 · 접미)과 스키마 검사 로더를 들고 Resource 층(티어 5)에서 등록하는데 에셋 종류 표는 접미사만 보는 티어 0 표라 한 구조체에 넣으면 칸이 절반 비고 층이 섞입니다(겹치던 접미사 글자는 `VfxEffectDef::kExtension` 을 가리키게 고침).

### 5.4 단위

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| AT 검증 | **AT1~AT5 코드는 끝**(`AssetTypeDef` · `AssetTypeRegistry` · `SW_ASSET_TYPE`(`Engine/Common/`, 티어 0), 엔진 타입 33 종 등록, `AssetKind` · `cook_suffixes` · `asset_kinds` 삭제, 제외 묶음 `_excludeGroup` · `SW_TARGET_EXCLUDED_GROUP_TABLE`). **남은 것**: 전 프리셋 컴파일, `AssetTypeRegistryTest` · `ResourceHostTargetTest` · 씬 · 프리팹 쿠킹 왕복, 전용 서버 팩이 텍스처 · 셰이더 바이너리 · 오디오를 빼는지, `-L lint` · `-L nogpu` | 위 폴더 | PK | 위 남은 것 | 없음 |

### 5.5 위험

정적 등록이 링크에서 빠질 위험은 `AssetTypeRegistryTest` 가 접미사 상수를 쓰는 모든 타입의 등록을 봅니다. 에디터 188 줄 29 파일이라 다른 에디터 단위와 같은 시간대에 병합하지 않습니다. 순서에 기댄 규칙(`.png` 가 Texture 와 SpriteClip 이미지 양쪽)은 케이스로 옮깁니다. 키트 DLL 이 등록한 종류는 모듈 언로드 때 `ResourceDataKindRegistrar` 와 같은 패턴으로 빠져야 합니다.

## 6. 텍스트 형식을 역할마다 하나로 (TX)

**역할 규칙**: 저작자가 편집하고 리소스 id · GUID 로 참조되며 쿠킹 대상이면 XML, 시작 때 `ConfigManager` 가 읽는 호스트 · 도구 설정과 사용자 상태는 JSON 입니다. 형식은 등록이 막습니다(TX7): `AssetTypeRegistry::isSuffixFormatAllowed` 가 `.json` 접미사를 쿠킹 입력(`CookSource`, 지금은 `.prefab.json` 하나)으로만 받고(`AssetTypeRegistryTest.JSONSuffixIsOnlyACookSource`), 에셋 로더는 `XMLDocument` 만 받으며 JSON 읽기는 `ConfigManager`/`IConfig` 에서만 닿습니다. 에셋 폴더 코드가 `JSONSerializer` 를 직접 부르는 틈만 린트 게이트로 둡니다. `.meta` 는 XML(`<AssetMeta guid sourcePath imported/>`), 에디터 · 사용자 상태는 리플렉션 + JSON 이고 옛 형식 리더는 두지 않습니다. 외부 규격(`.po` · `.xlf` · glTF · VRM · `imgui.ini`)은 건드리지 않고, 임포터가 쓰는 JSON 은 XML 을 쓰게 바꿔 다시 임포트합니다.
기각: `KeyValueFile` 만 지우고 JSON 에셋 유지(로더가 형식을 고르는 구조가 남음), 전부 JSON(에셋 XML 은 직렬화 · 마이그레이션 · 참조가 이미 XML), `.meta` 를 JSON(에셋 곁에 놓이는 에셋의 일부).

### 6.4 단위

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| TX 검증 | **TX1~TX4 코드는 끝**(TX1 `AssetMeta` 를 `toXML` · `parseXML` 로만 읽고 쓰며 깨진 `.meta` 를 새 GUID 로 덮어쓰지 않음, `.meta` 167 개 재작성 · GUID 동일 확인. TX2 `EditorPanelVisibility` · `EditorViewportOverlayFile` · `UserAppearancePresetFile`(판 3, 옛 판 리더 삭제). TX3 `KeyValueFile` 삭제). **남은 것**: 전 프리셋 컴파일, 에셋 레지스트리 생성 결과 비교 · GUID 복구 · 임포트 테스트, 에디터 레이아웃 저장 · 복원, 외형 프리셋 왕복, `-L lint` · `-L nogpu`, Dev 기동 `.meta` 스캔 시간 전후 | 위 폴더 | AT 검증 | 위 남은 것 | 있습니다(`.meta` 스캔 시간) |

### 6.5 JSON 분류 (TX4 가 채운다)

`Resource/` 의 `.json` 69 개. 에셋(임포터 산출물 · 저작 데이터, 팩에 듦)은 XML 로: `.skeleton` · `.rig` · `.vertexanimation` · `.bonelod` · `.animgraph` · `.sprite` · `.bonemask` · `.retarget` · `.facial` · `.visemes`(39, TX5), `engine/animation/` 의 `animationcrowd.json` · `animationlod.json` · `lipsync.json`(3, TX5), 지역화 `.strings` · `.translation` · `.locproject` · `.cultures` · `.tm`(13, TX6, `.po` · `.xlf` 교환은 그대로), `.dialogue` · `.seq` · `.avatar` · `common/sql/*/migrations.json`(5, TX7). JSON 유지: `editor/theme/*.tokens.json`(5, 에디터 표시 데이터 — 읽기 경로가 `ConfigManager` 의 엄격한 JSON 읽기인지 TX7 에서 확인), 도구 데이터 `*.sources.json` · `models_raw/**/*.clips.json` · `engine/automation/allowedlogs.json`(4). 변환 스크립트는 종류마다 하나씩 만들고 변환 뒤 지웁니다(범용 변환기 없음).

### 6.6 위험

재작성량이 `.meta` 167 + JSON 69 = 236 개이고 GUID 보존이 가장 큽니다(TX1 비교가 검사). 지역화 XML 은 130 ~ 300 KB 로 크므로 팩에서도 원본을 읽는지 TX6 에서 먼저 확인하고 측정해 나쁘면 그 종류만 쿠킹본을 둡니다. 임포트 결과를 다시 만들면 스탬프가 모두 바뀌므로 종류별로 한 번에 한 종류씩 합니다.

## 7. 하지 않는 것

쿠킹된 셰이더 바이너리를 커밋하지 않는 것(계속 커밋, 5-12 `구조` 행 — PK3 스탬프 검사가 소스와 맞는지 봄), 백엔드 등급제(네 백엔드를 같은 기능으로 유지), 외부 형식과 `assetregistry.txt` 의 형식 통일.

## 8. 사용자 결정

정해졌다: 네 정책을 한다(2026-10-20), 셰이더 바이너리는 계속 커밋, 백엔드 등급제는 하지 않는다(결정 기록 5-12). 권고안으로 진행해 구현한 것: HC3 의 엔진 필드는 `<Engine>` 요소, TX2 외형 프리셋은 옛 형식 리더 없이 새 JSON(판 3), PK3 커밋 훅은 빌드된 `App` 이 없으면 `Resource/*/shaders/` 를 고친 커밋을 막는다(쿠킹 표만 고친 커밋은 `ShaderCookStampTest` 가 본다).
