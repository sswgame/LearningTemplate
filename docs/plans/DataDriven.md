# 데이터 주도 구조 정리 계획

> 상태: 구현중 · 선행: 없음 · 영역: engine · 진행: 13/14

값의 정본은 데이터 파일이고 코드 · 헤더는 형식만 가진다. 필수 값이 없으면 기본값 · 대체로 가지 않고 오류로 멈춘다. 위반을 하나씩 고치지 않고, 위반을 **쓸 수 없는 형식**으로 바꾸는 순서다. 6 영역 조사가 드러낸 위반 가운데 W 묶음과 D · P · G 단위가 코드까지 들어갔고, 남은 것은 **빌드 · 골든 확인**과 7.1 의 잔여 위반이다.

## 1. 진행 요약

| 단위 | 내용 | 상태 |
|---|---|---|
| W0 · W2 · W3 · W4 · W8 | 엔진 기본 에셋 경로, 에디터 경로, 게임 데이터 필수 읽기 · 계층 역전 제거 | 끝 |
| W1 | 엔진 기본 에셋(남은 것은 P1) | 일부 |
| W5 도구 | 설정 읽기를 필수 / 선택 둘로, 파일 스캔 실패는 멈춤, CMake JSON 읽기 하나 | 끝 |
| W6 테스트 | `EngineDefaultAssets{}` 기본 생성은 컴파일 안 됨, 테스트 경로는 `test::getEngineDefaultAssets()` | 일부(폴백 단언 테스트 훑기 남음) |
| W7 셰이더 잔여 | 죽은 `ALPHA_TEST` 가지 정리, `material_keywords` 판정 | 끝(골든 제외) |
| D1 ~ D5 | 새 읽기 함수, `GameSettings` 커스텀 모음 삭제, 키트 카탈로그를 `readFields` 로, 경로 칸을 `DataPath` 로 | 들어감, 빌드와 `GameFrameworkTest` 확인 전 |
| D6 · D7 · D8 · D9 | Base 카탈로그 · 엔진 손 읽기 · 로드 결과 세 값 · 폴백 인자 읽기 함수 삭제 | 코드 끝남, 빌드 전 |
| P1 | 경로 상수 · 컴포넌트 기본 머티리얼을 `EngineDefaultAssets` 로 | 코드 끝남, 빌드 전 |
| P2 | 렌더러 첨부를 파이프라인 XML 이 모두 선언 | 일부, 네 백엔드 골든 확인 남음 |
| P4 | 테스트가 원본을 거침 | 코드 끝남, 빌드 전 |
| G1 | 게임 코드의 튜닝 리터럴을 데이터로 | 코드 끝남, 빌드 전 |
| G2 | Shipping 그림 비교를 구운 팩 시나리오 스크린샷으로 | 코드 · 데이터 끝, 기준 그림 뜨기 남음 |

첫 일: 전 프리셋 컴파일, 모든 게임 기동(필수 데이터가 하나라도 없으면 멈춘다), 에디터 캐릭터 편집 · 공원 배치를 한 번에 확인한다. 이어서 P2 골든과 G2 기준 그림(Shipping 빌드에서 `SW_UPDATE_GOLDEN=1` 로 `AppSmokeTest.ShippingSceneMatchesGoldenImage` 를 돌린다. 뜨기 전에는 Shipping 에서 실패하며 폴백은 없다)을 뜬다.

## 2. 근본 원인

