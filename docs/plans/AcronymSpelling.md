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

## 규모 (사전 실행, 2026-10-10)
`py -3 Scripts/lint/fixer/FormatAcronymSpelling.py --report --out <파일>` 의 결과다. 바뀔 이름은 고유 식별자, 사용은 코드 안의 자리 수, 파일은 그 이름이 나오는 C++ 파일 수,
파일 이름은 이름이 바뀌는 C++ 파일 수, 데이터는 `--apply-files` 가 고칠 데이터 파일(Resource · Config · Test 의 XML · JSON …)의 자리 · 파일 수다.

| 약어 | 바뀔 이름 | 사용 | 파일 | 파일 이름 | 데이터 사용 · 파일 | 충돌 |
|---|---|---|---|---|---|---|
| Hud · Ik · Dds · Tls · Udp · Url · Uuid | 13 · 11 · 26 · 33 · 8 · 19 · 7 | 117 · 113 · 121 · 374 · 41 · 139 · 137 | 16 · 15 · 17 · 28 · 8 · 25 · 12 | 5 · 3 · 3 · 1 · 3 · 0 · 3 | 3 · 2 (Hud) | 0 |
| Rts · Srpg · Sql · Ai | 25 · 43 · 52 · 88 | 1,079 · 859 · 661 · 878 | 16 · 11 · 24 · 61 | 8 · 11 · 19 · 30 | 3 · 1 (Rts), 16 · 8 (Ai) | 0 |
| Lod · Api · Rpc · Pso | 88 · 32 · 10 · 57 | 632 · 294 · 39 · 398 | 37 · 32 · 5 · 26 | 6 · 3 · 2 · 3 | 0 | 1 (Api) |
| Xml · Json · Http · Io | 300 · 126 · 40 · 36 | 4,245 · 1,847 · 582 · 471 | 426 · 150 · 31 · 56 | 19 · 9 · 14 · 8 | 0 | 1 (Io) |
| Gpu | 138 | 1,457 | 131 | 26 | 2 · 2 | 0 |
| Ui | 277 | 4,807 | 225 | 105 | 44 · 18 | 1 |
| Cpu · Rhi · Aabb | 17 · 36 · 13 | 92 · 392 · 120 | 25 · 91 · 18 | 0 | 0 | 3 (Rhi) |
| Dsp · Mmo · Acl · Gui · Rpg · Jrpg · Ssl(제품 `OpenSSL`) | 2 · 20 · 5 · 2 · 2 · 38 · 7 | 42 · 221 · 24 · 16 · 10 · 641 · 62 | 1 · 5 · 6 · 5 · 1 · 9 · 3 | 1 · 3 · 2 · 2 · 1 · 9 · 2 | 0 | 0 |
| Id | 1,123 | 17,504 | 1,212 | 5 | 1,157 · 85 | 3 |
| 합계(약어 둘이 든 이름은 한 번) | 2,667 | 38,002 | 1,894 | 302 | 1,225 · 112 | 9 |

사전 실행이 따로 내는 목록(바꾸기 전에 사람이 본다):
- **충돌 쌍 9**: 새 철자가 이미 있는 이름 — `Id`→`ID` · `Rhi`→`RHI` · `Ui`→`UI`(맨 이름 — 열거형 값과 이름공간이 겹치는지), `_pRhiDevice` · `pRhi` · `editorApi` · `platformIo` · `textureId` · `pTextureId`
  (한 함수 안에 옛 철자와 새 철자 변수가 같이 있는지).
- **외부 헤더에도 있는 이름 91**: `ThirdParty/` · vcpkg · Windows SDK 헤더에도 나오는 이름. 대부분 우리 지역 변수와 겹친 것(`nodeId` · `typeId`)이지만 Box2D 이벤트 멤버
  (`shapeIdA` · `bodyIdA`)와 `HttpResponse` 는 남의 이름일 수 있다 — `Id` 단계에서 남의 이름만 등록부 `kExternalName` 에 올린다. 코드모드는 외부 헤더를 읽지 않는다(기계마다 SDK 가 달라도 같은 결과).
- **문자열에도 나오는 이름 175**: 리플렉션 타입 이름 · 테스트 이름 · 로그 · 내보내기 심볼처럼 문자열로 찾는 이름. 코드모드는 문자열을 고치지 않으므로 그 약어 단계에서 문자열 쪽을 손으로 맞춘다.
- **셰이더에도 나오는 이름 13**(`vertexId` · `instanceId` · `g_VisibleInstanceIds` · `GpuBatchInfo` …): 코드모드는 HLSL 을 고치지 않는다. C++ 과 이름을 맞춰 쓰는 것(`GpuBatchInfo`)은 그 단계에서 셰이더도 같이 고친다.
- **이어 붙은 대문자 약어 19**(규칙 4): `RHIGPUTimestamp` · `RTSAICommander` · `SRPGAISettings` · `XMLJSONBinaryRoundtrip` · `ProfilerGPUAPI` · `VulkanRHIAPIVersion` · `kRTSXML` — 그 약어 단계에서 한 쪽을 풀어 쓴다.

