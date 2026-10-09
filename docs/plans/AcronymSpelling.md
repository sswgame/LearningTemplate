# 약어 철자 통일 계획

약어는 어디서나 대문자로 쓴다. 지금은 타입 이름이 `RHIDevice` · `AABB` · `TagID` 는 대문자, `UiSystem` · `GpuScene` · `AiPerception` · `HttpClient` · `XmlCatalog` 는 Pascal 형태로 갈려 있고,
함수 이름은 약어를 한 단어로 쓰는 규칙(`updateUi` · `initRhi` · `queryAabb`, AGENTS.md "Function names")이라 약어인지 이름에서 보이지 않는다. 사용자 결정(2026-10-10): **다 대문자로 통일한다.**

## 규칙 (AGENTS.md 에 옮길 문안)
1. 약어는 대문자로 쓴다: `UI` · `GPU` · `AI` · `HTTP` · `XML` · `JSON` · `SQL` · `IO` · `LOD` · `RTS` · `SRPG` · `HUD` · `URL` · `UUID` · `DDS` · `TLS` · `UDP` · `IK` · `API` · `RPC` · `PSO` · `CPU` · `RHI` · `AABB` · `ID` · `QA` · `DSP` · `MMO` · `ACL` · `GUI`.
   무엇이 약어인지는 한 파일의 등록부(`Scripts/lint/` 아래)가 정한다 — 게이트와 코드모드가 같은 목록을 읽는다. 줄임말(`Nav` · `Anim` · `Gimmick`)은 약어가 아니라서 그대로다.
2. **타입 · 파일 · 네임스페이스 · 열거형 이름**: 어디에 있든 대문자(`UISystem`, `GPUScene`, `AIPerception`, `HTTPClient`, `XMLCatalog`, `JSONWriter`, `UISystem.h`).
3. **함수 · 변수 · 멤버 이름**: 단어 가운데나 끝에서는 대문자(`updateUI`, `initRHI`, `queryAABB`, `isValidUTF8`, `entityID`). 이름 맨 앞에서는 소문자 전체(`uiSystem`, `_gpuScene`, `aiTarget`) — 첫 글자가 대문자면 camelCase 가 아니다.
   포인터 접두 뒤에서는 단어 가운데로 본다(`pUISystem`, `_pGPUScene`).
4. 대문자 약어가 이어 붙으면 단어 경계가 가려지므로 이어 붙이지 않는다(`UIAI…` 같은 이름은 한 쪽을 풀어 쓰거나 `UI` · `AI` 사이에 단어를 둔다). 등록부 검사가 이어 붙은 대문자 약어 쌍을 보고한다.
5. **폴더 · 모듈 · CMake 타깃 · 시험 실행 파일 · 프리셋 이름도 같은 규칙**이다(사용자 결정 2026-10-10). 폴더는 이미 `UI` · `AI` · `RHI` · `GL` · `DX` · `API` · `ABI` 로 대문자인 곳이 많다. 소문자 폴더(`Resource/` 아래, 경로가 곧 에셋 id 라 `CheckResourceCasing` 이 소문자를 강제한다)와 `Config/Game/<게임>.json` 의 게임 이름은 약어 규칙 대상이 아니다.
6. 소문자 확장자 · 리소스 경로(`*.ui.xml`) · 전역 변수 접두(`gv_ui…`) · 서드파티 이름은 그대로다.

## 규모 (식별자 토큰 단위 어림, 2026-10-10)
| 약어 | 고유 식별자 | 사용 | 파일 이름 |
|---|---|---|---|
| Ui | ≈260 | ≈5,700 | 80 |
| Gpu | ≈310 | ≈6,300 | 23 |
| Xml · Json | ≈230 · ≈170 | ≈4,700 · ≈3,900 | 17 · 8 |
| Http | ≈410 | ≈1,800 | 12 |
| Ai · Sql · Rts · Srpg · Lod · Hud · … | ≈400 | ≈7,000 | ≈40 |
| Id | ≈4,560 | ≈59,500 | 3 |

