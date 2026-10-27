# 품질 후보

> 상태: 후보 · 선행: 없음 · 영역: quality · 옛번호: B-5, B-7, B-15, C-28, C-33, E-4, E-10 · 진행: 해당없음

테스트 틀, CI, 린트 스크립트, 부하 검증, 쿠킹, 간헐 실패 관찰 가운데 아직 착수하지 않은 후보의 목록입니다. 착수하면 절을 계획 문서로 옮기고 여기서 지웁니다.
이미 계획 문서가 있는 일은 [BuildSpeed](BuildSpeed.md) · [GpuVerification](GpuVerification.md) · [ImportRuleOverlap](ImportRuleOverlap.md) · [SourceScanGates](SourceScanGates.md) · [RuntimeQualityAdjust](RuntimeQualityAdjust.md) 에 있습니다.

## 확인 대기

### LinuxManualChecks
제목: 리눅스에서 자동으로 안 도는 것
옛번호: B-7
왜 필요한가: X11 입력(좌표 · `XkbSetDetectableAutoRepeat`)이 자동으로 돌지 않는다.
왜 지금 못 하는가: CI 에 xvfb 는 있지만 입력을 넣을 XTest(`libxtst`)와 그것을 쓰는 호스트 테스트가 없다(별도 설계가 필요). 자동 반복은 서버가 키를 누르고 있는 동안 만드는 이벤트라 `XSendEvent` 로는 흉내 낼 수 없다.
풀리는 조건: 사용자가 WSL 확인을 요청할 때(2026-10-10 결정, WSLg 의 `DISPLAY=:0` 으로 손으로 한 번), 또는 XTest 입력 테스트를 계획 문서로 올릴 때.

### ImageIntakeChecks
제목: 이미지 도입의 확인
옛번호: B-15
남은 것: 노멀 맵 열 장(`textures_raw/surfaces/*_normal.jpg`, 모두 Poly Haven `nor_gl` = 초록이 위)을 다시 굽는 일. 셰이더(`toon.hlsl`)가 glTF 규약(초록이 위)인데 `Base_NormalMap` 이 `invert_green: true` 로 구워 DDS 의 초록이 원본과 반대였다(BC5 를 디코드해 견줌, 상관 -0.997). 2026-10-27 `invert_green` 을 껐고(결정 기록 5-5) 저장소 설정은 `EditorTexturePipelineTest.RepositoryNormalMapsKeepTheEngineGreenConvention` 이 지킨다. 구운 열 장과 `import.stamp` 는 아직 옛 규칙의 것이다 — 매니페스트 id `normal-maps-reimport`. 다시 구워 커밋하면 이 절을 지운다(이 열 장을 쓰는 머티리얼은 아직 없다).

## 정리

### TestFrameworkRemainders
제목: 테스트 틀 개편의 남은 것
옛번호: C-28
왜 필요한가: 틀 · 정책 · 확장 지점은 들어갔다(`Test/README.md`, 결정 기록 5-2 끝).
남은 것: 벤치 스위트를 부하 스위트로 옮긴 뒤의 `CoreTest` · `EngineTest` `seconds` 와 `loadSeconds` 는 뺄셈 어림이라 다음 정책 재측정 때 실제 값으로 맞춘다(통합 담당). `NetSimDestructionMatrixTest` 는 정확성 테스트라 nogpu, 호스트 벤치(`NetUDPBenchTest` · `GlyphBenchTest`)는 호스트에 남겼다. 자료: 2026-10-23 Debug `ctest -L nogpu`(`-j` 전체 겹침) 한 바퀴에서 `EngineTest_NoGPU` 조각 다섯이 103 ~ 128 s, `CoreTest` 32 s 였다 — `EngineTest` `seconds`(130)로 계산한 조각당 약 26 s 의 네 배라 `timeoutScale` 2 를 곱한 한도(208 s) 대비 절반을 넘는다(`_timeoutScaleReason` 의 "절반을 넘으면 다시 잰다"). 겹친 실행의 값이라 단독 실행 값과 둘 다 재서 `seconds` 의 정의(부하 걸린 PC 의 한 실행)를 지키는 쪽으로 맞춘다.

