# 다음 세션 인계

> 결론: 할 일 목록은 이 문서 하나다. **남은 일**만 적고 끝나면 줄을 지운다(끝난 일은 git). 설계 계획 · 열린 판정은 [DesignBacklog](DesignBacklog.md), 규칙은 CLAUDE.md · AGENTS.md · `docs/agents/` · 스킬.

- 절차는 스킬이 정본: 묶음 끝 검증 · push `preflight`, Linux `linux-verify`, 시나리오 실패 `scenario-debug`. 사람이 보는 실행은 `py -3 Scripts/dev/watch_scenarios.py`.
- 코드 주석의 `감사 <ID>` 가 가리키는 닫힌 항목은 `git log --grep '<ID>'` 로 찾는다.
- 작업자는 NEXT 를 고치지 않는다 — 통합 커밋이 줄을 지운다(커밋 메시지 `감사 <ID>` 꼴).

## 0. 다음 순서

1. **zonecutscene 의심할 자리:** 안에서 생긴 겹침 진입이 8 프레임 안에 온다는 가정(shortcut 주석), 씬 XML 로 붙인 원격 조종자(시작 때 `RemoteController.NoConnection` — 형식으로 막기), `setInputPeer` 공개, 와일드웨스트 매니페스트에 `GF_NetSequence` 없음, 서버 판정이 필요한 다른 볼륨(자동 저장 · 튜토리얼)의 `hasActivationAuthority` 기본 참, `ZoneTracker` 진입에 나타남 규칙 없음.
2. **허브 다음 단계:** 다른 게임 흡수(DesignBacklog "게임 기능 · 미니게임" 차례). 이름 바꾸기 도구가 못 하는 곳은 그 도구에 더한다.
3. 그다음: 씬 스트리밍 (라) HLOD · 셀 로드와 텍스처 · 메시 스트리밍 우선순위 공유 · 로드 큐 디버그, 렌더 그래프 ③ 이후(VFX → 자동 노출 → FrameGraphExecutor → FramePassExecutor), 헤더 팬인 다이어트(혼자 마지막), 에셋 GUID 2 · 3 단계(5 절).
4. **엔진 전수 조사 판단**(DesignBacklog 같은 이름 절): 1 절(결함에 가까운 것)을 먼저, 2 절 구조 장치, 3 절 에디터 · 키트, 4 절 성능(측정 뒤)을 갈래로 나눠 맡긴다. 씬 관리 · 월드 스트리밍 전수 조사(사용자 요청) 결과가 없으면 다시 돌린다.
5. **감사 열린 항목(7 절):** 묶음마다 "확인" 의 테스트를 먼저 써서 지는 것을 본 뒤 고친다.

## 1. 통합 빌드에서 볼 것

- **5-4 리플렉션(감사 A-24):** 생성 TU 비중 Release 13.4 %(목표 8 % 미만). 넣은 후보 둘(`ReflectionMetadataText`, `FunctionDesc::invokeConstructor`)을 커밋마다 Release 로 지어 `py -3 Scripts/dev/build_time_report.py --preset Ninja-Release --top 20` — 흔들림 안이면 그 커밋만 되돌린다.
- **RHI 5-2 측정:** `D3D12.ComputeTimestampClock` 어긋남, Vulkan 비동기 컴퓨트 `GPU.<패스>` · Tracy 겹침.
- **실장치 · 백엔드:** Vulkan AMD 브레드크럼 · 128 타임스탬프 슬롯, DX12 placed 텍스처(`D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES` 가 RT+SRV 를 받는지), Vulkan placed 텍스처(`RT.Frame.transientPlacements`, 검증 레이어 경고 없이 — lavapipe · Android 포함).
- **손 확인을 시나리오로:** 에디터 설정 표 값을 고친 뒤 Ctrl+Z(형식 페이지 그리드는 아직 단계를 올리지 않는다), 'Game Config - Shooter3D' 열어 저장, 옵션 메뉴 렌더 품질 변경 → 그림자 · 텍스처 풀 값.
- **배포 도구:** N2 `py -3 -m Scripts server-image`, G1 `slice` 12 게임.
- **에디터 MVVM:** w-edthread4 · w-edmvvm2 통합 빌드 확인. 남은 View 정리는 `Common/GUI` 나머지(상태 표시 · 도킹 레이아웃 · 테마). UI 스레드 이전은 하지 않는다.

