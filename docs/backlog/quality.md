# 백로그: 테스트, CI, 검증, 빌드 속도

테스트 틀, CI, 린트 스크립트, 부하 검증, 빌드 속도, 간헐 실패 관찰의 남은 일입니다. 상태 태그의 뜻, 일을 고르는 순서, 새 항목을 쓰는 법은 [백로그 안내](README.md)에 있습니다.

- `B-5` [검증대기] **`LoaderFuzzer` `Compression` 타임아웃은 고쳤고 퍼저 재확인과 씨앗이 남았다**(실행 38806424034). 원인은 헤더의 원본 크기(620 MB)를 압축 크기(0)와 견주지 않고 할당한 것이었다 —
  코덱의 팽창률 상한(`ICompressionCodec::decompressBound`)과 `vector` 소멸 루프 생략으로 고쳤다(결정 기록 5-3, 2026-10-17). 퍼저는 리눅스 전용이라 로컬에서는 `CompressionTest.FuzzTimeoutInputIsRejectedBeforeAllocating` 이
  그 입력을 그대로 재현해 할당 전에 끊기는지(용량 0)만 본다. 남은 일: ① 다음 야간 퍼징에서 `Compression` 이 통과하는지 확인 ② 그 입력을 `Test/EngineTest/LoaderFuzzTargets.cpp` `seedCompression` 씨앗에 더하기
  (소유 파일 밖이라 두었다). 같은 모양(헤더 원본 크기로 바로 `resize`)이던 `NetCompressionUtil::decompressEnvelope` · `LocalSlotEnvelope` `decompressBody` · `ResourcePakReader::decompressData` 도
  할당 전에 `decompressBound` 와 견준다(팩은 할당을 `decompressData` 안으로 옮겨 부르는 쪽이 먼저 resize 하지 못한다).

- `B-7` [검증대기] **리눅스에서 아직 자동으로 안 도는 것**: X11 입력(좌표 · `XkbSetDetectableAutoRepeat` — WSLg 의 `DISPLAY=:0` 이 있으니 손으로 한 번), yad 파일 대화상자의 두 번째
  `--file-filter`(man 으로만 확인). 사용자가 WSL 확인을 요청할 때만 한다(2026-10-10 결정).