1. **기본값이 C++ 멤버 초기값에 산다.** 엔진 기본 에셋 경로, 컴포넌트의 머티리얼 경로, 패널의 처음 문서, 게임 코드의 카탈로그 · 오디오 경로. 데이터는 "기본값과 다른 것만 적는다"여서 정본이 코드다.
2. **읽기 결과를 버릴 수 있다.** 로더가 `bool` 을 돌려주고 호출부가 `(void)` 로 버리거나 경고만 남기고 통과한다. "필수" 라는 뜻이 형식에 없다.
3. **같은 값의 정본이 둘이다.** 카탈로그 경로가 게임 코드와 데이터 종류 등록표에, 로딩 화면 경로가 두 헤더에 따로 있다.
4. **테스트가 정본을 우회한다.** C++ 초기값을 정본처럼 쓰고 경로 리터럴이 흩어져 있다.
5. **읽기 API 가 폴백을 시그니처로 갖는다.** `XMLNode::getAttribute*` · `getChild*` 의 `fallback = 0`, `JSONValue::as*`, `KeyValueFile::get*`, 이름으로 찾는 값 모음(`Blackboard::getFloat` 등). 호출부를 고쳐도 다음 호출부가 같은 실수를 한다.

## 3. 구조적 해법

원칙: 형식이 막고 린트 게이트는 형식이 막지 못하는 틈(`.cpp` 안의 문자열 리터럴, 파이썬 · CMake 폴백)에만 둔다. 기준선은 0 으로 수렴해야 하고 형식으로 막힌 종류는 게이트에서 뺀다.

- **경로는 `DataPath` 형식이다.** 기본 생성자가 없고 리터럴로 만들 수 없으며 데이터 읽기(`GameDataXML::readFields`)만 값을 채운다. 키나 파일이 없으면 기동 · 쿠킹 · 로드가 멈춘다. 사용자가 텍스트 칸에서 고치는 경로는 `EditableResourcePath` 로, 처음 값은 `DataPath` 에서만 받고 칸을 떠날 때 리소스 id 인지 검사한다.
- **필수 / 선택이 형식에 보인다.** `PROPERTY( Required )` 와 `std::optional<T>`.
- **읽기 API 에서 폴백 인자를 없앴다.** 읽는 길은 셋뿐이다.
  1. 스키마 읽기: `GameDataXML::readFields` 가 리플렉션 Def 를 읽는다(기본값은 Def 초기값, `Required` · `Min` · `Max` 검사).
  2. 필수 손 읽기: `[[nodiscard]] std::optional<T> readRequiredAttribute<T>( name )` — 없거나 깨지면 오류를 남기고 `nullopt`.
  3. 선택 손 읽기: `[[nodiscard]] bool readOptionalAttribute<T>( name, std::optional<T>& out )` — 없으면 `out` 이 비고 true, 깨지면 오류와 false.
  `JSONValue` 도 같은 세 길이다(JSON 필수는 `read<T>()`, 선택은 `readOptionalInto`).
- **로드 결과는 세 값이다.** `ConfigReadResult`(Loaded · Missing · Invalid), `ReloadOutcome`(Reloaded · NotTracked · Failed). 정상인 없음이 따로 있어 버릴 이유가 없고 `Failed` 는 호출부가 `switch` 로 고른다.
- **이름으로 찾는 값은 로드 때 키를 확인한다.** 핸들로 바꾸고 대상 표에 없으면 로드 오류. 실행 중에 키가 생기는 블랙보드는 `std::optional<T>` 을 돌려준다.
- **자동화 단계는 검사와 실행이 같은 `parse*` 함수로 한 번 읽는다.** 임포터는 접근자 단위로 읽고 실패하면 임포트가 실패한다.
- **이름은 상수 한 곳에서 파생한다.** 패널 id 는 `kPanelId` 하나, 입력 액션 · 카탈로그 id 는 데이터가 정의하고 코드는 핸들 `PROPERTY` 로 부른다.
- **명시적 오류 에셋만 대체로 남긴다**(마젠타 누락 텍스처 · `missingmaterial.material`): 데이터가 가리키고 오류 로그를 남기는 한 경로로만 쓴다.
- **계층 역전을 끊는다.** GameFramework 에디터가 `game/shooter3d/…` 를 직접 가리키지 않고 활성 게임 설정에서 읽는다.
- **렌더러 첨부는 파이프라인 XML 이 모두 선언한다**(P2): 선언하지 않은 이름은 오류, 컬러를 그리는 패스는 컬러 출력 선언 필수, Present 는 SourceColor 필수 입력, `executePass` 는 패스 서술을 참조로 받는다.