## 2. 알려진 실패 · 흔들림

- **`Scenario.playground.desktop.editor.playreload` 격리(만료 2026-11-27, `Config/Rules/Test/Quarantine.json`):** 게임 모듈 교체가 시계를 170 초 넘게 붙잡는 단계 — 격리를 풀고 다시 돌린다.
- **shooter3d dx11 골든 한 번씩 짐:** crowd `fight` 25.7 %, enemydeath `death_b` 1.37 %. 다시 지면 같은 체인 앞 단계와 `scenario_bisect --golden-margins` 로 화소 자리(dx11 군중 장면의 정착 조건 의심).
- **`editor.tilemapdatalayers` 단독 실패:** `<EditorText>` "글 칸이 키보드 초점을 잡지 않았다" — `EditorSelfTestInput::isKeyboardTargetStable` 를 `scenario-debug` "단독으로만 지면" 으로.
- **Linux:** sequencercuts · sequencerkeys `SequencerUnboundCount`(`--gpu software chain editor` 로 가른다), netresourcedigest, 에디터 체인 단계 약 18(입력 초점 의심), themepark 체인 멈춤, lavapipe `blendEnable` + `R16G16_SNORM`(재현 안 되면 지운다). `editor.screenrectview` 골든 1.09375 %(`all` 에서만) — 화소 자리를 보고 그 사진 앞 대기를 정착 조건으로. 골든: 에디터 `.linux` 24 장, empty 의 boat · decals · distortion(없음) · realtimegi · surfaceshowcase 2 · tessellationstage · geometrystage(Windows 와 57 ~ 100 % 다름). 데스크톱 WebGPU 골든 0(`scenario_bisect.py --golden-coverage`).
- **`AudioDataVariantTest.GraphVariantSavesOnlyItsDifference`:** 전체 실행 한 번 "base … could not be read" — 다시 지면 앞 케이스와 스크래치 도메인 마운트 상태.
- 흔들리는 게임 골든: 다시 지면 단계 · 백엔드 · 직전 `Assets.Pending` 을 적는다.

## 3. 플랫폼 (요구 판은 [Platform](agents/Platform.md))

웹부터.

- **웹 실행:** Web-Shipping 빌드 → `py -3 -m Scripts web-check --game Empty`(WebGPU · 오류 없음 · 화면 PNG · IDBFS 영속 · `[WebAudio] output started` — `WebAudioBackend` 는 아직 컴파일한 적 없다). 입력 · 클립보드 판정을 위해 웹 시나리오를 쿠킹하거나 앱 반응을 페이지로 내보내는 길을 먼저 낸다.
- **웹 온라인:** https 페이지용 `wss://`(수락 쪽 TLS 종단), 게임 세션의 브라우저 길(WebSocket · WebTransport 데이터그램).
- **Android 실기기:** `CookAssets` → `py -3 -m Scripts android-apk --game <게임>` → 기동 · 회전(pre-rotation) · APK 팩 읽기(`AAssetDir_getNextFileName` 폴더 앞머리) · 터치 · 클립보드(JNI `ClipboardManager` Looper 제약) · AAudio 장치 분리 → 다시 열기.

## 4. 실행 · 측정이 필요한 일

측정이 문턱을 넘은 것만 고친다. 이득이 없으면 줄을 지운다. "켤 코드" 커밋은 따로 — 값이 흔들림 안이면 그 커밋만 되돌린다. 설계 절의 문턱은 DesignBacklog 각 절의 "측정".

