# 데이터 주도 구조 정리 계획 (2026-10-16)

> 상태: 구현중 · 선행: 없음 · 영역: engine · 옛번호: C-30, C-36 · 진행: 13/14

값의 정본은 데이터 파일이고 코드 · 헤더는 형식만 가진다. 필수 값이 없으면 조용히 기본값 · 대체로 가지 않고 오류로 멈춘다. 이 문서는 6 영역 조사(2026-10-16)가 드러낸 위반을 근본 원인부터 없애는 순서다. 진행 표는 린트 게이트 `CheckDataDrivenStructure`의 `--summary` 가 정본이다(기준선이 줄면 진척).

## 1. 근본 원인

1. **기본값이 C++ 멤버 초기값에 산다.** `EngineDefaultAssets.h` 경로, 컴포넌트 `_materialPath{ "engine/materials/…" }`, 패널의 처음 문서, 게임 `*Game.cpp` 의 카탈로그 · 오디오 경로. 데이터 파일은 "기본값과 다른 것만 적는다"고 되어 있어 정본이 코드다.
2. **읽기 결과를 버릴 수 있다.** 로더가 `bool` 을 돌려주고 호출부가 `(void)load…` 로 버리거나 경고만 남기고 `return true`. "필수" 라는 뜻이 형식에 없다.
3. **같은 값의 정본이 둘이다.** 카탈로그 경로가 게임 코드와 데이터 종류 등록표에, 로딩 화면 경로가 `GameSettings.h` 와 `LoadingScreenController.h` 에 따로 있다.
4. **테스트가 정본을 우회한다.** `EngineDefaultAssets{}` 를 기본 생성해 C++ 초기값을 정본처럼 쓰고, 경로 리터럴이 흩어져 있다.
5. **막는 장치가 없었다.** `CheckDataDrivenLiterals` 는 PROPERTY 숫자 · 문자 리터럴 기본값만 본다.

## 2. 구조적 해법 (게이트는 마지막 안전망이다)

원칙: 위반을 **쓸 수 없는 형식**으로 만든다. 린트 게이트는 형식이 막지 못하는 틈에만 둔다. 게이트로 시작하면 위반을 만드는 구조를 그대로 둔 채 감시만 하게 된다.

- **경로는 문자열이 아닌 형식으로**: 엔진 기본 에셋 · 시스템 설정 · 게임 설정의 경로 칸은 `DataPath`(가칭) 형식이다. 기본 생성자가 없고 초기값 리터럴로 만들 수 없으며 오직 데이터 읽기(`GameDataXML::readFields`)만 값을 채운다. 키가 없거나 파일이 없으면 기동 · 쿠킹 · 로드가 오류로 멈춘다.
- **필수 / 선택을 형식에 드러낸다**: `PROPERTY( Required )` 와 `Optional<T>` 처럼 "없음"이 형식에 보이는 칸. 기본값을 코드가 채우는 세 번째 길을 없앤다.
- **읽기 결과는 버릴 수 없다**: 설정 로더는 `[[nodiscard]]` 결과를 돌려주고 호출부가 오류로 멈출지 고른다(`-Werror=unused-result`). `bool` 을 돌려주고 경고만 남기는 모양 자체를 없앤다.
- **정본은 한 곳, 파생은 코드가 만든다**: 엔진 기본 경로는 `enginedefaultassets.xml`, 게임 경로는 게임의 `gamesettings.xml`.
- **이름은 상수 한 곳에서 파생**: 패널 id 는 `kPanelId` 하나, 입력 액션 · 카탈로그 id 는 데이터가 정의하고 코드는 핸들 `PROPERTY` 로 부른다.
- **명시적 오류 에셋만 대체로 남긴다**: 마젠타 누락 텍스처 · `missingmaterial.material` 은 데이터가 가리키고 오류 로그를 남기는 한 경로로만 쓴다.
- **계층 역전을 끊는다**: `GameFramework` 에디터가 `game/shooter3d/…` 를 직접 가리키지 않고 활성 게임의 설정에서 읽는다.
- **테스트는 정본을 통한다**: `EngineDefaultAssets{}` 기본 생성이 컴파일되지 않으므로 테스트도 데이터에서 읽는 도우미를 쓴다.
- **게이트(`CheckDataDrivenStructure`)는 마지막**: 형식이 막지 못하는 곳(`.cpp` 안의 문자열 리터럴, 파이썬 · CMake 폴백)만 기준선 동결로 지킨다. 기준선은 0 으로 수렴해야 하고, 형식으로 막힌 종류는 게이트에서 뺀다.