### LogGateRemainders
제목: 로그 게이트의 남은 것
옛번호: C-33
왜 필요한가: 시나리오 · 모든 테스트 케이스는 선언하지 않은 Warning · Error 로 진다(결정 기록 5-2, `Source/Engine/Automation/README.md`).
남은 것:
- 표식 없는 Warning · Error 줄은 2026-10-23 Debug `ctest -L nogpu` 로그에서 1,843 줄(전체 3,218 줄 중)이었고, 2026-10-27 에 두 묶음으로 전부 코드로 손봤다(첫 묶음 1,376 줄, 이번 묶음 나머지 약 460 줄 — 로그 줄 단위로 센 값, 돌려 보기 전). 통합이 매니페스트 id `shared-module-unload-tests`, `log-gate-cleanup-tests` 로 확인한다(앞은 공용 모듈 이미지 소유 412 줄, 뒤는 나머지이고 판정기를 넓혔으니 상시 항목 `ctest-nogpu` 전체가 같은 확인이다). 고친 방식은 넷이다.
- 로그 게이트 · 제품 결함: `SQLiteDriver` 가 `busy_timeout` 을 WAL 전환 뒤에 걸어 작업자 둘이 새 파일을 함께 열면 `database is locked` 로 연결이 지곤 했다(잠금 시한을 설정 문장에서 떼어 `sqlite3_busy_timeout` API 로 열자마자 건다 — 문장 순서에 기대지 않는다. SQLite 를 여는 곳은 `SQLiteDriver::openConnection` 하나뿐이고 다른 `sqlite3_open` 은 없다). 선택 곁 파일을 `FileUtil::readFile` 로 더듬어 "File not found" Error 를 내던 곳은 `FileUtil::exists` 를 먼저 묻는다(`HeightfieldImporter` 의 곁 구멍 마스크 · 레이어 4..7 스플랫, `ModelImporter` 의 glTF 외부 버퍼, `ShaderCookDriver` 의 컴파일 실패 뒤 출력, `EditorNetPlaySession` 의 죽은 자식의 트레이스, 앞 묶음의 `ComplexShapingTest`). 같은 모양을 `Source` · `Test` 전체에서 찾았고 나머지는 이미 `exists` · `isRegularFile` 로 가드되어 있다.
- 로그 게이트 · 테스트 결함: 검(`durability` 와 `maxStack` 을 함께 적음), 시퀀서 플레이어 없는 시네마틱 카메라, 라이브러리가 가리키는 클립을 나중에 등록한 오디오 그래프, 지연 파괴가 끝나기 전에 같은 id 로 되살린 편집기 되돌리기, 디버그 HUD 설정 스키마 없이 연 HUD 문서(UI 내비 테스트), 엔진 기본 에셋 서비스를 걸지 않아 **엔진 로컬라이제이션 프로젝트를 모음에서 빼 놓고 지나가던** `LocalizationGatherTest.RepositoryProjectsAreUpToDate`(이제 엔진 프로젝트도 확인하므로 최신이 아니면 지는 것이 맞는 발견이다), `LogTest` 가 지역 싱크의 콘솔로도 흘리던 Error 줄(레벨이 확인과 무관해 Info 로).
- 로그 게이트 · 의도된 거절: 거절을 부르는 호출에 `SW_TEST_DEFENSIVE_SCOPE`. 본문이 거의 전부 거절 시험인 작은 테스트(네트워크 거절 · 깨진 자료 읽기 · 저장 실패 보고 등 60 개)만 본문 앞에 한 번 씌웠다 — 긍정 단계가 섞인 테스트는 거절을 부르는 호출(또는 그 블록)에만 씌웠다(처음 씌운 129 개 가운데 약 70 개를 호출 단위로 좁혔다). 저장소 전체를 읽는 두 테스트(`SceneTest.RepositoryScenesAndPrefabsKeepComponentNames` · `ResourceDataSchemaTest`)는 자동화 시나리오가 일부러 검증 이슈를 내게 지은 장면 오브젝트 둘의 문구만 거름 글(`makeValidationFixtureNeedles`)로 표시한다. `LogTest` 의 캡처는 방어 표식을 뗀 원문으로 비교한다.
- 로그 게이트 · 판정 입력과 판정 제외: 판정기를 시험하는 `TestFrameworkTest` 는 실제 로그 대신 판정기에 줄을 값으로 넣는다(`CaseLogJudge::recordLine`, 안쪽 판정기는 로그를 막은 구간에서도 값을 판정한다). 시나리오 실행기의 로그 게이트 자체를 시험하는 케이스는 표식을 붙일 수 없으므로(표식은 실행기도 건너뛴다) `SW_TEST_JUDGE_IGNORE_LINES( "[AutoProbe]", 이유 )` 로 케이스 판정에서만 뺀다.
- 로그 게이트 · 판정기는 모든 스위트로 넓혔다(`HostLogJudge` → `CaseLogJudge`, 스위트 종류로 가르지 않음). 이제 CPU 테스트의 표식 없는 Warning · Error 도 그 케이스를 진다 — 확인은 상시 항목 `ctest-nogpu` 전체이고, 지는 케이스가 나오면 원인을 고치거나 그 호출에 범위를 씌운다(판정기를 좁히지 않는다). 돌려 보기 전이라 로그에 안 나온 구성(Release · Shipping, 시간에 기대는 배경 스레드 줄)에서 남은 줄이 있을 수 있다.
- 에디터 켠 짧은 실행(`-gv_profileFrames=90`)을 다른 GPU 작업과 겹쳐 돌리면 종료 때 `Task fencing timeout (5000 ms) before module reload` Error 가 났다(36 회 중 3 회, 단독 재실행 0 회). 종료 경로의 문구는 고쳤고(`ModuleHost::drainRenderWorkers` 가 종료 앞이면 "at shutdown" 문구에 남은 태스크 수와 첫 미완료 태스크 이름을 찍는다). 5 초 넘게 도는 에디터 배경 작업이 무엇인지는 모른다 — 다시 보이면 로그의 첫 미완료 태스크 이름과 `py -3 -m Scripts stacks <pid>` 의 워커 스택으로 가린다.