- **에디터 첫 패널 · Hierarchy 8000(E1, DesignBacklog 같은 이름 절):** `App -EnableEditor -gv_editorPanelTimes=120 -gv_traceSlowTickMs=100` — 첫 그리기 50 ms 넘는 패널은 준비를 `onPanelOpened` 잡으로, 100 ms 넘는 프레임 0. 큐브 8000: Hierarchy 정지 avg 0.5 ms 미만, Ctrl+A · 이름 거르기 16 ms 미만.
- **S8 후보(Release, 반복):**
  - 대기 호출 표: `OnlineServerCallTableTest.CompletionCostByPending` — 차이가 흔들림보다 크면 `_mapCall` 을 둔다.
  - 반사 검증 중첩 타입: `ReflectionValidationTest.NestedElementCost` — 흔들림보다 작으면 `NestedTypeCache` 를 되돌린다.
  - 파서 토큰화: `ReflectionParser.TokenizeStats` — 10 % 넘으면 토큰 저장소 재사용.
  - 씬 저장: `game/abilityarena/automation/editor/scenesave`, `Scene.save.*` — write 가 16 ms 넘고 serialize 보다 크면 write 를 워커로.
  - 씬 쿠킹: `--cook-all` DDC 없이 · 있이 — scenes 가 벽시계 절반을 넘고 가장 느린 씬이 합의 1/4 아래면 씬 단위 병렬(캐시 격리 먼저).
  - 팩 씬 청크 고르기: `PackCooker.SceneChunkPlan` — 씬마다 다시 훑는 몫이 팩 쿠킹의 1/4 을 넘으면 `ReachTraversal` 뿌리 표식으로 한 번에 훑는다.
  - GPU 업로드: shooter3d · meadowvillage 첫 진입 × 백엔드 셋 × `-gv_gpuUploadQueue=0/1` — pump 가 RT 의 10 % 를 넘거나 waiting 이 계속 0 이 아니면 RHI Thread 형 시제품.
  - 렌더 그래프 폭: `-gv_renderGraphParallelMinWidth=2 · 3 · 4 · 99` × DX12 · Vulkan — 3 · 4 가 흔들림 밖으로 빠르면 기본값을 바꾼다.
  - 그 밖: Tracy `GT.Frame` 자식 합 90 % 이상, 배치 크기 12 · 24 · 48, `load/*` × 워커 1 · 2 · 4 · 8, W25 GPU 비교(c05a3ee251 대 지금), W-S4 `-ftime-trace`, W-R1 RT 프레임 힙 할당, W-R2 PSO 조회, 5-2 `RT.Bind.*`, DX12 기록 경로 락 제거 전후 `bench.json` p50(291d69074e), 골든 여유, 군중 `load/navcrowd`, 잔상 메모리 `game.empty.afterimage`, 볼륨 안개 `RT.Fog.Volume`.
- **메시 인덱스 전환 뒤:** 모든 골든 대조(차이는 버그)와 농장 · 군중의 캐릭터당 GPU 측정.
- **글리프 아틀라스 압축(감사 RN-14):** `App --test --test-filter=GlyphAtlasTest.HangulScrollGlyphsRasterized` 를 1e4d3dbe8c 와 fc54031411 에서 — `rasterized` 가 흔들림 밖으로 안 줄거나(예상 30184 → 약 15300) `missing` 이 프레임당 10 자를 넘으면 `_glyphRetainFrameCount` 를 1 로 재고, 그래도 지면 fc54031411 만 되돌린다.
- **N×M 측정 후보**(재고 나서 해시로): `DataInheritance.cpp` `findEntry`, `ComponentRegistry.cpp:60,75`, `PrefabPool.cpp:80,92`, `ShooterNetPawnComponent.cpp:535,571`, `MaterialCache.cpp:350`, `DestructionState.cpp:779,855`, `ContentBrowserCollectionStore.cpp:108`, `EditorAssetFileCommands.cpp:154,199,569`, `EditorNodeGraph.cpp:333,543,696`, `ModResolver.cpp:161`, `ProjectSettingsDocument.cpp:45,62`, `ComponentStateStore.cpp:82`.
- **Shooter3D 소총 자세 클립:** 쥔 자세 클립 + 왼손 IK 로 — CC0 poly.pizza "SWAT"(Quaternius, `https://static.poly.pizza/713f6535-f4f3-4367-a4c6-ced126ae0936.glb`, `Idle_Gun` · `Gun_Shoot` · `Run_Shoot`)를 `models_raw/quaternius` 에 넣고 UBC 로 리타깃해 상체 층(Aim)으로, IK 견착은 손 자리 보정으로.

## 5. 설계 남은 일

결함 · 계약 줄을 먼저, "문턱을 넘을 때만" 줄은 4 절 값이 나온 뒤. 초안은 DesignBacklog 의 같은 이름 절.

