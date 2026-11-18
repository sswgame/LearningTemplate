# 다음 세션 인계

> 질문: 무엇이 남았고 무엇부터 하는가?
> 독자: 이어서 일하는 세션(에이전트와 사람).
> 결론: 할 일 목록은 이 문서 하나다. **남은 일**만 적고 끝나면 줄을 지운다(끝난 일은 git 기록에). 판단은 [DesignBacklog](DesignBacklog.md), 플랫폼 판정은 [PlatformTargets](PlatformTargets.md), 규칙은 CLAUDE.md · AGENTS.md · `docs/agents/` · 스킬에 있다.

절차의 정본은 스킬이다: 묶음 끝 검증 · push `preflight`, Linux `linux-verify`, 시나리오 실패 `scenario-debug`. 시나리오 체계는 `Test/README.md`, 사람이 보는 실행은 `py -3 Scripts/dev/watch_scenarios.py`. 코드 주석의 `감사 <ID>` 가 가리키는 닫힌 항목은 `git log --grep '감사 <ID>'`(앞 커밋 몇은 `<ID>` 만 — `git log --grep '<ID>'`)로 찾는다.

## 0. 다음 순서

1. **다음 묶음:** 작업자 갈래(w-gentu2 · w-rn4 · w-s8 · w-n11 · w-gvsnap · w-k11 · w-a13 · w-rn6 단계 · w-editorthread 단계)를 둘 · 셋씩 병합 → Debug · Release 증분 빌드로 컴파일 먼저 → `preflight --no-linux` → push → 워크트리 지움. 할 일을 끝낼 때 Linux(`linux_scenarios.py all` + sanitize)를 모아서 한 번.
   - 작업자 몫: `GameObjectManager.inl` 머리말의 "포함을 잊으면 링크에서 멈춘다" 를 맞춘다 — clang 은 `-Werror=undefined-func-template`(`sw_require_template_definitions`)로 컴파일에서 멈추고 MSVC 만 링크에서 멈춘다. 그 뒤 에셋 GUID 2 · 3 단계(5 절).
2. **감사 열린 항목(7 절):** 묶음마다 "확인" 의 테스트를 먼저 써서 지는 것을 본 뒤 고친다. 작업자에게는 NEXT 를 고치지 말게 하고 통합 커밋이 줄을 지운다 — 커밋 메시지는 `감사 <ID>` 꼴(옛 백로그 번호와 겹친다).

## 1. 통합 빌드에서 함께 볼 것 (빌드 없이 넘어온 묶음)

단위 · 시나리오로 확인되는 몫은 2026-11-18 전체 실행(Debug `App --test` 4822 · Release ctest 111 · 서버 단위 4288, Windows)에서 통과해 지웠다. 남은 것은 측정 · 실장치 · 손 확인이다.

- **5-4 리플렉션(감사 A-24 남은 몫):** `.gen.cpp` 합계 1.65 MB(전 7.86 MB, 달성). 생성 TU 비중(옛 출력을 빼고 세게 고친 `build_time_report.py`): Debug 8.6 %(목표 8 % 미만, 거의 닿음), Release 15.8 % → 13.4 %(11-18, `~TypeInfo` 를 TypeInfo.cpp 로 뺀 뒤 생성 TU 합 1126 s → 822 s, 가장 무거운 묶음 14.6 s → 8.7 s). 다음 후보 둘을 넣었다(메타데이터 글 칸 대입 → `ReflectionMetadataText`, `$ctor` → placement new 한 줄 + `FunctionDesc::invokeConstructor`). 커밋마다 Release 로 지어 `py -3 Scripts/dev/build_time_report.py --preset Ninja-Release --top 20` — 흔들림 안이면 그 커밋만 되돌린다.
- **RHI 5-2 측정:** `D3D12.ComputeTimestampClock` 어긋남, Vulkan 비동기 컴퓨트 `GPU.<패스>` · Tracy 겹침.
- **실장치 · 백엔드 동작:** Vulkan AMD 브레드크럼 · 128 타임스탬프 슬롯, DX12 placed 텍스처(`D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES` 가 RT+SRV 를 받는지), Vulkan placed 텍스처(`RT.Frame.transientPlacements`, 검증 레이어 경고 없이 — lavapipe · Android 포함).
- **손 확인을 시나리오로:** 에디터 설정 표 값을 고친 뒤 Ctrl+Z(형식 페이지 그리드는 아직 단계를 올리지 않는다), 'Game Config - Shooter3D' 를 열어 저장, 옵션 메뉴에서 렌더 품질을 바꿔 그림자 · 텍스처 풀 값이 바뀌는지(바꾸는 순간 `GT` 에 `pollPackets` 대기 한 번).
- **배포 도구:** N2 `py -3 -m Scripts server-image`(Linux 서버 패키지 tar.gz), G1 `slice` 12 게임.

## 2. 알려진 실패 · 흔들림