기각한 대안:
- 게이트 기준선만 낮추며 호출부 고치기 — API 가 폴백 인자를 받는 한 새 코드가 같은 줄을 다시 쓰고, 게이트는 변수로 넘긴 폴백을 놓친다.
- `readOptionalAttribute( name, absentValue )` — 호출부에 리터럴 기본값이 다시 생긴다.
- `bool try*( name, out )` — 없음과 깨짐을 못 가른다.
- 블랙보드도 핸들만 쓰기 — 키를 행동 트리가 실행 중에 만든다.

## 4. 정한 판단

- `REFLECT( Alias = 옛이름 )` 와 별칭 읽기 경로는 갖춰 두되 제품 데이터 · 게임 코드에는 쓰지 않는다. 옛 형식 리더만 쓰는 이중 읽기 경로는 데이터를 다시 쓰고 없앤다. 스키마 버전 이행은 별개라 남긴다.
- 셰이더 안의 "없으면 항등"은 폴백이 아니라 선택 자원의 부재다(SSAO 없음 → 가림 1, 인스턴스 구조버퍼 없음 → `g_World`, 라이트 그리드 없음 → 전체 목록). `kInvalidIndex` 로 부재를 나타낸다. 필수 입력이 없을 때 다른 값으로 대신하는 것(키 라이트)은 없앴고 다시 들이지 않는다.
- 도구 훅의 fail-open 은 훅 오류로 세션이 막히지 않게 하려는 것이라 남기고 이유를 주석에 적는다.
- 사용자 설정 `EditorGeneralPreferences::_startupScene` 과 넷 플레이 실행 파일 경로(디스크 경로)는 리소스 데이터 경로가 아니라 그대로 둔다.
- 에디터 색 · 아이콘 표와 UI 크기 리터럴은 디자인 토큰 작업(editorux 묶음)이 맡는다.

## 5. W2 에서 남긴 것

- 팩 키의 도메인 떼고 재조회: 도메인 없는 리소스 id 가 기대고 있어, 키를 전역 경로로 바꾸려면 그 쓰임을 먼저 없앤다.
- 프리팹 JSON 리더는 데이터 0 이지만 에디터가 쓴다(에디터 소관). `PrefabAsset` · `SceneDocument` 의 XML ↔ bin 두 리더는 옛 형식이 아니라 쿠킹 파이프라인이다.
- 같은 모양은 고쳤다: `NavMeshSettings::ensureDefaults` 삭제, `PhysicsDesc` 의 모르는 재질은 거절(`[[nodiscard]]`), `MaterialUtil::parseBlendMode` 는 [SilentFallbackAudit](SilentFallbackAudit.md) 에서 오류로. `CameraPreset` 은 GameFramework 소관이라 보지 않았다.

## 7. 남은 순서

데이터 속성의 리터럴 기본값과 경로 리터럴 · 조용한 폴백 · 버린 로드 결과는 근본 원인이 겹쳐 한 순서로 묶는다.

### 7.1 게이트 잔여

`CheckDataDrivenLiterals` · `CheckDataDrivenStructure` 의 현재 값이다(괄호는 처음 값).

| 종류 | 값 |
|---|---|
| `AttributeFallback` | 없음(886): 폴백 읽기를 지워 형식으로 막혔고 종류를 게이트에서 뺐다 |
| `TuningConstexpr` | 45(48) |
| `AssetPathLiteral` | 8 안팎(40): 네 설정 표의 `kResourcePath` 를 `EngineDefaultAssets` 표로 옮겼다(게이트 값은 다음 `--print-baseline` 에서 다시 적는다) |
| `SilentFallback` | 15(122) |
| `DiscardedLoadResult` | 64(164) |
| `MagicNameLiteral`(보고만) | 72(75) |
| `LiteralGetDefault`(보고만) | 27: 필수 키는 `[]` 와 이름 있는 오류, 선택 키는 없음을 형식으로 |

