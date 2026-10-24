# 데이터 주도 구조 정리 계획 (2026-10-16)

> 상태: 구현중 · 선행: 없음 · 영역: engine · 옛번호: C-30, C-36 · 진행: 13/14

값의 정본은 데이터 파일이고 코드 · 헤더는 형식만 가진다. 필수 값이 없으면 조용히 기본값 · 대체로 가지 않고 오류로 멈춘다. 결정은 [결정 기록](../09_Decisions.md) 5-4 "유지보수성을 위한 데이터화"이고, 이 문서는 6 영역 조사(2026-10-16)가 드러낸 위반을 근본 원인부터 없애는 순서다. 진행 표는 린트 게이트 `CheckDataDrivenStructure`의 `--summary` 가 정본이다(기준선이 줄면 진척).

## 1. 근본 원인

1. **기본값이 C++ 멤버 초기값에 산다.** `EngineDefaultAssets.h` 경로 약 35개, 컴포넌트 `_materialPath{ "engine/materials/…" }`, 패널의 처음 문서, 게임 `*Game.cpp` 의 카탈로그 · 오디오 경로, 디렉터 생성자. 데이터 파일(`enginedefaultassets.xml`)은 비어 있고 "기본값과 다른 것만 적는다"고 되어 있어 정본이 코드다.
2. **읽기 결과를 버릴 수 있다.** 로더가 `bool` 을 돌려주고 호출부가 `(void)load…` 로 버리거나 경고만 남기고 `return true`. "필수" 라는 뜻이 형식에 없다.
3. **같은 값의 정본이 둘이다.** 카탈로그 경로가 게임 코드와 데이터 종류 등록표(`SW_RESOURCE_DATA_KIND`)에, 로딩 화면 경로가 `GameSettings.h` 와 `LoadingScreenController.h` 에, 파이프라인 XML 과 `EngineDefaultAssets` 에 따로 있다.
4. **테스트가 정본을 우회한다.** `EngineDefaultAssets{}` 를 기본 생성해 C++ 초기값을 정본처럼 쓰고(5곳), 경로 리터럴이 약 600줄. 초기값을 지우면 한꺼번에 깨지거나 거꾸로 조용히 통과한다.
5. **막는 장치가 없었다.** `CheckDataDrivenLiterals` 는 PROPERTY 숫자 · 문자 리터럴 기본값만 본다(기준선 904).

## 2. 구조적 해법 (게이트는 마지막 안전망이다)

원칙: 위반을 **쓸 수 없는 형식**으로 만든다. 린트 게이트는 형식이 막지 못하는 틈에만 둔다. 게이트로 시작하면 위반을 만드는 구조를 그대로 둔 채 감시만 하게 된다.