- `B-12` [검증대기] **통합 3(`8fce26aaf..6f3373831`, 81 커밋)의 검증이 반쯤 남았다.**
  - **할 일**: Debug `-L hostgpu`(`ctest -j 1`, 약 16 분), Shipping `-L nogpu` · `-L hostgpu`, 게임 넷(Shooter3D · StarSkirmish · HarvestValley · VoxelCraft)을 `-game=` 으로 실행,
    `ctest -N` 개수를 병합 전과 견줘 옮긴 테스트(`TestDismemberment` · `TestWitcherRPG` · `TestTacticsSRPG` · `TestProgression` · `TestSpatial` · `ProfilerTimeline`)가 사라지거나 두 번 잡히지 않았는지.
    정적: `dumpbin /exports` 로 `GameFramework.dll` 이 `SW_GF_API` 심볼과 `bindGameService` · `unbindGameService`(C 링크)를 내보내고 `SWGame.dll` 이 가져오는지. 같은 이름의 반사 헤더 둘이면 구성이 `FATAL`, 구성 뒤 기존 헤더에 반사 매크로를 넣으면 빌드가 서는지
    (`cmake/Engine/CheckReflectionCandidates.cmake`, `--target <타깃>` 만 지으면 검사가 안 돈다). `StringBuilder::append( string )` 과 `VulkanRHIRequiredVersion` include 빌드.
    CI(push 뒤 첫 전체 CI) 리눅스: GLX 컨텍스트, 다중 상속 경고, `SceneNavigation` 의 `NavDebugView` 값 멤버(내보내지 않은 타입), `OpenGLRHICommandList.cpp`.
    Release 측정: 텍스처 로드 중 다른 스레드의 `find` · `acquire` 대기, `Engine.tick` 의 틱 뒤 실행 경로 · `AudioEngine` 게시 · `GT.GPUScene.build`, `GameObject.h` 가 `TagSystem.h` 를 끌어오지 않는지(`-ftime-trace` 전후).
  - **시작 전에 알아야 할 함정**: 옛 빌드 폴더는 `cmake --preset` 을 먼저 다시 돌린다(파일을 옮겨 코드젠 목록이 낡으면 `missing and no known rule`).
    처음 돌린 테스트는 기대값부터 의심한다 — `ReflectionEnumInfoTest`(`CameraRole::Editor` → `Viewport`), `GameObjectManagerTest.PostTickOnComponentSkipsPendingDestroy`, `GameObjectManagerPoolTest.ModuleUnloadDrains{Deferred,Requeued}PostTickWork`,
    `ServiceClientCallTableTest.ForeignResponseDuringSendDoesNotTakeTheSendingDelegate`, 온라인 클라이언트 취소 테스트 둘 · `KitDestroyedWhileFailingQueuedCallsGetsNoMoreCalls` ·
    `KitDestroyedDuringReadyNotificationIsSkipped`(후자는 테스트 서버 Hello 가 `kGame+0x100` · `+0x200` 키트를 받아 주는지에 달렸다 — 판 불일치로 Ready 에 못 가면 테스트 결함부터), `TestGameService`(회수 · `ExplicitOwnerDecidesWhichImageReleasesTheService`),
    `ModuleHostTest.SharedModuleCannotBeRegisteredForReload`(Windows 대소문자 케이스 포함), `PrefabTest.CookedPrefabMatchesItsSource`(틀리면 쿠킹본이 어긋난 것인지 쓰기가 불안정한 것인지부터),
    `TestRHISupport` 두 스레드 `tickCompleted`, `TestRenderPassGPU` 동시 `acquire`(DX12 · DX11 만 — 작업 스레드 안 `SW_TEST_DEFENSIVE_SCOPE` 가 스레드마다 따로 동작하는지), `EditorPreferencesStoreTest.ReloadedSectionReadsTheFileOnce`.
  - **확인 방법(실기동, `-gv_profileFrames=60` 으로 스스로 끝나게)**: 에디터 씬 뷰 카메라가 게임 뷰 수집에서 빠지고 BugIt 캡처가 정상 · 씬을 바꾼 뒤 되돌리기 기록이 비고 종료가 깨끗 ·
    탈것 · 말 · 카메라 감독 · 물 · 지형 · 식생 · 플레이어 청크가 틱 뒤 실행 교체 뒤에도 정상(탑승 중 내림 · 오브젝트 파괴에서 크래시 없음) · 키트 하나만 리로드했을 때 `GameService` 가 새 인스턴스를 가리킴 ·
    에디터 복사 → 에디터 모듈 리로드 → 붙여넣기(`ObjectCopyBuffer`) · 환경설정을 고친 직후 리로드해도 값이 남음(새 섹션 읽기 포함) · `registerModule` 이 공용 모듈(대소문자만 다른 이름 포함)을 거절 ·
    Shooter 적 체력이 `Vitality`(0 아래로 안 내려가고 최대 1 미만이면 1 — **데이터 체력 0 이 예전엔 즉사, 지금은 1 로 산다**; 쓰러짐 · 절단 확인) ·
    텍스처 로드 중 다른 키 조회가 막히지 않고 같은 키 동시 요청이 한 번만 만듦, 리로드 직후 한 프레임 안에 머티리얼 SRV 가 새 것으로(네 백엔드) · DX12 에서 두 스레드 `tickCompleted` 안전 ·
    온라인 응답이 보내는 중 다른 id 로 와도 현재 호출이 안 사라짐 · 로그 범주 이름 변경(`AbilitySystemComponent*` · `ScheduleSystem*` 가 파일 이름으로 — 로그 필터 도구가 있으면 맞춘다) ·
    에디터 HUD 섹션 · 개발 콘솔 명령(락 추가, 모듈 로드 교착 없음) · 디버그 HUD 섹션을 켜고 끄면 프로파일러가 따라 켜지고 꺼짐 · `GameInstanceBase` 의 `GameSettings` 소유자가 게임 모듈 리로드 뒤 회수 · 다시 걸림 ·
    실패 콜백 · Ready 알림 중 키트가 파괴돼도 크래시 없음 · 모듈 언로드 때 `Deferred work kept re-queueing` 오류 로그가 정상 경로에서 안 남.

- `B-14` [검증대기] **2026-10-15 `verify` 묶음 이후 남은 검증 둘.** ① Release 네 백엔드 스크린샷(`-gv_benchMeshes=8 -gv_benchAnimate=0 -gv_screenshotFrame=30`): main 은 포워드 · 디퍼드 모두 네 백엔드가 바이트까지 같다.
  perfbase 와 포워드는 같고, 디퍼드 차이는 `ee6fff232`(반해상도 블룸)의 의도된 변경이다(결정 기록 5-5). 디퍼드 비교 기준은 그 커밋 이후다.
  ② Linux 전용 파일은 Windows 에서 컴파일되지 않아 기계적 치환만 하고 짓지 못했다(`PosixCrashHandler.cpp` · `EpollStreamTransport.cpp` · `FileUtil.cpp` 의 `st_mtim` 분기 · `HardwareProbe.cpp`). 사용자가 요청할 때만 WSL 로 확인한다.