- **렌더링:** TransientLeasePool 을 뷰 풀에, 같은 자리 후처리 머티리얼이 앞 결과를 이어 읽기, VFX · 프로브 디스패치 전이를 묶음 API 로. 셰이더: 공통 `PixelContext` · 표면 평가와 패스별 출력 분리, 표면 모델 · 필수 입력 선언과 미지원 조합의 쿠킹 실패 — Lit · Unlit · Emissive · 툰 · 알파 마스크 패스 분류 목록부터.
- **R-15 후속("텍스처 스트리밍 밉 범위 대기 줄" 절):** 1 — GI 복셀화를 GPU 원본으로(`SceneVoxelizer.cpp`). 2 — `MeshCPUAccess` 를 `Mesh::create` · `MeshCache::request` 에 기본값 없이, 업로드 뒤 CPU 정점을 놓는다(1 뒤).
- **RHI 5-2(측정 뒤에만):** Vulkan 동적 오프셋 · 푸시 디스크립터, WebGPU 바인드 그룹 캐시 은퇴, RHI 스레드, 복사 큐, SM6.6 바인드리스, RT · 메시 셰이더, 서브할당(감사 R-4). L-2 상수 버퍼 기록 값을 레코드로(미러 락 대기가 `RT.Graph.parallelLevel` 합의 2 % 를 넘을 때만).
- **5-3 병렬 오브젝트**("병렬 오브젝트 남은 몫" 절): PO-1 서비스 구간 · 근접 카운터 측정 → PO-2 서명 나누기 + 파도 → PO-3 남은 직렬 몫 약 5 ms 를 구간으로 잰다.
- **5-4 리플렉션:** RF-7 밀집 타입 색인(W-R6)은 5-3 SoA 와 함께, 문턱을 넘을 때만. FUNCTION: 인자 있는 메서드를 타입 위젯 · `ReflectCallResult` 오류 표시로, 그다음 반복 작업용 액션 목록.
- **코어 · 온라인:** O3-5 실제 해시 부하 구성(`Config/Server/Online/` · `OnlineLoadBot`).
- **도구 · 에디터(문턱을 넘을 때만):** V2 timeline 모으기 → V3 그리지 않는 대기 → V4 shooter3d 적용, T2 체인 샤드(ctest 벽시계 10 % 이상 안 줄면 되돌림), R3 에셋 색인 디스크 캐시, E2 문턱 넘는 패널, D3 공유 DDC 측정, S2 · S3 원본 떼기(원본 2 GB 문턱).
- **시나리오:** 실시간 GI 정착 조건(넣으면 GI 장면 사진 시점이 바뀐다 — 골든과 함께).
- **데이터 · 카탈로그:** 매직 리터럴 기준선이 큰 파일의 튜닝 값과 게임 모드 상태 `switch` 를 PROPERTY · 규칙 카탈로그로, `sw::expected` Core 밖 이전. `FighterCatalog` 엄격도와 `ActionPlatformerCatalog` 전이 검사를 참조 표로 옮길지 정하기, 잘못된 참조가 쿠킹 · 시작을 통과하는 회귀 테스트.
- **키트:** MMO 키트의 부하 목표와 운영 비용.
- **키트 셋 가르기(규칙 · 디렉터 · 클라이언트):** TacticsSRPG 가 본보기(규칙은 키트 루트, 디렉터 `Component/SRPGGameModeComponent`, 뷰 · 입력 · HUD 는 `Client/` 의 `GF_Client_TacticsSRPG`). Fighting · Farming · CityBuilder 는 키트 안에 섞인 곳이 없고 디렉터 · 뷰가 폐기할 게임(Playground FightingDemo · HarvestValley · MeadowVillage · FrontierTown · NileCity)에 있다 — 허브 미니게임으로 옮길 때 같은 계약으로 키트 `Component/` · `GF_Client_<X>` 에 둔다. 나머지 순서(같은 계약): ActionPlatformer · ActionCombat · BattleRoyale · MechArena · Metroidvania · CardGame · KartRacing · PartyArena · Rhythm · AsymmetricHorror · CoopScavenger · GhostHunt · SurvivalHorror · ClassicJRPG · MonsterCollector · MonsterHuntRPG · OpenWorldWestern · CreatureLife · RestaurantSim · ThemePark · RealTimeStrategy · SideScrollConquest · ActionAdventure.
- **상용 대비:** T3 샤드 시간 균형(외부 CI 러너는 사용자 결정), U1 방아쇠 진동 · Android Vibrator(백엔드가 생길 때).

