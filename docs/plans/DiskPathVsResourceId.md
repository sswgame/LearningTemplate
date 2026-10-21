# 디스크 경로와 리소스 id 의 입구 나누기 (2026-10-21)

> 상태: 구현중 · 선행: 없음 · 영역: engine · 옛번호: C-70 · 진행: 3/3

후보 `DiskPathVsResourceId`(`Candidates-engine.md`)를 계획으로 올린 문서입니다. 사용자가 안 B 를 골랐고(2026-10-22) D1 ~ D3 이 들어갔습니다(`wt/cleanup347`, 빌드 · 테스트 미확인). 남은 것은 5절의 확인입니다.

## 1. 근본 원인

2026-10-19 CI 실패는 쿠커 · 테스트가 디스크로 훑은 절대 경로를 리소스 id 입구(`SceneDocument::loadXML` → `ResourceIO::readTextResource`)에 넣어 생겼습니다. Shipping(낱개 파일 불허)이 그것을 거절했습니다. `loadXMLFromDiskFile` 계열로 고쳤지만 입구는 그대로라 같은 모양이 또 날 수 있습니다.

원인은 둘이 겹친 것입니다.
- 리소스 id 입구가 OS 절대 경로도 받습니다(`ResourceIO.cpp` 의 찾는 순서 0단계 — 임시 파일 · 외부 세이브용). 그래서 디스크 경로와 리소스 id 가 같은 `string_view` 로 들어옵니다.
- 리소스 루트 **안**의 절대 경로를 거절하는 규칙이 구성마다 다릅니다(`isLooseFileAllowed` 가 거짓일 때만 거절). Dev 는 받고 Shipping 만 거절하므로, 잘못 넣은 호출이 Dev 테스트를 지나 배포 구성에서야 드러납니다.

같은 모양이 남을 수 있는 곳은 `ResourceIO::readTextResource` · `readBinaryResource` · `readBinaryResourceAsync` · `hasResource` 와 그것을 부르는 모든 `load( string_view )` 계열 입구입니다. `collectResources` 는 이미 절대 경로를 리소스 id 가 아니라고 거절합니다.

## 2. 안과 권고

| 안 | 내용 | 막는 것 | 비용 |
|---|---|---|---|
| A | 리소스 루트 안 절대 경로 거절을 **모든 구성**에서 한다. 루트 밖 절대 경로(임시 · 세이브)는 지금처럼 받는다 | Dev 에서도 같은 실패가 나서 테스트가 잡는다 | 루트 안 절대 경로를 리소스 입구에 넣는 Dev 호출부(에디터 · 테스트)를 디스크 입구로 옮긴다 |
| B(권고) | 리소스 id 입구는 **리소스 id 만** 받는다. OS 절대 경로면 오류로 멈추고, 디스크 파일은 디스크 입구(`ResourceIO::readTextFile` · `load*FromDiskFile`)로만 읽는다 | 입구가 둘로 나뉘어 디스크 경로가 리소스 입구로 들어갈 길이 없어진다. 구성과 무관하다 | A 의 호출부에 더해 임시 파일 · 세이브를 리소스 입구로 읽는 호출부도 옮긴다 |
| C | `ResourceId` 형식(문자열에서 저절로 바뀌지 않는 형식)을 입구 인자로 둔다 | 컴파일에서 막는다 | 입구와 호출부 수백 곳의 시그니처가 바뀐다. 리터럴 id 를 쓰는 곳마다 생성 함수가 붙는다 |

권고는 **B** 입니다. 구성마다 다른 규칙을 없애는 것이 근본 원인이고, B 는 그것을 입구 하나의 규칙("리소스 id 만")으로 끝냅니다. 디스크 쪽은 이미 `LooseFilePath` · `readTextFile` · `load*FromDiskFile` 로 갈라져 있어 새 형식이 필요 없습니다.
C 는 B 뒤에도 같은 결함이 다시 나올 때 합니다. 디스크 입구를 형식(`DiskFilePath`)으로 막는 안은 하지 않습니다 — Shipping App 이 쿠킹 호스트라 디스크 입구를 컴파일에서 뺄 수 없습니다.

## 3. 구현 단위

| 단위 | 내용 | 비고 |
|---|---|---|
| D1 ✅ | 리소스 입구에 OS 경로를 넣는 호출부를 찾는다 | 정적 조사(빌드 없이). 결과는 4절 |
| D2 ✅ | 목록의 호출부를 옮긴다 — 디스크 파일은 디스크 입구(`readTextFile` · `readBinaryFile` · `load*FromDiskFile`), 루트 밖 폴더를 리소스로 읽는 곳(테스트 임시 폴더)은 붙인 디스크 도메인의 id | 4절 |
| D3 ✅ | `ResourceIO` 의 0단계(OS 절대 경로를 디스크에서 바로)와 3단계(풀지 못한 id 를 작업 폴더 기준으로 다시 열기)를 지웠다. 리소스 입구는 OS 경로면 모든 구성에서 오류 로그를 남기고 false 다 | `ResourceIO.h` · `SceneDocument.h` 주석도 고쳤다 |