- `C-2` [정리] **빌드 속도 — 계획은 [docs/plans/BuildSpeed.md](../plans/BuildSpeed.md).** 남은 것은 12 단계(리플렉션 헤더 팬아웃 — 다음으로 큰 것), 5 단계(유니티 범위 — 한 쌍만 잼, 재측정 필요), 6 단계(`Engine.cpp`)다. 범위 결정은 [결정 기록](../09_Decisions.md) 5-2.

- `C-48` [정리] **소스 게이트 공유 훑기(`SourceScan`)로 못 옮긴 게이트와 픽서.** 파일마다 보는 소스 게이트 28 개는 `SourceScan` · `RunSourceGates`(CTest · CI 의 `CheckSourceFiles`)로 옮겼다(2026-10-17 `lintspeed`).
  못 옮긴 것: `NamespaceBlocks` · `IncludeOrder`(줄 끝 · utf-8-sig 를 따로 다루고 픽서와 판정이 나뉨), `ThirdPartyIsolation` · `SuppressionReasons`(C++ 와 CMake 두 파일 집합), `KitNamespaces`.
  픽서(`FormatBranchBraces` 등)도 파일마다 따로 읽고 주석을 가리는 같은 틀이다. `ConditionParts` · `EditorStyleLiterals` · `DataDrivenLiterals` 는 추가 줄 · 기준선 로직을 저마다 중복해 든다 —
  공유 드라이버가 판정 단위(줄 끝 · 인코딩 · 파일 집합)와 추가 줄 · 기준선을 형식으로 갖게 한다.

- `C-15` [정리] **`dev/MoveEditorState.py`**(옛 위치의 에디터 로컬 상태를 옮기는 PC 마다 한 번짜리 이동기, 2026-10-13 들임)는 모든 PC 가 돌렸으면 지운다 — 추적되지 않는 파일을 옮기므로 돌렸는지 저장소에서는 알 수 없다.
  이 PC 는 `--dry-run` 에 옮길 것이 없다(2026-10-16). 다른 PC 는 사용자가 확인한 뒤에 지운다.

- `C-28` [정리] **테스트 틀 개편(2026-10-15)의 남은 것.** 틀 · 정책 · 확장 지점은 들어갔다(`Test/README.md`, 결정 기록 5-2 끝). 남은 것 —
  ① 벤치 스위트는 부하 스위트로 옮겼다(2026-10-15). 옮긴 뒤의 `CoreTest` · `EngineTest` `seconds` 와 `loadSeconds` 는 뺄셈 어림이라 다음 정책 재측정 때 실제 값으로 맞춘다.
  `NetSimDestructionMatrixTest` 는 손실 회선의 수렴(정확성) 테스트라 nogpu 에 남겼고, 호스트 벤치(`NetUDPBenchTest` · `GlyphBenchTest`)는 스위트 종류가 하나뿐이라 호스트로 남겼다.
  ② 구성 차이 건너뜀 중 `SW_TEST_REQUIRES_FEATURE` 로 아직 안 바꾼 것 — Shipping 의 에디터 · 설정 파일 · 개발 전역 변수 · 서버 팩 오디오 쪽(`TestAppScenario` · `TestConfigManager` · `TestNetClientServer` 등 열 곳 안팎).
  필요하면 `BuildFeature` 에 `Editor` · `DevGlobalVariables` 를 더한다.
  ③ JUnit XML 리스너(`ITestRunListener`) — CI 대시보드가 필요해지면.

