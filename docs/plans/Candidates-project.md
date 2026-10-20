# 프로젝트 후보

> 상태: 후보 · 선행: 없음 · 영역: project · 옛번호: C-11, C-16, C-17, C-57, C-74, C-75, C-76, G-11, H-1 · 진행: 해당없음

저장소 규칙, 서드파티, 리소스처럼 한 영역에 속하지 않는 착수 전 후보의 목록입니다. 착수하면 절을 계획 문서로 옮기고 여기서 지웁니다. 긴 함수 정리는 [LongFunctions](LongFunctions.md) 로 착수했습니다.

## 정리

### RuleExceptionAuditApply
제목: 규칙 예외 감사에서 정한 셋의 적용
옛번호: C-11
왜 필요한가: 2026-10-07 규칙 예외 감사에서 정한 셋이 아직 적용되지 않았습니다. 결정과 근거는 [결정 기록](../09_Decisions.md) 5-2 입니다.
남은 것:
- 경고 목록: 두 플랫폼 공통의 명시적 경고 목록.
- 무시 주석: `(void)` 이유 주석은 `[[nodiscard]]` 실패 가능 함수에만 단다.
- 출처 불명 리소스: 출처를 모르는 리소스를 교체한다.

### CommentRewriteRemaining
제목: 6 차 주석 작업에서 처리하지 못한 구역
옛번호: C-16
왜 필요한가: 새 문체로 다시 쓰는 주석 작업이 아래 구역에서 끝나지 않았습니다. 같은 방식으로 `py -3 -m Scripts worktree-make c6<이름>` 후 이어서 합니다.
남은 것:
- Engine: Graphics · Renderer 의 13KB 넘는 큰 파일 약 100 개(작은 파일은 끝났다), Object · Scene · Resource · Serialization · Spatial · Common 대부분, UI 의 `Console`, Compression · Observability · Profiling · Telemetry 전부. `SceneNavigation.h` · `GameObjectManager.h` 는 충돌 때문에 다시 쓰지 못했다.
- Editor: 주석 25 줄 이상인 약 45 개(`Common/GUI`, `Commands`, `Panels/Animation` · `TileMap` · `Viewport` 등)와 그보다 짧은 파일. `EditorIconGlyphs.h` 는 생성 파일이라 제외한다.
- GameFramework: Base 헤더는 끝났다(2026-10-21). 다른 작업이 고치던 헤더 열한 개(`GameInstanceBase.h`, `IGame.h`, `GameSettings.h`, `GameDataXML.h`, Online 의 `ServerDirectoryControl.h`, `BlockingHTTPClient.h`, `GroupChannelMembership.h`, `ServiceReport.h`, `NetSecurity.h`, `OnlineServerKit.h`, `OnlineServiceHost.h`)와 Base 의 .cpp · .inl 이 남았다. Kits/Feature 의 옛 어휘(꼬리표, 그새, 거두다, 짓다)가 남은 약 35 개(Network · Online · Voxel 등), Kits/Feature/Online 의 Account 밖 폴더.
- Core: Math, File, Process, Delegate, Memory, Diagnostics, CommandLine, Container, Task, Network 나머지, LogSink, String. RuntimeAPI 는 손대지 않았다. Games 는 Empty 벤치 씬 일부와 VoxelCraft 전체.
- 용어 '시험' → '테스트': `Scripts/**` · `Test/**` · `docs/Config/**` 약 300 곳(2026-10-13 셈)과 셰이더 주석 2 곳(`waterwaveprobe.hlsl` · `lighting2d.hlsli`). 셰이더는 바꾸면 `App --cook-shaders` 로 다시 쿠킹한다.
함정: 파일이 겹쳐 미뤄진 정리입니다. 같은 폴더를 다른 작업이 건드리는 중이면 미룹니다.