## 4. 정한 것과 옮긴 곳 (2026-10-22)

- **입구가 둘이다.** 리소스 입구(`ResourceIO::readTextResource` · `readBinaryResource` · `readBinaryResourceAsync` · `hasResource` · `collectResources`)는 리소스 id 만 받고, OS 경로는 `rejectsOSPath` 가 오류로 거절한다. 디스크 입구는 `ResourceIO::readTextFile` · `readBinaryFile`(새로 둠)이다.
- **루트 밖 폴더는 붙여서 id 로 읽는다.** `ResourcePaths::mountDiskDomain( 이름, 폴더 )` 가 디스크 폴더를 도메인으로 붙이고, 그 id 는 팩을 보지 않고 디스크에서 읽는다(낱개 파일을 막는 배포 구성에서도). 리소스 루트 안 폴더는 붙일 수 없다. 언리얼이 디렉터리를 마운트 지점으로 붙이는 것과 같은 자리다.
  테스트는 `test::makeTempResourceID( 디스크 경로 )` 로 프로세스 임시 폴더를 도메인 `testtemp` 로 붙이고 id 를 얻는다 — 쓰기는 디스크 경로, 읽기(로더 · 캐시 · 컴포넌트 경로 칸)는 id 다. 로더마다 디스크 입구를 늘리지 않아도 캐시(`MeshCache` · `SharedAssetCache` · `PrefabCache` · `MaterialCache`)를 지나는 테스트가 그대로 돈다.
- **Source 에서 옮긴 곳**: 사용자 바인딩(`InputMap::loadUserBindings` · `ShellInputActions::loadUserBindings` — 디스크에 쓴 플레이어 파일), 모델 임포트의 클립 데이터(`ModelImportUtil::readClipData`), `Archive` 의 파일 생성자(디스크만 — `ShaderReflectionLibrary` 의 매니페스트는 리소스로 읽어 버퍼로 연다), 로컬라이즈 파일 읽기(디스크를 먼저 보던 두 갈래를 리소스 하나로), 에디터 도구 문서(`EditorToolAssetCommands` — 애니메이션 그래프 · 대화 그래프 · 스프라이트 클립 · 시퀀스 · 타일맵에 `loadFromDiskFile` 을 더하고 그것으로 읽는다).
- **테스트에서 옮긴 곳**: 임시 파일을 리소스 로더로 읽던 약 140 곳(`makeTempResourceID`), 쿠킹 작업 기록 테스트(디스크 입구), 설정 문서 대조(`JSONDocument::loadFile`), 낱개 우선 모드 테스트(임시 리소스 루트의 같은 키), 절대 경로 테스트는 "모든 구성에서 거절" 로 다시 썼다(`ResourceTest.ResourceEntryRefusesOSPathsInEveryMode`).
- 조사는 정적이다(같은 변수가 디스크 API 와 리소스 입구에 함께 들어가는 곳, 임시 경로 변수가 로더 · setter · 경로 칸으로 가는 곳). 함수 인자 · 구조체로 건너간 경로는 놓쳤을 수 있다 — 테스트 실행의 `is an OS path, not a resource id` 오류 줄이 남은 곳을 알린다.

## 5. 남은 확인

- 2026-10-22 통합: nogpu 테스트의 `is an OS path` 오류는 의도한 거절(`ResourceTest.ResourceEntryRefusesOSPathsInEveryMode`)만 남았다. 같이 고친 것 — VAT 쿠커가 쿠킹 폴더 없이 id 를 디스크 경로로 쓰던 것(메시 곁의 디스크 자리로), 퍼즈 대상이 OS 임시 경로를 리소스 입구에 넘겨 아무것도 먹이지 못하던 것.
- 녹화 시나리오는 `saved` 도메인으로 붙여 id 로 다시 돌린다(결정 기록 5-4, `ScenarioRecorder::makeReplayID`). id 의 도메인 뒤는 표기를 그대로 두므로 리눅스도 원래 표기로 찾는다.
- 전체 빌드와 `EngineTest` · `EditorTest` · `SmokeTest` 를 Debug · Shipping 으로 돌려 `is an OS path, not a resource id` 오류 줄을 0 으로 만든다. Shipping 쿠킹(`--cook-scenes`)과 에디터 도구 문서 열기 · 저장을 확인한다.
- 리눅스: 붙인 도메인의 새 파일 쓰기 경로(`ResourcePaths::makeAbsolutePath`)는 키를 소문자로 붙인다 — 대소문자가 섞인 케이스 폴더에 id 로 **쓰는** 테스트가 있으면 디스크 경로로 쓴다.