- **shooter3d crowd `fight` dx11 한 번 25.7 %(2026-11-18, preflight30 — 큰 영역 70 px):** 체인만 다시 돌려 통과. 다시 지면 같은 체인 앞 단계와 `scenario_bisect --golden-margins` 로 화소 자리를 본다(enemydeath dx11 과 같은 백엔드 — dx11 군중 장면의 정착 조건을 의심).
- **shooter3d enemydeath `death_b` dx11 한 번 1.37%(2026-11-18, ST-3 통합 전체 실행):** 곧바로 두 번 다시 돌려 통과. 다시 지면 같은 체인의 앞 단계와 `scenario_bisect --golden-margins` 로 화소 자리를 본다.
- **`editor.tilemapdatalayers` 단독 실패:** 이름 칸을 누른 뒤 `<EditorText>` 가 "글 칸이 키보드 초점을 잡지 않았다"(화면에는 칸이 활성). 체인에서는 앞 단계가 가린다 — `EditorSelfTestInput::isKeyboardTargetStable` 가 두 프레임 같은 대상을 못 보는 까닭을 `scenario-debug` "단독으로만 지면" 으로.
- **Linux:** editor 체인 sequencercuts · sequencerkeys 의 `SequencerUnboundCount`(Linux 에서만, `--gpu software chain editor` 로 가른다), netresourcedigest, 에디터 체인 단계 약 18(WSLg · Xvfb 입력 초점 의심), themepark 체인 멈춤, lavapipe `blendEnable` + `R16G16_SNORM`(코드가 블렌드 비트 없으면 덮어쓰기로 바꿔 옛 증상일 수 있다 — 재현 뒤 지운다). 골든: 에디터 `.linux` 24 장(창 크기가 달라 눈으로), empty 의 boat · decals · distortion(없음) · realtimegi · surfaceshowcase 2 · tessellationstage · geometrystage(Windows 와 57 ~ 100 % 다름 — dzn 차이인지 결함인지). 데스크톱 WebGPU 골든 0(`scenario_bisect.py --golden-coverage`).
- **`AudioDataVariantTest.GraphVariantSavesOnlyItsDifference`:** 전체 실행 한 번에 "base … could not be read" 로 졌다(단독 1800 회 재현 안 됨) — 다시 지면 그 로그의 앞 케이스와 스크래치 도메인 마운트 상태를 본다.
- **Linux `editor.screenrectview` 골든:** 2026-11-13 · 11-18 두 번 모두 정확히 1.09375 % (허용 1 %) — `all`(체인 넷 동시)에서만 진다. 앞 142 단계를 붙인 이분 탐색(혼자 도는 체인)은 통과. 부하에 따른 정착 차이로 본다 — 다시 지면 `/tmp/lt.chain.editor.out` 과 화소 자리(`scenario_bisect --golden-margins`)로 어느 화소인지 보고 그 사진 앞 대기를 정착 조건으로 바꾼다.
- 흔들리는 게임 골든: 다시 지면 단계 · 백엔드 · 직전 `Assets.Pending` 을 적는다.

## 3. 플랫폼 (판정 · 요구 판은 [PlatformTargets](PlatformTargets.md))

순서: 웹부터(언리얼은 웹을 내렸고 Godot 웹은 GL Compatibility 만이라 WebGPU 길은 참고할 상용 사례가 적다).

- **웹 실행:** Web-Shipping 빌드(쿠킹이 단계 텍스처 16 계약을 처음 건다) → `py -3 -m Scripts web-check --game Empty`(WebGPU · 오류 없음 · 화면 PNG · IDBFS 영속 · `[WebAudio] output started` — `WebAudioBackend` 는 아직 컴파일한 적 없다). 입력 · 클립보드(paste)는 판정할 길이 없다 — 배포 빌드에 시나리오 실행기가 없어 웹 시나리오를 쿠킹하거나 앱 반응을 페이지로 내보내는 길을 먼저 낸다.
- **웹 온라인:** 단위 `WebSocketProtocolTest` · `WebSocketAcceptTransportTest`. `ws://` 만이라 https 페이지용 `wss://`(수락 쪽 TLS 종단), 게임 세션(암호화 UDP)의 브라우저 길(WebSocket · WebTransport 데이터그램 갈래).
- **Android 실기기:** `CookAssets` → `py -3 -m Scripts android-apk --game <게임>` → 기동 · 회전(pre-rotation 방향) · APK 팩 읽기(`AAssetDir_getNextFileName` 이 폴더 앞머리를 붙이는지) · 터치 · 클립보드(JNI `ClipboardManager` 가 Looper 제약 없이 읽고 쓰는지) · AAudio 장치 분리 → 다시 열기.

## 4. 실행 · 측정이 필요한 일

순서: 측정이 문턱을 넘은 것만 고친다. 이득이 없으면 값을 DesignBacklog "측정과 성능 후보(S8)" 에 한 줄 적고 줄을 지운다. "켤 코드" 커밋은 따로라 값이 흔들림 안이면 그 커밋만 되돌린다. 설계 절의 측정 문턱은 DesignBacklog 각 절의 "측정" 이다.