- **경로는 문자열이 아닌 형식으로**: 엔진 기본 에셋 · 시스템 설정 · 게임 설정의 경로 칸은 `DataPath`(가칭) 형식이다. 기본 생성자가 없고 초기값 리터럴로 만들 수 없으며 오직 데이터 읽기(`GameDataXML::readFields`)만 값을 채운다. `string _shaderForwardLit{ "…" }` 는 컴파일되지 않는다. 키가 없거나 파일이 없으면 기동 · 쿠킹 · 로드가 오류로 멈춘다.
- **필수 / 선택을 형식에 드러낸다**: `PROPERTY( Required )` 는 그대로 쓰고, 선택 값은 `Optional<T>` 처럼 "없음"이 형식에 보이는 칸이다. 기본값을 코드가 채우는 세 번째 길을 없앤다.
- **읽기 결과는 버릴 수 없다**: 설정 로더는 `[[nodiscard]]` 결과를 돌려주고 호출부가 오류로 멈출지 직접 고른다. `(void)load…` 가 컴파일 경고 → 오류(`-Werror=unused-result`)다. `bool` 을 돌려주고 경고만 남기는 로더 모양 자체를 없앤다.
- **정본은 한 곳, 파생은 코드가 만든다**: 엔진 기본 경로는 `enginedefaultassets.xml`, 게임 경로(카탈로그 · 오디오 · UI)는 게임의 `gamesettings.xml`. 데이터 종류 등록표와 각 게임 코드는 그 값을 읽는 쪽이지 따로 적는 쪽이 아니다.
- **이름은 상수 한 곳에서 파생**: 패널 id 는 패널 클래스의 `kPanelId` 하나, 입력 액션 · 카탈로그 id 는 데이터가 정의하고 코드는 데이터에서 가져온 핸들로 부른다(`MeadowVillage` · `HarvestValley` 처럼 핸들 `PROPERTY`).
- **명시적 오류 에셋만 대체로 남긴다**: 마젠타 누락 텍스처 · `missingmaterial.material` 처럼 눈에 띄는 오류 표시는 데이터가 가리키고 오류 로그를 남기는 한 경로로만 쓴다.
- **계층 역전을 끊는다**: `GameFramework` 에디터가 `game/shooter3d/…` · `game/themepark/…` 를 직접 가리키지 않고 활성 게임의 설정에서 읽는다.
- **테스트는 정본을 통한다**: 테스트용 `EngineDefaultAssets{}` 기본 생성이 컴파일되지 않으므로(위 형식) 테스트도 데이터에서 읽는 도우미를 쓴다.
- **게이트(`CheckDataDrivenStructure`)는 마지막**: 형식이 막지 못하는 곳(`.cpp` 안의 문자열 리터럴, 파이썬 · CMake 의 폴백)만 기준선 동결로 지킨다. 구조 수정이 진행되면 기준선은 0 으로 수렴해야 하고, 형식으로 막힌 종류는 게이트에서 뺀다.

## 3. 작업 묶음 (순서와 소유 파일)

동시 빌드는 둘이라 코드 변경 묶음은 둘씩만 돈다.

| 묶음 | 내용 | 순서 |
|---|---|---|
| W1 엔진 기본 에셋 | `EngineDefaultAssets` 전 필드 · `ShaderCookRequest` 리터럴 · 시스템 설정 경로 상수(물리 · 내비 · 오디오 믹서 · UI 문서 · Render2D · 네트워크 에뮬레이션) · 컴포넌트 기본 머티리얼 → XML. 필수 읽기 형식과 `[[nodiscard]]` 결과. `EngineDefaultAssets` 의 경로 칸은 끝났고 남은 것은 7.5 의 P1 | 셰이더 묶음(경로 일부 선행) 병합 뒤 |
| W5 도구 | `Scripts/common/Config.py` 의 `{}` 반환 · `FileScan.py` 조용한 통과 · `AssetValidation` 기본값 · CMake `ERROR_VARIABLE` 뒤 빈 값 · `DEFAULT_VALUE` | 지금(빌드 없음, `TestTargets.cmake` 는 testspeed 뒤) |
| W6 테스트 | `EngineDefaultAssets{}` 기본 생성 · 정본 경로 리터럴 · 폴백을 단언하는 테스트 | W1 · W2 뒤 |
| W7 셰이더 잔여 | 죽은 `ALPHA_TEST` 가지 · `material_keywords` 주석 무시 · 셰이더 안 조용한 대체(AO · 월드 · 키 라이트) 설계 판단 · 파이프라인 XML · 재질 반복 | 셰이더 묶음 뒤 |

## 4. 정한 판단

- `REFLECT( Alias = 옛이름 )` 와 별칭 읽기 경로는 없애지 않고 갖춰 둔다(결정 기록 5-4 "이름 변경 리다이렉트", 2026-10-18 사용자 결정): 출시 데이터가 생겼을 때 바로 쓰도록 기능은 두되, 제품 데이터 · 게임 코드에는 별칭을 쓰지 않는다. 옛 XML · JSON · bin 이중 읽기 경로 중 옛 형식 리더만 쓰는 곳을 세어 데이터를 다시 쓰고 없앤다. 스키마 버전 이행(SchemaMigrate)은 별개라 남긴다.
- 도구 훅(`.claude/hooks`)의 fail-open 은 훅 오류로 세션이 막히지 않게 하려는 것이라 남기고 이유를 주석에 적는다.

## 5. W2 에서 남긴 것

