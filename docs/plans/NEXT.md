# 다음 세션 인계

새 세션은 이 문서를 먼저 읽습니다. 한 일은 `git log`, 결정과 근거는 [결정 기록](../09_Decisions.md), 검증 방법은 [검증과 측정](../08_Verification.md), 남은 일 전체는 [백로그](../backlog/README.md)(영역별 파일 · 상태 태그, `py -3 -m Scripts backlog` 로 목록), 작업 방식은 [작업 방식](../11_Workflow.md), 병렬 작업자 공통 규칙은 [작업자 규칙](WorkerRules.md)입니다. 이 문서에는 지금 상태 · 다음 할 일 · 함정 · 시작 절차만 둡니다.

## 1. 지금 상태 (2026-10-16)

main 에 들어갔고 push 되지 않은 것: finalverify 병합(배포본 테스트 둘 개발 빌드 전용 표시 등 — 2026-10-15 CI 의 Shipping 4 잡 실패 원인), 백로그를 `docs/backlog/` 로 나눔, CI 교착 검출 컴파일 확인을 매일 잡으로 이동. 원격 브랜치는 `main` 하나.
진행 중인 작업자(결과 미확인): `testspeed`(테스트 시간 줄이기 · `race` 라벨 · `-j` 병렬화, `D:\w	estspeed`), `shaderrefactor`(셰이더 공통화 · 매크로 제거 · 파일 이름 lower_snake_case). 끝났는지 `git worktree list` 와 `git log` 로 본다. 테스트 시간의 근거: Debug `AppTest_HostOnly` 한 바퀴 57 분, 같은 검증이 Shipping 에서 65 초(`DataRaceDetector` 가 `SW_DEBUG` 에서만 켜져 레이스 검사는 Debug 가 맡는다).

## 2. 다음 할 일 (백로그 번호)

1. **`C-35`**(`engine.md`) 서브틱 활용 조사 — `PostUpdate` 41 개 분류, 이득이 없으면 서브틱 · `TickPhase` 삭제 후보.
2. **CI 첫 결과 확인** — 리눅스 · PCH 끈 Windows CI 는 빌드 속도 변경(PCH 에서 헤더 여섯 제거, 임포트 라이브러리 갱신)으로 아직 돌지 않았다. 교착 검출 컴파일 확인을 옮긴 `header-self-contained.yml` 도 첫 실행을 본다(Windows Debug 잡이 32 → 약 18 분이 되는지). 야간 `load.yml` 은 한 번도 돌지 않았다(`C-29` ⑤).
3. **`D5-4` · `D5-3` · `D3-11`** — 시나리오 자동화의 남은 것(탑승 · 마을 사람 길 찾기 · 에셋 임포트 흐름).
4. **빌드 속도 12 단계** — 리플렉션 헤더 팬아웃([빌드 속도 계획](BuildSpeed.md)).

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
- `D:\w\perfbase` · `perfbase2` 워크트리는 성능 기준선이라 남긴다.

## 4. 새 세션 시작 절차

1. 이 문서, [작업 방식](../11_Workflow.md), [작업자 규칙](WorkerRules.md) 순서로 읽는다. 백로그는 `py -3 -m Scripts backlog` 의 표와 `--id <번호>` 로 내 항목만. C++ 을 쓰기 전에 [AGENTS.md](../../AGENTS.md) 의 해당 절.
2. `git worktree list` 로 남은 워크트리를 보고 병합이 끝난 것을 지운다.
3. 큰 일은 사용자와 순서를 확인한 뒤 시작한다. 시작 전에 graphify 그래프를 갱신한다(`graphify update Source`, 약 4 분).