(노이즈 포함 — 서드파티 이름 · 단어 일부가 섞였다. 코드모드의 사전 실행이 정확한 수를 낸다.)

## 단계
### 1. 도구와 게이트 (코드 변경 없음)
- 약어 등록부 한 파일 + `Scripts/lint/fixer/FormatAcronymSpelling.py`(코드모드) + `Scripts/lint/gate/CheckAcronymSpelling.py`(게이트, 표 규칙 2 · 3 · 4).
- 코드모드는 식별자 토큰만 바꾼다: 문자열 리터럴 · 주석 · 서드파티 헤더(`ThirdParty/`, vcpkg) · `gv_` 접두 · 매크로 `SW_*` 는 건드리지 않는다. 사전 실행(--check)이 바뀔 식별자 목록과 충돌 쌍(`UiX` ↔ `UIX` 가 이미 둘 다 있는 이름)을 낸다.
- FixPass 마다 badSample · goodSample (`CheckFixersAreAlive`) · 게이트 `selfTestCases`.

### 2. 약어마다 한 커밋 (작은 것부터)
순서: Hud · Ik · Dds · Tls · Udp · Url · Uuid → Rts · Srpg · Sql · Ai → Lod · Api · Rpc · Pso → Xml · Json · Http · Io → Gpu → Ui → Cpu. `ID` 도 같은 규칙으로 바꾼다(사용자 결정 2026-10-10: `Id` 보다 `ID` 가 명확하다). 규모가 다른 약어의 열 배라 맨 끝에 둔다. `TagID` 는 이미 대문자. 이름 맨 앞이나 접두 `_` 뒤의 `id` 는 소문자 그대로(`_id`, `id`), 가운데·끝은 `ID`(`entityID`, `getOwnerID`).
한 커밋의 일: 코드모드 --all → 파일 `git mv`(대소문자만 바뀌는 이름은 Windows 에서 두 단계로) → include 경로 치환 → 데이터 다시 쓰기 → reconfigure(코드젠) → Debug 컴파일 → 커밋.
- **데이터**: 리플렉션 타입 이름이 씬 · 프리팹 · 설정 XML/JSON 에 문자열로 들어 있다(`UiCanvas…`, `UiDocument` 등 리소스 17 파일에서 확인). 옛 이름 별칭은 두지 않는다 — 데이터를 같은 커밋에서 다시 쓴다(`ResourceDataSchemaTest`).
- **RHI ABI 도장 · 모듈**: 타입 이름이 모듈 ABI 에 걸리면 도장을 다시 만들고 모든 모듈을 같이 다시 짓는다(CLAUDE.md "RHI ABI stamps").
- **대소문자만 다른 파일 이름**: Windows 는 대소문자를 구분하지 않아 `git mv UiX.h UIX.h` 가 두 단계(임시 이름 경유)여야 한다. 코드모드가 이 순서를 지킨다.

### 2-2. 폴더 · 모듈 · 타깃 이름 (사전 목록, 2026-10-10)
Pascal 형태로 남은 약어 폴더는 열네 곳이다(`Tools/vcpkg` 등 서드파티 제외):