- 팩 키의 도메인 떼고 재조회 — 도메인 없는 리소스 id(검색 우선순위로 푸는 형식)가 떼어 낸 키를 1단계에서 맞히는 데 기대고 있다. 키를 전역 경로로 바꾸려면 도메인 없는 id 쓰는 곳을 먼저 없애야 한다.
- `attachmentFormatOrDefault` · `getAttachmentClearColorOrDefault` — 근본 원인은 선언하지 않은 첨부를 `ensureNamed` 가 코드 포맷으로 만드는 것이다. 선언 필수화는 파이프라인 XML(셰이더 작업 소관)과 함께 한다.
- `REFLECT( Alias )` — 제품 데이터 사용 0, 테스트 4. 별칭 유지로 결정돼 제거하지 않는다(4절). 프리팹 JSON 리더는 데이터 0 이지만 에디터가 쓴다(에디터 작업 소관).
- XML 저작본 ↔ bin 쿠킹본 두 리더(`PrefabAsset` · `SceneDocument`)는 옛 형식이 아니라 쿠킹 파이프라인이다.
- 같은 모양이 더 있던 곳은 고쳤다: `NavMeshSettings::ensureDefaults` 를 지웠고 영역 · 에이전트 종류를 선언하지 않은 표는 `validate` 가 거절한다(매니페스트 id `nav-settings-required-test`). `PhysicsDesc` 의 모르는 재질은 첫 재질로 가지 않고 바디 · 셰이프 만들기를 거절한다 — 이름을 적지 않은 것만 "첫 재질" 이다(Jolt · Box2D 의 `resolveMaterial` 이 `nullptr` 를 돌려주고 `[[nodiscard]]`; 매니페스트 id `physics-unknown-material-3d-test`, `physics-unknown-material-2d-test`). `MaterialUtil::parseBlendMode` 는 [SilentFallbackAudit](SilentFallbackAudit.md) 에서 오류로 바꿨고, `GameDataXML` 은 필수 이름 읽기(`readRequiredName`)로 갔다. `CameraPreset` 은 이번에 보지 않았다(GameFramework 작업 소관).

## 6. W4 에서 볼 것

W4(게임 데이터의 필수 읽기 · 계층 역전 제거)는 코드에 들어갔다. 빌드 · 실행 확인 기록은 찾지 못했다. 새 `PROPERTY` 의 리플렉션 생성물, 모든 게임의 기동(필수 데이터가 하나라도 없으면 멈춘다), 에디터 캐릭터 편집 · 공원 배치 시나리오를 묶음 빌드에서 본다.

## 7. 남은 순서 (2026-10-18 설계)

C-30(데이터 속성의 리터럴 기본값)과 C-36(경로 리터럴, 조용한 폴백, 버린 로드 결과)은 근본 원인이 겹칩니다. 그래서 한 순서로 묶습니다.

### 7.1 지금 수치

2026-10-18 에 두 게이트로 센 값과 D6 ~ G1 뒤(2026-10-21, `wt/datadriven`)의 값입니다. 진행 표의 원본은 각 게이트의 출력입니다.

| 게이트 | 종류 | 2026-10-18 | 2026-10-21 |
|---|---|---|---|
| `CheckDataDrivenLiterals` | `AttributeFallback` | 886 | 없음(D9 — 폴백 읽기를 지워 형식으로 막혔다, 종류를 게이트에서 뺐다) |
| `CheckDataDrivenLiterals` | `TuningConstexpr` | 48 | 45 |
| `CheckDataDrivenStructure` | `AssetPathLiteral` | 40 | 19 |
| `CheckDataDrivenStructure` | `SilentFallback` | 122 | 30 |
| `CheckDataDrivenStructure` | `DiscardedLoadResult` | 164 | 87 |
| `CheckDataDrivenStructure` | `MagicNameLiteral`(보고만) | 75 | 70 |
| `CheckDataDrivenStructure` | `LiteralGetDefault`(보고만) | 0 | 1(P3 — 필수 키는 `[]` 와 이름 있는 오류, 선택 키는 없음을 형식으로) |