- `C-33` [정리] **로그 게이트(2026-10-15)의 남은 것.** 시나리오 · 호스트 스위트는 선언하지 않은 Warning · Error 로 진다(결정 기록 5-2, `Source/Engine/Automation/README.md`). 남은 것 —
  - CPU 단위 테스트(nogpu)는 아직 Error 도 세지 않는다. 2026-10-15 한 번에 표식 없는 Warning · Error 가 약 1,500 줄(일부러 깨진 카탈로그 픽스처 · 외형 스키마 테스트 · 경로 오류 등).
    `SW_TEST_DEFENSIVE_SCOPE` 로 감싸거나 원인을 고친 뒤 `HostLogJudge` 를 모든 스위트로 넓힌다. 줄 목록은 `ctest -L nogpu` 뒤 LastTest.log 의 `[Warning]` · `[Error]` 를 (호출자, 문구) 로 묶어 본다.
  - 전용 서버를 서버 설정이 없는 게임(`Config/Server/<게임>.json` 이 Empty 뿐)으로 띄우면 Dev 기본값 Warning 두 줄(`Server config … could not be read`, `ServerConfig using cpp defaults`). 서버를 낼 게임이 정해지면 설정 파일을 둔다.
  - 에디터 켠 짧은 실행(`-gv_profileFrames=90`)을 다른 GPU 작업과 겹쳐 돌리면 종료 때 `Task fencing timeout (5000 ms) before module reload` Error 가 났다(36 회 중 3 회, 단독 재실행 0 회).
    종료 경로(`ModuleHost::drainRenderWorkers`)는 리로드가 아닌데 같은 문구를 쓰고, 5 초 넘게 도는 에디터 배경 작업이 무엇인지 모른다 — 다시 보이면 `py -3 -m Scripts stacks <pid>` 로 워커 스택을 남긴다.
    2026-10-16 독점 GPU 단독 DX12 5 회 0 회.

- `C-29` [정리] **부하 검증(2026-10-15)의 남은 것과 첫 측정이 드러낸 것.** 쓰는 법은 [검증과 측정](../08_Verification.md) "부하 검증" 절, 결정은 결정 기록 5-2 끝.
  ① **개발 배치의 기동 열기 309 ~ 316 번 중 셋은 Shipping 에 없는 일이다** — 키트 DLL 섀도 복사 61(읽고 쓰고 PDB 까지 복사), 모듈 매니페스트 70(`Bin/Modules/*.module.json` 낱개 — 언리얼 `.modules` 처럼 한 파일로 묶을 수 있다),
  셰이더 원본 해시 약 30(`.hlsl` · `.hlsli` 를 읽어 쿠킹 스탬프를 대조). LowEnd 흉내(8 ms)에서 이것만 약 1.3 s 다. 배포 기동을 재려면 쿠킹 팩을 올린 Release(`--cooked-dir`) 로 부하 시나리오를 돌리는 길을 먼저 만든다.
  ② 렌더 벤치 · aiskirmish 의 최악 프레임에 13 ~ 20 ms 한 프레임이 판마다 나오다 말다 한다 — 디스크와 무관하다(측정 창 안 파일 열기 0, 흉내를 꺼도 나온다). 어느 구간인지 Tracy(`gv_profileTracePath`)로 그 프레임을 본다.
  ③ **실행 중 자동 품질 조정**(언리얼 Dynamic Resolution · 유니티 Adaptive Performance)은 아직 없다. 지금은 `load scale` 이 밖에서 단계를 내려 예산을 지키는 첫 단계를 찾는다.
  첫 측정에서는 모든 프로필이 GT · RT · GPU p99 예산(16.7 ms)의 1/10 안팎이라 실행 중 조정의 이득이 보이지 않았다 — 무거운 게임 장면이 예산을 깨면 그때 렌더 스케일부터(`gv_renderScale`) 들인다.
  ④ 하드웨어 흉내는 코어 수 · 메모리 · 디스크 · 해상도 · 품질까지다. 클럭 · 캐시 · GPU 성능은 흉내 내지 않으니 LowEnd 의 프레임 시간이 Native 보다 짧게 나올 수 있다 — 실제 저사양 기기로 한 번 재서 흉내와의 차이를 적는다.
  ⑤ 야간 잡(`load.yml`) 첫 실행(38813564584)은 성공했다 — 리눅스에서 OnlineLoadBot 이 서버 키트를 올려 connect × Mid, replication × Mid · LowEnd 가 통과했다(오류율 0, tick p99 약 15 ms). 러너에는 기준선이 없어 알림만 나오므로 기준선 비교는 이 PC 에서만 본다.