## 관찰

### ShardExitCodeAfterPass
제목: 통과 뒤 0 이 아닌 종료 코드와 종료 멈춤
옛번호: E-4, E-10
왜 필요한가: 테스트가 모두 통과한 뒤 종료에서 지는 CI 실패가 세 번 났다 — Windows Debug `EngineTest_NoGPU_Shard3`(run 37457443235, 한 번, 로그가 `TaskManager Shutdown cleanly` 에서 끝나고 크래시 스택 없음), 리눅스 Shipping `EngineTest_NoGPU_Shard2`(run 38163565253, 요약까지 찍힌 뒤 마지막 줄 없이 180 초 한도 — `stop()` 안에서 멈춤), 리눅스 Shipping `EngineTest_NoGPU_Shard5`(run 38439985866, `TestHostRuntime::stop` 의 `destroyAll` 에서 끝나지 않음 — 같은 실행의 오디오 디코드 태스크 수명 결함(고침)이 워커를 망가뜨려 join 이 멈춘 것인지 미확정). 이 PC Debug 로 같은 조각 15 회 순차 · 12 회 4 개 동시 — 재현 안 됨.
왜 지금 못 하는가: 재현되지 않고, 종료 경로는 서브시스템 분해로 다시 쓰였으며 단계 앞마다 `[TestHost] stopping: …`, `main` 이 끝에 `[TestHost] shut down - exit code N` 을 이미 찍는다 — 코드를 더 읽어서는 좁혀지지 않는다.
풀리는 조건: 다시 나면 마지막 줄 `[TestHost] shut down - exit code N` 이 없으면 `TestHostRuntime::stop` 안(마지막으로 찍힌 `stopping:` 단계), 있으면 정적 소멸자다. 계측이 든 이후 CI 주석에서 처음 나는 실행으로 가른다.