- **에디터 첫 패널 · Hierarchy 8000(DesignBacklog "에디터 첫 패널 · Hierarchy 8000 측정 계획", E1):** `App -EnableEditor -gv_editorPanelTimes=120 -gv_traceSlowTickMs=100`(`Saved/Editor` 지운 것 · 둔 것) — 첫 그리기 max 50 ms 넘는 패널은 준비를 `onPanelOpened` 잡으로(`ModulesModel::ensureLoaded` 후보), 100 ms 넘는 프레임 0. 큐브 8000 씬: Hierarchy 정지 avg 0.5 ms 미만 · 구조 세대가 같으면 수집 0 회, Ctrl+A 16 ms 미만, 이름 거르기 글자마다 16 ms 미만, 한 줄 옮기기 · 칸 끌기는 형제 순서 · 프로퍼티 기록 절의 판정.
- **검증 시간(Windows 19 분 — 2026-11-13 증분, 11-12 의 53 분에서):** build 5 분(배포 프리셋 셋이 4 분) · cpu 2.7 분 · Release ctest 11.3 분. 공통 헤더 · CMake 폴더를 고치면 전체 재빌드라 50 분대. Linux `all` 21 분(shooter3d 체인 872 초) + sanitize 3 분.
- **S8 후보(Release, 반복):**
  - 대기 호출 표: `OnlineServerCallTableTest.CompletionCostByPending` P 16 · 256 · 4096 ns/완료 · `service_store_pending_peak` — 차이가 흔들림보다 크면 `_mapCall` 을 둔다.
  - 반사 검증 중첩 타입: `ReflectionValidationTest.NestedElementCost` — 흔들림보다 작으면 `NestedTypeCache` 를 되돌린다.
  - 파서 토큰화: 변경 없음 · 헤더 하나 · 모듈 전체, `ReflectionParser.TokenizeStats` — 10 % 넘으면 토큰 저장소 재사용(fixture 결과 보존).
  - 씬 저장: `game/abilityarena/automation/editor/scenesave`, `Scene.save.*` — write 가 16 ms 넘고 serialize 보다 크면 write 를 워커로.
  - 씬 쿠킹: `--cook-all` DDC 없이 · 있이, `Headless.CookStageTimes` · `SceneCooker.SceneCookTime` — scenes 가 벽시계 절반을 넘고 가장 느린 씬이 합의 1/4 아래면 씬 단위 병렬(캐시 격리 먼저).
  - 팩 씬 청크 고르기: `--cook-all`(씬 패턴 켬 · `_listScenePattern` 비움), `PackCooker.SceneChunkPlan` 로그와 고르기 단계 벽시계 — 씬마다 다시 훑는 몫이 팩 쿠킹의 1/4 을 넘으면 씬 훑기를 한 번의 훑기에서 뿌리별 닿음으로 모으는 길(`ReachTraversal` 뿌리 표식)로 바꾼다.
  - GPU 업로드 · 텍스처: shooter3d · meadowvillage 첫 진입 × 백엔드 셋 × `-gv_gpuUploadQueue=0/1`, `RT.TextureUpload.*` — pump 가 RT 의 10 % 를 넘거나 waiting 이 계속 0 이 아니면 RHI Thread 형 시제품.
  - 렌더 그래프 폭: `-gv_renderGraphParallelMinWidth=2 · 3 · 4 · 99` × DX12 · Vulkan — 3 · 4 가 흔들림 밖으로 빠르면 기본값을 바꾼다.
  - 그 밖: Tracy `GT.Frame` 자식 합 90 % 이상, 배치 크기 12 · 24 · 48, `load/*` × 워커 1 · 2 · 4 · 8 과 구조 지표, W25 GPU 비교(c05a3ee251 대 지금 — 노이즈 안이면 되돌림), W-S4 `-ftime-trace`, W-R1 RT 프레임 힙 할당, W-R2 PSO 조회, 5-2 `RT.Bind.*`, DX12 기록 경로 락 제거 전후 `bench.json` p50(291d69074e), 골든 여유(`--golden-margins`), 군중 `load/navcrowd`(넘으면 한 종류 군중을 지역으로 쪼갬), 잔상 GPU · CPU 메모리 `game.empty.afterimage`(품질 단계별 상한), 볼륨 안개 해상도별 `RT.Fog.Volume`.
