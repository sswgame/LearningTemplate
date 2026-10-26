# 프로젝트 후보

> 상태: 후보 · 선행: 없음 · 영역: project · 옛번호: C-74, C-75, C-76, H-1 · 진행: 해당없음

저장소 규칙, 서드파티, 리소스처럼 한 영역에 속하지 않는 착수 전 후보의 목록입니다. 착수하면 절을 계획 문서로 옮기고 여기서 지웁니다. 긴 함수 정리는 [LongFunctions](LongFunctions.md) 로 착수했습니다.

## 정리

### InvalidMeasurementCleanup
제목: 무효가 된 측정의 재측정
옛번호: C-74
왜 필요한가: 빌드 조건이 바뀌어 근거가 무효가 된 측정을 다시 잰다(폐기 · 태그 · 낡은 값 표시는 끝냈다).
남은 것:
- `HalfResolutionBloomMeasurement`(옛 D2-9) · `TerrainWaterVegetationRemainders`(옛 D2-5 ⑹ · ⑺, Debug 값을 Release 로) · `RuntimeQualityAdjust` ②(옛 C-29, 13 ~ 20 ms 프레임의 Tracy) · `BuildSpeed` 5 단계(옛 C-2, 공유 설치가 조용할 때).
- `UdpSocketLayerScaling`(옛 B-10): Windows 서버 스레드 % 는 다른 빌드의 부하가 섞여 무효. 다른 빌드가 없을 때 다시 잰다.
- 실행 명령: 이 항목들은 `Test/QA/VerifyBatch.json` 에 이미 있다 — `half-resolution-bloom` · `runtime-quality-adjust-tracy` · `build-speed-unity-batch`(`need_command` 에 한 명령으로 안 되는 까닭), `terrain-bench-release` · `net-udp-bench`(명령 있음, `pending`). 새 항목을 만들지 않고 그 id 로 돌린다.
- 부하 기준선(`Test/Load/Baselines.json` 의 connect · replication · soak, 커밋 `14c775c55`)은 온라인 B([고정 배치형 코덱 계약](../../Source/Engine/Serialization/Fixed/README.md)) 이전 값이다. B 가 들어간 뒤 `py -3 -m Scripts load suite baseline --runs 3 --record` 로 다시 뜬다(옛 C-29 ⑤). 매니페스트 `load-baseline-suite` 는 `--record` 가 없어 판정만 하므로, 갱신할 때는 `args` 에 `"--record"` 를 더한 항목(초안: `{"id": "load-baseline-record", "kind": "measure", "config": "Release", "platform": "windows", "gpu": "exclusive", "pending": true, "timeout_minutes": 240, "tool": "scripts", "args": ["load", "suite", "baseline", "--runs", "3", "--record"], "limit": "기준선 갱신 — 새 값이 예산 안인지 본다", "judge": "exit", "source": "docs/plans/Candidates-project.md InvalidMeasurementCleanup", "result_doc": "docs/plans/Candidates-project.md"}`)을 verifycmd 정리가 끝난 뒤 더한다.
지금 못 하는 까닭: 전부 Release 빌드와 GPU · 서버를 혼자 쓰는 측정이다. 풀리는 조건: 통합이 끝나 빌드가 조용할 때 항목별 후보 절의 순서대로.
함정: 항목이 이름 체계로 옮겨지는 중이라 옛 번호는 `py -3 -m Scripts plans --find` 로 새 이름을 먼저 찾는다.

### DecisionMeasurementColumn
제목: 결정 기록의 측정 조건 칸 채우기
옛번호: C-75
왜 필요한가: 측정 조건(기계 · 규모 · 날짜 · 근거 커밋)이 떨어져 나가면 "그 조건에서 이득 없음" 이 무조건 금지가 된다. 형식은 정했고(결정 기록 3절 머리, `[측정: 기계 · 규모 · 날짜 · 근거 커밋]`, 모르면 `미상`) 3절의 측정 기반 기각 줄은 채웠다. 기계가 `미상` 인 칸은 재측정 때 `PC SW` 처럼 적어 고친다.
남은 것:
- 재측정: 스핀 늘리기(2→50 us) · hot pool 은 09-20 측정(조건 변수 시절, 0.7 ms 프레임, ±40 % 흔들림, p99 없음)이 근거라 무거운 프레임(8 ms 이상)에서 다시 잰다. `flatMapInProcesses` 16 → 8 워커는 `ctest -j` 겹침에서 다시 잰다.
- 재검토: "레인 없이 GT 병렬화" 기각은 High · Normal · Low 큐(`TaskManager`)가 생겨 전제가 사라졌다. 레인 있는 설계로 다시 판단한다.
지금 못 하는 까닭: 재측정은 무거운 프레임 · `ctest -j` 겹침이 필요하고, 재검토는 사용자가 판단할 일이다. 풀리는 조건: 빌드가 조용할 때, 재검토는 사용자가 GT 병렬화를 다시 올릴 때.

