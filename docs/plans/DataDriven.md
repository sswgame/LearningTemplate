# 데이터 주도 구조 정리 계획 (2026-10-16)

값의 정본은 데이터 파일이고 코드 · 헤더는 형식만 가진다. 필수 값이 없으면 조용히 기본값 · 대체로 가지 않고 오류로 멈춘다. 결정은 [결정 기록](../09_Decisions.md) 5-4 "유지보수성을 위한 데이터화"이고, 이 문서는 6 영역 조사(2026-10-16)가 드러낸 위반을 뿌리부터 없애는 순서다. 진행 표는 린트 게이트 `CheckDataDrivenStructure`의 `--summary` 가 정본이다(기준선이 줄면 진척).

## 1. 뿌리 원인

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
| W0 형식 | `DataPath` · `Optional` · `[[nodiscard]]` 로더 결과 · 소유 규칙을 만들고 `EngineDefaultAssets` 한 구조체에 먼저 적용(첫 적용) | 셰이더 묶음 병합 뒤 가장 먼저 |
| W1 엔진 기본 에셋 | `EngineDefaultAssets` 전 필드 · `ShaderCookRequest` 리터럴 · 시스템 설정 경로 상수(물리 · 내비 · 오디오 믹서 · UI 문서 · Render2D · 네트워크 에뮬레이션) · 컴포넌트 기본 머티리얼 → XML. 필수 읽기 형식과 `[[nodiscard]]` 결과 | 셰이더 묶음(경로 일부 선행) 병합 뒤 |
| W2 엔진 조용한 폴백 | `Material.cpp` 로드 실패 · `ResourcePaths` 우선순위 · `Render2DSettings` · 오디오 믹서 · `AnimationSystem` · `PhysicsSettings::ensureDefaults` · `CharacterDataReader` fallback 인자 · `CompressionStream` · 머티리얼 불리언 파싱 · 팩 도메인 떼고 재조회 | W1 뒤 |
| W3 에디터 | 도구 기본값(`EditorToolDefaults`) · 테마 폴백 · 패널 처음 문서 · `findPanel` id 상수 | W1 뒤, W2 와 병행 |
| W4 게임 | `gamesettings.xml` 로 카탈로그 · 오디오 · UI 경로, 이중 정본 제거, 입력 액션 · 카탈로그 id, 로드 실패 `return true` → 오류, 슈터 무기 폴백, 계층 역전 | W1 뒤, 게임별로 쪼갬 |
| W5 도구 | `Scripts/common/Config.py` 의 `{}` 반환 · `FileScan.py` 조용한 통과 · `AssetValidation` 기본값 · CMake `ERROR_VARIABLE` 뒤 빈 값 · `DEFAULT_VALUE` | 지금(빌드 없음, `TestTargets.cmake` 는 testspeed 뒤) |
| W6 테스트 | `EngineDefaultAssets{}` 기본 생성 · 정본 경로 리터럴 · 폴백을 단언하는 테스트 | W1 · W2 뒤 |
| W8 게이트 | `CheckDataDrivenStructure` — 형식이 막지 못하는 틈의 기준선 동결(이미 `datagate` 가 만드는 중, 병합은 W0 뒤에 기준선을 다시 뜨고) | 마지막 |
| W7 셰이더 잔여 | 죽은 `ALPHA_TEST` 가지 · `material_keywords` 주석 무시 · 셰이더 안 조용한 대체(AO · 월드 · 키 라이트) 설계 판단 · 파이프라인 XML · 재질 반복 | 셰이더 묶음 뒤 |

## 4. 정한 판단

- 게임이 데이터를 못 읽으면 `return true` 로 모듈만 띄우던 설계(주석에 의도 기록)는 방침과 충돌해 오류로 멈추게 한다.
- `REFLECT( Alias = 옛이름 )` 와 XML · JSON · bin 이중 읽기 경로: 별칭 · 옛 형식 리더는 두지 않는 방침이라 쓰는 곳을 세어 데이터를 다시 쓰고 없앤다. 스키마 버전 이행(SchemaMigrate)은 별개라 남긴다.
- 사용자 설정(사용자 폴더의 파일)이 깨졌을 때는 엔진 필수 데이터와 달라 복구가 필요하다: 조용히 기본값으로 가지 않고 깨진 파일을 옆으로 치우고 오류 로그를 남긴 뒤 새로 만든다.
- 도구 훅(`.claude/hooks`)의 fail-open 은 훅 오류로 세션이 막히지 않게 하려는 것이라 남기고 이유를 주석에 적는다.

## 5. W2 결과와 남은 것 (2026-10-16)

고친 것: 머티리얼 로드 실패 · 체커 텍스처 실패 → 초기화 실패(대체는 `MeshComponent` 한 곳), 머티리얼 불리언 · 품질 · LOD · usage · 범위 글 → 읽기 오류(`parseBoolToken( …, bool& )`, `parseOptionalBoolField`),
`Render2DSettings` 정본 하나(게임 팩 덮어쓰기 · 코드 `Default` 표 제거, 렌더러 초기화가 `reloadActive` 로 읽고 실패면 멈춤), 리소스 우선순위 빈 목록 → `ResourcePaths::initialize` 실패,
오디오 믹서 · 물리 설정 · 엔진 기본 에셋 · 문화권 표 → 기동 실패(코드 그래프 · `ensureDefaults` · built-in defaults 제거), 군중 · LOD 표 → 단언하고 기능을 끈다(코드 표로 대신하지 않음),
압축 코덱 대체 제거, `CharacterDataReader` 를 필수 `readXxx( node, name )` · 선택 `readOptionalXxx( node, name, absentValue )` 로 가름, 알림 인자의 "적지 않음" 값을 처리기 선언(`AnimationNotifyParamDef`) 한 곳으로,
깨진 사용자 설정은 `.corrupt` 로 치우고 오류, 맨 `.scene` 접미사 별칭 제거.

남긴 것(이유):
- 팩 키의 도메인 떼고 재조회 — 도메인 없는 리소스 id(검색 우선순위로 푸는 형식)가 떼어 낸 키를 1단계에서 맞히는 데 기대고 있다. 키를 전역 경로로 바꾸려면 도메인 없는 id 쓰는 곳을 먼저 없애야 한다.
- `attachmentFormatOrDefault` · `getAttachmentClearColorOrDefault` — 뿌리는 선언하지 않은 첨부를 `ensureNamed` 가 코드 포맷으로 만드는 것이다. 선언 필수화는 파이프라인 XML(셰이더 작업 소관)과 함께 한다.
- `REFLECT( Alias )` — 제품 데이터 사용 0, 테스트 4. 리더 제거는 리플렉션 파서 · 코드젠 변경이라 따로 한다. 프리팹 JSON 리더는 데이터 0 이지만 에디터가 쓴다(에디터 작업 소관).
- XML 저작본 ↔ bin 쿠킹본 두 리더(`PrefabAsset` · `SceneDocument`)는 옛 형식이 아니라 쿠킹 파이프라인이다.
- 같은 모양이 더 있는 곳: `NavMeshSettings::ensureDefaults`, `PhysicsDesc` 의 모르는 재질 → 첫 재질, `MaterialUtil::parseBlendMode` 의 모르는 이름 → Opaque, `GameDataXML` · `CameraPreset`(GameFramework 작업 소관).