- **메시 인덱스 전환 뒤(StructureRefactor 10 절):** 모든 골든 대조(정점 순서가 같으면 그림이 같아야 한다 — 차이는 버그)와 농장 · 군중의 캐릭터당 GPU 측정.
- **글리프 아틀라스 압축(감사 RN-14, w-glyph):** Release `App --test --test-filter=GlyphAtlasTest.HangulScrollGlyphsRasterized` 를 측정 장치 커밋(1e4d3dbe8c, 예전 통째 비우기)과 본 변경(fc54031411)에서 한 번씩 — 로그의 `rasterized` · `missing`(파이썬 모형 예상: 30184 · 0 → 약 15300 · 1600, 하한 13760). rasterized 가 흔들림 밖으로 안 줄거나 missing 이 프레임당 10 자를 넘으면 `_glyphRetainFrameCount` 를 1 로 재고, 그래도 지면 본 변경만 되돌린다. 실제 화면은 `-gv_stressTestUiWidgets` 화면의 `Text.GlyphsRasterized` · `Text.GlyphsRelocated`.
- **N×M 측정 후보**(목록이 작으면 해시가 손해이거나 순서를 지켜야 한다 — 재고 나서): `DataInheritance.cpp` `findEntry`(가장 큰 후보), `ComponentRegistry.cpp:60,75`, `PrefabPool.cpp:80,92`, `ShooterNetPawnComponent.cpp:535,571`(틱마다 적 수²), `MaterialCache.cpp:350`, `DestructionState.cpp:779,855`, `ContentBrowserCollectionStore.cpp:108`, `EditorAssetFileCommands.cpp:154,199,569`, `EditorNodeGraph.cpp:333,543,696`(프레임마다 노드 × 고른 노드), `ModResolver.cpp:161`, `ProjectSettingsDocument.cpp:45,62`, `ComponentStateStore.cpp:82`.
- **Shooter3D 소총 자세 클립:** 지금은 리그 IK 견착(`ubc_player_body.rig.xml` TwoHandHold). 상용(Lyra · ALS)처럼 쥔 자세 클립 + 왼손 IK 로 — CC0 poly.pizza "SWAT"(Quaternius, `https://static.poly.pizza/713f6535-f4f3-4367-a4c6-ced126ae0936.glb`, `Idle_Gun` · `Gun_Shoot` · `Run_Shoot`)을 `models_raw/quaternius` 에 넣고 UBC 로 리타깃해 상체 층(Aim)으로, IK 견착은 손 자리 보정으로.

## 5. 설계 남은 일

순서: 결함 · 계약 줄을 먼저, "문턱을 넘을 때만" 줄은 4 절 값이 나온 뒤. 판단 · 초안은 DesignBacklog 의 같은 이름 절에 있다.

- **렌더링:** TransientLeasePool 을 뷰 풀에, 같은 자리 후처리 머티리얼이 앞 결과를 이어 읽기, VFX · 프로브 디스패치 전이를 묶음 API 로. 셰이더: 공통 `PixelContext` · 표면 평가와 패스별 출력(포워드 색 / G버퍼 인코딩) 분리, 표면 모델 · 필수 입력 선언과 미지원 조합의 쿠킹 실패 — Lit · Unlit · Emissive · 툰 · 알파 마스크 패스 분류 목록부터(언리얼 셰이딩 모델 대조).
- **R-15 후속(판단은 "텍스처 스트리밍 밉 범위 대기 줄" 절):** 1 — GI 복셀화를 GPU 원본으로(`SceneVoxelizer.cpp`, 기하 셰이더 없는 3 축 투영 · 프레임 업로드 단계, 스킨 · 모프는 CPU 정점 Keep). 2 — `MeshCPUAccess` 를 `Mesh::create` · `MeshCache::request` 에 기본값 없이, 업로드가 끝난 뒤 CPU 정점을 놓는다(언리얼 `bAllowCPUAccess`, 의존 1).
- **RHI 5-2(측정 뒤에만):** Vulkan 동적 오프셋 · 푸시 디스크립터, WebGPU 바인드 그룹 캐시 은퇴, RHI 스레드, 복사 큐, SM6.6 바인드리스, RT · 메시 셰이더, 서브할당은 감사 R-4. L-2 상수 버퍼 기록 값을 레코드로(L-1 의 미러 락 대기가 `RT.Graph.parallelLevel` 합의 2 % 를 넘을 때만, `D3D12RHIDevice` · `RHIConstantBufferMirror`, R6-3 · R6-4 뒤, 테스트 `SecondWriteInAFrameIsReported`).
- **5-3 병렬 오브젝트**(완전 ECS 는 기각, 이득이 흔들림 안이면 되돌림 — "병렬 오브젝트 남은 몫" 절): PO-1 서비스 구간 `GT.Scene.service.<이름>` · 근접 카운터 `Gimmick.ProximityCandidates`(의존 없음). PO-2 서명 나누기 + 파도(`FrameSystemParallelPlan`, PO-1 값 뒤). PO-3 캐릭터 일괄 이동(청크 병렬 · 결정성 `PhysicsCharacterTest.BatchMoveEqualsOneByOne`) 측정: Release `Bin` 에서 `App -scenario=game/empty/automation/load/charactermove.scenario.xml -unattended -workers=N`(N = 1 · 4 · 8)의 `GT.Physics.moveCharacters`.
- **5-4 리플렉션:** RF-7 밀집 타입 색인(W-R6)은 5-3 SoA 와 함께, 문턱을 넘을 때만(`ReflectionTypeRegistryTest.TypeIndexIsReusedWithANewGeneration`). FUNCTION: 인자 있는 메서드를 타입 위젯 · `ReflectCallResult` 오류 표시로, 그다음 반복 작업용 액션 목록(언리얼 `CallInEditor` 대조).
- **에디터 UI 스레드 분리(사용자 목표 2026-11-08, 판단은 DesignBacklog "에디터 UI 스레드 분리 검토"):** 1 MVVM(대부분 끝) → 2 입력 큐(진행 중, 브랜치 w-editorthread) → 3 출력 계약(뷰 크기 · Play/Pause/Step 을 명령으로, 한 프레임 지연 계약) → ImGui 프레임을 UI 스레드로(모듈 리로드 · RHI 교체 · 종료 전 drain, 자동화 결정성 테스트). 단계마다 통합 빌드 · 에디터 체인으로 확인한 뒤 다음 단계.
- **코어 · 온라인 설계 일:** O3-5 실제 해시 부하 구성(`Config/Server/Online/` · `OnlineLoadBot`, 도착률은 감사 O-12).
- **도구 · 에디터(측정이 문턱을 넘을 때만):** V2 timeline 모으기(의존 없음) → V3 그리지 않는 대기(`engine/automation/waitrenderoff`) → V4 shooter3d 적용, T2 체인 샤드(ctest 벽시계 10 % 이상 안 줄면 되돌림), R3 에셋 색인 디스크 캐시, E2 문턱 넘는 패널, D3 공유 DDC 측정, S2 · S3 원본 떼기(원본 2 GB 문턱).
- **시나리오 · 테스트 프레임워크 개편(ST-1 ~ ST-6 은 main):** ST-3 이 남긴 것 — 실시간 GI 정착 조건(넣으면 GI 장면 사진 시점이 바뀐다 — 골든과 함께).
- **데이터 · 카탈로그:** 매직 리터럴 기준선이 큰 파일(데모 UI · 게임 모드 컴포넌트)의 튜닝 값과 게임 모드 상태 `switch` 를 PROPERTY · 규칙 카탈로그로, `sw::expected` Core 밖 이전. `FighterCatalog`(기술 → 프레임) 엄격도와 `ActionPlatformerCatalog` 전이 검사를 참조 표로 옮길지 정하기, 상속 · 다른 팩 참조와 잘못된 참조가 쿠킹 · 시작을 통과하는 회귀 테스트.
- **키트:** MMO 키트의 부하 목표와 운영 비용.
- **상용 대비:** T3 샤드 시간 균형(외부 CI 러너는 사용자 결정), U1 방아쇠 진동 · Android Vibrator(백엔드가 생길 때).

