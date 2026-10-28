# 품질 후보

> 상태: 후보 · 선행: 없음 · 영역: quality · 진행: 해당없음

테스트 틀 · 린트 · 부하 검증 · 쿠킹 · 간헐 실패 관찰에서 아직 안 한 일의 목록이다. 착수하면 절을 계획 문서로 옮기고 여기서 지운다. 이미 계획 문서가 있는 일은 [GpuVerification](GpuVerification.md) 에 있다.

## 한눈에 보기

| 절 | 한 줄 설명 | 착수 조건 유형 |
|---|---|---|
| LinuxManualChecks | X11 입력 자동 테스트 | XTest 테스트를 계획 문서로 올릴 때 |
| ImageIntakeChecks | 노멀 맵 열 장 다시 굽기 | 바로 가능 |
| LogGateRemainders | 종료 때 `Task fencing timeout` 원인 | 다시 보일 때 |
| WriteGateRemainders | 쓰기 게이트 밖 추적 파일 쓰기 | 새 쓰기 길이 생길 때 |

## 테스트 · 검증

### LinuxManualChecks
제목: 리눅스 X11 입력의 자동 테스트
왜 필요한가: X11 입력(좌표 · `XkbSetDetectableAutoRepeat`)이 자동으로 돌지 않는다.
착수 조건: XTest 입력 테스트를 계획 문서로 올릴 때.
남은 첫 일: 소비자가 먼저다 — 테스트 틀이 지워졌고 시나리오 런타임이 새 프레임워크로 바뀔 예정이라, XTest(`libxtst`) 주입기를 지금 들이면 쓰는 곳 없는 서드파티 링크(`libxtst-dev` 필수화)만 생긴다. 새 틀이 서면 그 위에서 입력을 넣을 XTest 와 호스트 테스트를 설계한다. 자동 반복은 서버가 키를 누르고 있는 동안 만드는 이벤트라 `XSendEvent` 로 흉내 낼 수 없다.

### LogGateRemainders
제목: 로그 게이트의 남은 것
왜 필요한가: 시나리오 · 모든 테스트 케이스는 선언하지 않은 Warning · Error 로 진다. 표식 없는 줄은 코드로 손봤고 상시 `ctest-nogpu` 전체가 확인한다. 돌려 보기 전이라 Release · Shipping 이나 시간에 기대는 배경 스레드 줄이 남았을 수 있고, 지는 케이스는 원인을 고치거나 그 호출에 범위(`SW_TEST_DEFENSIVE_SCOPE`)를 씌운다(판정기를 좁히지 않는다).
착수 조건: 에디터를 켠 짧은 실행(`-gv_profileFrames=90`)이 다른 GPU 작업과 겹쳐 종료 때 `Task fencing timeout (5000 ms) before module reload` Error 를 다시 낼 때(36 회 중 3 회, 단독 0 회).
남은 첫 일: 5 초 넘게 도는 에디터 배경 작업이 무엇인지 가린다. 로그의 첫 미완료 태스크 이름(`ModuleHost::drainRenderWorkers` 가 찍는다)과 `py -3 -m Scripts stacks <pid>` 의 워커 스택을 본다.

### WriteGateRemainders
제목: 쓰기 게이트의 남은 것
왜 필요한가: 자동화 실행 중 `Resource/` 의 `scratch/` 밖 쓰기는 `FileUtil::isWriteAllowed` 가 거절한다. 입구 밖의 직접 호출은 `CheckFacilityBypass` 규칙이 센다(기준선 0, 예외는 `Scripts/lint/rules/CheckFacilityBypass.toml` 에 이유와 함께). 입구 밖 쓰기는 대부분 `FileUtil` 로 옮겼고 남은 예외(로그 파일 · 크래시 기록 · 자식 프로세스 파이프 · 키 파일 `chmod` · `imgui.ini`)는 이유가 표에 있다.
착수 조건: 게이트가 안 지키는 쓰기 길이 새로 생길 때.
남은 첫 일: 게이트는 `Resource/` 루트 아래만 지켜서 `docs/` · `Config/` 같은 추적 파일에 자동화가 쓰는 길은 못 막는다. 현재 그런 쓰기는 명시 옵션(`--record` · `--apply`)의 스크립트와 `scene-load-bench`(별도 프로세스가 `Resource/` 에 만들고 지운 뒤 `git status` 로 확인)뿐이다. 이 모양이 더 생기면 게이트를 넓힌다.

## 관찰

### ImageIntakeChecks
제목: 노멀 맵 열 장 다시 굽기
왜 필요한가: 셰이더(`toon.hlsl`)는 glTF 규약(초록이 위)인데 `textures_raw/surfaces/*_normal.jpg` 열 장(Poly Haven `nor_gl`)을 `invert_green: true` 로 구워 DDS 초록이 원본과 반대였다. 설정은 고쳤고 `EditorTexturePipelineTest.RepositoryNormalMapsKeepTheEngineGreenConvention` 이 지킨다. 구운 열 장과 `import.stamp` 는 아직 옛 규칙의 것이다.
착수 조건: 없음(이 열 장을 쓰는 머티리얼은 아직 없다).
남은 첫 일: 열 장을 다시 구워 커밋하고 이 절을 지운다.