- `C-41` [정리] **Linux · GPU 검증 구조.** 새 기능의 Linux 결함이 늦게 드러난다(2026-10-17: Linux 잡이 며칠 configure 에서 죽어 있었고 소프트 바디는 처음 돌자마자 발산). CI 에 GPU 가 없어 hostgpu 테스트는 로컬뿐이다. 순서는 결정 기록 5-12 `C-41` 행이다(GPU 러너 · 자체 호스트 러너는 하지 않는다).
  끝난 것: ① 세션 시작에 main CI 실패를 알린다(`ci-jobs --summary`, 세션 가드). ② 셰이더 전 백엔드 확인(`CheckShaderStamps` 게이트 — 쿠킹 표의 네 백엔드 폴더마다 `cook.stamp` 가 지금 소스와 같은지, 빌드 없이 린트 잡에서).
  ③ 요구 백엔드(`Test/TestPolicy.json` 의 `requiredBackends` → hostgpu 항목의 `--required_backends=`)가 서지 못하면 건너뛰지 않고 진다(`RHITestDevice` · AppSmoke 의 `runApp`). App 시나리오 러너(`TestAppScenario` · `TestAppUI`)의 "아무 백엔드도 못 돌렸다" 건너뜀은 아직 이 형식을 거치지 않는다.
  ④ 소프트웨어 렌더러 묶음(라벨 `swrender`, `<타깃>_SoftwareRender` — 정책 `softwareRenderCases` · `softwareRequiredBackends`, CI-Debug 잡이 WARP · lavapipe+xvfb 로 돈다).
  남은 것: CI 의 `Test on software renderer` 단계는 보고만 한다(continue-on-error) — 첫 실행들이 초록이면 떼고 `softwareRenderSeconds` 를 잰 값으로 고친다. App 시나리오 러너(`TestAppScenario` · `TestAppUI`)의 "아무 백엔드도 못 돌렸다" 건너뜀을 `test::reportUnusableBackend` 로 옮긴다.

- `C-42` [정리] **증분 쿠킹 · 파생 데이터 캐시(DDC).** 리소스를 100MB 이상 들이는 중이라 에셋이 늘면 쿠킹 시간이 병목이 된다. 현재 쿠킹의 입력 해시 · 재사용 범위를 확인하고 증분 쿠킹과 공유 캐시를 설계한다(셰이더는 입력 해시 스탬프가 있다 — 결정 기록 5-5).

- `C-44` [정리] **credits 표와 임포트 규칙이 "넓은 와일드카드 + 첫 매칭"이다.** 새 팩 파일이 다른 팩의 출처로 붙거나 다른 팩의 배치 오프셋(`ModelImportConfig.json`)이 적용돼도 경고가 없다(2026-10-17 `res3` 도입에서 확인; 이번엔 새 줄을 표 앞에 두어 피했다). 팩마다 하위 폴더를 하나씩 두고 패턴이 하위 폴더를 넘지 못하게 하거나, 파일 하나에 규칙이 둘 이상 맞으면 오류로 낸다. `CheckResourceCredits` 가 출처 목록(해시) 형식(`audio.sources.json` 의 모양)을 모든 리소스 도메인에서 읽게 넓히는 일을 포함한다.
  출처 목록의 해시가 맞으려면 산출물이 결정적이어야 하므로, 인코더가 만드는 다른 산출물(텍스처 DDS 변환 등)의 비결정 바이트도 점검한다.
  이미지 출처 목록(TOML, `textures_raw/cc0_sources.toml`)과 오디오 출처 목록(JSON)의 형식이 갈렸다 — 도구 데이터를 `Resource/` 에 둘 형식 · 위치를 한 번에 정한다. 이미지 쪽 스크립트(`Scripts/dev/MakeCc0Import.py`)는 파이썬 3.11 이상(`tomllib`)이 필요하다.
- `C-46` [정리] **게임 패키지에 다른 게임의 팩까지 모두 들어간다.** 확인한 사실(2026-10-18, 코드로 — 쿠킹한 팩은 이 PC 에 없어 크기는 원본으로 쟀다):
  `cookAllPacks`(`Scripts/generate/CookAssets.py`)는 `engine.pack` · `common.pack` 과 함께 `Resource/game/` 아래 **모든** 폴더를 `game_<이름>.pack` 으로 한 출력 폴더에 굽는다 — `SW_ACTIVE_GAME` 을 보지 않는다.
  `MakePackage --game <이름>` 은 그 `Bin/Packs` 폴더를 통째로 담는다. 그래서 `common`(원본 33.6MB, 그중 오디오 33.1MB)은 모든 패키지에 들어가지만, 런타임이 `game` → `common` → `engine` 순으로 늘 올리는 공용 도메인이라 이것은 설계대로다.
  서버 패키지는 `--build-target Server` 가 오디오를 빼므로(`CookContract.json`) 공용 오디오가 들지 않는다.
  판단: `common` 만 고르는 팩 선언은 지금 필요 없다. 더 큰 낭비는 다른 게임의 팩이다(원본 `empty` 73.5MB · `shooter3d` 39.1MB 가 모든 게임 패키지에 든다) — 쿠커가 패키지의 게임(`SW_ACTIVE_GAME`)의 `game_` 팩만 굽게 하는 것이 먼저다.
  공용 오디오를 일부 게임만 쓰게 되면 그때 `Config/Game/<게임>.json` 에 쓰는 공용 팩을 적는 선언을 들인다.