### 에셋 정체성(GUID) 2 · 3 단계 — 판단은 [DesignBacklog](DesignBacklog.md) "에셋 정체성" 절

- **통합 검증에서 볼 것(1 단계):** 단위 `AssetGUIDTest` · `AssetDatabaseTest` · `AssetPackIndexTest`, 시나리오 `editor.assetidentity`(가져오기가 새 GUID, `asset.replaceContent` 가 GUID 유지), `editor.assetmove` · `editor.flowimport` · `contentbrowser`(`.meta` 가 브라우저 · 빠른 열기 · 참조 색인에 안 보이는지), 모든 쿠킹(팩마다 색인, `PackCooker.PackCookNoIdentity` 없음), `--check-text` · 머티리얼 그래프 생성 검사(머리 때문에 "낡았다" 가 안 나오는지), Content XML 을 바이트로 다시 쓰고 견주는 테스트. `asset-ids` 는 린트 규칙이다(`lint --all`).
- **2 단계 — 참조를 GUID 로:**
  - Content 전수 이전: `migrate-asset-refs --fix` 로 `AssetRef<T>` 칸을 `guid:<32자>` 로(지금 0). 그 뒤 `AssetRefBase::assign` 의 옛 경로 글 읽기를 걷는다. `_base` · `DataPath` 칸과 임포트 산출물 참조는 산출물 줄 뒤.
  - 세이브(`SW_SAVE_FORMAT_UPGRADE`)와 서버 기록(`Content/common/sql/*/migrations.xml`)의 경로를 GUID 로 — 배포된 팩 색인 + 마지막 리다이렉트 표로 한 번 풀어 쓴다.
  - 설정의 경로 칸(`enginedefaultassets.xml`), 게임 설정 · 카탈로그 `_base` 의 파일 부분. 경로 캐시 키는 `AssetRedirect::toCacheKey` 한 곳을 GUID 로.
  - 임포트 산출물 줄: 원본 GUID → 산출물 경로를 임포트 기록에서 팩 색인에. 내용 주소 공유 파일(`textures_raw/_shared/`)은 저장소 경로 해시 GUID 로 `.meta` 를 함께.
  - 옮기기용 리다이렉트(`AssetRedirect` · `redirects.xml` · `move-asset` 줄 더하기)를 걷고 GUID→GUID 표만 남긴다(읽는 쪽을 먼저 옮긴 뒤 — `deprecation-and-migration`).
  - localization 표(`strings` · `translation` · `tm`) 쓰기가 머리를 옮겨 쓰게 하고 `*/localization/*` 건너뛰기를 걷는다. 시나리오 사본(`scratch/`)이 원본 GUID 로 겹치는 것 — 사본 도메인을 색인에서 빼거나 새 GUID.