`TuningConstexpr` 와 `GameTimeScale` 의 `SilentFallback` 은 이미 줄었으므로 기준선만 낮춥니다(D0).
끝난 묶음은 W0, W1 의 엔진 기본 에셋 경로, W2, W3 의 에디터 경로, W4, W8 입니다.

### 7.2 근본 원인: 읽기 API 가 폴백을 형식으로 가지고 있다

886 줄을 호출부마다 고치면, 다음 호출부가 같은 실수를 할 수 있는 구조가 남습니다. 폴백은 호출부가 아니라 읽기 API 의 시그니처에 있습니다.

- `XMLNode::getAttributeInt` · `getAttributeFloat` · `getAttributeBool` 과 `getChildInt` · `getChildFloat` · `getChildBool`(`Engine/Serialization/XML/XMLDocument.h`)은 `fallback = 0` 기본 인자를 받습니다.
  속성이 없으면 폴백을 돌려주고, 값이 숫자가 아니어도 경고만 남기고 폴백을 돌려줍니다. 인자를 적지 않은 `getAttributeFloat( "x" )` 도 조용히 0 이 됩니다. 이 API 를 부르는 줄은 약 1,010 개입니다(Engine 69, GameFramework 944).
- `JSONValue::asInt` · `asUint` · `asFloat` · `asBool`(`fallback = 0`)과 `KeyValueFile::getInt` · `getFloat` · `getBool` 이 같은 구조입니다.
- 이름으로 찾는 값 모음도 같은 구조입니다. `Blackboard::getFloat`, `StatBlock::getValue`, `AbilitySystemComponent::getAttributeValue`, `GameplayAbility::getParameter`, `GameplayEffectSpec::getSetByCallerMagnitude`, `ElementMaterialDef::getParam`, `FitInteractionDef::findArgument`, `FloatCurve::evaluate` 가 모르는 이름이나 빈 곡선에 호출부의 폴백을 돌려줍니다.
- `GameSettings::getCustomProperty*` 는 제품 코드 호출 0, 데이터 사용 0 이고 테스트(`TestGameFramework.cpp:1641-1658`)만 씁니다.

버린 로드 결과 164 줄도 대부분 근본 원인이 하나입니다. `bool` 하나가 서로 다른 세 결과를 섞어서, 호출부가 정상인 결과까지 버리려고 `(void)` 를 씁니다.

- **검사하고 다시 읽기.** 자동화 실행기(`Engine/Automation`, 31 줄)는 검사 함수가 단계 인자를 읽고, 실행할 때 같은 인자를 다시 읽으며 결과를 버립니다("검사에서 확인했다").
- **없음과 실패를 섞기.** 선택 설정 파일(`ModelImportConfig`, `TextureImportConfig`)과 핫 리로드(`reloadChangedAsset`, `reloadChangedFile`)는 "파일이 없다"와 "내 파일이 아니다"를 실패와 같은 `false` 로 돌려줍니다.
- **원소마다 읽기.** 모델 임포터(`ModelImportAnimation.cpp`, `ModelImportGeometry.cpp`)가 키 하나, 정점 하나마다 `readFloats` 를 부르고 실패를 0 으로 남깁니다.

### 7.3 해법

- **읽기 API 에서 폴백 인자를 없앱니다.** 값을 읽는 길은 셋만 남깁니다.
  1. 스키마 읽기: `GameDataXML::readFields` 가 리플렉션 Def 를 읽습니다. 기본값은 Def 의 초기값이고, `PROPERTY( Required )` 와 `Min` · `Max` 가 검사됩니다. 데이터 카탈로그는 모두 이 길로 갑니다.
  2. 필수 손 읽기: `[[nodiscard]] std::optional<T> readRequiredAttribute<T>( name )` 입니다. 없거나 값이 깨지면 원소와 속성 이름으로 오류를 남기고 `nullopt` 를 돌려줍니다.
  3. 선택 손 읽기: `[[nodiscard]] bool readOptionalAttribute<T>( name, std::optional<T>& out )` 입니다. 없으면 `out` 이 비고 true 이며, 깨진 값은 오류와 false 입니다. "없음"은 기본값이 아니라 형식(`std::optional` 필드)으로 남습니다.
  `JSONValue` 도 같은 세 길로 맞춥니다(`KeyValueFile` 은 [구조 단순화](StructureSimplification.md) TX3 에서 지웠다 — `.meta` 는 XML, 에디터 · 사용자 상태는 JSON). 마지막 단위(D9)에서 폴백 인자를 받는 함수를 지우면 남은 호출부는 컴파일 오류가 됩니다. 그 뒤 `AttributeFallback` 종류와 해당 `fallbackParameter` 줄은 게이트에서 뺍니다.