### 에셋 정체성(GUID) 2 · 3 단계 — DesignBacklog "에셋 정체성" 절

- **1 단계 통합 검증:** 단위 `AssetGUIDTest` · `AssetDatabaseTest` · `AssetPackIndexTest`, 시나리오 `editor.assetidentity` · `editor.assetmove` · `editor.flowimport` · `contentbrowser`(`.meta` 가 안 보이는지), 모든 쿠킹(`PackCooker.PackCookNoIdentity` 없음), `--check-text` · 머티리얼 그래프 생성 검사.
- **2 단계 — 참조를 GUID 로:**
  - Content 전수 이전: `migrate-asset-refs --fix` 로 `AssetRef<T>` 칸을 `guid:<32자>` 로(4003 개). 먼저 경로를 담은 같은 이름 글 칸(`GameFramework` `_material` · `_sound`, `GF_ThemePark` `_shape`, `GF_Server_Leaderboard` · `GF_Server_Matchmaking` `_catalog` — 값 59 개)을 `AssetRef<T>` 로 바꾸고 빌드로 `AssetReferenceFields.generated.json` 을 다시 낸 뒤 돌린다. 임포트 산출물 참조 898 개 · `_base` · `DataPath` 칸은 산출물 줄 뒤.
  - 세이브(`SW_SAVE_FORMAT_UPGRADE`)와 서버 기록(`Content/common/sql/*/migrations.xml`)의 경로를 GUID 로 — 배포된 팩 색인 + 마지막 리다이렉트 표로 한 번 풀어 쓴다.
  - 설정의 경로 칸(`enginedefaultassets.xml`), 게임 설정 · 카탈로그 `_base` 의 파일 부분. 경로 캐시 키는 `AssetRedirect::toCacheKey` 한 곳을 GUID 로.
  - 임포트 산출물 줄: 원본 GUID → 산출물 경로를 임포트 기록에서 팩 색인에. 내용 주소 공유 파일(`textures_raw/_shared/`)은 저장소 경로 해시 GUID 로 `.meta` 를 함께.
  - 옮기기용 리다이렉트를 걷고 GUID→GUID 표만 남긴다(읽는 쪽을 먼저 옮긴 뒤 — `deprecation-and-migration`).
  - localization 표 쓰기가 머리를 옮겨 쓰게 하고 `*/localization/*` 건너뛰기를 걷는다. 시나리오 사본(`scratch/`)의 GUID 겹침 — 사본 도메인을 색인에서 빼거나 새 GUID.
- **3 단계 — 원본 저장소 떼기:** 원본(png · ogg · glb)을 LFS · 별도 저장소로 떼고 `_source` 가 그 자리를 가리킨다(S2 · S3).

## 6. 리소스 · 그 뒤

- **Sprite Fusion 타일셋**(CC0): Castle · Tiny Swords(Pixel Frog), Dungeon(0x72), Four Seasons Platformer(Analog Studios), Retro Gameboy(Ben Leggett) — `https://www.spritefusion.com/downloads/tilesets/<이름>.zip`. 1Bit Platformer 는 Kenney 원본에서. **VFX 25 종**(MIT, credits 에 고지): 캔버스 TypeScript 를 브라우저로 재생해 시트로 굽는다. **PixelFX Piper**: 이용 조건 확인 뒤.
- **로우폴리 에셋을 게임 품질로(CC0):** 게임마다 플레이스홀더 · 원시 도형이 남은 자리를 `-gv_screenshot` 사진으로 모아 CC0 팩으로 채운다(판정: 원시 도형 · 단색 플레이스홀더 0).
- Empty 게임을 TestBed 로 전수 개명(옛 이름 린트로 막기). `new-online-kit` 로 키트 하나 지어 보기. `-cooked-content` 로 띄워 조립 경로 에셋이 빠지지 않는지. 단위 테스트 빈 곳(`Scripts/dev/test_coverage.py` "없음"), `AccountTicketConnect` 루프백 성공 · 만료 · 재시도 실패와 비밀 값이 로그에 새지 않는지.

## 7. 감사 열린 항목