### NamingAuditNext
제목: 다음 이름 점검
옛번호: C-17
왜 필요한가: 2026-10-13 이름 점검에서 남긴 것입니다. 기준은 [docs/agents/NamingCpp.md](../agents/NamingCpp.md) "Class suffixes" 절과 [결정 기록](../09_Decisions.md) 5-1 입니다.
남은 것: 아직 보지 않은 네 가지.
- Context 접미어: `Context` 접미어 타입이 무엇을 담는지.
- Util 정적 클래스: `*Util` 정적 클래스 중 한 타입의 메서드가 된 것이 나은 것.
- Unit 계열: `Unit` 계열(812 회).
- 단독 value: 남은 지역 · 멤버 변수 단독 `value` 약 50 곳.
함정: `Settings` · `Config` · `Def` 기준은 docs/agents/NamingCpp.md "Type suffixes" 입니다.

### SubNumberReferenceCleanup
제목: 정의 없는 하위 번호 참조 정리
옛번호: C-57
왜 필요한가: 결정 기록 · 계획 문서 · 커밋이 `D7-5.10` · `D7-5.3` · `D7-5.6` · `D5-1.*` · `D2-5.4~5.8` · `D3-8.6` 처럼 하위 번호로 가리키는데 옛 항목 본문에는 그 번호가 없었습니다(원 숫자 ①② 만 있었다). 항목을 계획 문서로 옮겼으므로 이름 체계로 다시 확인이 필요합니다.
남은 것: 하위 번호를 가리키는 쪽을 계획 문서의 단위 이름이나 본문의 말로 바꾼다. `py -3 -m Scripts plans --find <옛번호>` 로 위치를 찾는다.

### InvalidMeasurementCleanup
제목: 무효가 된 측정의 재측정
옛번호: C-74
왜 필요한가: 빌드 조건이 바뀌어 근거가 무효가 된 측정을 다시 잰다. 2026-10-20 조사로 폐기 · 태그 · 낡은 값 표시는 끝냈고, 아래는 실제로 재야 하는 것이다(각각의 후보 절에 자세한 조건이 있다).
남은 것:
- `HalfResolutionBloomMeasurement`(옛 D2-9) · `TerrainWaterVegetationRemainders`(옛 D2-5 ⑹ · ⑺, Debug 값을 Release 로) · `HeaderIncludeSlimming` ②(옛 C-13, 2026-10-05 측정 기준이라 지금 TU 수로 다시 판정) · `RuntimeQualityAdjust` ②(옛 C-29, 13 ~ 20 ms 프레임의 Tracy) · `BuildSpeed` 5 단계(옛 C-2, 공유 설치가 조용할 때).
- `UdpSocketLayerScaling`(옛 B-10): Windows 서버 스레드 % 는 다른 빌드의 CPU 부하가 섞여 무효로 표시했다. 다른 빌드가 없을 때 다시 잰다.
- 부하 기준선(`Test/Load/Baselines.json` 의 connect · replication · soak 은 커밋 `14c775c55`, 2026-10-15)은 온라인 B([리플렉션 형식 계획](ReflectionFormats.md)) 이전 값이다. B 가 들어간 뒤 `py -3 -m Scripts load suite baseline --runs 3 --record` 로 다시 뜬다(옛 C-29 ⑤).
- 판단이 모호해 남김: 옛 `D6-6` · `D7-5` 의 "온라인 부하 봇 Debug 값" 은 현재 `UiDrawPerformance` · `EpollAndRealStoreVerification` 본문에 없다. 부하 봇 Debug 값은 [검증과 측정](../08_Verification.md) 2절 "온라인 부하 봇 참고값" 에 Debug 로 표시돼 있고 Release 측정은 `OperationsAndDeployment` 후보가 맡는다. 더 지울 것이 있는지는 그 절을 고치는 사람이 본다.
함정: 항목이 이름 체계로 옮겨지는 중이라 옛 번호는 `py -3 -m Scripts plans --find` 로 새 이름을 먼저 찾는다.