- **이름으로 찾는 값은 로드할 때 키를 확인합니다.** 데이터가 이름을 적은 곳은 로드할 때 이름을 핸들로 바꾸고, 대상 테이블에 없으면 로드 오류입니다. 실행 중 찾기는 핸들로 하므로 "모르는 이름"이 생기지 않습니다.
  핸들로 바꿀 수 없는 찾기(블랙보드처럼 실행 중에 키가 생기는 것)는 `std::optional<T>` 을 돌려주고, 호출부가 없음을 직접 처리합니다.
- **로드 결과는 세 값입니다.** `[[nodiscard]] enum class LoadOutcome { Loaded, Absent, Failed }` 와 `ReloadOutcome { Reloaded, NotTracked, Failed }` 를 둡니다. 정상인 `Absent` · `NotTracked` 가 따로 있으므로 `(void)` 로 버릴 이유가 없어집니다.
  `Failed` 는 함수 안에서 오류를 한 번 남기고, 호출부는 멈출지 이어 갈지를 `switch` 로 고릅니다.
- **자동화 단계는 한 번만 읽습니다.** 검사 단계가 인자를 타입 있는 단계 값(`std::variant` 의 단계별 구조체)으로 만들고 실행기는 그 값을 씁니다. 다시 읽는 코드가 없으므로 버릴 결과도 없습니다. 언리얼 자동화의 잠복 명령(latent command)도 인자를 가진 객체로 만든 뒤 실행합니다 [기억].
- **임포터는 접근자 단위로 읽습니다.** `cgltf_accessor_unpack_floats` 로 접근자 하나를 통째로 읽고, 실패하면 임포트가 실패합니다. 원소마다 성공을 따질 이유가 없어집니다.
- **남은 경로 상수는 엔진 기본 에셋 테이블로 옮깁니다.** `Engine/UI` 의 문서 경로 17 줄(`DebugHUD`, `OptionsMenuScreen`, `UINotificationService`, `UISubtitleService`, `UISystem`, `UIResolutionPresetTable`)과 설정 테이블의 `kResourcePath` 상수, 컴포넌트 기본 머티리얼(`TileMapRendererComponent`, `FoliageComponent`, `TerrainComponent`, `WaterBodyComponent`)은 `EngineDefaultAssets` 의 `DataPath` 필드가 됩니다. 이미 있는 형식(W0)을 쓰므로 새 형식은 없습니다.
- **렌더러 첨부는 파이프라인 XML 이 모두 선언합니다.** `attachmentFormatOrDefault` 와 `getAttachmentClearColorOrDefault`(18 줄)의 근본 원인은 선언하지 않은 첨부를 `ensureNamed` 가 코드 포맷으로 만드는 것입니다. 선언하지 않은 첨부 이름은 오류로 바꾸고, 포맷과 클리어 색은 XML 에만 둡니다.

### 7.4 대안과 기각 이유

- **게이트 기준선만 낮춰 가며 호출부를 고치는 방법.** 기각합니다. API 가 폴백 인자를 받는 한, 새 코드가 같은 줄을 다시 씁니다. 게이트는 리터럴 인자만 보므로 변수로 넘긴 폴백은 놓칩니다.
- **`readOptionalAttribute( name, absentValue )` 처럼 없을 때의 값을 받는 선택 읽기.** 기각합니다. W2 의 `CharacterDataReader::readOptionalXxx` 가 이 형태인데, 결국 호출부에 리터럴 기본값이 다시 생깁니다. 기본값의 원본은 Def 초기값 하나여야 합니다. `CharacterDataReader` 도 D9 에서 같은 형태로 맞춥니다.
- **언리얼 `FJsonObject::TryGetNumberField` 와 같은 `bool try*( name, out )`.** 부분적으로 받아들입니다. 없음과 깨짐을 가르지 못하므로 선택 읽기의 반환을 `bool` 로 두되 "없음"은 `std::optional` 이 비는 것으로 구분합니다 [기억].
- **블랙보드도 핸들만 쓰게 하는 방법.** 기각합니다. 블랙보드 키는 실행 중에 행동 트리가 만듭니다. 언리얼 블랙보드는 키 선언 에셋이 있지만 `GetValueAsFloat` 은 없는 키에 0 을 돌려줍니다 [기억]. 여기서는 그 0 대신 `std::optional` 을 돌려줍니다.