## 3. 작업 묶음 (순서와 소유 파일)

동시 빌드는 둘이라 코드 변경 묶음은 둘씩만 돈다. 끝난 묶음: W0, W1 의 엔진 기본 에셋 경로, W2, W3 의 에디터 경로, W4, W8.

| 묶음 | 내용 | 순서 |
|---|---|---|
| W1 엔진 기본 에셋 | 남은 것은 7.5 의 P1 | 셰이더 묶음 병합 뒤 |
| W5 도구 | 끝(2026-10-28 확인): `Config.py` 는 `requireConfig` · `readOptionalConfig` 둘뿐(없음은 `None`), `FileScan.py` 는 못 읽으면 멈춤, `AssetValidation` 은 검사별 필수 키 · 모르는 키 거절(`getCheckNames` 표), CMake JSON 읽기는 `JsonValue.cmake` 하나, `DEFAULT_VALUE` 는 `SilentFallback:cmake` 게이트가 0 건으로 막는다. 남은 `ERROR_VARIABLE` 셋(`StageModuleRuntimeDlls` · `PythonUtils` · `Vcpkg` 계획)은 결과 코드를 보고 실패를 알린다 | 끝 |
| W6 테스트 | `EngineDefaultAssets{}` 기본 생성은 컴파일되지 않는다(끝). 테스트의 경로는 `test::getEngineDefaultAssets()` 로 읽는다 — 엔진 기본 표에 든 파일(소켓 종류 · 절단면 채널 · 파괴 프로필 · 툰 템플릿 · 스프라이트 머티리얼)의 상수를 지우고 테스트도 표로 옮겼다(2026-10-28). 폴백을 단언하는 테스트 훑기는 남았다 | 일부 |
| W7 셰이더 잔여 | 끝(2026-10-28): 죽은 `ALPHA_TEST` 가지는 셰이더 소스에 없다(RHI 주석만 남음 — RHI 소관), `material_keywords` 는 주석을 지우고 보며 엔진 패스가 읽는 머티리얼 define 은 살아 있는 것으로 센다(`MATERIAL_OUTLINE`, 파이썬 테스트 둘), AO · 월드 · 키 라이트는 판단을 내렸다(선택 자원의 부재는 항등, 키 라이트는 이미 없음). 남은 것: 파이프라인 XML 선언(P2 와 같은 일)과 네 백엔드 골든은 GPU 호스트 | 끝(골든 제외) |

## 4. 정한 판단

- `REFLECT( Alias = 옛이름 )` 와 별칭 읽기 경로는 없애지 않고 갖춰 둔다(2026-10-18 사용자 결정): 출시 데이터가 생겼을 때 바로 쓰도록 기능은 두되 제품 데이터 · 게임 코드에는 쓰지 않는다. 옛 형식 리더만 쓰는 XML · JSON · bin 이중 읽기 경로는 데이터를 다시 쓰고 없앤다. 스키마 버전 이행(SchemaMigrate)은 별개라 남긴다.
- 셰이더 안의 "없으면 항등" 은 폴백이 아니라 선택 자원의 부재다(2026-10-28): 파이프라인이 선언하지 않은 선택 입력(SSAO 결과 → 가림 없음 1, 인스턴스 구조버퍼 없는 풀스크린 → `g_World`, 라이트 그리드 없음 → 전체 목록)은 `kInvalidIndex` 로 부재를 나타내고 항등으로 읽는다(켜지 않은 포스트 효과와 같다). 필수 입력이 없을 때 다른 값으로 대신하는 것(3D 라이트가 없을 때 키 라이트)은 이미 없앴고 다시 들이지 않는다.
- 도구 훅의 fail-open 은 훅 오류로 세션이 막히지 않게 하려는 것이라 남기고 이유를 주석에 적는다.