- `C-50` [정리] **리소스 도입 용량 예산을 원본이 아니라 쿠킹 DDS 까지 합쳐 잡는다.** 2026-10-17 `res1` 에서 재질 31MB 가 57MB 로, 파티클 4.8MB 가 27MB 로 늘었다.
  현재 이미지 도입분은 120.7MB(원본 44.2MB + DDS 76.4MB)이며 110MB 이하로 줄이려면 재질 10 → 8(약 -12MB), HDRI 4 → 3(약 -7.5MB), 파티클 40 → 20(약 -8MB) 중 둘쯤.
- `B-15` [검증대기] **`res1` 이미지 도입의 확인 둘.** ① normal 맵 G 채널 방향 — 기존 `invert_green` 규칙으로 구웠을 뿐 렌더로 확인하지 않았다(Poly Haven `_nor_gl`).
  ② Kenney "Smoke Particles" 팩은 362x336 등 2의 거듭제곱이 아니라 `texture-limits` 규칙 위반이다 — 들이려면 변환(2의 거듭제곱으로 맞춤)이 필요하다. 지금 들인 Particle Pack 의 `smoke_*.png` 는 512x512 라 해당 없다(통합 때 확인).

- `B-16` [검증대기] **2026-10-18 `A-25` · `A-26` · 렌더 스레드 수정의 다른 백엔드와 UI 전용 화면 확인.** `fixgfx` 는 GPU 테스트를 DX12 호스트에서만 돌렸고, 활성 씬이 없는 프레임을 그리지 않게 한 변경이
  Vulkan 에서 `UNDEFINED` 백버퍼를 `Load` 로 여는 결함을 드러냈다(`RenderThread::executeFrameBody` 가 그래프가 그렸을 때만 `Load` 하게 고침, Debug `race` 5/5 로 확인).
  2026-10-18 확인: Release(`-DSW_GAMES=ALL`) `-L hostgpu` 9/9 통과(샤드가 백엔드를 어떻게 나눴는지는 로그로 못 봤다), 씬 없는 프레임(캔버스 시험 그림 + 로딩 화면)은 네 백엔드가 같은 그림으로 UI 를 그린다.
  남은 확인: **타이틀 화면과 에디터가 씬을 닫은 상태**의 캡처, `-gv_uiDemo` 견본 화면이 안 보인 이유(로딩 화면이 가렸는지). `gpu-verifier` 로 캡처한다.

- `E-1` [관찰] **다음 CI 실행으로 확인할 것**(리눅스는 컴파일까지 되고 테스트 단계에서 진다, CI 읽는 법은 [검증과 측정](../08_Verification.md) 4절).
  고쳤고 통과를 확인할 것: `OnlineLoadBotTest.TwoHundredBots…`(캐시 TTL 을 서버 틱 시각으로), `EpollStreamTransport` 경합 보고, `StreamTransportTest.PlatformAbortWithNothingPendingStillCloses`(전송 모델별 기대),
  ASan 둘(OpenSSL 스택 수명, `NetRequestTest` 선언 순서), `ServerBootTest` 접속 로그, Linux Debug `EngineTest_NoGPU` 샤드의 180 초 초과(멈춤이 아니라 느림 — 샤드를 여섯으로 늘렸다).
  원인을 아직 모르는 것: `UIOptionsMenuTest.RebindIgnoresUIActionsWhileListening`(리눅스 Debug · Shipping · ASan, 포커스 위젯 ID 839 ≠ 823 `Tab.controls` — 창이 닫힌 뒤 탐색 Up 이 한 번 발화한 것으로 추정, 다음 실패 메시지가 포커스 위젯 이름을 보인다),
  ASan 중단 두 건(`PlatformLoginTest.Oidc…` · `NetRequestTest.IdempotentRetry…`), TSan 샤드의 새니타이저 보고.
  Windows STL containers 잡에서 건너뛰게 한 테스트 셋(`MemoryTagTest` 둘, `DestructionDamageTest.EventLogHash…`)이 기본 컨테이너 구성에서도 가끔 지는지 지켜본다.