### 7.5 단위

동시 빌드는 둘까지입니다. D1(새 읽기 함수 `readRequiredAttribute` · `readOptionalAttribute` · `readRequiredChild` · `readOptionalChild`, `JSONValue::readRequired` · `readOptional`, `KeyValueFile::readRequired` · `readOptional`, `GameSettings` 커스텀 모음 삭제)과 D2 ~ D5(키트 카탈로그를 `readFields` 스키마 읽기로)는 들어갔습니다. `GameSettings` 의 경로 칸 8 개도 `DataPath` 가 됐고(`_startMap` 은 필수, 읽기는 리플렉션 칸 표), 빌드와 `GameFrameworkTest` 확인이 남았습니다. 같은 모양이던 `BootstrapConfig::_packRoot` · `LoadingScreenSettings::_documentPath` · `EditorToolDefaults` 의 선택 경로 3 칸도 `DataPath` 가 됐습니다(빌드 확인이 남았습니다). 사용자가 텍스트 칸에서 고치는 경로(`InputMapPanel::_inputMapPath` · `UIPreviewPanel::_documentPath`)는 `EditableResourcePath` 입니다 — 처음 값은 `DataPath` 에서만 받고, 고친 글은 칸을 떠날 때 리소스 id 인지 검사해 확정합니다(빈 글 · OS 절대 경로는 거절). 사용자 설정 `EditorGeneralPreferences::_startupScene`(빈 값이 "게임의 첫 씬" 이라는 선택)과 넷 플레이 실행 파일 경로(디스크 경로)는 리소스 데이터 경로가 아니라 그대로 둡니다. 머티리얼 패널의 값 읽기(`MaterialPreviewShading::parseFloats`)는 개수를 배열 형식이 정하는 정확한 읽기와 `parseColor`(RGB · RGBA)로 바꿨고, 못 읽은 값은 0 으로 메우지 않고 원문 칸과 경고로 보입니다(빌드 확인이 남았습니다). D6 ~ D8 은 폴더가 겹치지 않으므로 둘씩 병렬로 돌립니다. 각 단위는 끝날 때 두 게이트의 기준선을 `--print-baseline` 으로 다시 적습니다.

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| D6 | Base 의 카탈로그(CameraPreset 43, Combat 36, AI 35, Ability 22, Inventory 17 등)와 이름으로 찾는 값 모음(블랙보드, 스탯, 어빌리티 속성, SetByCaller, 원소 규칙) | `GameFramework/Base/`, 데이터 | D1 | Base 테스트, 대표 셋 실행 | 없음 |
| D7 | 엔진의 손 읽기(Input 33, TileMap 15, Telemetry 10, Character Fit, `FloatCurve`, Sprite 클립) | `Engine/Input/`, `Engine/TileMap/`, `Engine/Telemetry/`, `Engine/Character/Fit/`, `Engine/Utility/FloatCurve.*`, `Engine/Animation/Sprite/` | D1 | 입력 맵, 타일맵, 텔레메트리 테스트 | 없음 |
| D8 | 로드 결과 세 값과 자동화 단계 한 번 읽기, 임포터 접근자 읽기, 에디터 명령의 결과 보고 | `Engine/Automation/`, `Engine/Serialization/Reflected*`, `Editor/Common/`, `Editor/Panels/` | D1 | 자동화 시나리오 전체, 에디터 시나리오, 임포트 테스트 | 없음 |
| D9 | 폴백 인자를 받는 읽기 함수를 지우고, 형식으로 막힌 게이트 종류를 뺍니다 | D1 의 폴더, `Engine/Character/CharacterDataReader.*`, `Scripts/lint/` | D2 ~ D8 | 전 프리셋 컴파일, `-L lint`, `-L nogpu` | 없음 |
| P1 | 경로 상수와 컴포넌트 기본 머티리얼을 `EngineDefaultAssets` 로 옮깁니다 | `Engine/UI/`, `Engine/Config/EngineDefaultAssets.*`, `Engine/Environment/`, `Engine/Object/Component/2D/TileMapRendererComponent.*`, 엔진 데이터 | 없음 | 기동, UI 테스트, 대표 셋 | 없음 |
| P2 | 렌더러 첨부를 파이프라인 XML 이 모두 선언합니다(W7 과 함께) | `Engine/Renderer/Frame/`, 파이프라인 XML | 셰이더 묶음 | 네 백엔드 × 대표 셋, 골든 이미지 | 없음 |
| P4 | 테스트가 원본을 거치게 합니다(W6) | `Test/` | D9, P1 | `-L nogpu` | 없음 |
| G1 | 게임 코드의 튜닝 리터럴(C-30 ②)을 데이터로 옮깁니다. NileCity 자동 계획표 좌표, HarvestValley 작물 → 모델 테이블, StarSkirmish `makeAISettings`, FarmSun 라이트 곡선 | 게임마다 `Source/Games/<게임>/`, 그 팩 데이터 | 없음 | 게임별 기동과 스크린샷 비교 | 없음 |
| G2 | Shipping 의 그림 비교(C-30 ⑤)를 구운 팩의 시나리오 스크린샷으로 둡니다 | `Test/AppTest/`, 팩 시나리오 | 없음 | Shipping 빌드와 `-L hostgpu` | 없음 |