## 5. W2 에서 남긴 것

- 팩 키의 도메인 떼고 재조회 — 도메인 없는 리소스 id(검색 우선순위로 푸는 형식)가 이에 기대고 있다. 키를 전역 경로로 바꾸려면 도메인 없는 id 쓰는 곳을 먼저 없애야 한다.
- 렌더러 첨부 선언 필수화는 7.5 P2 가 맡는다.
- 프리팹 JSON 리더는 데이터 0 이지만 에디터가 쓴다(에디터 작업 소관). XML 저작본 ↔ bin 쿠킹본 두 리더(`PrefabAsset` · `SceneDocument`)는 옛 형식이 아니라 쿠킹 파이프라인이다.
- 같은 모양은 고쳤다: `NavMeshSettings::ensureDefaults` 삭제(선언하지 않은 표는 `validate` 가 거절), `PhysicsDesc` 의 모르는 재질은 첫 재질로 가지 않고 거절(`resolveMaterial` 이 `nullptr`, `[[nodiscard]]`), `MaterialUtil::parseBlendMode` 는 [SilentFallbackAudit](SilentFallbackAudit.md) 에서 오류로. `CameraPreset` 은 보지 않았다(GameFramework 소관).

## 6. W4 에서 볼 것

W4(게임 데이터의 필수 읽기 · 계층 역전 제거)는 코드에 들어갔다. 새 `PROPERTY` 의 리플렉션 생성물, 모든 게임의 기동(필수 데이터가 하나라도 없으면 멈춘다), 에디터 캐릭터 편집 · 공원 배치 시나리오를 묶음 빌드에서 본다.

## 7. 남은 순서 (2026-10-18 설계)

C-30(데이터 속성의 리터럴 기본값)과 C-36(경로 리터럴, 조용한 폴백, 버린 로드 결과)은 근본 원인이 겹쳐 한 순서로 묶습니다.

### 7.1 지금 수치

2026-10-28(main) 게이트 값(괄호는 2026-10-18 값). 원본은 각 게이트의 출력입니다. 남은 것의 성격은 표 아래에 적었다.

| 게이트 | 종류 | 값 |
|---|---|---|
| `CheckDataDrivenLiterals` | `AttributeFallback` | 없음(D9 — 폴백 읽기를 지워 형식으로 막혔다, 종류를 게이트에서 뺐다)(886) |
| `CheckDataDrivenLiterals` | `TuningConstexpr` | 45(48) |
| `CheckDataDrivenStructure` | `AssetPathLiteral` | 12(40) |
| `CheckDataDrivenStructure` | `SilentFallback` | 15(122) |
| `CheckDataDrivenStructure` | `DiscardedLoadResult` | 64(164) |
| `CheckDataDrivenStructure` | `MagicNameLiteral`(보고만) | 72(75) |
| `CheckDataDrivenStructure` | `LiteralGetDefault`(보고만) | 27(P3 — 필수 키는 `[]` 와 이름 있는 오류, 선택 키는 없음을 형식으로)(0) |

남은 것(2026-10-28 훑은 결과, 기준선은 올리지 않았다):