### DecisionMeasurementColumn
제목: 결정 기록의 측정 조건 칸 채우기
옛번호: C-75
왜 필요한가: 측정 조건(기계 · 규모 · 날짜 · 근거 커밋)이 떨어져 나가면 "그 조건에서 이득 없음" 이 무조건 금지가 된다. 형식은 정했고(결정 기록 3절 머리, `[측정: 기계 · 규모 · 날짜 · 근거 커밋]`, 모르면 `미상`), 낡은 문서 셋(`EditorCharacterEditor` · `EnginePartition` 3 단계 · `DataDriven` 4절)은 2026-10-20 에 고쳤다.
남은 것:
- 3절의 나머지 측정 기반 기각에 같은 줄을 적는다(지금은 태스크 · TaskManager 후보 셋 · 콜드 프리셋 · 층 DLL 넷뿐). 날짜 · 기계가 본문에 없으면 `git log -S` 로 찾고, 못 찾으면 `미상`.
- 재측정: 스핀 늘리기(2→50 us) · hot pool 은 09-20 측정(조건 변수 시절, 0.7 ms 프레임, ±40 % 흔들림, p99 없음)이 근거라 무거운 프레임(8 ms 이상)에서 다시 잰다. 기계는 `미상` 이다.
- 재검토: "레인 없이 GT 병렬화" 기각은 High · Normal · Low 큐(`TaskManager`)가 생겨 전제가 사라졌다. 레인 있는 설계로 다시 판단한다.

### CarriedOverChecks
제목: 그 밖에 확인이 필요한 이월 항목
옛번호: C-76
왜 필요한가: 다른 항목에 속하지 않고 확인만 남은 이월 조각입니다.
남은 것:
- 리플렉션 크기: 리플렉션 단계 1 의 실행 파일 크기 증가(전후 Release `App.exe` · 모듈 DLL)를 `ReflectionFormats.md` 에 적는다(7-a 만의 값은 `Engine.dll` +0.08 %).
- 셰이더 bindless 입문: `shaderdoc` 결과를 받은 뒤 분기를 숨기는 진입 함수 구조 변경을 따로 결정한다.
- 겹침 도구: 항목마다 "건드릴 파일 · 겹침 주의"를 적고 겹치지 않는 묶음을 뽑는 도구. 이전 세션에서 사용자가 시킨 일로 기억되나 지시를 찾지 못했다, 사용자에게 확인.
- 리눅스 CI 뒤: 리눅스 CI 가 초록이 된 뒤 `E-1` `E-4` `E-10` `C-29 ⑤` 관찰 항목과 `B-5` `B-7` 을 본다.

### KoreanLogStrings
제목: 로그 문자열의 한글 줄을 영어로
옛번호: 없음
왜 필요한가: 한글이 든 `SW_LOG` 줄이 약 200 개(전체 3309 줄의 6 %)라 AGENTS.md 의 "로그 · assert 문자열은 영어" 규칙에 어긋납니다.
남은 것: 한글 로그 줄을 찾아 영어로 바꾼다. 로그 문자열을 매칭하는 테스트가 있으면 함께 고친다.
함정: 주석 문체 작업(CommentRewriteRemaining)과 같은 파일을 건드리므로 같은 작업자에게 묶는다.

## 조건부

### ThirdPartyGaps
제목: 서드파티 빈자리
옛번호: G-11
왜 필요한가: 2026-10-05 후보 중 사용자가 고르지 않은 것입니다. 리눅스 오디오 출력 없음(`XAudio2AudioBackend` 만, 리눅스는 `NullAudioBackend`) → miniaudio(퍼블릭 도메인/MIT-0). `gv_renderScale` 을 읽는 업스케일 없음 → AMD FidelityFX FSR(MIT). 아랍어 셰이핑 없음(양방향은 `Engine/Text/TextBidi` 단순판 — 포개진 방향 제어 문자 · 숫자 앞뒤 기호 규칙 미지원) → HarfBuzz(MIT) + SheenBidi(Apache 2.0).
착수 조건: 사용자가 해당 빈자리를 고를 때.
남은 것: 꽂을 자리는 `Engine/Text/ITextShaper` — 런 하나, 같은 글꼴 바이트 `IFontRasterizer::findFaceBytes` 로 `hb_face` 를 만든다. SheenBidi 는 `TextBidi` 를 대신한다.
함정: 들이면 Jolt · Recast · Tracy 처럼 엔진 인터페이스 뒤 + 격리 게이트로 하고, vcpkg 변경은 main 에서 먼저 한다.