C-30 ④(에디터 색과 아이콘 테이블, UI 크기 리터럴)는 디자인 토큰 작업(editorux 묶음)이 맡으므로 이 표에 넣지 않습니다.
D6 의 데이터 다시 쓰기는 키트가 읽는 데이터 파일만 건드립니다. 두 키트가 한 파일을 읽으면 같은 단위에 넣습니다. 맡기기 전에 `grep` 으로 확인합니다.

**2026-10-21 상태(`wt/datadriven`, 빌드 미확인).** D6 · D7 · D8 · D9 · P1 · P4 · G1 은 코드가 들어갔고, P2 는 일부, G2 는 남았습니다. 검증 열의 확인이 모두 남아 있습니다.

- D6 · D7: 카탈로그 · 손 읽기가 `readFields` 와 inout 선택 읽기(`readOptionalAttributeInRange` · `GameDataXML::readOptionalNumber`)로 갔고, 이름으로 찾는 값은 `std::optional` 을 돌려준다. 카메라 프리셋 데이터 각도는 라디안으로 다시 썼다.
- D8: 자동화 단계는 계획의 "variant 로 한 번 읽어 넘기기" 대신 **검사와 실행이 같은 `parse*` 함수**를 쓴다 — 단계 레지스트리의 함수 포인터(검사 · 준비) 모양을 바꾸지 않기 위해서다. 버릴 결과가 없어진 것은 같다.
  처음 읽기의 세 값은 새 형식 대신 이미 있던 `ConfigReadResult`(Loaded · Missing · Invalid)를 임포트 설정에 썼고, 다시 읽기는 `ReloadOutcome`(Reloaded · NotTracked · Failed)이다. 임포터는 `cgltf_accessor_unpack_floats` 로 접근자 단위로 읽는다.
- D9: XML `getAttribute*` · `getChild*` · `tryGetAttributeIntInRange`, JSON `asInt` 류, `KeyValueFile::get*` 를 지웠다. JSON 키 없는 값은 `read<T>()`, 선택 키는 `readOptionalInto`(없으면 넣어 둔 값 그대로)다. `CharacterDataReader` 의 선택 읽기도 inout 이다.
  게이트에서 `AttributeFallback` 종류를 뺐다. `fallbackParameter` 모양은 새 코드가 다시 쓸 수 있으므로 남겼다(형식으로 막히지 않는다).