- `AssetPathLiteral` 12: 설정 표의 `kResourcePath` 가 정적 `SW_RESOURCE_DATA_KIND` 등록(`LipSync` · `AnimationCrowd` · `AnimationLOD`)에 들어가 있어 등록이 서브시스템보다 먼저라 표로 못 옮긴다 — 등록 형식(경로를 늦게 푸는 길)을 바꾸는 일. 나머지는 부트 순서가 얽힌 설정(`PhysicsSystem` · `Render2DSettings` · `NavMeshSettings` · `IAudioService` · `AutomationLogPolicy`), 에디터 팩 자기 경로(`EditorPaths::kLocalizationProject` — `EditorToolDefaults` 칸으로 옮기려면 그 테스트 JSON 도 고친다), 데모 · 벤치(`UIDemoScreen` · `BenchScene`).
- `SilentFallback` 15: 렌더러(`FrameRenderer*` · `GPUSceneBuilder` — 렌더러 소관), RHI 로그, 값이 없을 때의 항등을 인자로 받는 모양(`AudioCurvePoint::evaluate` · `GimmickCircuit::readNumber` · `ScheduleCatalog::readClock`). 뒤 셋은 `std::optional` 반환으로 바꾸고 호출부가 없음을 정하게 한다.
- `DiscardedLoadResult` 64: 끈적한 오류 상태로 결과를 쌓는 읽기(`Archive` 는 `readLatched` 하나로 모았고 `AnimationClip::readRaw` 는 `void` 로 바꿨다), 건너뛰기 · 채움 읽기, 편집기 패널의 "깨진 글도 연다" 호출. `loadFromAttributes` 는 읽은 개수를 돌려주는 함수라 이름이 로드 결과로 잡히는 것이다.

### 7.2 근본 원인: 읽기 API 가 폴백을 형식으로 가지고 있다

호출부마다 고치면 다음 호출부가 같은 실수를 할 수 있습니다. 폴백은 읽기 API 의 시그니처에 있었습니다(`XMLNode::getAttribute*` · `getChild*` 의 `fallback = 0`, `JSONValue::as*`, `KeyValueFile::get*`, 이름으로 찾는 값 모음 `Blackboard::getFloat` · `StatBlock::getValue` · `AbilitySystemComponent::getAttributeValue` 등).

버린 로드 결과도 근본 원인이 하나입니다. `bool` 하나가 서로 다른 세 결과를 섞습니다: 검사하고 다시 읽기(자동화 실행기), 없음과 실패 섞기(선택 설정 파일 · 핫 리로드), 원소마다 읽기(모델 임포터).

### 7.3 해법

- **읽기 API 에서 폴백 인자를 없앱니다.** 읽는 길은 셋만 남깁니다.
  1. 스키마 읽기: `GameDataXML::readFields` 가 리플렉션 Def 를 읽습니다. 기본값은 Def 의 초기값이고 `PROPERTY( Required )` 와 `Min` · `Max` 가 검사됩니다. 데이터 카탈로그는 모두 이 길로 갑니다.
  2. 필수 손 읽기: `[[nodiscard]] std::optional<T> readRequiredAttribute<T>( name )`. 없거나 깨지면 원소와 속성 이름으로 오류를 남기고 `nullopt`.
  3. 선택 손 읽기: `[[nodiscard]] bool readOptionalAttribute<T>( name, std::optional<T>& out )`. 없으면 `out` 이 비고 true, 깨진 값은 오류와 false. "없음"은 기본값이 아니라 형식(`std::optional`)으로 남습니다.
  `JSONValue` 도 같은 세 길입니다(`KeyValueFile` 은 TX3(구조 단순화)에서 지웠다).
- **이름으로 찾는 값은 로드할 때 키를 확인합니다.** 이름을 핸들로 바꾸고 대상 테이블에 없으면 로드 오류. 핸들로 바꿀 수 없는 찾기(블랙보드처럼 실행 중에 키가 생기는 것)는 `std::optional<T>` 을 돌려줍니다.
- **로드 결과는 세 값입니다.** 정상인 `Absent` · `NotTracked` 가 따로 있어 `(void)` 로 버릴 이유가 없습니다(`ConfigReadResult`(Loaded · Missing · Invalid), `ReloadOutcome`(Reloaded · NotTracked · Failed)). `Failed` 는 함수 안에서 오류를 한 번 남기고 호출부는 `switch` 로 고릅니다.
- **자동화 단계는 한 번만 읽습니다.** 구현은 검사와 실행이 같은 `parse*` 함수를 쓰는 모양이다(단계 레지스트리의 함수 포인터 모양을 바꾸지 않기 위해).
- **임포터는 접근자 단위로 읽습니다.** `cgltf_accessor_unpack_floats`, 실패하면 임포트가 실패.
- **남은 경로 상수는 엔진 기본 에셋 테이블로**(P1), **렌더러 첨부는 파이프라인 XML 이 모두 선언**(P2, 선언하지 않은 첨부 이름은 오류).

