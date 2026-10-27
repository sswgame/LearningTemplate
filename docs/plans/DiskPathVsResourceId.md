# 디스크 경로와 리소스 id 의 입구 나누기 (2026-10-21)

> 상태: 측정대기 · 선행: 없음 · 영역: engine · 옛번호: C-70 · 진행: 3/3

**남은 확인(통합에서 확인):** 매니페스트 id `cook-scenes-shipping`, `editor-document-open-save`(5절). 확인이 끝나면 통합 담당이 이 문서를 닫는다.

후보 `DiskPathVsResourceId` 를 계획으로 올린 문서입니다. 사용자가 안 B 를 골랐고(2026-10-22) D1 ~ D3 이 들어갔습니다(`wt/cleanup347`).

## 1. 근본 원인

2026-10-19 CI 실패는 쿠커 · 테스트가 디스크로 훑은 절대 경로를 리소스 id 입구(`SceneDocument::loadXML` → `ResourceIO::readTextResource`)에 넣어 생겼습니다(Shipping 이 낱개 파일을 거절). 원인은 둘이 겹친 것입니다.
- 리소스 id 입구가 OS 절대 경로도 받습니다(`ResourceIO.cpp` 찾는 순서 0단계 — 임시 파일 · 외부 세이브용). 디스크 경로와 리소스 id 가 같은 `string_view` 로 들어옵니다.
- 리소스 루트 **안**의 절대 경로를 거절하는 규칙이 구성마다 다릅니다(`isLooseFileAllowed` 가 거짓일 때만). Dev 는 받고 Shipping 만 거절하므로 잘못 넣은 호출이 Dev 테스트를 지나 배포 구성에서야 드러납니다.

같은 모양이 남을 수 있는 곳: `ResourceIO::readTextResource` · `readBinaryResource` · `readBinaryResourceAsync` · `hasResource` 와 그것을 부르는 모든 `load( string_view )` 입구(`collectResources` 는 이미 거절).

## 2. 안과 결정

| 안 | 내용 | 비용 |
|---|---|---|
| A | 리소스 루트 안 절대 경로 거절을 **모든 구성**에서 한다(루트 밖은 받음) | 루트 안 절대 경로를 넣는 Dev 호출부를 디스크 입구로 옮긴다 |
| **B(채택)** | 리소스 id 입구는 **리소스 id 만** 받는다. OS 절대 경로면 오류로 멈추고 디스크 파일은 디스크 입구(`readTextFile` · `load*FromDiskFile`)로만 읽는다 | A 의 호출부 + 임시 파일 · 세이브를 리소스 입구로 읽는 호출부 |
| C | `ResourceId` 형식을 입구 인자로(컴파일에서 막음) | 입구와 호출부 수백 곳의 시그니처가 바뀐다 |

**C 도 들어갔다(2026-10-27, `wt/resid`, 감사 SG U12)** — 리소스 입구(`readTextResource` · `readBinaryResource` · `readBinaryResourceAsync` · `hasResource` · `collectResources` 의 폴더)의 인자가 `ResourceID`(`Engine/Resource/ResourceID.h`)이고, 글에서 암시적으로 만들 수 없어 일반 문자열 · 디스크 경로를 그대로 넘기면 컴파일되지 않는다. 호출 161 곳이 `ResourceID{ … }` 로 글을 id 로 쓴다는 것을 드러낸다. 실행 중 OS 경로 거절(B)은 그대로다 — 형식은 섞는 실수를 막고, 내용(OS 경로)은 입구가 본다. 남은 단계: 각 에셋의 `loadFromResource( string_view )`(선언 73 · 호출 약 390)와 `collectResources` 의 결과 목록, `ResourcePaths::toResourceID` 의 반환을 `ResourceID` 로 올리면 감싸는 자리가 데이터 읽기 쪽으로 올라간다.

B 는 구성마다 다른 규칙을 입구 하나의 규칙("리소스 id 만")으로 끝낸다. 디스크 쪽은 이미 `LooseFilePath` · `readTextFile` · `load*FromDiskFile` 로 갈라져 있어 새 형식이 필요 없다. 디스크 입구를 형식(`DiskFilePath`)으로 막는 안은 하지 않는다 — Shipping App 이 쿠킹 호스트라 디스크 입구를 컴파일에서 뺄 수 없다.