## 단계
### 1. 도구와 게이트
- 등록부 `Scripts/lint/AcronymRegistry.py`: 약어 · 줄임말(`kNotAcronym`) · 제품 이름(`kProductName` — `ImGui` 는 그대로, `OpenSsl` → `OpenSSL`) · 남의 이름(앞머리 · 이름공간 · `kExternalName`) · 강제 약어(`kEnforced`)와 철자 판정 한 자리.
- 코드모드 `Scripts/lint/fixer/FormatAcronymSpelling.py`: `--all --acronym Hud` 가 식별자를, `--apply-files --acronym Hud` 가 파일 이름(대소문자만 바뀌면 두 단계 `git mv`) · 파일 이름을 적은 곳(include · CMake · 문서) · 데이터 파일을,
  `--report` 가 위 규모 표와 목록을, `--rename-folders` 가 2-2 의 표를 낸다(옮기지 않는다). 시험은 `Test/PythonTest/TestAcronymSpelling.py`(작은 저장소에서 적용 전후 clang 컴파일).
- 게이트 `Scripts/lint/gate/CheckAcronymSpelling.py`: `kEnforced` 와 `--enforce` 의 약어만 막고 나머지는 요약 줄의 숫자로만 보인다. 약어 하나를 바꾼 커밋이 그 약어를 `kEnforced` 에 올린다.
- 함수 이름의 대문자 약어는 지금 `CheckFunctionVocabulary` 의 `AcronymRun` 이 막는다 — 첫 약어 단계(3절과 같은 커밋)에서 그 검사를 등록부의 강제 약어로 바꾼다.

### 2. 약어마다 한 커밋 (작은 것부터)
순서: Hud · Ik · Dds · Tls · Udp · Url · Uuid → Rts · Srpg · Sql · Ai → Lod · Api · Rpc · Pso → Xml · Json · Http · Io → Gpu → Ui → Cpu. `ID` 도 같은 규칙으로 바꾼다(사용자 결정 2026-10-10: `Id` 보다 `ID` 가 명확하다). 규모가 다른 약어의 열 배라 맨 끝에 둔다. `TagID` 는 이미 대문자. 이름 맨 앞이나 접두 `_` 뒤의 `id` 는 소문자 그대로(`_id`, `id`), 가운데·끝은 `ID`(`entityID`, `getOwnerID`).
한 커밋의 일: `--report --acronym X` 로 충돌 · 의심 목록 확인 → `--apply-files --acronym X`(파일 이름 · include · 데이터) → `--all --acronym X`(식별자) → 문자열 · 셰이더 쪽 손 맞춤 → `kEnforced` 에 X → reconfigure(코드젠) → Debug 컴파일 → 커밋.
- **데이터**: 리플렉션 타입 이름이 씬 · 프리팹 · 설정 XML/JSON 에 문자열로 들어 있다(`UiCanvas…`, `UiDocument` 등 리소스 17 파일에서 확인). 옛 이름 별칭은 두지 않는다 — 데이터를 같은 커밋에서 다시 쓴다(`ResourceDataSchemaTest`).
- **RHI ABI 도장 · 모듈**: 타입 이름이 모듈 ABI 에 걸리면 도장을 다시 만들고 모든 모듈을 같이 다시 짓는다(CLAUDE.md "RHI ABI stamps").
- **대소문자만 다른 파일 이름**: Windows 는 대소문자를 구분하지 않아 `git mv UiX.h UIX.h` 가 두 단계(임시 이름 경유)여야 한다. 코드모드가 이 순서를 지킨다.

### 2-2. 폴더 · 모듈 · 타깃 이름 (사전 목록, 2026-10-10)
`FormatAcronymSpelling.py --rename-folders` 가 찾은 Pascal 형태의 약어 폴더다(Engine 폴더 재배치 뒤 경로, `Resource/` · `Scripts/` · 서드파티 제외):

| 지금 | 바뀐 이름 | 따라 바뀌는 것 |
|---|---|---|
| `Online/Http` (GameFramework Base) | `HTTP` | include 경로 |
| `Serialization/Json` · `Serialization/Xml` (Engine) | `JSON` · `XML` | include 경로 |
| `Kits/Rpg` · `Rpg/WitcherRpg` · `Test/.../Kits/Rpg` | `RPG` · `WitcherRPG` | 키트 모듈 `GF_WitcherRpg` → `GF_WitcherRPG`, 매니페스트 · DLL · 게임의 모듈 표(`SWGame.module.json`) |
| `Rpg/ClassicJrpg` | `ClassicJRPG` | 모듈 `GF_ClassicJRPG` |
| `Strategy/TacticsSrpg` | `TacticsSRPG` | 모듈 `GF_TacticsSRPG` |
| `Storage/SqlStore/Shared` · `Storage/SqlStore/Server` | `SQLStore` | 모듈 `GF_SQLStore` · `GF_Server_SQLStore`, `CheckThirdPartyIsolation` 의 링크 주인 규칙 |
| `SqlStore/Shared/Sql` · `SqlStore/Shared/Driver/Sqlite` | `SQL` · `SQLite` | include 경로(`SQLite` 는 제품 철자) |
| `GameFramework/Base/Online/Security/OpenSsl` | `OpenSSL` | `CheckThirdPartyIsolation` · 그 폴더의 CMake(이름은 서드파티 제품 이름 `OpenSSL` 을 따른다) |
| `GameFramework/Base/UI/Hud` | `HUD` | include 경로 |
| `Online/Account/Shared/Api` · `Online/Economy/Shared/Api` (키트) | `API` | include 경로 |
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