## 보류

### CcZeroAssetsMechArena
제목: 게임별 CC0 리소스 배치와 SD 메카 테스트 게임
옛번호: H-1
왜 필요한가: 테스트 게임마다 CC0 리소스를 배치하고, 건담풍 사람형 메카가 CC0 로 없어 SD 메카 생성기와 MechArena 테스트 게임을 만듭니다.
착수 조건: 사용자 결정 "안정화된 뒤 개발".
남은 것:
- 라이선스: **CC0 급만**. 출처 표기 의무 · 재배포 금지가 붙은 것은 넣지 않고, 내려받기 스크립트 우회도 안 한다.
- 확인한 원본: VRoid 알파판 샘플 D · E · F · G · 남녀 기본(OpenGameArt `vroid-studio-cc0-models`, VRM 0.x — 툰 셰이더 · VRM 임포트는 있음), KayKit(GitHub 사용자 `KayKit-Game-Assets` 의 `*-1.0` 저장소, LICENSE.txt CC0 — Space Base · Restaurant · City Builder · Dungeon Remastered · Furniture · Halloween), 음악 OpenGameArt CC0 `jaoan`(고토) · `menu-music-2`(도장풍 루프).
- 배치함(2026-10-17 `res3`, 씬 · 코드 연결은 `D4-17`): StarSkirmish ← Space Base, Shooter3D ← Dungeon. NileCity ← City Builder 는 실제로 쓰지 못했다.
- 남은 배치안: ThemePark ← Restaurant · Halloween, HarvestValley ← jaoan(음악만; 일본 시골 모델은 Quaternius 를 쓰지 않아 비어 있다), AbilityArena ← VRoid + menu-music-2. 프리팹 · 데이터 연결까지 한다(안 쓰면 에셋 검증 `orphans`).
- SD 메카: 건담풍 사람형 메카는 CC0 로 쓸 만한 것이 없다(Quaternius 메카는 동물이 탄 보행기, OGA Shock Bot 은 리얼풍). 파이썬으로 SD 메카 부품 메시를 만들어 부품마다 뼈 하나에 붙이고, KayKit 사람형 뼈대 이름에 맞춰 KayKit 애니메이션(CC0)을 쓰는 생성기 + MechArena 테스트 게임(`GF_MechArena` 키트는 있고 테스트만 씀). 건담 고유 요소(V 안테나 · 얼굴 마스크 · 흰 · 파랑 · 빨강 · 노랑 배색)는 피한다.
- 탈것: 말(`MountMovementComponent` · 서부극 `WesternHorseMountComponent`) · 차(`ArcadeVehicleComponent`)는 기본 도형(캡슐 말 · 상자 차)으로만 테스트했다. CC0 말 메시 · 걸음새 애니메이션(Gait 0..4 · Turn)과 차 모델을 찾으면 서부극 · 카트 테스트 게임에 붙인다.
- 카트 키트: `Kits/Genre/Casual/KartRacing` 의 플레이어 경로를 `ArcadeVehicleComponent::toVehicleInput`(의도 → 모터 입력)으로 바꾸는 것은 카트 테스트 게임이 생길 때 한다(지금 키트는 InputMap 을 읽지 않고 `KartRace::resolveInput` 이 이미 "같은 모터에 다른 조종자" 다).
함정: 물리 차(`WheeledVehicleComponent`)는 바퀴 메시를 바퀴 자세(조향 · 회전 · 서스펜션)로 옮기지 않는다. 차 모델이 생기면 바퀴 자식 이름 표와 `getVehicleWheelPose` 창구를 더한다.