| 지금 | 바뀐 이름 | 따라 바뀌는 것 |
|---|---|---|
| `Online/Http` (GameFramework Base) | `HTTP` | include 경로 |
| `Utility/Json` · `Utility/Xml` (Engine) | `JSON` · `XML` | include 경로(0-3 에서 `Serialization` 으로 옮기면 그 뒤 이름) |
| `Kits/Rpg` · `Rpg/WitcherRpg` · `Test/.../Kits/Rpg` | `RPG` · `WitcherRPG` | 키트 모듈 `GF_WitcherRpg` → `GF_WitcherRPG`, 매니페스트 · DLL · 게임의 모듈 표(`SWGame.module.json`) |
| `Rpg/ClassicJrpg` | `ClassicJRPG` | 모듈 `GF_ClassicJRPG` |
| `Strategy/TacticsSrpg` | `TacticsSRPG` | 모듈 `GF_TacticsSRPG` |
| `Storage/SqlStore` · `Storage/Server/SqlStore` | `SQLStore` | 모듈 `GF_SQLStore` · `GF_Server_SQLStore`, `CheckThirdPartyIsolation` 의 링크 주인 규칙 |
| `Engine/Network/OpenSsl` | `OpenSSL` | `CheckThirdPartyIsolation` · `Source/Engine/CMakeLists.txt`(이름은 서드파티 제품 이름 `OpenSSL` 을 따른다) |
| `Core/Uuid` · `Test/CoreTest/Uuid` | `UUID` | include 경로 |
| `Test/Qa` | `QA` | `Test/QA/Golden` · `Test/QA/Perf` · `Games.json` 을 읽는 `Scripts/qa/*.py` 의 경로, `Test/PythonTest/CMakeLists.txt` · `Test/README.md` · docs 의 `Test/Qa` 서른 곳 안팎 |
| `Engine/Audio/Dsp` | `DSP` | include 경로 |
| `Kits/Feature/Network/NetMmo` | `NetMMO` | 모듈 `GF_NetMMO` |
| `Engine/Animation/Codec/Acl` | `ACL` | include 경로 · `AclAnimCodec` → `ACLAnimCodec` (ACL 라이브러리 이름과 맞춤) |
| `Editor/Common/Gui` | `GUI` | include 경로 |
| `Test/EditorUiTest` | `EditorUITest` | 실행 파일 · CTest 이름 · `CheckTestSuites` 의 `XxxTest` 규칙 · `.vscode/launch.json` · CLAUDE.md 의 시험 목록 |

- **`Scripts/` 의 폴더(`qa` · `lint` · `gate` …)는 파이썬 패키지 이름이라 소문자 규칙이 따로 있다**(`CheckScriptLayout`) — `Scripts/qa` 는 그대로 두고 `Test/Qa` 만 `Test/QA` 로 바꾼다. 줄임말(`Net` · `Dev` · `Anim` · `Resp` · `Std`)과 제품 이름(`Box2D` · `Jolt` · `FreeType`)은 약어가 아니라서 대상이 아니다.
- 모듈 이름은 DLL 파일 이름 · 핫 리로드의 섀도 복사본 · 매니페스트 · `ResolvedModules.txt` 가 쓰므로 CMake 를 새로 구성(reconfigure)하고 오래된 `Bin/Modules` 산출물은 configure 가 지운다(Dev Bin 정리).
- `CheckProductNames.py` · `CheckGameFrameworkLayers.py` 의 이름 표를 같이 고친다.
- 대소문자만 바뀌는 이동은 임시 이름을 거친다(2 단계 `git mv`).

### 3. 문서 · 규칙
AGENTS.md "Function names" 의 약어 문단과 타입 문단을 위 규칙으로 바꾸고, `CheckFunctionVocabulary.py` 의 `AcronymRun` 검사를 반대로(약어는 대문자) 고친다. docs/04_CodingGuidelines.md 예시, 주석의 옛 이름, README 도 같은 커밋에서 치환한다.

## 순서
엔진 분할 계획의 0-3(Engine 폴더 재배치)과 GameFramework 폴더 재배치가 끝난 뒤에 한다 — 파일 이동과 이름 변경이 같은 파일을 두 번 건드리지 않게. 이름 점검(명확성) 결과의 이름 변경도 같은 코드모드 틀에 싣는다.

## 위험
- 치환 범위가 매우 커서(수만 토큰) 한 커밋이 수백 파일이다. 약어마다 나누고, 각 커밋 뒤 전 프리셋 컴파일 + `ctest -L lint · nogpu`.
- 서드파티 헤더와 이름이 겹치는 곳(`Vk…` 는 약어 목록에 없음, `GL…`), 외부 도구가 읽는 이름(리플렉션 파서 출력, 셰이더 경로)은 사전 실행 목록에서 먼저 가려낸다.
