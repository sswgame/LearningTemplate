# 다음 세션 인계

이 문서는 **지금 상태**만 둡니다. 새 세션은 이 문서를 먼저 읽고, 나머지는 아래에서 찾습니다. 한 사실은 한 곳에만 씁니다.

| 알고 싶은 것 | 어디 |
|---|---|
| 남은 일 한 건 한 건 | 후보 문서 `Candidates-<영역>.md` — `py -3 -m Scripts plans --area <영역>`(목록), `--file <이름>`(머리 줄과 절 목록) |
| 설계와 구현 단위 | 계획 문서 — `py -3 -m Scripts plans`(목록) |
| 앞으로 할 일의 순서 | [로드맵](Roadmap.md) |
| 결정과 근거 | [결정 기록](../09_Decisions.md) |
| 작업 방식 · 검증 | [작업 방식](../11_Workflow.md), [검증과 측정](../08_Verification.md) |
| 병렬 작업자 공통 규칙 · 재현 명령 | [작업자 규칙](WorkerRules.md) |

한 일은 `git log` 가 정본입니다.

## 1. 지금 상태 (2026-10-19)

- main 은 `332fae747` 까지 push 했고, 그 CI(실행 39143580252)는 열 잡 중 아홉이 통과했다. 이전에 졌던 Shipping · 서버 Shipping · Windows Debug 가 고쳐졌다. 남은 하나였던 Linux TSan 의 `EditorNetPlaySessionTest.DedicatedServerAndTwoClientsConnectPauseAndMergeTraces` 는 고쳤다(원인: 느린 구성에서 Client1 이 첫 틱 안에 `quit` 을 받아 종료 트레이스가 "끝난 프레임 없음" 으로 빠짐 — 테스트가 이제 프로세스마다 틱 하나가 끝나길 기다린다). 다음 main CI 의 TSan 잡으로 확인한다.
- 타입별 컴포넌트(`TypedComponents`)는 T3 · T4 · 7-d-3 까지 끝났다. 삭제 대기 불변식을 형식으로 지키고 버킷 순회가 등록형 목록 이하가 됐다. 등록형 목록은 대부분 등록 순서에 기대서 옮기지 않기로 닫았다(8.3). 7-d-3(`PROPERTY` 타입 핸들 허용, 2026-10-19 사용자 결정)은 직렬화와 인스펙터 콤보 선택기(3.8)까지 구현했다. 남은 것은 선택기의 화면 확인(`gpu-verifier`: 콤보 · `(none)` · 되돌리기 · Paste 비활성)과 앱 측정 넷(8.4). 새 후보 `StableAddressMap`(`Candidates-engine.md`).
- 이후 문서 개편(커밋됨, **아직 push 안 함**): 백로그를 없애고 계획 문서(머리 줄) + `Candidates-<영역>.md`(후보 절 152개) + `Roadmap.md`(순서) + `NEXT.md`(상태) 로 일원화했다. 일의 식별자는 이름이고 옛 번호는 `plans --find`. README 의 진행 표는 `status-board --write README.md` 로 만든 생성물이다. 코드는 바꾸지 않았다(`Scripts` 도구와 린트 게이트만). push 전에 [작업 방식](../11_Workflow.md) 3절대로 `full-build` 증거를 남긴다(Scripts 변경이라 Python 테스트 38개는 통과했다).
- 구성 검증: Shipping · Shipping-Server · CI-Debug-STL 의 빌드 + nogpu 통과, Debug `full-build` 증거는 문서 개편 이전 커밋의 것이다.
- 측정 · GPU 묶음(작업자 `meas`)은 한도(429)로 중단했다. 워크트리 없음, 임시 탐침은 지웠다. 다시 하면 임시 쿠킹 폴더에서 한다(main 체크아웃에 탐침 파일을 만들지 않는다).
- 워크트리: main 하나뿐이다(병합이 끝난 `cifix2` · `rootword` · `plans` 는 지웠다). `feat-pv` · `feat-net` 은 이미 없다.
- 근본 원인 후보(미해결): 디스크 경로와 리소스 id 가 같은 `string_view` 입구를 쓴다 → 후보 `DiskPathVsResourceId`. 워크트리가 `vcpkg_installed` 를 나눠 쓴다 → 후보 `WorktreeVcpkgInstalledDir`.
- 사용자 결정 대기: 없음.