## 3. 구현 단위 (D1 ~ D3 끝)

D1 호출부 정적 조사, D2 호출부 이전, D3 `ResourceIO` 의 0단계(OS 경로를 디스크에서 바로)와 3단계(풀지 못한 id 를 작업 폴더 기준으로 다시 열기) 삭제 — 리소스 입구는 OS 경로면 모든 구성에서 오류 로그를 남기고 false 다.

## 4. 정한 것

- **입구가 둘이다.** 리소스 입구(`readTextResource` · `readBinaryResource` · `readBinaryResourceAsync` · `hasResource` · `collectResources`)는 `ResourceID` 만 받고(형식) OS 경로는 `rejectsOSPath` 가 거절한다(내용). 디스크 입구는 `ResourceIO::readTextFile` · `readBinaryFile`.
- **루트 밖 폴더는 붙여서 id 로 읽는다.** `ResourcePaths::mountDiskDomain( 이름, 폴더 )` 가 디스크 폴더를 도메인으로 붙이고 그 id 는 팩을 보지 않고 디스크에서 읽는다(낱개 파일을 막는 구성에서도). 리소스 루트 안 폴더는 붙일 수 없다(언리얼 마운트 지점과 같은 자리). 테스트는 `test::makeTempResourceID( 디스크 경로 )` 로 임시 폴더를 도메인 `testtemp` 로 붙이고 id 를 얻는다 — 쓰기는 디스크 경로, 읽기(로더 · 캐시 · 컴포넌트 경로 칸)는 id.
- 옮긴 Source: 사용자 바인딩(`InputMap::loadUserBindings` · `ShellInputActions::loadUserBindings`), `ModelImportUtil::readClipData`, `Archive` 의 파일 생성자(디스크만), 로컬라이즈 파일 읽기, 에디터 도구 문서(`EditorToolAssetCommands` 의 `loadFromDiskFile`). 테스트는 임시 파일을 리소스 로더로 읽던 약 140 곳(`makeTempResourceID`), 절대 경로 테스트는 "모든 구성에서 거절"(`ResourceTest.ResourceEntryRefusesOSPathsInEveryMode`)로 다시 썼다.
- 조사는 정적이라 함수 인자 · 구조체로 건너간 경로는 놓쳤을 수 있다 — 테스트 실행의 `is an OS path, not a resource id` 오류 줄이 남은 곳을 알린다(파일을 쓰지 않는 "없는 파일" 경로 변수는 정적 조사가 놓쳤다).
- 녹화 시나리오는 `saved` 도메인으로 붙여 id 로 다시 돌린다(결정 기록 5-4, `ScenarioRecorder::makeReplayID`). `ResourcePaths::toResourceID` 가 붙인 도메인 폴더 아래 절대 경로를 `<도메인>/…` id 로 바꾸고 `EditorSceneFileCommands::loadScene` 은 id 로 못 바꾸면 폴백 대신 오류다.
- 리눅스: 붙인 도메인의 새 파일 쓰기(`resolveWritePath`)는 이미 있는 폴더는 디스크 표기를, 없는 폴더부터는 id 의 표기를 쓴다(같은 폴더 옆에 대소문자만 다른 새 폴더를 만들지 않는다). id 를 `toResourceID` 로 만들었다면 표기가 이미 소문자라 새 폴더를 id 로 쓰고 원래 표기 디스크 경로로 읽는 테스트는 읽지 못한다.

## 5. 남은 확인

2026-10-23 Debug `full-build` · Shipping 빌드 뒤 `EngineTest` · `EditorTest` · `SmokeTest` 전체(GPU 호스트 스위트 제외)가 통과했고(Debug 3958 · 474 · 71, Shipping 3554 · 443 · 6) `is an OS path, not a resource id` 줄은 의도한 거절 6 줄만 남았다(Shipping 의 `MaterialTest.GameDomainShaderMaterialMatchesItsStruct` 실패는 쿠킹하지 않은 빌드 폴더 탓으로 무관). 같이 고친 것: VAT 쿠커가 쿠킹 폴더 없이 id 를 디스크 경로로 쓰던 것, 퍼즈 대상이 OS 임시 경로를 리소스 입구에 넘기던 것, "없는 파일" 을 디스크 경로로 넘기던 테스트 셋. 확인 대기: 매니페스트 id `cook-scenes-shipping`, `editor-document-open-save`.