### 7.4 대안과 기각 이유

- **게이트 기준선만 낮춰 가며 호출부를 고치기.** API 가 폴백 인자를 받는 한 새 코드가 같은 줄을 다시 쓰고, 게이트는 변수로 넘긴 폴백을 놓칩니다.
- **`readOptionalAttribute( name, absentValue )`.** 호출부에 리터럴 기본값이 다시 생깁니다. 기본값의 원본은 Def 초기값 하나여야 합니다.
- **`bool try*( name, out )`.** 없음과 깨짐을 가르지 못하므로, 반환을 `bool` 로 두되 "없음"은 `std::optional` 이 비는 것으로 구분합니다.
- **블랙보드도 핸들만 쓰게 하기.** 키를 행동 트리가 실행 중에 만들므로 기각, 대신 `std::optional` 을 돌려줍니다.

### 7.5 단위

D1(새 읽기 함수, `GameSettings` 커스텀 모음 삭제)과 D2 ~ D5(키트 카탈로그를 `readFields` 로)는 들어갔고, `GameSettings` · `BootstrapConfig::_packRoot` · `LoadingScreenSettings::_documentPath` · `EditorToolDefaults` 의 경로 칸은 `DataPath` 가 됐습니다(빌드와 `GameFrameworkTest` 확인이 남았다). 사용자가 텍스트 칸에서 고치는 경로(`InputMapPanel::_inputMapPath` · `UIPreviewPanel::_documentPath`)는 `EditableResourcePath` 입니다 — 처음 값은 `DataPath` 에서만 받고 고친 글은 칸을 떠날 때 리소스 id 인지 검사해 확정합니다. 사용자 설정 `EditorGeneralPreferences::_startupScene` 과 넷 플레이 실행 파일 경로(디스크 경로)는 리소스 데이터 경로가 아니라 그대로 둡니다. 각 단위는 끝날 때 두 게이트의 기준선을 `--print-baseline` 으로 다시 적습니다.

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| D6 | Base 의 카탈로그와 이름으로 찾는 값 모음 | `GameFramework/Base/`, 데이터 | D1 | Base 테스트, 대표 셋 실행 | 없음 |
| D7 | 엔진의 손 읽기(Input, TileMap, Telemetry, Character Fit, `FloatCurve`, Sprite 클립) | `Engine/Input/` · `TileMap/` · `Telemetry/` · `Character/Fit/` · `Utility/FloatCurve.*` · `Animation/Sprite/` | D1 | 입력 맵, 타일맵, 텔레메트리 테스트 | 없음 |
| D8 | 로드 결과 세 값, 자동화 단계 한 번 읽기, 임포터 접근자 읽기, 에디터 명령의 결과 보고 | `Engine/Automation/`, `Engine/Serialization/Reflected*`, `Editor/Common/`, `Editor/Panels/` | D1 | 자동화 시나리오 전체, 에디터 시나리오, 임포트 테스트 | 없음 |
| D9 | 폴백 인자를 받는 읽기 함수를 지우고, 형식으로 막힌 게이트 종류를 뺍니다 | D1 의 폴더, `Engine/Character/CharacterDataReader.*`, `Scripts/lint/` | D2 ~ D8 | 전 프리셋 컴파일, `-L lint`, `-L nogpu` | 없음 |
| P1 | 경로 상수와 컴포넌트 기본 머티리얼을 `EngineDefaultAssets` 로 | `Engine/UI/`, `Engine/Config/EngineDefaultAssets.*`, `Engine/Environment/`, `TileMapRendererComponent.*`, 엔진 데이터 | 없음 | 기동, UI 테스트, 대표 셋 | 없음 |
| P2 | 렌더러 첨부를 파이프라인 XML 이 모두 선언(W7 과 함께) | `Engine/Renderer/Frame/`, 파이프라인 XML | 셰이더 묶음 | 네 백엔드 × 대표 셋, 골든 이미지 | 없음 |
| P4 | 테스트가 원본을 거치게 합니다(W6) | `Test/` | D9, P1 | `-L nogpu` | 없음 |
| G1 | 게임 코드의 튜닝 리터럴(C-30 ②)을 데이터로 | `Source/Games/<게임>/`, 그 팩 데이터 | 없음 | 게임별 기동과 스크린샷 비교 | 없음 |
| G2 | Shipping 의 그림 비교(C-30 ⑤)를 구운 팩의 시나리오 스크린샷으로 | `Test/AppTest/`, 팩 시나리오 | 없음 | Shipping 빌드와 `-L hostgpu` | 없음 |