## 2. 첫 일

1. push 뒤 CI 를 확인한다(`py -3 -m Scripts ci-jobs --runs`). TSan 잡이 통과하는지 본다.
2. 그다음은 [로드맵](Roadmap.md) 의 `## 지금 순서` 맨 위부터.

## 3. 함정

- 빌드 로그를 bash `grep` 으로 세지 않는다: `Start-Process *>` 로그는 UTF-16 이라 항상 0 이다. `Out-File -Encoding utf8` 로 쓰고 `Select-String | Measure-Object` 로 센다.
- `worktree-remove` 는 브랜치도 지운다. 병합 뒤 `git merge-base --is-ancestor` 로 확인하고 지운다(커밋이 없는 변경이 있으면 건너뛴다 — 버려도 되면 `--force`).
- 앱 창을 전경으로 올려 캡처하지 않는다. 게임 뷰는 `-gv_screenshot`, 에디터 UI 는 `-gv_screenshotEditor` · `<CaptureEditorUI>`([에디터 UX 기반](EditorUX.md)).
- Edit 도구와 `sed -i` 가 CRLF 파일을 LF 로 바꾼 적이 있다. 수정 뒤 `git diff --stat` 로 파일 전체가 바뀌지 않았는지 본다.
- 정렬 할당 객체를 기반 포인터로 지우면 배포본 힙이 깨진다(Debug 는 조용). 모듈 안 정적 객체의 소멸자에서 스레드를 join 하지 않는다.
- 무효 bindless 인덱스(`0xFFFFFFFF`)는 DX12 페이지 폴트, Vulkan DEVICE_LOST 다. 셰이더를 바꾼 뒤에는 `App.exe --cook-shaders`.
- `castTo<Component>( Component*, 타입 )` 은 항상 성공한다. 종류 거르기는 `getTypeInfo()->isDerivedFrom( 타입 )`.
- 새 .cpp 가 "incomplete type" 으로 서면 `Component.h` · `GameObject.h` · `Scene.h` · `SceneManager.h` · `RHITypes.h` 는 PCH 에 없으니 직접 include 한다.
- 에디터 모듈의 새 REFLECT 헤더는 `Source/Editor/CMakeLists.txt` 의 `REFLECTION_HEADERS` 에 넣는다(안 넣으면 `StaticType` 미정의 링크 오류).
- 같은 PC 에서 빌드 · 테스트를 겹치면 시간 제한에 걸려 다시 돈다. 검증 중에는 다른 빌드를 하지 않는다. 비결정적 실패는 3~5 회 재실행해 재현율을 적는다.
- 검증 스크립트는 빌드 종료 코드와 `ctest` 가 읽는 바이너리의 갱신 시각을 함께 본다(구성 이름 오타로 옛 바이너리가 "통과"한 적이 있다).
- 서버 구성 빌드 폴더의 낡은 컴파일러 런처 캐시(없는 `sccache.exe`)는 `cmake --preset <이름> -UCMAKE_CXX_COMPILER_LAUNCHER -UCMAKE_C_COMPILER_LAUNCHER` 로 지우고 다시 구성한다.
- 병합 비용: 작업자 둘을 따로 통과시킨 뒤 합치면 의미 충돌이 난다. 병합마다 전체 빌드를 돌리고 관련 스위트를 Release 와 Debug 둘 다 돌린다. 빌드 전 컴파일러 프로세스 수를 본다.
- `D:\w\perfbase` · `perfbase2` 워크트리는 성능 기준선이라 남긴다.

## 4. 새 세션 시작 절차

1. 이 문서, [작업 방식](../11_Workflow.md), [작업자 규칙](WorkerRules.md) 순서로 읽는다. 목록은 `py -3 -m Scripts plans`(계획 문서 · 후보)로 보고, 내 문서만 `--file <이름>` 으로 읽는다. C++ 을 쓰기 전에 [AGENTS.md](../../AGENTS.md) 의 해당 절.
2. `git worktree list` 로 남은 워크트리를 보고 병합이 끝난 것을 지운다.
3. 큰 일은 사용자와 순서를 확인한 뒤 시작한다. 시작 전에 graphify 그래프를 갱신한다(`graphify update Source`, 약 4 분).