- `E-2` [관찰] **Windows CI Debug · STL 잡의 키트 테스트 22 건 실패**(run 37251047781, `a380e2ee3` — 유니티 켬 + PCH 끔 Debug 에서만): 재현 안 됨 — 이 PC 의 같은 구성
  (`CI-Debug -DSW_ENABLE_PCH=OFF`)에서 통과했다. B4 로 실패 보고(`CiFailureReport.py`)가 나아진 뒤 다음 CI 주석으로 본다.
  그 뒤 액션 키트 테스트 넷이 네 실행 연속으로 지는 간헐 실패가 되었다 — 일회성으로 보지 않는다.

- `E-4` [관찰] **CI Windows Debug `EngineTest_NoGPU_Shard3` 가 모두 통과한 뒤 0 이 아닌 종료 코드로 졌다**(run 37457443235 · `28d5627e3`, 한 번 — 직전 실행은 통과).
  로그는 `TaskManager Shutdown cleanly` 에서 끝나고 크래시 스택 주석이 없었다. 이 PC Debug 로 같은 조각(`--test_shard=2/3 --host_suites=exclude`) 15 회 순차 ·
  12 회 4 개 동시 — 재현 안 됨. 하네스가 이제 마지막 줄 `[TestHost] shut down - exit code N` 을 찍는다: 다시 나면 그 줄이 없으면 `TestHostRuntime::stop`
  안(부트스트랩 · 로거 · 메모리 프로파일러 종료), 있으면 정적 소멸자다.
  **같은 증상이 리눅스 Shipping 에서 나왔다**(run 38163565253, `734f7fa08`, 조각 여섯으로 늘린 뒤 `EngineTest_NoGPU_Shard2`): 테스트는 10 초에 끝나 요약까지 찍혔는데
  마지막 줄이 없이 180 초 한도로 졌다 — `stop()` 안에서 멈춘 것이다. 어느 단계인지 가르려고 `stop()` 이 단계 앞마다 `[TestHost] stopping: …` 을 찍는다. 다시 나면 그 줄로 좁힌다.

- `E-5` [관찰] **CI Windows STL 잡이 파일 잠금으로 두 번 연속 졌다**(`334cae46e` · `734f7fa08`): 첫 번은 `OnlineLoadBot.exe` 링크("being used by another process"), 둘째는 모듈 링크 뒤 `vcpkg z-applocal`
  실행("resource busy or locked"). 둘 다 `BuildTestBinaries`(Test 단계)의 ninja 에서 났다. `StageModuleRuntimeDlls.cmake` 가 z-applocal 을 다섯 번까지 다시 띄운다.
  `OnlineLoadBot.exe` 쪽은 막지 못했다. 근본 의문: `Build` 단계가 끝난 트리에서 `BuildTestBinaries` 가 또 294 단계를 링크한다(ninja 가 `Re-checking globbed directories` 뒤
  `Re-running CMake` 를 도는 것을 로컬 드라이런에서 봤다) — 빌드 직후 두 번째 빌드가 빈손이 아닌 이유를 찾으면 Test 단계의 재링크와 잠금 경합이 함께 사라진다.

- `E-6` [관찰] **Shipping `EngineTest_NoGPU` · HostOnly 간헐 세그폴트**(09-20 · 21 · 22 에 한 번씩, 2026-10-06 nogpu 3 회 · host 3 회 재실행 깨끗). 다시 나면: 크래시 핸들러의
  스택 파일(`Bin/Saved/Logs/crash_<세션>.stack.txt` — Shipping 도 이제 PDB 가 있어 함수 이름), CI 는 그 파일을 아티팩트로 · 스택을 주석으로 올린다(`CiFailureReport.py`).

- `E-7` [관찰] **WSL lavapipe 가 가끔 서피스를 잃는다**(`AppTest_HostOnly` 43 회 중 3 회, 첫 `vkAcquireNextImageKHR` 가 `VK_ERROR_SURFACE_LOST_KHR`) — 이제 서피스 · 스왑체인을
  다시 만들고(사양대로) `Vulkan surface lost at acquire|present (N time(s) …)` 경고를 남긴다. WSL 에서 50 회 돌려 경고 수 · 실패 수를 본다(복구가 되면 항목을 지운다):
  `cd build/WSL-Debug/Bin && for i in $(seq 50); do ./AppTest --host_suites=only --test_filter=AppSmokeTest.* || echo FAIL $i; done` 와 `Saved/Logs` 의 경고 줄 수.