- **3 단계 — 원본 저장소 떼기:** 원본(png · ogg · glb)을 LFS · 별도 저장소로 떼고 `_source` 가 그 자리를 가리킨다(위 S2 · S3).

## 6. 리소스 · 그 뒤

- **Sprite Fusion 타일셋**(상세 페이지마다 CC0): Castle · Tiny Swords(Pixel Frog), Dungeon(0x72), Four Seasons Platformer(Analog Studios), Retro Gameboy(Ben Leggett) — `https://www.spritefusion.com/downloads/tilesets/<이름>.zip`. 1Bit Platformer 는 Kenney 원본에서. **VFX 25 종**(MIT, credits 에 고지): 캔버스 TypeScript 를 브라우저 MCP 로 재생해 시트로 굽는다. **PixelFX Piper**: 이용 조건 확인 뒤 시트로.
- **로우폴리 에셋을 실제 게임 품질로(사용자 지시 2026-11-01, CC0):** Quaternius 적용(11-02) 뒤 끝났다는 판정이 없다. 게임마다 플레이스홀더 · 원시 도형이 남은 자리를 `-gv_screenshot` 사진으로 모아 목록을 만들고, 남은 자리를 CC0 팩으로 채운다(판정: 게임 사진에 원시 도형 · 단색 플레이스홀더 0).
- Empty 게임을 TestBed 로 전수 개명(옛 이름 린트로 막기). `new-online-kit` 로 키트 하나 지어 보기. `-cooked-content` 로 띄워 경로를 조립해 읽는 에셋이 빠지지 않는지. 단위 테스트 빈 곳(`Scripts/dev/test_coverage.py` "없음"), `AccountTicketConnect` 를 루프백 온라인 호스트로 성공 · 만료 · 재시도 실패와 비밀 값이 로그 · 오류에 새지 않는지.

## 7. 감사 열린 항목

상용 대비 감사(2026-11-12, 기준 main `e480b07498` — 줄 번호의 기준. 원문은 `git show aa79565a0a:docs/CommercialGapAudit.md`, 마지막 판은 `f0e5340264^`). 순서: 결함 · 계약 줄을 먼저, 성능 줄은 측정이 문턱을 넘은 것만. 줄 앞은 감사 ID 와 옛 "다음 할 일" 번호 `[n]`(코드 주석 · DesignBacklog 제목이 가리킨다 — 다시 매기지 않는다). 온라인 줄은 `security-and-hardening`, 외부 API 주장은 기억 기준이라 context7 로 대조한다.