남은 것의 성격:
- `AssetPathLiteral`: 설정 표 경로(`LipSync` · `AnimationCrowd` · `AnimationLOD` · `NavMeshSettings`)는 `EngineDefaultAssets` 의 칸이 됐고, 정적 `SW_RESOURCE_DATA_KIND` 등록은 경로 전체가 아니라 접미 규칙(`/animation/lipsync.xml` 처럼)만 가진다 — 등록은 서브시스템보다 먼저라 표를 읽을 수 없으니 경로를 가리키지 않고 "이런 모양의 파일" 로만 적는다. 남은 것은 부트 순서가 얽힌 설정, 에디터 팩 자기 경로, 데모 · 벤치.
- `SilentFallback`: 렌더러 · RHI 로그(렌더러 소관). 없을 때의 항등을 인자로 받던 `AudioCurvePoint::evaluate` · `GimmickCircuit::readNumber` · `ScheduleCatalog::readClock` 은 `std::optional` 반환으로 바꿨다(호출부가 `value_or` 로 없음을 정한다).
- `DiscardedLoadResult`: 끈적한 오류 상태로 결과를 쌓는 읽기, 건너뛰기 · 채움 읽기, 편집기 패널의 "깨진 글도 연다" 호출. `StatBlock::loadFromAttributes` 는 읽은 개수를 돌려주는 함수라 이름만 로드 결과로 잡혔으므로 `addAttributes` 로 바꾸고 `(void)` 를 지웠다. 남은 `(void)` 는 대기 · 해제 · 호출자가 이유를 적어 둔 것이다.

### 7.5 단위

| 단위 | 내용 | 선행 |
|---|---|---|
| D6 | Base 의 카탈로그와 이름으로 찾는 값 모음 | D1 |
| D7 | 엔진의 손 읽기(Input · TileMap · Telemetry · Character Fit · `FloatCurve` · Sprite 클립) | D1 |
| D8 | 로드 결과 세 값, 자동화 단계 한 번 읽기, 임포터 접근자 읽기, 에디터 명령의 결과 보고 | D1 |
| D9 | 폴백 인자를 받는 읽기 함수 삭제, 형식으로 막힌 게이트 종류 제거 | D2 ~ D8 |
| P1 | 경로 상수 · 컴포넌트 기본 머티리얼을 `EngineDefaultAssets` 로 | 없음 |
| P2 | 렌더러 첨부를 파이프라인 XML 이 모두 선언(W7 과 함께) | 셰이더 묶음 |
| P4 | 테스트가 원본을 거치게 함(W6) | D9, P1 |
| G1 | 게임 코드의 튜닝 리터럴을 데이터로 | 없음 |
| G2 | Shipping 그림 비교를 구운 팩의 시나리오 스크린샷으로 | 없음 |

- D1 ~ D5(새 읽기 함수, `GameSettings` 커스텀 모음 삭제, 키트 카탈로그를 `readFields` 로)는 들어갔고 `GameSettings` · `BootstrapConfig::_packRoot` · `LoadingScreenSettings::_documentPath` · `EditorToolDefaults` 의 경로 칸은 `DataPath` 가 됐다.
- D6 의 데이터 다시 쓰기는 키트가 읽는 데이터 파일만 건드린다. 두 키트가 한 파일을 읽으면 같은 단위에 넣는다.
- D9 는 `fallbackParameter` 모양이 형식으로 막히지 않아 게이트에 남겼다.
- 각 단위가 끝날 때 두 게이트의 기준선을 `--print-baseline` 으로 다시 적는다.
- 사용자 결정은 없다. 폴백 인자를 지우는 것은 방침의 적용이고 데이터는 다시 쓰면 된다.