- `E-8` [관찰] **CI Windows 러너(WARP)의 픽셀 테스트 실패**(`RenderPassGPUTest` 를 host 스위트로 빼서 우회) — 이 PC 에서 `SW_RHI_SOFTWARE_ADAPTER=1`(`-gv_rhiSoftwareAdapter=1`)로
  같은 래스터라이저를 고를 수 있다. 2026-10-06 이 PC 의 WARP(DX12 · DX11)로 `RenderPassGPUTest.*` 71 개 픽셀 테스트이 모두 통과했다(Vulkan 은 CPU 디바이스가
  없어 빠지고 GL 은 하드웨어) — WARP 자체는 컴퓨트 컬링 · 인디렉트를 한다. 러너 쪽(WARP 판 · 창 없는 세션)을 다음 CI 실패의 주석으로 가른다.

- `E-10` [관찰] **Linux Shipping `EngineTest_NoGPU_Shard5` 가 종료에서 멈춰 180 초 시간 초과**(CI 실행 38439985866). 케이스는 모두 통과하고 요약까지 찍힌 뒤
  `[TestHost] stopping: destroying objects`(`TestHostRuntime::stop` 의 `destroyAll`)에서 끝나지 않았다. 같은 실행의 오디오 디코드 태스크 수명 결함(해제된 `AudioClipCache` 를 워커가 씀 — 고쳤다)이
  워커를 망가뜨려 태스크 매니저 join 이 멈춘 것인지 미확정. 종료 경로가 서브시스템 분해로 다시 쓰인 뒤 새 기준에서 다시 본다.

- `E-12` [관찰] **`editor.uipreview` Vulkan 1 회 "레이블 없음"**(2026-10-13 전체 체인, 프레임 180 `uipreview.language.en` — 콤보는 눌렀는데 항목이 없었다). 2026-10-15 Vulkan 단독 10 회(유휴 5 · CPU 부하 5)
  모두 통과해 재현하지 못했다. 에디터 입력 단계(`EditorClick` · `EditorDrag` · `EditorHover`)가 이제 레이블이 그려질 때까지 시계를 60 프레임까지 붙잡고(`AutomationRunner::holdUntilReady`)
  기다린 프레임을 `[Scenario] … held the clock for N frame(s)` 로 남긴다. 다시 지면 그 줄로 "늦게 열렸다"(기다려서 통과) · "닫혔다"(60 프레임 뒤에도 없음)를 가른다.

- `E-14` [관찰] **`meshmodifiers` Vulkan 체인 1 회(2026-10-15).** 에디터 체인(다른 hostgpu 와 GPU 공유 실행 중)에서 frame 585 `MeshModifiers.ReferenceListCount == 1, got 0`. 단독 Vulkan 2/2 통과,
  앞선 순차 hostgpu 네 백엔드도 통과했다. 2026-10-16 독점 GPU 단독 Vulkan 5/5 통과. 참조 목록 항목 추가가 부하 아래 한 프레임 늦는지(고정 프레임 간격) 볼 것.

- `E-15` [관찰] **`shooter3d.closewindow` GL 1 회(2026-10-16).** 공유 GPU 잠금으로 돈 Debug hostgpu(`EveryGamePackScenarioPassesOnEveryBackend`)에서 frame 10 `ExpectCursorClip` 이 클립 0,0,2560,1440(화면 전체)을 봤다 —
  같은 때 다른 App 창(스크린샷 캡처)을 띄우고 있었다. 이 시나리오는 실제 포커스 · 커서 클립을 쓰므로(`input="mixed"`) 독점으로 단독 GL 5/5 통과. 겹친 창이 포커스를 가져간 것으로 본다.

- `E-16` [관찰] **WSL VM(Ubuntu-26.04)이 `wsl --shutdown` 후에도 1 초 안에 다시 뜬다.** VS Code 의 WSL 연결이나 다른 도구가 깨우는 것으로 보이며 원인을 특정하지 못했다.

- `E-17` [관찰] **`ShaderCookerTest.FailedSourcesAreLeftOutOfTheCookStamp` 가 `ctest -L nogpu -j` 에서 1 회 실패했다**(2026-10-17 `integ2`, Debug, `isCookedOutputCurrent( binDir, goodPath )` 가 false).
  혼자 5 회 반복 · 샤드 단독 재실행은 통과했다. 병렬 실행 중 공유 헤더 캐시(`invalidateSharedHeaderCache`)나 임시 폴더를 다른 케이스와 나누는지 본다.

---

[◀ 백로그 안내](README.md)