- **P-4 [14] Android IME 조합** — Android 가 미확정 글을 못 받는다(웹 조합 · 소프트 키보드는 섰다). 상용: 유니티 `TouchScreenKeyboard`, Godot `virtual_keyboard_show`. 제안 GameTextInput. 확인: `WindowTextInputTest` Android 조합 케이스, `web-check` 의 `compositionend` 화면.
- **O-12 [30] 원장 경로 부하 측정(통합 담당이 돌린다)** — PostgreSQL · RESP 를 띄우고 `SW_SHOOTER3D_DB_PASSWORD` · `SW_SHOOTER3D_CACHE_PASSWORD` 를 둔 뒤 빌드 `Bin` 에서 `OnlineLoadBot --scenario=<저장소>/Source/Programs/OnlineLoadBot/Scenarios/ledger_mix.json --load=<저장소>/Source/Programs/OnlineLoadBot/Scenarios/ledger_mix.load.xml --local-server=0 --online-config=<저장소>/Source/Programs/OnlineLoadBot/LoadBot.online.json --store-config=<저장소>/Config/Server/Online/Shooter3D.external.json --report=<빌드>/Testing/Logs/OnlineLoadBot.ledgerMix.json` — 보고서의 `_server._conflictRetryBasisPoints` · `_busyBasisPoints` · `_droppedArrivalCount` 를 이 줄에 적고, 종료 코드 5 면 원장 감사(재화 총량 보존)가 깨진 것이다.
- **R-4 [31] 자원마다 전용 할당** — `MaterialRenderProxy.cpp:48` 인스턴스마다 상수 버퍼 · 커밋 자원 · `vkAllocateMemory`. 상용: 언리얼 `FD3D12FastConstantAllocator`, 유니티 SRP Batcher, Godot VMA. 제안 `RHIBufferSlice` + D3D12MA / VMA. 확인: 머티리얼 인스턴스 5000 의 버퍼 바이트 · Vulkan 할당 수.
- **RN-6 [34] GPUScene 부분 수집 · 업로드(고정 칸 · 프리리스트 · 칸 순 버퍼 · 구성원 고치기)** — 1~4 단계 끝(`9af8af22b3` · `ee1b3d50d8` · 2 단계 칸 순 발행 · 3 단계 `patchBatchMembership` · 4 단계 머티리얼 대기 칸만 다시 보기). 잴 것: 측정 장치 커밋 `31f2492dc8` 과 4 단계 커밋에서 각각 `ctest -R Scenario.empty.load.instancechurn` 를 돌려 `GT.GPUScene.build.collect` · `GT.GPUScene.build.batches` · `RT.GPUScene.slotInstances` p50 을 견준다(`GT.GPUScene.build.patchEnter` 가 0 이 아니어야 구성원 고치기를 탔다). 남은 한계: 배치 사이 여유 칸이 없어 드나들면 차례 목록(uint32)을 O(N) 다시 쓴다(GPU 는 바뀐 구간만) · 새 키는 다시 나눈다(그리기 띠 · 차례 순서를 지키며 배치를 끼울 자리가 없다) — 측정이 문턱을 넘으면 배치 용량(유니티 GRD)부터.
- **A-13 [36] 씬 스트리밍 — 측정만 남음**: `--cook-all` 뒤 `App -scenario=game/empty/automation/load/streamingcells.scenario.xml -unattended -cooked-content` 로 구간 `GT.Scene.service.SceneStreaming` 최대 프레임(셀 400 × 상자 2000, 짓기 예산 `_addToWorldBudgetMicroseconds` 2000 us). 셀 안 엔티티 단위 나눠 짓기 · 머리만 읽기 · 내비메시 타일 스트리밍까지 들어갔다(씬 쿠킹본 판 9, `.navmesh` 판 2 — 다시 쿠킹).
- **S-8 [37] 캐릭터 일괄 이동 측정** — `App -scenario=game/empty/automation/load/charactermove.scenario.xml -unattended -workers=N`(N = 1 · 4 · 8, 캐릭터 500)의 `GT.Physics.moveCharacters` 를 잰다(PO-3).
- **N-11 [43] 송신률 조절** — 코드 끝(`NetSendThrottle` AIMD, 계수 `Config/Engine/NetSendThrottle.json`). 통합 확인: `App --test --test-filter=NetSendThrottleTest.*,NetEndpointTest.*,ConfigFileSchemaTest.*,GameNetSessionFrameHooksTest.*` — 손실 20 % 에서 조절 연결의 `_resentMessageCount` 가 고정 버킷의 3/4 미만이고 속도가 하한(20000)에 머무는지, 손실 0 에서 상한으로 돌아오는지.
- **A-24 [47]** 리플렉션 묶음 TU 가 헤더 하나에 전체를 다시 파싱 — 1 절 5-4 리플렉션.
- **K-11 [50] 슈터 적 풀링 확인(통합)** — 코드는 섰다(`Component::resetToSpawnState` 풀 계약 · `PrefabPoolPolicy` · 종류마다 `_pool`). 통합 담당이 Tracy `GameModeFlush` 를 풀 전후로 재고, shooter3d 시나리오(`enemydeath` · `dismemberment` · `effectpool` · `autoplay`)를 한 번에 돌린다 — 다시 꺼낸 적이 래그돌 · 절단 · 무기 · 빙의 없이 서는지.

## 8. 다음 감사 범위

감사가 깊이 보지 못한 곳이다(결함 목록이 아니다).

- **RHI · 렌더러:** 백엔드별 PSO 생성 스레드 안전 · 전송 티켓 계약, WebGPU · GL CommandContext, Vulkan 장치 고르기, EGL · GLX 문맥, `FramePassExecutorPasses.cpp`, `RenderGraph::executeParallel` 배리어 묶음, 모프 · 정점 애니메이션 풀, 실시간 프로브, forward 파이프라인 XML.
- **직렬화 · 입력:** `SerializerUtil.cpp` · `XMLSerializer.cpp` · `DataInheritance.cpp` · `ResourceComposer.cpp`, ReflectionParser `CodeGenerator.cpp`, `InputMap` 평가와 고정 틱 사이 엣지 유실, `WaylandWindow.cpp`, UI 접근성 브리지.
- **온라인:** `OnlineCore/Server/HTTP`(HPACK · HTTP/2 동적 표 · 프레임 길이 상한), Apple · Google 영수증 검증기 본문, RESP 재연결 뒤 응답 짝, O-7 죽은 서버의 파티 티켓 풀기, 비교 후 쓰기(748bca0b0d), O-6 명단 캐시 기록(13d9d54758 · 6d8fc7620d).
- **게임플레이 · 에디터:** `ScheduleSystem.cpp`, BehaviorTree · Blackboard, Appearance, 장르 키트 14 개 내부, 임포터(Model · Texture · Vrm), Animation 패널, 프리팹 오버라이드 적용, 도킹 · 레이아웃.
- **실행으로만 확인되는 것:** 성능 항목 전부, 외부 API 동작에 기댄 항목(R-1 · P-5 · P-13 · O-15).