줄 앞은 감사 ID 와 옛 번호 `[n]`(코드 주석 · DesignBacklog 가 가리킨다 — 다시 매기지 않는다). 결함 · 계약 줄을 먼저, 성능 줄은 측정이 문턱을 넘은 것만. 온라인 줄은 `security-and-hardening`, 외부 API 주장은 `source-driven-development` 로 대조한다.

- **P-4 [14] Android IME 조합**(DesignBacklog "Android IME 조합" ①부터). 확인: `WindowTextInputTest` Android 조합 케이스, `web-check` 의 `compositionend` 화면.
- **O-12 [30] 원장 경로 부하 측정(통합):** PostgreSQL · RESP 를 띄우고 `SW_SHOOTER3D_DB_PASSWORD` · `SW_SHOOTER3D_CACHE_PASSWORD` 를 둔 뒤 빌드 `Bin` 에서 `OnlineLoadBot --scenario=<저장소>/Source/Programs/OnlineLoadBot/Scenarios/ledger_mix.json --load=<저장소>/Source/Programs/OnlineLoadBot/Scenarios/ledger_mix.load.xml --local-server=0 --online-config=<저장소>/Source/Programs/OnlineLoadBot/LoadBot.online.json --store-config=<저장소>/Config/Server/Online/Shooter3D.external.json --report=<빌드>/Testing/Logs/OnlineLoadBot.ledgerMix.json` — `_server._conflictRetryBasisPoints` · `_busyBasisPoints` · `_droppedArrivalCount` 를 이 줄에 적는다. 종료 코드 5 면 원장 감사(재화 총량 보존)가 깨진 것이다.
- **R-4 [31] 자원마다 전용 할당**(DesignBacklog "자원 구간 할당" ①부터, VMA · D3D12MA 는 라이선스 확인 뒤): `MaterialRenderProxy.cpp:48`. 확인: 머티리얼 인스턴스 5000 의 버퍼 바이트 · Vulkan 할당 수.
- **RN-6 [34] GPUScene 부분 수집 측정:** `31f2492dc8` 과 4 단계 커밋에서 `ctest -R Scenario.empty.load.instancechurn` — `GT.GPUScene.build.collect` · `build.batches` · `RT.GPUScene.slotInstances` p50 비교(`build.patchEnter` 0 아님). 문턱을 넘으면 배치 용량부터.
- **A-13 [36] 씬 스트리밍 측정 길:** 개발 구성 쿠킹본 실행을 ctest 하나로 세운다(쿠킹 → `-cooked-content` 기동 → 시나리오 하나, 쿠킹 순서는 배포 확인과 같게: 텍스처 임포트 · 현지화 · 셰이더 — 지금은 `engine.en.locres` · dx12 셰이더 바이너리가 팩에 없어 멈춘다). 그 위에서 `GT.Scene.service.SceneStreaming` 최대 프레임(셀 400 × 상자 2000, 짓기 예산 2000 us)을 잰다.
- **A-24 [47]** 리플렉션 묶음 TU 의 헤더 다시 파싱 — 1 절 5-4.
- **K-11 [50] 슈터 적 풀링 확인(통합):** Tracy `GameModeFlush` 를 풀 전후로 재고, shooter3d `enemydeath` · `dismemberment` · `effectpool` · `autoplay` 를 한 번에 — 다시 꺼낸 적이 래그돌 · 절단 · 무기 · 빙의 없이 서는지.

## 8. 다음 감사 범위

- **RHI · 렌더러:** 백엔드별 PSO 생성 스레드 안전 · 전송 티켓 계약, WebGPU · GL CommandContext, Vulkan 장치 고르기, EGL · GLX 문맥, `FramePassExecutorPasses.cpp`, `RenderGraph::executeParallel` 배리어 묶음, 모프 · 정점 애니메이션 풀, 실시간 프로브, forward 파이프라인 XML.
- **직렬화 · 입력:** `SerializerUtil.cpp` · `XMLSerializer.cpp` · `DataInheritance.cpp` · `ResourceComposer.cpp`, ReflectionParser `CodeGenerator.cpp`, `InputMap` 평가와 고정 틱 사이 엣지 유실, `WaylandWindow.cpp`, UI 접근성 브리지.
- **온라인:** `OnlineCore/Server/HTTP`(HPACK · HTTP/2 동적 표 · 프레임 길이 상한), Apple · Google 영수증 검증기, RESP 재연결 뒤 응답 짝, O-7 죽은 서버의 파티 티켓 풀기, 비교 후 쓰기(748bca0b0d), O-6 명단 캐시 기록.
- **게임플레이 · 에디터:** `ScheduleSystem.cpp`, BehaviorTree · Blackboard, Appearance, 장르 키트 14 개 내부, 임포터(Model · Texture · Vrm), Animation 패널, 프리팹 오버라이드 적용, 도킹 · 레이아웃.
- **실행으로만 확인되는 것:** 성능 항목 전부, 외부 API 동작에 기댄 항목(R-1 · P-5 · P-13 · O-15).

