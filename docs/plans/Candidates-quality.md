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
왜 필요한가: 시나리오 · 호스트 스위트는 선언하지 않은 Warning · Error 로 진다(결정 기록 5-2, `Source/Engine/Automation/README.md`).
남은 것:
- CPU 단위 테스트(nogpu)는 아직 Error 도 세지 않는다. 표식 없는 Warning · Error 가 2026-10-23 Debug `ctest -L nogpu` 로그에서 1,843 줄(전체 3,218 줄 중)이다. 큰 덩어리: "이미 등록됨 · 두 번 등록됨"(데이터 종류 · 자동화 탐침 · 단계 · 개발 명령 · 서비스 팩토리) 416 줄 — 원인은 `ModuleHostTest.SharedModuleCannotBeRegisteredForReload` 가 `loadSharedModule( "GameFramework" )` 로 올린 원본 이미지를 매니저가 내리지 않아(공용 모듈 목록이 매니저별인데 이미지는 프로세스 전역이다) 뒤따르는 `ArchitectureTest.LiveReload*` 가 같은 모듈을 섀도 복사본으로 다시 올릴 때 정적 등록이 이름 중복으로 거절된 것이다. 2026-10-27 `LiveReloadManager::shutdown` 이 공용 모듈을 마지막에 내리게 고쳤다(`unloadSharedModules`). 줄 묶음은 넷(각 102 ~ 106 줄)이었다. 앞 둘(`ArchitectureTest.LiveReload*`, `ReloadedDependentsBindToTheCurrentImages`)은 이 수정으로, 뒤 둘(`ModuleAPITest.FullGameSceneAndComponentLifecycle`, 실행 두 번 — 앞 케이스 `ExportGameAPI` 가 일부러 내리지 않은 원본 GameFramework 와 섀도 복사본이 겹친 것)은 그 케이스와 `GameModuleRepeatedReloadCycle` · `UnloadKeepsTheImagesTheModulePulledIn` 을 자식 프로세스 케이스로 옮기고 공용 모듈을 `loadSharedModule` 로 한 벌만 올리게 해서 풀었다. 통합이 매니페스트 id `shared-module-unload-tests` 로 모든 표식 없는 "이미 등록됨" 줄이 0 인지 센다. 외형 카탈로그의 일부러 깨진 픽스처 `[Appearance]` 394 줄, `GameObject` 의 `Pending_N` · 부모 참조 경고 약 85 줄, `ShooterBlockerComponent` 알 수 없는 형식 56 줄, `ShaderCompiler` 포함 오류 50 줄. `SW_TEST_DEFENSIVE_SCOPE` 로 감싸거나 원인을 고친 뒤 `HostLogJudge` 를 모든 스위트로 넓힌다. 줄 목록은 `ctest -L nogpu` 뒤 LastTest.log 의 `[Warning]` · `[Error]` 를 (호출자, 문구) 로 묶어 본다. 고치려면 테스트를 돌려 줄이 사라졌는지 봐야 해서 통합이 돌릴 수 있는 때에 한다.
- 에디터 켠 짧은 실행(`-gv_profileFrames=90`)을 다른 GPU 작업과 겹쳐 돌리면 종료 때 `Task fencing timeout (5000 ms) before module reload` Error 가 났다(36 회 중 3 회, 단독 재실행 0 회). 종료 경로의 문구는 고쳤고(`ModuleHost::drainRenderWorkers` 가 종료 앞이면 "at shutdown" 문구에 남은 태스크 수와 첫 미완료 태스크 이름을 찍는다). 5 초 넘게 도는 에디터 배경 작업이 무엇인지는 모른다 — 다시 보이면 로그의 첫 미완료 태스크 이름과 `py -3 -m Scripts stacks <pid>` 의 워커 스택으로 가린다.

## 관찰

### ShardExitCodeAfterPass
제목: 통과 뒤 0 이 아닌 종료 코드와 종료 멈춤
옛번호: E-4, E-10
왜 필요한가: 테스트가 모두 통과한 뒤 종료에서 지는 CI 실패가 세 번 났다 — Windows Debug `EngineTest_NoGPU_Shard3`(run 37457443235, 한 번, 로그가 `TaskManager Shutdown cleanly` 에서 끝나고 크래시 스택 없음), 리눅스 Shipping `EngineTest_NoGPU_Shard2`(run 38163565253, 요약까지 찍힌 뒤 마지막 줄 없이 180 초 한도 — `stop()` 안에서 멈춤), 리눅스 Shipping `EngineTest_NoGPU_Shard5`(run 38439985866, `TestHostRuntime::stop` 의 `destroyAll` 에서 끝나지 않음 — 같은 실행의 오디오 디코드 태스크 수명 결함(고침)이 워커를 망가뜨려 join 이 멈춘 것인지 미확정). 이 PC Debug 로 같은 조각 15 회 순차 · 12 회 4 개 동시 — 재현 안 됨.
왜 지금 못 하는가: 재현되지 않고, 종료 경로는 서브시스템 분해로 다시 쓰였으며 단계 앞마다 `[TestHost] stopping: …`, `main` 이 끝에 `[TestHost] shut down - exit code N` 을 이미 찍는다 — 코드를 더 읽어서는 좁혀지지 않는다.
풀리는 조건: 다시 나면 마지막 줄 `[TestHost] shut down - exit code N` 이 없으면 `TestHostRuntime::stop` 안(마지막으로 찍힌 `stopping:` 단계), 있으면 정적 소멸자다. 계측이 든 이후 CI 주석에서 처음 나는 실행으로 가른다.