## 9. 코드 `shortcut:` 정리 (2026-11-18 전수 — NEXT 에 없던 조건부 할 일)

정본 모양이 있으면 크기와 무관하게 고친다. 정본이 없고 "규모가 커지면" 인 것만 문턱을 적고 남긴다. 영역마다 작업자 갈래(w-sc-*)로 진행.

- **렌더 · 셰이더(w-sc-render):** 변위 UV 이음매(`material_displacement.hlsli` 머리말 — 언리얼 Crack Free Displacement 처럼 메시 빌드가 지배 UV 인접 인덱스를 만들어야 한다), ClearCoat · Anisotropic 셰이딩 모델(`material_functions.hlsli:570 · 580`, `appearance_lit.hlsl:38 · 408`, `shading_model.hlsli:156`).
- **게임플레이 · 애니메이션 · 물리(w-sc-gameplay):** VFX 죽은 자리(`VFXComponent.h:45` — 살아 있는 수만 그리는 배치는 RN-6 의 `GPUSceneBuilder` 항목 수 변경 뒤), 프리팹 JSON 저장 칸 빼기(`PrefabAsset.cpp:336` — JSON 프리팹이 생길 때 JSON 값 차이 비교를 더한다).
- **에디터 · 온라인 · 도구(w-sc-misc):** MVVM 갈래(w-editorthread)와 겹쳐 남김 — 상태 문서 자동 저장(`EditorStateDocumentPanel.h:26`), 캐릭터 에디터 셋(`CharacterEditorPanel.h:190`, `BodySurfacePick.h:7`, `CharacterEditorLocalStore.h:42`), 관리 도구 마지막 대상만(`AdminToolSession.h:158`). 남은 것 — 시나리오 옛 `<Wait>`(`AutomationScenario.cpp:86` — 22 파일 123 곳, `<AdvanceSimulation>` 은 그리지 않아 골든이 바뀔 수 있어 시나리오마다 조건 대기 · 진행을 가르고 돌려 본다), 문턱만: 팩 쓰기 재읽기 해시(`PackWriter.cpp:226` — 슬롯 배치를 배포 쿠킹에 켤 때), 백엔드 교체 예산(`AutomationPerBackend.cpp:575` — 지금 최다 5 회 +75 초, 상한 540 초), 데스크톱 팩 변형(`AssetAndToolTargets.cmake:57` — 쿠킹은 대상 무관이 정한 설계, 데스크톱 팩 크기가 문제일 때), 스크립트(`test_history.py:284` — D4 남는 위험, `naming_words.py:107` — 판정 불변).

## 사용자에게 받을 것

영수증 · 푸시 · 외부 로그인의 실제 계정과 키(지금은 로컬 가짜 끝점 — `Config/Server/Online/Providers/`, `Scripts/net/online_local_run.py`). 새 의존성(인코더 라이브러리)의 라이선스. 공유 DDC 를 둘 자리(NAS · 클라우드 버킷)와 쓰기 권한 머신, 원본 전체를 Git LFS 로 넓히기(호스팅 용량 · 비용 — S1 `.gitattributes`)와 이력 다시 쓰기(`git lfs migrate`, 되돌리기 어렵다), 원본 별도 저장소, 밤새 도는 퍼징 · 여러 머신 샤드에 쓸 외부 CI 러너.

## 꼭 알아야 할 결정

- **리소스:** 무료 · 공개 라이선스만(CC0 우선, MIT 등은 고지와 함께) — 묶음 페이지 라이선스를 확인해 credits 에 URL · 날짜. 에이전트가 직접 그리지 않는다. 유료 생성 API(Agent Sprite Forge)는 들이지 않는다. Quaternius 는 2026-08 QAL(원본 재배포 금지)로 바뀌어 새 묶음은 그 페이지가 CC0 일 때만. 원본은 Git LFS, 받은 팩은 `/AssetLibrary/`(무시), 쓰는 것만 `common/models_raw · textures_raw/<출처>/<묶음>/`.
- 범용 스크립팅 언어는 들이지 않는다([판단](DesignBacklog.md#핫-리로드와-스크립팅-언어-판단)). 런타임 아틀라스는 하지 않는다. 메모리 풀은 측정 뒤(출하 할당기 선택은 감사 C-6 의 `SW_MEMORY_BACKEND`), GC 는 권하지 않는다.
- 웹은 wasm32(memory64 는 Safari 미지원). 교차 빌드는 호스트 빌드(`SW_HOST_BUILD_DIR`, 기본 `build/Ninja-Release`)의 ReflectionParser · App 을 쓴다. 교차 프리셋은 vcpkg 설치 폴더를 따로 쓴다.
- 최상위 `Mods/` 는 `Test/Mods/`. App 의 `Dev/` 는 남긴다.
- 줄끝: `.gitattributes` 가 `* text=auto eol=lf`.