## 9. 코드 `shortcut:` 정리

정본 모양이 있으면 크기와 무관하게 고친다. 정본이 없고 "규모가 커지면" 인 것만 문턱을 적고 남긴다.

- **렌더 · 셰이더:** 변위 UV 이음매(`material_displacement.hlsli:22` — PN-AEN 식 패치 인덱스), 코트 칸 없는 외형의 젖음 · 기름막(`appearance_lit.hlsl:408` — 젖은 피부 클로즈업이 중요해지면 번호를 나눈다), 홍채 노멀(`appearance_lit.hlsl:38` — 오목한 홍채 노멀을 BRDF 둘째 노멀로).
- **게임플레이:** 프리팹 JSON 저장 칸 빼기(`PrefabAsset.cpp:336` — JSON 프리팹이 생길 때).
- **에디터 · 온라인 · 도구:** 상태 문서 자동 저장(`EditorStateDocumentPanel.h:26`), 관리 도구 마지막 대상만(`AdminToolSession.h:158`), 시나리오 옛 `<Wait>`(`AutomationScenario.cpp:86` — 22 파일 123 곳, 시나리오마다 조건 대기 · 진행을 가르고 돌려 본다). 문턱만: 팩 쓰기 재읽기 해시(`PackWriter.cpp:226`), 백엔드 교체 예산(`AutomationPerBackend.cpp:575` — 상한 540 초), 데스크톱 팩 변형(`AssetAndToolTargets.cmake:57`), `test_history.py:284`, `naming_words.py:107`.

## 사용자에게 받을 것

영수증 · 푸시 · 외부 로그인의 실제 계정과 키(지금은 로컬 가짜 끝점 — `Config/Server/Online/Providers/`). 새 의존성(인코더 라이브러리 · VMA · D3D12MA — 둘 다 MIT)의 라이선스. 공유 DDC 자리와 쓰기 권한 머신, 원본을 Git LFS 로 넓히기(용량 · 비용 — S1)와 이력 다시 쓰기(`git lfs migrate`, 되돌리기 어렵다), 원본 별도 저장소, 퍼징 · 샤드용 외부 CI 러너.

## 지킬 결정

- **리소스:** 무료 · 공개 라이선스만(CC0 우선, MIT 등은 고지와 함께) — 묶음 페이지 라이선스를 확인해 credits 에 URL · 날짜. 에이전트가 직접 그리지 않는다. 유료 생성 API 는 들이지 않는다. Quaternius 새 묶음은 그 페이지가 CC0 일 때만(2026-08 QAL). 원본은 Git LFS, 받은 팩은 `/AssetLibrary/`(무시), 쓰는 것만 `common/models_raw · textures_raw/<출처>/<묶음>/`.
- 범용 스크립팅 언어는 들이지 않는다([판단](DesignBacklog.md#핫-리로드와-스크립팅-언어-판단)). 런타임 아틀라스는 하지 않는다. 메모리 풀은 측정 뒤(출하 할당기는 감사 C-6 의 `SW_MEMORY_BACKEND`), GC 는 들이지 않는다.
- 웹은 wasm32(memory64 는 Safari 미지원). 교차 빌드는 호스트 빌드(`SW_HOST_BUILD_DIR`, 기본 `build/Ninja-Release`)의 ReflectionParser · App 을 쓰고, 교차 프리셋은 vcpkg 설치 폴더를 따로 쓴다.
- 최상위 `Mods/` 는 `Test/Mods/`. App 의 `Dev/` 는 남긴다. 줄끝은 `.gitattributes` 의 `* text=auto eol=lf`.