C-30 ④(에디터 색과 아이콘 테이블, UI 크기 리터럴)는 디자인 토큰 작업(editorux 묶음)이 맡습니다. D6 의 데이터 다시 쓰기는 키트가 읽는 데이터 파일만 건드리고, 두 키트가 한 파일을 읽으면 같은 단위에 넣습니다(맡기기 전에 `grep` 으로 확인).

**2026-10-21 상태(`wt/datadriven`, 빌드 미확인).** D6 · D7 · D8 · D9 · P1 · P4 · G1 은 코드가 들어갔고, P2 는 일부, G2 는 기준 그림만 남았습니다. 검증 열의 확인이 모두 남아 있습니다.

- D9: XML `getAttribute*` · `getChild*`, JSON `asInt` 류, `KeyValueFile::get*` 를 지웠다. JSON 키 없는 값은 `read<T>()`, 선택 키는 `readOptionalInto`(없으면 넣어 둔 값 그대로)다. `AttributeFallback` 종류를 게이트에서 뺐고 `fallbackParameter` 모양은 형식으로 막히지 않아 남겼다.
- P2(일부): 파이프라인 검증 문제를 로드 실패로 바꾸고 `ensureNamed` · `attachmentFormatOrDefault` · `getAttachmentClearColorOrDefault` 를 지웠다(`getTargetClearColor( name )` 하나 — 풀이 든 선언, 풀에 없는 이름(백버퍼)은 렌더러 클리어 색). `executePass` 가 패스 서술을 참조로 받아 서술 없는 실행이 형식으로 막혔고, 컬러를 그리는 패스는 표의 `kDrawsDeclaredColor` 로 로드가 컬러 출력 선언을 요구한다(검증 3-1). Present 는 SourceColor 를 필수 입력으로 받는다. 엔진 파이프라인 6 개는 모두 선언을 갖췄다. **남은 것: 네 백엔드 골든 확인**(그림은 같아야 한다).
- G2(코드 · 데이터 끝): Empty 팩 시나리오 `game/empty/automation/shippingframe.scenario.xml`과 AppTest `AppSmokeTest.ShippingSceneMatchesGoldenImage`(Shipping 에서만, `Test/AppTest/Golden/shippingframe_<백엔드>.ppm.z`, 채널 차이 2). **남은 것: 기준 그림 뜨기** — Shipping 빌드에서 `SW_UPDATE_GOLDEN=1` 로 그 케이스를 돌린다(뜨기 전에는 윈도우 Shipping `-L hostgpu` 에서 실패, 폴백 금지). 첫 씬이 고정 틱에서 결정적인지(입자 난수 등)는 두 번 돌려 확인한다.

### 7.6 사용자 결정

없습니다. 폴백 인자를 지우는 것은 방침의 적용이고, 데이터는 다시 쓰면 됩니다.