### CarriedOverChecks
제목: 그 밖에 확인이 필요한 이월 항목
옛번호: C-76
왜 필요한가: 다른 항목에 속하지 않고 확인만 남은 이월 조각입니다.
남은 것:
- 리플렉션 크기: 단계 1 의 Release `App.exe` · 모듈 DLL 전후 값만 아직 없다(Debug 값은 `Source/Engine/Serialization/Fixed/README.md` 3.1 절). 두 커밋을 Release 로 지어 재야 한다. 커밋마다 새 빌드 트리가 필요해 매니페스트 `measure` 한 항목(`tool: scripts`)으로 달 수 없다(`build-speed-reflection-fanout` 은 Debug 팬아웃이라 다른 일).
- 셰이더 bindless 입문: `shaderdoc` 결과를 받은 뒤 분기를 숨기는 진입 함수 구조 변경을 따로 결정한다.
- 리눅스 CI 가 초록이 된 뒤 `E-1` `E-4` `E-10` `C-29 ⑤` 관찰 항목과 `B-5` `B-7` 을 본다.
풀리는 조건: 각각 빌드가 조용할 때 · shaderdoc 결과 · 사용자 확인 · 리눅스 CI 초록.

## 보류

### CcZeroAssetsMechArena
제목: 게임별 CC0 리소스 배치와 SD 메카 테스트 게임
옛번호: H-1
왜 필요한가: 테스트 게임마다 CC0 리소스를 배치하고, 건담풍 사람형 메카가 CC0 로 없어 SD 메카 생성기와 MechArena 테스트 게임을 만듭니다.
착수 조건: 사용자 결정 "안정화된 뒤 개발". 2026-10-24 확인: NEXT · Roadmap 에 안정화 선언이 없어 보류를 유지한다.
남은 것:
- 라이선스: **CC0 급만**. 출처 표기 의무 · 재배포 금지가 붙은 것은 넣지 않고 내려받기 스크립트 우회도 안 한다.
- 확인한 원본: VRoid 알파판 샘플 D · E · F · G · 남녀 기본(OpenGameArt `vroid-studio-cc0-models`, VRM 0.x), KayKit(GitHub `KayKit-Game-Assets` 의 `*-1.0` 저장소, CC0 — Space Base · Restaurant · City Builder · Dungeon Remastered · Furniture · Halloween), 음악 OpenGameArt CC0 `jaoan`(고토) · `menu-music-2`(도장풍 루프).
- 배치함(2026-10-17 `res3`): StarSkirmish ← Space Base, Shooter3D ← Dungeon(NileCity ← City Builder 는 쓰지 못했다). 남은 배치안: ThemePark ← Restaurant · Halloween, HarvestValley ← jaoan(음악만), AbilityArena ← VRoid + menu-music-2. 프리팹 · 데이터 연결까지 한다(안 쓰면 에셋 검증 `orphans`).
- SD 메카: CC0 로 쓸 만한 건담풍 사람형이 없다(Quaternius 메카는 동물이 탄 보행기, OGA Shock Bot 은 리얼풍). 파이썬으로 SD 메카 부품 메시를 만들어 부품마다 뼈 하나에 붙이고 KayKit 사람형 뼈대 이름에 맞춰 KayKit 애니메이션(CC0)을 쓰는 생성기 + MechArena 테스트 게임(`GF_MechArena` 키트는 있고 테스트만 씀). 건담 고유 요소(V 안테나 · 얼굴 마스크 · 흰 · 파랑 · 빨강 · 노랑 배색)는 피한다.
- 탈것: 말(`MountMovementComponent` · `WesternHorseMountComponent`) · 차(`ArcadeVehicleComponent`)는 기본 도형으로만 테스트했다. CC0 말 메시 · 걸음새 애니메이션(Gait 0..4 · Turn)과 차 모델을 찾으면 서부극 · 카트 테스트 게임에 붙인다.
- 카트 키트: `Kits/Genre/Casual/KartRacing` 의 플레이어 경로를 `ArcadeVehicleComponent::toVehicleInput` 으로 바꾸는 것은 카트 테스트 게임이 생길 때(지금 `KartRace::resolveInput` 이 이미 "같은 모터에 다른 조종자").
함정: 물리 차(`WheeledVehicleComponent`)는 바퀴 메시를 바퀴 자세로 옮기지 않는다. 차 모델이 생기면 바퀴 자식 이름 표와 `getVehicleWheelPose` 창구를 더한다.