- P1: 엔진 UI 문서 15 개 · 프리셋 표 둘 · 컴포넌트 기본 머티리얼 넷이 `EngineDefaultAssets` 칸이 됐다. UI 는 `UIEngineDocuments` 묶음을 기동 때 받는다. 컴포넌트 머티리얼 칸은 비면 표의 값이다.
- P2(일부): 파이프라인 검증 문제를 로드 실패로 바꾸고 `ensureNamed` · `attachmentFormatOrDefault` 를 지웠다(풀이 선언의 포맷 · 클리어 색을 든다).
  2026-10-22(`wt/planleftA`, 빌드 미확인): `getAttachmentClearColorOrDefault` 를 지우고 `getTargetClearColor( name )` 하나로 바꿨다 — 값은 풀이 든 선언이고, 풀에 없는 이름(백버퍼)은 렌더러 클리어 색(SceneColor 선언)이다. 호출부가 기본값을 넘기지 않으므로 패스마다 적던 값(뎁스 1, 노멀 `kNormalClear`, SSAO 의 테이블 `_pDefaultClear`)이 함께 지워졌다.
  엔진 파이프라인 XML 6 개는 모든 첨부에 `_clearColor` 와 `_bClear="true"` 를 적으므로 그림은 같아야 한다(바뀔 수 있는 곳은 `_bClear="false"` 첨부 — 데이터에 없다). 네 백엔드 골든 확인이 남았다.
  같은 날 "선언이 없을 때만 원본 이름" 단계도 지웠다. `executePass` 가 패스 서술을 참조(`const RenderGraphPassDesc&`)로 받아 서술 없는 실행이 형식으로 막혔고, 컬러를 그리는 패스는 표의 `kDrawsDeclaredColor`(지오메트리 · 하늘 · 외곽선 껍질 · TAA · 일반 풀스크린)로 로드가 컬러 출력 선언을 요구한다(검증 3-1).
  이름 후보(SceneColor · GBufferAlbedo/Normal · TransparentColor/LitColor · TaaColor · 그림자 맵 이름)와 Present 원본의 "가장 나중 컬러" 후보(`pickFirstExisting` — 도우미째 지움)도 없앴다. Present 는 SourceColor 를 필수 입력으로 받는다(계약). 엔진 파이프라인 6 개는 모두 선언을 갖추고 있다. 네 백엔드 골든 확인이 남았다.
- P4: 엔진 기본 에셋 경로를 리터럴로 적던 테스트 약 130 곳이 `test::getEngineDefaultAssets()` 를 거친다. 실린 파일을 늘어놓은 목록 · 테스트 데이터 글 안의 경로는 그대로다.
- G1: NileCity 계획표는 `data/autoplan.xml`, HarvestValley 작물 모델 표 · 해 곡선과 StarSkirmish 성향은 디렉터 PROPERTY(씬 데이터)다. 값은 옛 코드 그대로다.
- G2(코드 · 데이터 끝, 2026-10-22, 빌드 미확인): Empty 팩 시나리오 `game/empty/automation/shippingframe.scenario.xml`(고정 틱 30 프레임 스크린샷)과 AppTest `AppSmokeTest.ShippingSceneMatchesGoldenImage`(Shipping 에서만 — 그림을 `Test/AppTest/Golden/shippingframe_<백엔드>.ppm.z` 와 견준다, 채널 차이 2).
  남은 것은 기준 그림을 뜨는 일 하나다 — Shipping 빌드에서 `SW_UPDATE_GOLDEN=1` 로 그 케이스를 돌린다. 뜨기 전에는 윈도우 Shipping `-L hostgpu` 에서 그 케이스가 실패한다(벤치 골든과 같은 규칙, 폴백 금지).
  첫 씬이 고정 틱에서 결정적인지(입자 난수 등)는 기준을 뜰 때 두 번 돌려 확인한다.

### 7.6 사용자 결정

없습니다. 폴백 인자를 지우는 것은 방침(결정 기록 5-4)의 적용이고, 데이터는 다시 쓰면 됩니다.
