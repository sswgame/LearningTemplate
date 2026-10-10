# 작업 백로그 — 남은 일

> 여러 PC · 여러 세션이 함께 쓰는 **할 일 목록**입니다. 아직 열린 일과, 그 일을 시작하기 전에 알아야 할 함정만 둡니다.
>
> - 일을 끝내면 그 항목을 같은 커밋에서 **지웁니다.** 남길 교훈은 그 영역 README 의 "함정 · 계약" 절에, 정한 방향과 기각한 안은
>   [결정 기록](09_Decisions.md) 에, 검증 · 측정 방법은 [검증과 측정](08_Verification.md) 에 한두 줄로 옮깁니다. 무엇을 고쳤는지의 사연은 커밋 메시지에 씁니다.
> - 새로 찾은 결함과 미룬 일은 1절의 해당 영역에, 결정을 기다리는 질문은 1-11 에 적습니다.
> - 여러 PC 에서 고치는 문서라 작업 중에 pull 과 충돌이 날 수 있습니다. 커밋 전에 받아서 합칩니다.
> - 지나간 일의 이력은 `git log` 에 있습니다. 2026-10-03 까지의 전체 이력이 든 옛 백로그(약 19,400 줄, "최근에 끝낸 일" 포함)는 커밋 `7ce95fc8` 의
>   `docs/06_Backlog.md` 입니다 — `git show 7ce95fc8:docs/06_Backlog.md`. 코드 주석과 커밋 메시지가 옛 날짜 항목("2026-09-17 참고" 등)을 가리키면 거기서 찾습니다.

---

## 1. 남은 일

영역별로 묶었다. 영역 안에서는 위에 있을수록 먼저 볼 것이다. 줄 번호는 2026-10-03 기준이라 어긋날 수 있다 — 함수 이름으로 찾는다.
"확인 필요" 가 붙은 항목은 열려 있는지부터 확인하고 시작한다.

- **엔진 분할 — 계획은 [docs/plans/EnginePartition.md](plans/EnginePartition.md).** `Engine.dll` 을 티어 경계로 나누기 전에 의존 방향의 약한 고리를 푼다. 단위를 끝내면 계획 문서에서 지운다.
- **GameFramework 폴더 재배치 — 계획은 [docs/plans/GameFrameworkLayout.md](plans/GameFrameworkLayout.md).** `Base` 24 개 폴더를 층으로 묶고, 평평한 폴더와 키트 안을 나눈다. 엔진 분할 0-3 뒤에 한다. 단위를 끝내면 계획 문서에서 지운다.
- **이름 정리 — 계획은 [docs/plans/NamingPass.md](plans/NamingPass.md).** 이름 점검 제안을 채택 · 기각 · 문서화로 가린 결과와 순서. 린트 · 어휘표가 먼저, 기계적 치환은 약어 코드모드 틀에.
- **빌드 속도 — 계획은 [docs/plans/BuildSpeed.md](plans/BuildSpeed.md).** 사용자 결정(2026-10-10): 1 ~ 6 단계까지 하고 7 ~ 9 는 필요할 때만. 컴파일이 CPU 시간의 85 %, 헤더 파싱이 한 TU 의 57 %, PCH 가 타깃마다 86 개. 1 단계(시험 빌드 분리)와 0 단계 도구(`RunBuildBaseline` · `RunIncludeCost`)는 끝났고, 기준선 측정은 다른 빌드가 없을 때 남았다.

### 1-1. 직렬화 · 리플렉션

- **XML 문자열 속성은 읽을 때 끝 공백(줄바꿈)을 잘라 왕복이 고정점이 아니다**(2026-10-06, `SerializationRoundTripTest` 가 찾음). `MissingComponent::_originalText`
  가 줄바꿈으로 끝나 다시 쓰면 `&#10;` 이 빠진다 — 지금은 오브젝트 상태 직렬화기가 원문을 그대로 다시 써서 데이터는 잃지 않고, 시험은 이 타입을 뺀다.
  끝 공백이 뜻을 갖는 문자열 칸이 생기면 XML 읽기의 자르기를 속성 값에서 걷어낸다.

- **JSON 소유 포인터 원소가 `{ "타입이름": {...} }` 꼴이 아니면 말없이 건너뛴다**(`JSONSerializerInternal::ContainerReader::readOwnedPointer`). 실패로 알리면 `_listComponent`
  칸 전체가 실패하므로 그 원소만 orphan 으로 남기는 길이 필요하다(XML 은 태그가 곧 타입이라 이 모양이 없다).

### 1-2. 오브젝트 · 씬 · 틱 · 물리

- **중첩 프리팹이 정식으로 없다.** 프리팹은 GameObject 하나(컴포넌트 트리)라 메시 · 이펙트 여럿을 담을 수는 있지만, 프리팹 안에 다른 프리팹의
  인스턴스를 오버라이드와 함께 두는 형식은 없다 — 지금은 컴포넌트가 로드 뒤 스폰하는 우회뿐이다(순환 스폰 방어는 있다, `PrefabTest.CircularReferenceSpawnProtection`).
  아이템 부품 · 탈것 · 건물처럼 조립하는 것이 늘면 필요하다: 상태 안의 프리팹 인스턴스 노드(경로 + 오버라이드), 원본 프리팹을 고치면 모든 상위가
  따라감, 순환은 로드 오류, 쿠킹은 펼치거나 참조 유지(로드 시간으로 고름), `collectReferencedPrefabPaths` 가 프리로드 수집에 그대로 쓰임.

- **강체 물리의 다음 조각.** (1) 볼록 껍질 · 삼각 메시 셰이프를 `.mesh` 에서 채우는 길이 없다 — 지금은 셰이프 서술자에 점 · 인덱스를 직접 적는다
  (`MeshCache` 의 CPU 정점이 필요하다) (3) 에디터에 물리 컴포넌트의 셰이프 시각화 · 기즈모가 없다(`gv_physicsDebugDraw` 가 씬 뷰의 디버그 선으로만 그린다)
  (4) Box2D 는 한 스레드로 돈다 — 2D 바디가 수천이 되면 전용 워커를 붙인다 (5) 3D 질의는 가장 가까운 것 하나 · 겹침 목록뿐이다(레이의 모든 닿음 ·
  스윕 다중 닿음이 필요해지면 더한다) (6) 겹침 월드(`PhysicsWorld` · `BoxCollider2DComponent`)는 강체 씬과 따로 돈다 — 키트의 투사체 · 근접 판정이
  Box2D 센서로 옮겨 가면 겹침 월드를 걷어낸다.
- **3D 내비메시(Recast & Detour, `Source/Engine/Navigation/README.md`)의 남은 것.** 베이크(쿠킹 · 런타임) · 경로 · 레이캐스트 · 군중 · 장애물 · 파괴 재베이크 ·
  디버그 선 · Shooter3D 적은 들어갔다. (1) 오프 메시 링크(사다리 · 점프 · 문)와 그 애니메이션 (2) 지형 높이장 · 식생을 베이크 기하로(`INavGeometrySource`)
  (3) 장애물이 많고 자주 움직이면 TileCache(압축 층) — 지금은 타일을 통째로 다시 베이크 (4) 에디터의 내비메시 보기 · 베이크 버튼 (5) 행동 트리 이동 노드가
  `INavMover` 를 쓰게(격자 · 내비메시 공통) (6) 다른 시험 게임(AbilityArena 등)의 적도 내비메시로.

### 1-3. 그래픽스 · RHI · 셰이더

- **2D 의 남은 것(2026-10-04 twod-basics)** — (1) 파이프라인을 게임이 데이터로 고르는 자리(지금은 `-gv_renderPipeline` 뿐 — 게임 프리셋 · gamesettings 에)
  (2) 픽셀 퍼펙트의 Upscale Render Texture(기준 해상도 타깃 + 정수 업스케일 패스) (3) 2D 빛 텍스처 · 자유 모양 빛 · 부드러운 그림자 · 빛 블렌드 스타일
  (4) 타일맵 청크(한 레이어 65536 칸)와 편집 중 미리보기(지금은 플레이 때 배치를 만든다), 에디터 칸에 아틀라스 그림 (5) 테두리를 픽셀로 적는 9-슬라이스.
- **방향광 그림자의 남은 것(shadow-fix)** — (1) 볼륨 맞춤(`_shadowViewDistance`)을 쓰는 게임은 ThemePark 하나다 — 직교 리그를 쓰는 NileCity · StarSkirmish ·
  HarvestValley 와 envshowcase(고정 150 m)를 옮겨 볼 것(스크린샷 전후). 바이어스가 텍셀 단위로 줄어 다른 여섯 게임의 여드름은 아직 눈으로 안 봤다(게임별 빌드). (2) 원근 카메라는
  캐스케이드 하나라 멀리 갈수록 텍셀이 크다 — CSM(분할 2~4, 경계 섞기). (3) 추가 뷰(CCTV · 분할 화면)도 주 시점 볼륨을 쓴다 — 다른 곳을 보는 뷰는 그림자가 빠진다
  (뷰별 맞춤이면 뷰별 그림자 패스 상수). (4) 깊이 첨부 readback 이 없어 `-gv_screenshotAttachment=ShadowMap` 이 실패한다(`readbackTexture2D 실패`).
  (5) 카메라 회전 중 2 m 양자화 경계에서 가장자리가 한 번씩 튈 수 있다 — 거슬리면 경계 구(회전 불변)로.
- **점광 · 스폿 그림자** — RHI 텍스처 차원(배열 · 큐브, 면 단위 타깃 · 올리기 · 읽기)은 있다. 남은 것: 그림자 패스 다중 뷰(면 여섯) → 셰이더 쪽(DX12 · Vulkan
  큐브 · 배열 bindless 테이블, DX11 · GL TextureCube 슬롯) + `swSampleShadowAtWorld`. 3 단계 전에 큐브 대신 2D 아틀라스(Unity URP · Godot — RHI 변경 없음)로 갈지 먼저 정한다.
- **컷 준비(프리웜)의 선행 조건 셋**(1-6 카메라 항목의 5 단계가 기다린다) — LOD 시스템이 없다, 밉 단위 텍스처 스트리밍이 없다(`AssetStreamingQueue` 는 에셋
  단위 비동기 읽기), PSO 를 미리 만드는 창구가 없다(DX12 PSO 생성 히치).
- **환경(지형 · 식생 · 물, `Engine/Environment`)의 남은 것** — 들어간 것과 계약은 [Environment/README.md](../Source/Engine/Environment/README.md).
  (1) Jolt 가 들어오면 지형 → `HeightFieldShape`(`TerrainHeightfield::getHeightSamples` · 구멍 칸) · 부력(`WaterBodyComponent::computeSurfaceHeight` · `isUnderwater`, 시간은
  `getWaveTime`)을 잇는다. (2) 강 경로가 점 목록이다 — 스플라인 컴포넌트가 들어오면 그것을 경로로 받는다. (3) 하늘 · 시간 · 높이 안개 + 물속 안개 패스(값은
  `WaterBodyComponent::findUnderwaterFog` 가 이미 준다) — 다중 뷰 병합 뒤. (4) 물의 굴절 · 화면 공간 두께는 반투명 패스가 장면 색 사본 · 장면 깊이를 입력으로 받는
  계약이 있어야 한다(지금은 지형 깊이를 정점에 굽는다). (5) 흔드는 식생의 그림자는 흔들리지 않는다(`shadowdepth.hlsl` 이 머티리얼 정점 변형을 모른다 — 풀은 그림자를 끔).
  (6) 지형 LOD 교체 프레임 — `TerrainBenchTest.LODSweepWorstFrame`(Release, 쇼케이스 지형을 600 프레임 동안 가로지름, 3 회): 교체 프레임 111 개의
  updateLODs(청크 메시) p50 0.1 ms · 최악 4.0~5.1 ms, GPUScene 수집 p50 0.95 ms · 최악 5.0~5.2 ms — 기준(GT 2 ms)을 넘는다. 메시 집합이 바뀌어 RT 는 정점 풀을
  통째로 다시 만든다(`RT.GPUScene.vertexPool`, 벤치에는 없다 — App 표로). LOD 메시를 미리 만들어 두거나 청크 정점을 풀에서 부분 갱신한다. 지오모프(LOD 튐) · 레이어 다섯 이상(두 번째 스플랫 — 머티리얼 텍스처 칸이 넷이다) · 에디터 칠하기 도구가 없다.
- **툰 머티리얼(`toon.hlsl`, MToon 1.0 체계)의 남은 것** — 노멀 맵(정점에 탄젠트가 없다) · UV 스크롤 애니메이션 · 셰이딩 시프트 / 림 곱 / 외곽선 두께 텍스처(머티리얼 텍스처 칸이 넷이라 기본 · 그림자 · 발광 · 맷캡만 받는다) · 디퍼드의 계단 셰이딩(G버퍼는 표면만 적어 램버트로 칠해진다) · 그림자 패스의 알파 컷오프
  (`shadowdepth.hlsl` 은 픽셀 스테이지가 없어 머리카락 카드가 사각형 그림자를 드리운다 — 모든 컷오프 머티리얼이 같다).
- **VRM 임포트의 남은 것** — 머티리얼(MToon) · 구간 메시 · 스켈레톤만 옮긴다. 표정(모프 타깃 · `blendShapeMaster`) · 스프링 본(`secondaryAnimation`) · humanoid 본 표 · firstPerson 은 읽지 않는다(0.x · 1.0 모두). 본 메시(`<이름>.mesh`)는 구간들을 다시 합친 것이라 디스크에 두 벌이다(VRoid 34k 삼각형 7 MB × 2) — 엔진 메시에 머티리얼 구간이 생기면 하나로 줄인다. VRoid 텍스처는 BC3 이다(Debug DirectXTex 의 BC7 은 512×256 한 장도 10 분이 넘는다 — Release 로 BC7 임포트를 다시 할 것). `ModelImporterTest.SkinnedModelImportsSkeletonClipsAndAttachments` 는 Debug 에서 혼자 31 초라 EditorTest 한도를 30 → 120 초로 올려 두었다 — 임포트를 줄이면 되돌린다.
- **캔버스(화면 2D)의 남은 것(runtime-ui 4-4)** — (1) 화면 사각형 뷰(PiP · 분할 화면)는 주 시점의 Canvas 뒤에 그려져 UI 를 덮는다 — 뷰를 다 그린 뒤 주 출력에
  한 번 그리게 옮긴다(언리얼은 모든 장면 뷰 뒤 Slate). (2) 렌더 텍스처 대상(월드 공간 UI)은 텍스처가 처음 만들어질 때의 크기로 그린다 — 위젯이 크기를
  나중에 바꾸면 텍스처를 다시 만들지 않는다(`TextureCache::declareRenderTarget` 은 이미 만든 것을 바꾸지 않는다).
- **반해상도 후처리** — 첨부별 `_resolutionDivisor`(1 · 2 · 4)는 있다. 남은 것:
  `deferredpipeline.xml` 블룸을 반해상도로 나누기, Release 로 p50 · p99 측정.
- **머티리얼 폴백 원소(`FrameRenderer::ensureMaterialFallbackBuffers` — 0 바이트)의 텍스처 인덱스 칸이 0 이다** — Vulkan 은 bindless 0 번 실제 텍스처를 읽는다(DX12 는 0 번을 null SRV 로 비웠다, 머티리얼은 `MaterialPacking` 이 `kInvalidIndex` 를 넣는다). 원소 레이아웃(`ShaderBindingSlot`)은 stride 만, 리플렉션 원소 칸은 `uint` 뿐이라 텍스처 칸을 가를 수 없다 — 셰이더 쪽 표식(텍스처 칸 매크로가 리플렉션에 남기는 이름 규칙)으로 칸을 알아 `kInvalidIndex` 로 채우거나, Vulkan 도 0 번을 null 서술자로 비운다.
- **월드 좌표 UV(트라이플래너) 프로토타입 머티리얼** — `engine/materials/prototypegrid_*` 는 메시 UV 를 따라 큐브 크기마다 칸이 늘어난다. 언리얼 `WorldGridMaterial` 처럼 월드 1 m 격자로 보이려면 셰이더 분기(월드 위치로 샘플)가 필요하다.

### 1-4. 에디터

- **에디터 보강 — 계획은 [docs/plans/EditorPlus.md](plans/EditorPlus.md).** 다음 세션이 할 일이다(사용자 결정, 2026-10-07). 확장 지점(EditorModule SHARED · 확장 모듈),
  환경설정 · 단축키 · 모듈 창, 인스펙터 다중 편집 · 콘텐츠 브라우저 역색인, 보기 모드 · 스크린샷 · RenderDoc · 타임라인 · assert 대화상자 · bugit · Test Runner,
  커브 · 맵 검사 · 노드 그래프 틀 · 패키징, 아이콘(R3 · R4 · R5 · R9), 패널 점검의 부족한 점(N1 ~ N12)을 단위마다 "확인 = 에디터 시나리오" 와 함께 적었다.
  옛 1-4 의 "에디터 · 개발 편의 기능" 가운데 C 확장 지점 · F 카탈로그 편집기 · G 프로파일링 · 캡처의 남은 것 · H 품질 · 작업 흐름, 그리고 설정 브라우저 패널은 그 문서로 옮겼다.
  단위를 끝내면 계획 문서에서 지우고, 다 끝나면 이 항목과 계획 문서를 지운다(남은 로드맵 줄은 여기로).
- **에디터 문서(`Source/Editor/README.md` 등)는 에디터 보강 뒤 새 문체로 다시 쓴다** — 5 차 문서 다시 쓰기에서 일부러 뺐다(보강하면서 패널 · 확장 지점이 바뀐다). 틀은 [문서 쓰기 지침](10_WritingDocs.md).
- **UI 미리보기 패널의 남은 것(runtime-ui 8-5 뒤, `Editor/Panels/UIPreviewPanel`).** (1) 언어 고르기(의사 문화권 `qps-ploc` · `qps-plocm` 포함) — 문화권이 전역이라
  미리보기 화면에만 거는 길(화면별 `LocalizationManager` 출처)이 필요하다. (2) 고른 위젯의 PROPERTY 를 인스펙터로 고치고 `UIDocumentWriter` 로 저장(되돌리기 —
  `CommandStack`) — 지금은 이름 · 사각형만 보인다. (3) 미리보기 안 입력 흉내(마우스 · 탐색 방향). (4) 콘텐츠 브라우저에서 `*.ui.xml` 두 번 누르면 이 패널로 —
  에셋 종류 `UiDocument` 의 열기 동작. (5) 애니메이션 미리 보기(재생 · 시간 막대 — 미리보기 화면은 Open 을 틀지 않아 문서 값 그대로다). (6) UI 문서 디자이너(팔레트 → 끌어 놓기 · 슬롯 손잡이) — 미리보기 패널 위에.
- **AbilityArena 자동 전투 실행은 종료 보고에 `Scene` 태그 232 B(1 블록)가 남는다**(2026-10-06, `Ninja-Debug-AbilityArena` 네 백엔드 모두 `-gv_arenaAutoPlay=1
  -gv_profileFrames=300`). 다른 게임 여섯 · Empty 는 0. 같은 진단(기준선 뒤 상세 추적 · 종료 직전 `getTopCallStacks`)으로 자리를 찾는다.
- **에디터를 켠 실행은 종료 보고에 `Editor` 태그 256 B(1 블록)가 남는다**(2026-10-06, `App.exe -dx12 -EnableEditor -gv_profileFrames=5`). 에디터 없는 실행은 0 이고
  `AppSmokeTest.ShutdownReturnsEveryTagToTheBaseline` 이 지킨다. 프로세스 정적 저장소가 기동 뒤 자란 몫일 것 — 기준선 직후 `setDetailedTrackingEnabled( true )` ·
  종료 보고 직전 `getTopCallStacks( LiveBytes )` 임시 진단으로 자리를 찾아 종료 끝에서 놓는다.

- **콘솔 · 치트의 남은 것(옛 "에디터 · 개발 편의 기능" 의 D)** — 게임 · 키트의 치트 명령(무적 · 아이템 주기 · 돈 …)을 각 게임 · 킷에 `SW_DEV_COMMAND` 로 단다(등록부 · 콘솔 ·
  엔진 명령은 들어갔다 — `Source/Engine/README.md` "개발 콘솔"). 리눅스 오버레이(`X11DevConsoleWindow`)는 실기로 띄워 보지 않았다.
  게임 창 콘솔은 셸 InputMap 액션 + `InputManager` 키보드 포커스로 받는다(`Input/DevConsoleController`). 남은 것: 패드는 포커스 밖이라 콘솔이 열린 동안
  패드 A · B · 십자키가 게임에도 간다(shooter3d 는 패드 `Back` 이 `CycleCamera` 와 겹친다), 플레이어별 재배치(`InputMap::loadUserBindings`)가 셸 맵에 걸려
  있지 않다, X11 그리기는 `XDrawString`(Latin-1)이라 한글이 깨진다.

- **패널 점검(2026-10-07) 결함 중 실행으로 판정할 것** — 코드로만 고쳤거나 원인 후보만 있는 것이다. 다음 검증 세션이 에디터를 띄워 가른다.
  (1) D21 Dialogue Graph 캔버스가 처음 열 때 일부만 덮는다 — 가설 고침(`30f18a1fc`: 캔버스 크기가 앞 프레임과 같을 때만 내용 맞추기). 열기 · Reload 직후
  캔버스 배경이 패널을 다 덮는지 스크린샷으로 본다. 그대로면 다음 후보는 imgui-node-editor 의 `FinishNavigation` 이 옛 보기 사각형을 되돌리는 것이다.
  (2) D26 자체 시험 `input.hierarchySearchTyping` 은 간헐로 진다(2026-10-10 Debug 세 번 중 두 번). 덮는 창 가설은 기각 — 진 판에서도 검색 칸이 활성(`active` = `field`,
  `wantTextInput=1`)인데 입력한 글자가 칸에 닿지 않는다. 다음은 글자 이벤트를 넣는 프레임과 칸이 활성이 되는 프레임의 순서(`EditorSelfTestInput`)를 본다.
  (3) D24 고정 픽셀에 배율을 곱한 64 곳(`EditorThemeUtil::getDpiScale`)은 배율 1.5 · 2 로 띄워 넘치거나 겹치는 곳이 없는지 본다.
  D1(뷰포트 클릭) · D2(기즈모)는 실행으로 확인했다(`20b7b5df5` · `6777ef3ce`). D4(Play 전의 편집 내역이 Stop 뒤에 남는다)는 시나리오 `undosurvivesplay` 가 네 백엔드에서 통과한다(`layout.reset` 으로 기본 배치에서 시작).
- **입력 흉내 창구(`EditorSelfTestInput` · `EditorSelfTestMarks::note`)로 아직 안 덮은 것** — 그래프 패널 ↔ 저장 커맨드 배선, 인스펙터 콤보 직접 편집, 드래그 드롭.

### 1-5. 핫 리로드 · 모듈

- **바깥 빌드(터미널 · IDE)의 리로드 트리거는 여전히 mtime 디바운스뿐이다** — 에디터가 시킨 빌드는 성공 뒤에만 올린다(`LiveReloadManager::notifyBuildStarted/Finished`).
  바깥 빌드도 "빌드 성공" 신호(ninja 종료 · 스탬프 파일)를 받으려면 빌드 쪽 협조가 필요하다.
- **모듈이 렌더 패스를 등록하는 창구가 없다.** `FramePassContext` · 커맨드 리스트 · 트랜지언트 풀을 모듈 경계 밖으로 내야 하고, 그것은 RT 안전 계약까지
  내보내는 일이다. 쓰는 모듈이 생기면 그때.

### 1-6. 게임프레임워크 · 킷 · 게임

- **사용자 설정(옵션 메뉴 백엔드)의 남은 것** — 백엔드 · 바인딩 API 는 `Source/Engine/UserSettings/README.md`, 메뉴는 `Engine/UI/Screen/OptionsMenuScreen`.
  (2) 값만 있고 읽는 곳이 없는 대상: `gv_renderScale`(업스케일 패스) · 시야 거리 · 후처리 ·
  텍스처 · 이펙트 품질 · 모션 블러 · `gv_colorVisionMode` 의 톤맵 쪽(톤맵에 상수 버퍼가 없어 미뤘다 — 함수는 `colorvision.hlsli`, UI 캔버스는 이미 쓴다),
  카메라 `gv_cameraFieldOfView` · `gv_cameraShakeScale` · `gv_cameraHeadBob`(cam-views 가 읽을 자리). (4) 해상도 선택지를 모니터 모드에서(선택지 공급자) · GPU 사양 조회(RHI 어댑터 · 전용 메모리)로 품질 자동 선택.
  (5) 게임 스키마에 키 바인딩 설정 — 시험 게임 일곱이 모두 입력 맵(`data/<게임>.input.xml`)을 쓴다(그 액션부터). (6) X11 `setDisplayMode`(EWMH 전체 화면)는 리눅스 실기 미확인.
- **패드 스틱 시점 · 가상 커서(a4-input 이 남긴 것).** 입력 맵에 바인딩마다의 배율 · 프레임 시간 곱(언리얼 Enhanced Input 의 Scale · Scale By Delta Time 모디파이어)이
  없어 오른쪽 스틱을 `Look` · `Camera.Look`(마우스 이동량 = 픽셀/프레임)에 묶으면 프레임률을 따르는 느린 값이 된다 — 그래서 시점 액션은 아직 마우스만이다.
  명령형 게임(StarSkirmish · NileCity)의 패드 A · B 는 커서 자리를 쓰는데 커서를 패드로 옮기는 가상 커서(언리얼 CommonUI 의 아날로그 커서)가 없다.
- **GameFramework 구조 정리(2026-10-04 리뷰, 사용자 승인).** 남은 것 —
  - 중간: Overworld `TileMap` 의 칸 손셈(`indexOf` · `isInBounds` — 크기는 파일 스키마 `TileMapXMLData` 가 든다) · NetConnection 메시지 버퍼 재사용 ·
    기반의 같은 손셈(NavGrid 4 · FlowField 4 · GridInventory 3 · PlatformTileMap 2 · GridReachability `% 너비` 1, 클래스마다 자기 `isInside` · `computeIndex` ·
    `toIndex` 사본 — NavGrid · GridReachability · ElementGrid)도 `GridTopology` 멤버로 · 게임 손셈(HarvestValley `FarmCropComponent` · `FarmSoilComponent`,
    NileCity `NileDirectorComponent` 의 `index % getWidth()`)은 키트가 `getTopology()` 를 열면 같이.
- **키트 조립 — 남은 것(2026-10-06).** 틀 · 키트 전부 · 조립 시험 둘(`KitCompositionTest` — Farming + CreatureLife, RTS + CityBuilder)과 `MeadowVillage` 는
  들어갔다(규칙: `Source/GameFramework/Kits/README.md` "키트 여럿을 한 게임에"). 남은 것 — RTS 가 땅 리비전마다 땅 격자 전체를 다시 칠하는 비용(64 × 64 에서 재고
  결정, 1-8), 둘째 조립(RTS + City)의 시험 게임(필요해지면 MeadowVillage 모양). 땅을 쓰는 게임은 아직 없다 — 격자 키트를 섞는 게임이 생기면 `GameStateSettings::_land*` 와 키트의 `bindLand` 를 건다(SRPG 는 전투 끝에
  `releaseLand`, 공원 놀이기구 발자국은 상태 바이트에 싣지 않는다 — 지은 칸은 땅이 든다).

- **카메라 — 프리셋 데이터 · 블렌드 · 시퀀서(사용자 승인 로드맵).** 1~3 단계(프리셋 XML · 블렌드 · 디렉터, 모드(직교 · 궤도 · 따라가기 · 1인칭 · 3인칭 ·
  CCTV) · 입력 · 카메라 매니저(뷰 타깃 블렌드), 흔들림 · 제약 · 프레이밍 · 스프링 암)와 4 단계(다중 뷰 렌더 · 컷 프레임 신호 · 초상화 굽기)는 들어갔다
  (`Source/GameFramework/README.md` "카메라" 절, `Source/Engine/Renderer/README.md` "다중 뷰"). 남은 것 —
  - 2 단계에서 남은 것: 직교 리그 네 게임(ThemePark · Harvest · Nile · StarSkirmish)은 리그 값이 곧 데이터다(모드 · 블렌드는 공유) — 리그를 지우고 디렉터 + 프리셋
    XML 로 옮기려면 게임 디렉터의 `setViewOverride` · `findGroundPoint` 를 디렉터 창구로 바꿔야 한다. 프레이밍의 가로 존은 16:9 로 센다(모드가 화면 비율을 모른다).
    2D(XY 평면) 카메라는 디렉터 밖의 `Follow2DCameraComponent` 다 — 2D 게임이 프리셋 · 블렌드를 원하면 디렉터 모드에 "XY 평면 따라가기" 를 더하고 그 컴포넌트를 지운다.
  - 4 단계에서 남은 것: 예산 · 보임 판정이 거칠다 — 보임은 "지정한 오브젝트가 주 카메라 절두체 안" 하나(가려짐 · 화면 크기 안 봄), 예산은 프레임당 뷰 수
    (`gv_renderViewBudget`)지 시간이 아니다. 화면 사각형 뷰는 에디터 GameView(ImGui 이미지)에서 검증하지 않았다.
  - 5 단계(시퀀서): 값 커브 트랙(시야각 · 초점 · 노출 — 블렌드 곡선과 같은 보간 함수), 카메라 컷 트랙(구간마다 프리셋 · 카메라, 프리셋의 블렌드로 전환 — 언리얼
    Camera Cut Track), 흔들림 트랙, 게임 ↔ 시네마틱 블렌드(시퀀스 시작 · 끝). 지금 시퀀서는 Clip(트랜스폼 보간) · Event 두 종류뿐이다.
    - 컷 준비(프리웜) — 컷 순간 LOD · 텍스처 · 셰이더가 바뀌는 게 보이지 않게. 컷 트랙은 다음 컷을 미리 안다 → 컷 N 초(또는 프레임) 전에 카메라 매니저에
      "곧 볼 시점(대기 뷰)" 을 등록한다(컷마다 · 시퀀스마다 프리롤 값, 언리얼 Sequencer 의 preroll frames). 게임 쪽 뷰 타깃 전환도 같은 창구를 쓴다.
      대기 뷰를 듣는 쪽(시스템이 생기면 붙는다): LOD 선택은 대기 뷰 기준 LOD 도 구해 컷 첫 프레임부터 그 LOD(컷을 넘는 LOD 크로스페이드는 끈다), 스트리밍은
      대기 뷰에서 보일 메시 · 텍스처 밉을 먼저 요청(언리얼 streaming source · AddViewLocation), 셰이더 · PSO 는 대기 뷰 절두체 안 머티리얼의 PSO 를 미리 만든다,
      그림자는 대기 뷰 기준 캐스케이드를 미리 맞춘다. 컷 프레임 신호(`RenderViewSettings::_bCut`)는 TAA 기록만 리셋한다 — 렌더러에 모션 벡터 · 자동 노출 ·
      모션 블러가 아직 없어서다. 그것들이 생기면 같은 신호로 0 · 즉시 맞춤 · 끔을 붙인다. 다중 뷰 렌더(4 단계)로 컷 1~2 프레임 전에
      대기 뷰를 저해상도로 숨겨 그려 업로드 · PSO 를 강제로 데운다. 선행 조건은 1-3 에 있다(LOD · 밉 스트리밍 · PSO 미리 만들기).
  - 6 단계: 렌즈 물리값(초점거리 · 센서 · 초점 거리 · 조리개 · 렌즈 시프트 · 더치) + 피사계 심도 패스(렌더러에 없다), 카메라별 후처리 덮어쓰기(노출 · 블룸 ·
    비네트 · 색 보정 · 레터박스 — 지금은 카메라별로 덮을 길이 없다), FOV kick · 줌 펀치, 돌리 레일(스플라인 — Spline Dolly · Rig Rail).
  - 1 단계에서 미룬 것: 디렉터의 켠 프리셋 · 블렌드 진행은 저장하지 않는다(다시 읽으면 시작 프리셋부터). 블렌드 도중 재활성은 섞인 포즈를 **고정**해 출발한다 —
    Cinemachine 처럼 나가는 블렌드를 살려 둔 채 겹쳐 섞으려면 블렌드 스택이 필요하다.

- **서드파티 — Jolt(3D 물리) · Box2D(2D 물리) · ACL(애니메이션 압축)(2026-10-04 사용자 결정), Recast · Tracy(2026-10-05 추가).** 모두 MIT · vcpkg 에 있다.
  물리 둘은 감쌌다(`IPhysicsScene3D` · `IPhysicsScene2D`, `Source/Engine/Physics/README.md`) — 경계는 `CheckThirdPartyIsolation.py` 가 지킨다(ACL 도 같은 표에 있다).
  ACL 코덱은 `Engine/Animation/Codec/ACL`(쿠킹 때 압축 → 코덱 id + 불투명 블롭). vcpkg 를 바꿀 때는 **다른 워크트리가 빌드 중이
  아닐 때** — 설치 폴더를 나눠 써서, 옛 매니페스트의 워크트리가 configure 하면 새 패키지를 지운다. Jolt 소프트 바디(천 · 헤어 카드)는 아직 감싸지 않았다.
  2026-10-05 사용자 결정으로 **Recast & Detour**(zlib, 정적 — `RecastNavigation::Recast` · `Detour` · `DetourCrowd` · `DetourTileCache`)와
  **Tracy**(BSD-3, 클라이언트만 · 기능 끔 — `Tracy::TracyClient`, Windows 는 공유 TracyClient.dll)를 vcpkg 로 들였다(`ThirdParty/{recastnavigation,tracy}`).
  Tracy 는 clang-cl 트리플릿의 C++14 기본값에 서지 못해 C++17 오버레이 포트(`ThirdParty/tracy/vcpkg-port/tracy`)를 둔다. Tracy 는 엔진 프로파일러의 두 번째
  출력으로 감쌌다(`Source/Engine/Profiling/README.md` — 헤더 경계는 같은 게이트, Shipping 은 링크하지 않는다). 뷰어(tracy-profiler.exe)는 저장소에 넣지 않는다.
  2026-10-06 **OpenSSL**(Apache-2.0 — 감싼 폴더 `GameFramework/Base/Online/Security/OpenSSL`, 같은 게이트) · **SQLite**(퍼블릭 도메인) · **libpq**(PostgreSQL License, `openssl` 기능만)를
  들였다 — Windows 는 지금 트리플릿대로 DLL. OpenSSL 은 네트워크 보안, SQLite 는 `GF_SQLStore`, libpq 는 `GF_Server_SQLStore` 가 쓴다(링크도 그 CMakeLists 에서만).
- **애니메이션(로드맵).** 지금 있는 것은 `Source/Engine/Animation/README.md`(임포트 · 코덱 · 재생 · 상태 기계 · AnimationSystem · GPU 스키닝 · 2D/3D 공용 재생).
  알림 디스패치(구간 알림 · 처리기 등록부 · `*.notifies.xml`)는 `Source/Engine/Character/README.md`.
  남은 것 — ① 그래프의 블렌드 스페이스 노드(지금 `BlendSpace` 는 행렬 하나라
  포즈 블렌드 스페이스로 다시 짓는다) · 그래프에 레이어 · 동기 그룹을 데이터로(지금 레이어는 `addLayer` 코드) · 에디터 그래프 패널이 조건 · 블렌드를 편집
  ② 후처리 리그 — 들어갔다(`PoseModifierComponent`, `Source/Engine/Animation/README.md` "후처리 리그" 절 · `Source/Engine/Character/README.md`). 남은 것: 시퀀서 트랙이 `setSlotWeight` 를 쓰기(칸은 있다),
  해석된 소켓 표의 표면 기준 소켓 체형 보정을 리그 대상에도, 에디터 리그 패널(노드 목록 · 대상 · 기즈모) ③ 애니메이션 LOD(가시성 · URO · 보간 · 본 LOD · 예산 · 2D 스프라이트는 들어갔다 —
  `AnimationLOD.h`) — 남은 것: 거리별 IK/물리 끔을 `AnimationLODState`(화면 크기)로(지금 스프링 본은 거리 기준점) · 메시 LOD 가 생기면 본 LOD 를 메시 LOD 와 묶기 ④ 군중 공유(묶음 · 사본 풀 · VAT 쿠킹은
  들어갔다 — `AnimationCrowd.h`) — 남은 것: 섞기 묶음(언리얼 Animation Sharing 의 블렌드 액터 — 지금 섞는 유닛은 사본으로 혼자 평가), Shooter3D 군중이 켜기 · 리타기팅(본 이름 표 · 비율) ·
  얼굴(모프 타깃 임포트 · 표정 커브 · 립싱크 · 깜빡임 · 시선은 들어갔다 — `FacialAnimationComponent`) — 남은 것: 음소 인식 립싱크(지금은 세 대역 모양
  분류 — 모음 넷 · 치찰음 정도만 가른다) · 실제 얼굴 에셋(KayKit 은 모프가 없다 — 합성 테스트 머리뿐) · 스킨 없는 메시의 모프(GPU 모프 풀이 스키닝 컴퓨트 안에서만
  가중치를 건다) · 실시간 얼굴 입력(Live Link Face 자리) ⑤ 파워드 래그돌(관절 모터가 애니메이션 포즈를 쫓음 — 전신 · 부분 래그돌 · 맞음 반응 · 기상 섞기는 `RagdollComponent`) ·
  2 차 움직임: 스프링 본은 들어갔다(리그 노드 `SpringChain`) → 헤어 카드 · 천(Jolt 소프트 바디), 가닥 헤어(TressFX)는 나중 ⑥ KayKit 텍스처(원본 GLB 에 든 `knight_texture` 등)를 `textures_raw/` 로
  옮겨 머티리얼을 만들 것 — 지금 캐릭터는 씬 기본 머티리얼(흰색)로 그려진다. Shooter3D 팩에는 이미 꺼낸 아틀라스(`textures_raw/kaykit_*.png`) · `materials/kaykit_*.material` 이 있다. ⑦ Shooter3D 는 이동 속도와 걷기 · 달리기 클립의 발 속도가 맞지 않아 발이 미끄러진다(재생 속도를 이동 속도에 맞추거나 루트 모션 · 거리 매칭).
- **프리로딩 · LOD · 사전 준비(로드맵).** ① 프리로드 세트(미리 올릴 에셋 + 미리 만들 프리팹 · 우선순위, 쿠킹 때 레벨 · 시퀀스 · 샷의 참조를 따라 자동 수집 —
  `collectReferencedPrefabPaths` 가 있다), `requestPreload` 가 진행률 · 완료를 준다, 프레임 예산(IO · 업로드 · PSO · 인스턴스 수), 참조 수 · LRU 로 내림(지금 캐시는
  약한 참조라 고정 단계가 필요하다) — 언리얼 AssetManager 번들 · Addressables ② 프리팹 풀(숨겨 둔 인스턴스를 켜고 돌려받기 — 탄 · 손님 · 유닛) · 시퀀서
  사전 스폰(프리롤 동안 캐릭터를 숨겨 만들어 첫 포즈 · LOD · 머티리얼을 준비) ③ 메시 LOD(임포트 때 `meshopt_simplify` 로 단계 생성, `.mesh` 판 올림, GPU 컬링이
  화면 크기로 고름, 디더 크로스페이드, 컷 · 대기 뷰 강제 LOD, HLOD 는 나중) ④ 컷 준비가 이것들을 묶는다(카메라 5 단계).
- **다중 월드 · 에디터 툴 창(로드맵, 카메라 4 단계와 한 덩어리).** 지금 `EngineLoop` 는 활성 씬 하나만 틱하고 그리며, GPUScene 빌더 · 스냅샷도 하나다 —
  그래서 프리팹 격리는 활성 씬을 빌리고(씬이 더러우면 막는다) 머티리얼 미리보기는 메인 뷰포트의 선택 오브젝트에 입혀 보인다. ① 다중 월드(미리보기 월드를
  따로 살림, 월드마다 시간 · 틱 정책 · 조명 환경 · 물리 월드) ② 월드별 렌더(씬마다 GPUScene, 뷰 목록의 뷰가 "어느 월드 · 카메라 · 렌더 타깃" 을 고름,
  안 보이는 창은 안 그림 — 카메라 4 단계의 다중 뷰를 다중 월드 × 다중 뷰로) ③ 툴 창 틀(미리보기 월드를 가진 도킹 창, 자기 렌더 타깃 · 에디터 카메라 ·
  기즈모, 창마다 선택 · Undo 범위 — 지금은 에디터 전체에 하나, 문서 계약은 있는 것, 키트 · 게임이 등록하도록 1-4 의 C 확장 지점 위에) ④ 위에 올릴 창:
  프리팹(격리 월드) · 머티리얼(미리보기 구체) · 메시/모델(LOD 비교) · 애니메이션(스켈레톤 · 타임라인 · 압축 오차) · 카메라 프리셋(블렌드 미리보기) ·
  래그돌/물리 에셋(관절 한계) · 이펙트 · 장르 도구(코스터 트랙 · 리듬 차트). 언리얼 FPreviewScene · 애셋 에디터 툴킷, 유니티 PreviewRenderUtility · Prefab Stage.
- **캐릭터 외형 편집(로드맵).** 지금: 형상 쪽(아래 ①~④ · ⑤ 의 소켓 이름 공간)은 `Source/Engine/Character` 에 있다(README "외형 조립 순서"), GPU 모프 풀(`Mesh::setGPUMorphEnabled`)은
  있다, 스켈레톤 에셋(`.skeleton.json`)은 본 · 레퍼런스 포즈 · 역 바인드와 임포트가 적은 본 부착 메시 표(소켓 파일을 처음 만들 근거)뿐이고 편집 창구가 없다.
  ①~④ 남은 것 — **통합**: `Mesh` · 포즈 ↔ `AppearanceGeometry` · `CharacterBoneArray` 변환, 체형 모프 · 피팅 델타(`FitPartResult::_listVertexDelta`)를 GPU 모프 풀에
  싣기(스키닝 앞), 병합 결과(`MeshMerger`)를 인덱스 · 정점 버퍼와 구간 그리기로, 애니메이션 시스템이 본이 움직인 프레임에만 `SocketBindingComponent::updateSocketTransform`, 표면 상태(`CharacterSurfaceState`)를 머티리얼 파라미터 · 마스크 텍스처로. **쿠킹**: 장비 정점 → 몸 전이
  (`SurfaceTransferUtil` — 모프 · 스킨 가중치)를 임포트 · 쿠킹 단계에, 아틀라스 굽기(`IMeshMergeHooks` 구현). **핫 리로드**: 소켓 · 레퍼런스 포즈 · 체형 · 피팅 표 · 부품
  피팅 · 표면 채널 파일을 고치면 외형을 다시 조립 — 소비자(외형 컴포넌트)가 `IAssetCache` 로 올린다(로더는 다 있다). **나중**: 천 시뮬레이션(Jolt 소프트 바디)이 같은 겹
  정보를 충돌체로(Mutable 의 Clip with Mesh · Clip Deform 이 같은 문제를 푼다). 참고: 언리얼 Mutable(Customizable Object) ·
  Skin Weights 전이, Character Creator 스마트 핏, Daz 오토핏 ⑤ 장비 해석 — 데이터 · 해석기는 있다(`GameFramework/Base/Gameplay/Appearance` — 슬롯 표 · 세트 · 아이템 외형 · 꾸미기 스키마 · 규칙 · `CharacterAppearance`
  프리셋, 장착 조건은 `Equipment`, 공유 코드 · 플레이어 프리셋 · 네트워크 동기화, 형식은 그 README). 외형 컴포넌트(`CharacterAppearanceComponent` — 프리셋 · 칸 덮어쓰기 → 몸 메시 ·
  소켓 부품 스폰 · 부착 · 포즈 따라가기 · 염색, 소켓 이름 공간 `AppearanceSocketRig`)는 들어갔다. 남은 것: 장비(`Equipment`)를 `CharacterAppearanceState` 로 잇기,
  부품 풀(스폰 대신 숨겨 둔 인스턴스), 외형 상태가 바뀌면 같은 인스턴스를 다른 소켓으로, 떨어져 나감 이벤트를 `SocketBindingComponent` ReleasedPhysics 로,
  결과 해시로 병합 결과를 캐시해 같은 차림의 NPC 가 나눠 쓰고 다시 조립하는 동안 이전 외형 유지(비동기), `finishLoad` 에 몸 영역 표 이름 넘기기, 데이터 파일 감시 →
  `AppearanceDatabase` 다시 읽기, 시퀀서 트랙 · 스폰 · 프리팹이 프리셋을 이름으로 가리키기 · 프리로드 세트가 프리셋 참조를 따라 모으기, 편집 창의
  "지금 모습을 프리셋으로 저장" · 썸네일 렌더 · 규칙 설명(`_listTrace`) 표시. 칸 점유(양손 무기)는 아직 외형만 본다 — 게임플레이에서 보조 손을 막을지는
  게임이 정한다 ⑥ 캐릭터 편집 창(다중 월드 툴 창 위) — 본 트리 + 기즈모 포즈 편집 · 소켓 추가/이동 ·
  체형 · 얼굴 슬라이더 · 장비 입히기와 체형을 바꿔 가며 피팅 확인(관통 표시 · 잘린 면 · 조임 강도 · 보정 조각 · 숨김 영역) · 장비 조합 미리보기(어느 규칙이 무엇을 숨기고 바꿨는지 설명 · 세트 입히기 · 규칙 충돌 표시) · 제약 리그 미리보기 · 애니메이션
  재생 · 좌우 대칭 편집.
- **남은 대기열 — 빠른 순(2026-10-04 사용자 지시, 다른 세션에서 워크트리 에이전트로).** 2026-10-04~05 의 병렬 웨이브(캐릭터 · 물리 · 외형 · 카메라 · 에디터 ·
  기믹 · 환경 · 옵션 · 리플렉션 · 오디오 · 2D · 현지화 · 코어 인프라 · QA 도구 · 애니메이션 셋 · 파괴 · 툰 · 내비 · Tracy · 네트워크 결함)는 모두 main 에 들어갔다.
  진행 방식은 메모리 "워크트리 에이전트 절차" — 결함 먼저 · 겹치는 파일 없게 나누고, 병합은 준비되는 대로, 전체 검증은 큰 묶음 뒤 한 번. **2D · 3D 에 다 쓰이는 기능은 공통 코어로**(사용자 지시).
  - **작음(S)**: 에디터 H(버그 리포트 · 시험 패널) · 단축키 편집기 · 환경설정 창 · 모듈 켜고 끄기 · DPI 실물 확인.
  - **중간(M)**: 대역폭 프로파일러 · 게임플레이 디버거 · 비주얼 로거 · 모듈 패키지 관리 · 점광/스폿 그림자 ·
    HZB 가림 컬링 · 메시 LOD(meshopt) · PSO 미리 만들기 · 에디터 G(보기 모드 — 프로파일러 표 · GPU 타임스탬프 · Tracy · RenderDoc 은 들어갔다) · 에디터 C(확장 지점) · 에디터 F
    (카탈로그 편집기) · 공용 커브 편집기 · 공용 노드 그래프 틀 · 인스펙터 개선 · 에셋 브라우저 · 맵 검사 패널 · 패키징 UI · 시나리오 녹화(실행 중 입력 → `.scenario.xml`) ·
    로딩 흐름의 남은 것(로딩 화면 · 페이드는 `LoadingScreenController` — 진행률(씬 매니저에 진행 질의가 없다 — 스트리밍 큐 바이트로) · 프리로드 세트 연결 ·
    팁 목록 데이터(`LoadingScreenSettings::_listTip` 은 있으나 gamesettings 칸 · 글 수집 규칙이 없다)) · 입력 확장 · 에셋 DCC 내보내기 · 포토 모드 · 리플레이/킬캠(바탕인 의도 기록 `.swintent` 은 있다 — 남은 것: 재생 UI · 카메라 · 되감기) · SSR · 업스케일러 · HDR 출력 · 데칼 · 하늘/시간대/높이
    안개 · 2D 스켈레탈 · 학습용 몫(장르 시작 템플릿 · 튜토리얼 · API 문서).
  - **큼(L)**: 제약 ·
    파티클/VFX · 텍스처
    밉 스트리밍 · 카메라 5 · 6 단계 · 에셋 레지스트리 · DDC · 증분 쿠킹 · 월드 편집 도구 ·
    천/머리카락 · 전술 AI · 볼류메트릭 안개/빛/구름 · 캐릭터 셰이딩 · 모션 캡처 공정 · 대규모 좌표 · PCG 저작 그래프 · GI/반사 프로브 · 플랫폼 서비스 ·
    패치/DLC · 모드/UGC.
  - **아주 큼(XL)**: 비주얼 스크립팅 · 월드 파티션/스트리밍/HLOD · 음성 채팅(온라인 구성은 1-7 "네트워크 서비스 계층").
- **런타임 UI 애니메이션 · 전환의 남은 것(runtime-ui 7-1 · 7-2 뒤, `Engine/UI/README.md`).** (1) 스타일 전환은 부모의 보간 값을 물려받는 글 칸에 내리지 않는다
  (자식 규칙에 `_transition` 을 따로 적는다) — CSS 는 물려받은 값도 보간된다. (2) 한쪽만 정한 칸(위젯 자기 칸 ↔ 시트 값)은 전환 없이 바뀐다 — 위젯 칸 값을 읽는 길이 필요.
  (3) 전환 지연(`transition-delay`) · 키 사이 사건 말고 곡선 위 사건.
  (4) 그림 캐시는 조상의 `kTransform` 이면 자손까지 다시 칠한다 — 변환을 캐시 밖에서 곱하는 쪽은 10-4 의 위젯 1 만 개 측정으로 판단.
- **런타임 UI 입력의 남은 것(위젯 트리 코어 뒤, `Engine/UI/README.md`).** (1) 명령 조종자(디렉터 넷) · 플레이어 뷰 카메라는 아직 UI 가 먹은 입력
  (`UISystem::isActionConsumed` · `isGameInputBlocked`)을 보지 않는다 — 플레이어 조종자만 본다. 그 게임이 메뉴를 띄우게 되면 같은 질의를 그 자리에 둔다.
  (2) 에디터 게임 뷰(Game 패널) 안의 포인터 좌표(창 픽셀 → 게임 뷰 렌더 타깃) — UI 가 게임 뷰에 그려지는 단계와 함께. (3) UI 행동 맵(`engine/input/ui.input.xml`)의
  키 리바인딩 — 키 바인딩 창(`KeyRebindScreen`)은 있다, UI 맵을 설정 대상으로 두는 길(`UserSettingsTargets` 의 입력 맵이 게임 맵 하나)이 남았다. (4) 글 입력 칸(`TextInputWidget`)은 끝에 붙이기 · Backspace(`UI.TextBackspace`) · Enter 확정만 — 커서 이동(좌우 · Home/End) · 선택 · 붙여넣기 · 조합 글 밑줄이 남았다.
- **런타임 UI 접근성의 남은 것(runtime-ui 9-1 뒤, `Engine/UI/README.md`).** (1) 음성 재생 쪽 자막 — 음성 이벤트(`GameSound`)에 자막 키를 실어
  `UISubtitleService::post` 로(오디오 키트의 모양을 보고 정한다 — 지금은 대화 러너만 보낸다). (2) "글자 배율 2 에서 옵션 메뉴가 넘치지 않는다" 시험은 옵션 메뉴(8-2)가 없어
  견본 문서로 한다(`UIAccessibilityTest.OptionsMenuFitsAtDoubleTextScale`) — 8-2 가 들어오면 엔진 옵션 메뉴 문서로 바꾼다.
- **옵션 · 일시정지 메뉴의 남은 것(runtime-ui 8-2 뒤, `Engine/UI/README.md`).** (1) 일시정지 메뉴에 타이틀로 · 끝내기 — 게임 흐름(`GameInstanceBase`)의
  명령이라 엔진 화면이 모른다(게임이 `PauseMenuScreen` 을 덮어쓰거나 명령 표를 거는 길). (2) 명령 조종자 게임(NileCity · StarSkirmish · ThemePark)과
  MeadowVillage 는 `_bUIPauseMenu` 를 켜지 않았다 — Esc 를 게임이 따로 쓰는지 보고 켠다. (3) 열거형 행은 콤보(펼침)뿐 — 패드에 맞는 좌우 고르기 위젯이 없다.
  (4) 키 바인딩 창의 Esc 길게 누르기는 키보드 Esc 만 — 패드 사용자는 취소가 없다(패드 B 를 바인딩할 수 있어야 해서). (5) 해상도 바꾸고 15 초 되돌림 ·
  키 바인딩 재시작 뒤 유지의 실기동 확인(Shooter3D · 패드)은 게임별 빌드 때. (6) 자동 크기 옵션 창은 1280×720 골든(`options.layout.txt`)에서 화면 안에 든다 —
  150 % 조건(853×480 UI 단위)을 보는 시험은 없다(`UIAccessibilityTest` 꼴로 더한다).
- **알림 · 힌트 · 목표 마커의 남은 것(runtime-ui 8-3 뒤, `Engine/UI/README.md`).** (1) 들어오기 · 나가기 애니메이션 — 항목은 화면이 아니라 조각이라 문서 Open · Close 가 닿지 않는다,
  `showEntry` · 제거 때 `UISystem::tween` 으로 `_opacity` 를 걸고 지우기를 페이드 뒤로 미룬다. (2) 목표 마커를 퀘스트 키트의 목표 오브젝트에 붙이는 한두 줄 — 키트 쪽 목표 오브젝트 모양을 보고. (3) 코드가 올리는 알림 글
  ("Game saved" · "다시 시작하면 적용")은 글 그대로라 글 수집에 들지 않는다 — 코드 글 키(`SW_LOCTEXT` 꼴)로 바꿀 것. (4) 입력 힌트 위젯을 따로 두지 않았다 —
  리치 텍스트 태그 하나로 충분한지 게임 HUD(8-1)에서 본다. (5) 항목마다 이름 `Message` · `Count` 가 트리 안에 겹쳐
  둘째 항목부터 "name is used twice" 경고가 난다 — 항목 조각을 `UserWidget` 으로 감싸 이름을 `<번호>.Message` 로.
- **런타임 UI 그리기 성능의 남은 것(runtime-ui 10-4 뒤, `Engine/UI/README.md` "성능").** 위젯 1 만 칸 · 글 10 칸/프레임 바뀜의 `GT.UI`(Layout + Paint) p50 은 0.29 ms 로
  목표(0.3 ms) 안이다(08 2 절). 남은 몫은 보이는 위젯 ~1150 개를 걷는 비용(Paint 걷기 ~180 us — 위젯 캐시 이어 붙이기 ~90 · 자르기 검사 ~20 · 나머지 방문)이고,
  더 줄이려면 패널마다 하위 출력 캐시(Slate Invalidation Panel)다 — 일괄 합치기 결정이 이어 붙이는 순서에 달려 있어, 하위 목록은 합치지 않은 일괄 그대로 들어야
  바이트가 같다. 창 크기 바꿈(전체 재배치) 한 프레임 · 첫 프레임 글리프 래스터화가 몇 프레임에 퍼지는지는 표 밖이다(워밍업 60 프레임이 버린다).
- **런타임 UI 오른쪽에서 왼쪽(RTL)의 남은 것(runtime-ui 9-3 뒤, `Engine/UI/README.md`).** (3) 가로 스크롤 패널은 RTL 에서도 왼쪽부터 보인다(Slate · CSS 는 오른쪽) —
  내용 자리를 거울로 놓으려면 `scrollIntoView` 의 부호도 바꿔야 한다.
- **오디오 엔진(2026-10-04, `Engine/Audio/README.md`)의 남은 것.** 믹서 · DSP · 공간화 · 이벤트 · 스냅샷 · 적응형 음악 · 씬 묶기는 들어갔다. (1) 데이터 핫 리로드 —
  `loadEventLibrary` · `loadMixer` 는 같은 이름이면 바꾸지만 파일 감시(에디터 `FileWatchDispatcher`)에 걸려 있지 않다. (2) 에디터 — 믹서 패널(버스 미터 · 음소거/솔로),
  이벤트 브라우저 · 미리 듣기, 보이스 · 가상화 프로파일러. (3) 긴 음악 스트리밍(지금은 클립을 통째로 디코드해 메모리에 든다 — 3 분 스테레오 ≈ 69 MB float).
  (4) 출력: HRTF(바이노럴) · 5.1/7.1 · 다중 리스너 믹스(지금은 가장 크게 들리는 리스너 하나), 리눅스 출력 백엔드(지금 Null — 오프라인 렌더). (5) 2D 물리(Box2D) 가림 ·
  포털/방 기반 가림(Wwise Rooms & Portals) · 회절. (6) 사이드체인 덕킹(대사 때 음악 낮추기 — 지금은 스냅샷으로), 컨볼루션 리버브, 그래뉼러 · 절차 소리
  그래프(MetaSounds 급). (7) 일곱 시험 게임 중 Shooter3D · StarSkirmish 만 이벤트로 바뀌었다 — 나머지는 아직 `GameSound::play( 경로 )`.
- **NPC 하루 일정(`GameFramework/Base/Actor/AI/Schedule`, 2026-10-04) — 연결할 것.** 일정 데이터 · 런타임 · 화면 밖 LOD · 잠 · 저장 · 추적은 들어갔다(`AI/Schedule/README.md`).
  남은 것 — (1) gimmick 병합 뒤: 스마트 오브젝트 시스템이 `IScheduleActivityLocator` 를 구현(`reserve` 는 NPC · 종류 · 날 · 시간 창에 멱등, 지금 점유만 되면
  어댑터가 예약표를 들고 칸 시작에 점유), 끼어들기(말 걸기)를 상호작용 프레임워크에서 `pushInterruption( "Talk" )` 로. (2) char-anim 병합 뒤:
  `IScheduleActivityAnimator` 구현 — `_animation` 이름을 애니메이터 그래프 상태로. (3) 쓰는 게임이 없다 — HarvestValley 마을 사람(데이터
  `game/harvestvalley/data/villagers.schedules.xml` 은 있다, 몸은 `NavAgent` 가 `ScheduleNpcView` 를 따라감, 저장은 `ScheduleSaveState` 를 게임 세이브에).
  (4) 에디터 패널(`dumpTimeline` · `explainNpc` 글을 그대로). (5) 주 단위 · 날짜 범위 일정, 자정을 넘는 칸, 관계 단계(호감도 수치 조건 — 지금은 태그로) 는 없다.

- **Shooter3D 캐릭터 통합의 남은 것(shooter-int, 2026-10-04).** KayKit 기사 플레이어 · 스켈레톤 적 · 외형 프리셋 · 입력 맵 · 탄도선 풀은 들어갔다(`Source/Games/Shooter3D/README.md`).
  연결할 것(anim-rig · anim-gameplay 는 main 에 들어왔다 — 연결 작업은 따로 정한다) — 리그: 왼손을 총에 붙이는 손 IK(목표는 무기 소켓 `MainHand.SupportHand`), 1인칭에서 머리만 숨기기(지금은 몸 전체를 숨긴다 — 본 숨김 ·
  스케일은 PoseModifier 자리), 스프링 본(망토). 게임플레이: 래그돌 쓰러짐(지금은 클립), 부위 히트박스(지금은 발에서 키까지 캡슐 하나, 중심 소켓 `Chest`),
  물리 질의 히트스캔 · 시야(지금 상자 목록 · 캡슐), 쓰러질 때 무기 떨어뜨리기(`SocketBindingComponent::release( Physics )` — 무기 프리팹에 강체를 더하면 된다),
  알림으로 휘두름 피해 · 발소리(지금 휘두름 피해는 `_attackHitTime` 초). 그 밖: 외형 부품 풀(스켈레톤마다 프리팹을 세우고 지운다), 1인칭에서 숨긴 몸의 애니메이션
  LOD(숨겨도 평가한다), 걷기 · 달리기 발 미끄러짐(위 애니메이션 ⑦).
- **어빌리티 시스템의 다음 조각(쓰는 게임이 생기면).** 언리얼 GAS 에 있고 여기 없는 것: 이펙트가 주는 어빌리티(장비가 스킬을 준다), 걸린 동안의 태그 조건
  (`OngoingTagRequirements` — 기절 중 버프 정지), 태그가 붙을 때 발동(`OwnedTagAdded` 트리거), 큐를 데이터로 이어 주는 큐 매니저(큐 태그 → 프리팹 · 사운드),
  어트리뷰트를 `SaveGame` 에 싣는 도우미, 에디터의 런타임 상태 패널(걸린 이펙트 · 태그 개수 · 쿨다운). 넣을 때마다 `AbilitySystemTest` 에 시험 하나.

- **로컬라이제이션 — 남은 것(데이터 쪽 파이프라인은 끝, `Engine/Localization/README.md`).** UI 글 위젯은 글 판을 따라 다시 푼다(runtime-ui 6-2). 아직 없는 것: `selectordinal`(서수) · 화폐 · 시간대 · XLIFF · 쿠킹된 이진 표(언리얼 `.locres` — 지금은 JSON 을
  그대로 읽는다) · 아랍어 이외 RTL 문화권 데이터 · `ja` 번역. 아이템 이름(Shooter3D)은 표에 모이지만 화면에 쓰는 코드가 아직 없다(무기 이름은 HUD 가 `getStringByText` 로 쓴다).

- **상호작용 · 기믹(2026-10-04 들어감 — `GameFramework/Base/Gameplay/Interaction` · `Gimmick` · `Spline`) 병합 뒤 남은 것.**
  - 물리: 기믹 프리팹의 `BoxCollider2DComponent` 에 3D 게임용 3D 트리거 · 강체 콜라이더 변형을 더한다(`Resource/common/prefabs/gimmicks`). 월드 질의 · 카메라 암 ·
    집기 · 눌림판 무게 · 발사대 · 컨베이어는 강체 물리에 이어졌다(`GameFramework/Base/Gameplay/Interaction` · `Gimmick` README).
  - 애니메이션: 상호작용 단계에 몽타주(클립)를 이름으로 잇기 — 맞춤 마커 → 워프 목표는 들어갔다(`InteractorComponent` 가 시작할 때 넣는다).
  - 렌더러: `InteractableComponent::getHighlightRequest`(Outline · Sense)를 읽는 외곽선 · 감각 모드 패스.
  - 에디터: 기믹 회로 그래프 편집 창(노드 · 배선 · 검증 오류 표시, 대상 오브젝트 고르기) — 지금은 인스펙터의 목록 편집뿐.
  - 네트워크: 회로 상태 바이트(`GimmickCircuit::saveState`)를 `NetClientServer` 스냅샷 · `RollbackSession` 상태에 싣기(모양은 준비됨, 배선 없음).

- **의도 기록 · 원격 조종자의 남은 연결(possess-auto C3 뒤).** `NetClientServer` 의 입력 바이트(`ReplicationClient::sendInput` 이 게임이 준 바이트를 싣는 자리)를
  `ControlIntent::write` 로, 서버 폰은 `RemoteControllerComponent` 로 — 폰을 쓰는 넷 시험 게임이 생기면(지금 0). 의도 기록을 켜고 `.swintent` 를 쓰는 명령줄 ·
  에디터 단추와 재생 UI · 킬캠 카메라 · 되감기는 아직 없다(`ControlSystem::setRecording` · `getHistory().saveToFile` · `IntentTrackControllerComponent` 는 있다).

- **CS2 식 서브틱 입력(2026-10-05 제안 — 넷 게임이 생기면; 지금 넷 키트를 쓰는 시험 게임은 0).** `RawInputEvent` · `NativeWindowEvent` 에 시각이 없다(`MSG::time` 을 버림).
  B1 사건 시각 + 입력 리플레이 판 4 · B2 프레임 안 자리와 그 순간까지의 시선 누적(시계 주입) · B3 `InputMap` 이 행동별 "누른 순간" 을 준다 · B4 클릭 순간의 시선으로 발사
  (쿨다운 남은 몫은 이미 고침) · B5 넷 입력 항목마다 스탬프 · `viewTick`(지금은 메시지마다 하나라 다시 보낸 옛 입력도 최신으로 판정 — 자리는 있다:
  `NetInputFormat::_bStamped` · `NetInputSendWindow::push( …, stamp )` · `NetInputEntry::_stamp`, 켜면 `kClientServer` 판을 올린다) · 서버 클램프(랙 보정
  `LagCompensationHistory::raycastAt(float)` 는 있다) · B6(선택) Win32 입력 스레드 — 먼저 `GetMessageTime` 해상도를 재고, 에디터 · 콘솔이 먼저 소비하는 판정을 우회하지 않게.
  롤백 · 락스텝 키트에는 넣지 않는다(프레임 단위 결정성이 계약).

- **GameFramework 구조 리뷰에서 남은 것(2026-10-05 — 공통 모듈 · 디렉터 베이스 · 층 게이트는 끝남).**
  ⑤ `EngineLoop.cpp` 의 절반이 기동 단계 구조체(낮음).

- **(보류 — 사용자 결정 "안정화된 뒤 개발") 게임별 CC0 리소스 배치 + SD 메카 시험 게임 MechArena.** 라이선스는 **CC0 급만**(출처 표기 의무 · 재배포 금지가 붙은 것은
  넣지 않는다 — 내려받기 스크립트 우회도 안 함). 확인한 원본: VRoid 알파판 샘플 D · E · F · G · 남녀 기본(OpenGameArt `vroid-studio-cc0-models`, VRM 0.x — 툰 셰이더 ·
  VRM 임포트는 있음) · KayKit(GitHub 사용자 `KayKit-Game-Assets` 의 `*-1.0` 저장소, LICENSE.txt CC0 — Space Base · Restaurant · City Builder · Dungeon Remastered ·
  Furniture · Halloween) · Quaternius(Poly Pizza 낱개 glb — 도리이 · 일본식 문 · 대나무 · 단풍 · 가을 소나무) · 음악 OpenGameArt CC0 `jaoan`(고토) · `menu-music-2`(도장풍 루프).
  배치안: NileCity ← City Builder · StarSkirmish ← Space Base · Shooter3D ← Dungeon · ThemePark ← Restaurant · Halloween · HarvestValley ← 일본 시골(Quaternius) + jaoan ·
  AbilityArena ← VRoid + menu-music-2. 프리팹 · 데이터 연결까지(안 쓰면 에셋 검증 `orphans`). 건담풍 사람형 메카는 CC0 로 쓸 만한 것이 없다(Quaternius 메카는 동물이 탄
  보행기, OGA Shock Bot 은 리얼풍) → 파이썬으로 SD 메카 부품 메시를 만들어 부품마다 뼈 하나에 붙이고 KayKit 사람형 뼈대 이름에 맞춰 KayKit 애니메이션(CC0)을 쓰는 생성기 +
  MechArena 시험 게임(`GF_MechArena` 키트는 있고 시험만 씀). 건담 고유 요소(V 안테나 · 얼굴 마스크 · 흰 · 파랑 · 빨강 · 노랑 배색)는 피한다.
  탈것: 말(`MountMovementComponent` · 서부극 `WesternHorseMountComponent`) · 차(`ArcadeVehicleComponent`)는 기본 도형(캡슐 말 · 상자 차)으로만 시험했다 —
  CC0 말 메시 · 걸음새 애니메이션(Gait 0..4 · Turn)과 차 모델을 찾으면 서부극 · 카트 시험 게임에 붙인다.
  카트 키트(`Kits/Genre/Casual/KartRacing`)의 플레이어 경로를 `ArcadeVehicleComponent::toVehicleInput`(의도 → 모터 입력)으로 바꾸는 것은 카트 시험 게임이 생길 때
  (지금 키트는 InputMap 을 읽지 않고 `KartRace::resolveInput` 이 이미 "같은 모터에 다른 조종자" 다).
  물리 차(`WheeledVehicleComponent`)는 바퀴 메시를 바퀴 자세(조향 · 회전 · 서스펜션)로 옮기지 않는다 — 차 모델이 생기면 바퀴 자식 이름 표와 `getVehicleWheelPose` 창구를 더한다.

### 1-7. Core · 태스크

- **네트워크 — 파괴 · 가상 서버에서 남은 것**(2026-10-05, `GF_NetSimulation` · `GF_NetDestruction`). ① 파괴 사건은 "신뢰 · 순서 없음" 채널(N21b) — 250 ms · 손실 15 % 의 사건 지연
  평균 · 최대 전/후는 `NetSimDestructionMatrixTest` 로그(`max event lag … mean …`)로 잴 것(빌드 뒤). 남은 최대는 한 메시지가 거듭 잃는 몫이다. 덩어리 멈춤 확정(신뢰 자세)도
  받는 쪽이 틱으로 끼우므로 옮길 수 있다 — 재고 나서. ② 전용 서버 프로세스(`Server`, 타깃 Game · Server)는 섰다 — 남은 것은 그 위에 파괴 시뮬레이션을 UDP 서버로
  돌리고 Windows 클라이언트(App)가 붙어 파괴 해시가 컴파일러 · 플랫폼(WSL 리눅스 서버 ↔ Windows 클라이언트)을 넘어 같은지 보는 것(서버 쪽 `NetSimulation` 호스트 모드 ·
  클라이언트 접속 인자 · 해시 로그 비교 스크립트). 같은 입력의 해시가 구성을 넘어 같은 것은 `DestructionDamageTest.EventLogHashMatchesTheRecordedValueOnEveryBuild`(해시 다섯)가
  지킨다(Windows Debug · Release · Shipping 같은 값 확인, 리눅스는 CI 잡 — 리눅스 clang 에서 다르면 그 잡이 진다).

- **전용 서버 타깃에서 남은 것**(2026-10-06, server-target). ① 서버의 크래시 묶기 · 텔레메트리 — 기동 표의 `Telemetry` 는 Client 대상(동의가 플레이어 설정)이라
  서버는 크래시 보고를 묶지 않는다; 서버용 동의 = 운영 설정으로. ② 서버 프로세스 지표 — 틱 시간 · 버린 틱은 `/metrics`(`_opsPort`)로 나간다; 메모리 ·
  접속 수 지표와, 게임 · 키트 모듈이 서버의 지표 · 상태 등록부를 받는 창구(엔진 서비스 줄 — 첫 서버 서비스 조립 때)는 남음. ③ 게임별 서버 기동 확인 — Empty 말고 각 게임을 `Server -gv_serverExitAfterTicks=60` 으로(게임 · 키트가 디바이스 · 창을 null 확인 없이 쓰는지).
  ④ 서버 빌드에서 렌더러 코드를 아예 빼기(지금은 컴파일만 되고 돌지 않는다 — 크기 · 링크 시간을 재고). ⑤ 리눅스 서버(`WSL-*-Server`) 빌드 · `readelf -d` 로
  libX11 없음 확인 · `ServerTest` — 이 묶음은 Windows 만 확인했다.

- **네트워크 — 포화된 연결의 비신뢰 줄**(N21a 뒤). 대역폭 몫을 다 쓰면 메시지가 기다리는데, 비신뢰 줄(`NetConnection::_listOutgoingUnreliable`)에는 상한이 없어 오래 포화되면
  옛 비신뢰가 쌓였다 몰려 간다. 언리얼은 포화면(`IsNetReady` 거짓) 액터 복제를 건너뛴다 — 키트가 `NetHost` 에 "이 연결이 포화인가" 를 묻거나 줄에 나이 상한을 둔다. 재고 나서.

- **네트워크 — 하지 않기로 한 것**(리팩토링 N13~N21 · N18b 는 끝남, N18c 는 측정으로 기각 — [결정 기록](09_Decisions.md) 3절):
  하지 않기로 한 것: NAT · 리플렉션 속성 복제(Iris) · 복제용 RPC · 외부 네트워크 라이브러리(암호 라이브러리는 아래 "네트워크 보안" 항목의 예외).

- **네트워크 서비스 계층(2026-10-06 사용자 결정 — 로그인 · 채팅 · 거래, MMO 가 아니어도 쓰는 키트).** 상태 복제(UDP)와 달리 순서 있는 신뢰 스트림 · 긴 연결 · 요청-응답이다.
  ① 스트림 전송(Core `Network/Transport/` — IOCP · epoll · 루프백)은 있다. 리눅스 epoll(`EpollStreamTransport`)은 WSL 빌드 · `StreamTransportTest` 미확인 —
  `WSL-Debug` · `WSL-Shipping` 에서 `--test_repeat=20` 과 변이(`setReceivePaused( false )` 의 할 일 빼기 → 백프레셔 시험이 진다)를 본다. io_uring 은 측정 뒤.
  ③ 구성은 `docs/` 가 아니라 이 항목이 정본 — **공통 기반**(GameFramework 기반 `Online/`): 서비스 틀(등록 · 라우팅 · 인증 문맥 · 오류 코드 · 판 협상) · 요청 보호(도배 제한 · 멱등 키 · 크기 상한) ·
  신원 원형(`AccountID` · 세션 토큰 검증) · 저장 계약(`IServiceStore` 영속 · `IEphemeralStore` 캐시 · `ILocalStore` 로컬 — 파일 백엔드는 바이너리/JSON/XML · 원자적 쓰기 · 체크섬 · 선택 압축/암호화) ·
  마이그레이션 적용기 · 감사 로그 · 서버 간 버스 · 예약 작업 · 원격 설정/기능 플래그 · 관측(지표 · 구조화 로그 · 추적 id). **드라이버 키트**: `GF_SQLStore`(SQLite) · `GF_Server_SQLStore`(PostgreSQL) ·
  `GF_Server_CacheStore`(RESP — 메모리 구현은 기반 `Online/Cache`). **기능 키트**: `GF_Account` · `GF_ServerDirectory` · `GF_Economy`(원장 · 지갑 · 상점 · 영수증 검증) · `GF_Trade`(원장 위) · `GF_Mailbox` · `GF_Chat` · `GF_Social` ·
  `GF_Leaderboard` · `GF_Matchmaking` · `GF_LiveOps` · `GF_Admin`(GM 도구 · 제재). 제품 이름은 드라이버 · 제공자 폴더에만(`CheckProductNames` — 조립점은 mapExemption 에 이유와 함께). 부하 시험 봇은 시험 도구.
  ④ **DB 결정(사용자)**: 영속 PostgreSQL(서버) · SQLite(개발 단독 서버 · 클라이언트 로컬), 캐시는 RESP 드라이버 하나 — 리눅스 Valkey(BSD-3), 윈도우 Garnet(MIT)(Redis 7.4+ 는
  RSAL/SSPL — "오픈소스 · 무료" 조건 밖). 캐시를 잃어도 영속 데이터는 맞아야 한다(거래 정본은 영속 트랜잭션). DB 호출은 전용 워커 + 연결 풀 + 비동기 완료.
  **서버는 윈도우 · 리눅스 둘 다 1 급**, 서버 전용 모듈은 클라이언트 Shipping 에 넣지 않는다(전용 서버 타깃 — Game · Client · Server).
  **들어간 기반**(`GameFramework/Base/Online/`): `Store`(영속 계약 `IServiceStore` — 비동기 일 · 트랜잭션 · 조건부 쓰기 · 멱등 기록, 메모리 구현 · 계약 시험) ·
  `Guard`(토큰 버킷 · 크기 상한) · `Identity`(`AccountID` · `IAccountDirectory`) · `Cache`(휘발성 계약 `IEphemeralStore` — 만료 · 원자 증감 · 임대 · 정렬 집합 · 발행/구독, 메모리 구현 · 계약 시험) ·
  `Audit`(감사 줄) · `Bus`(서버 간 버스 — 캐시 위 · 프로세스 안) · `Schedule`(예약 작업 — 회차 차지 · 임대 이어받기) · `Config`(원격 설정 · 기능 플래그 출시 비율) ·
  `Ledger`(원장 — 복식 이동 · 분개 키 멱등 · 보존 검사 · 환불 회수 빚) · `Mail`(우편 넣기 — 첨부 맡김 · 멱등 · 만료 색인) ·
  `Sanction`(계정 제재 레코드) · `IAccountSessionControl`(세션 끊기 창구).
  관측은 들어갔다 — 지표 · 상태 확인 · 운영 HTTP(`Engine/Observability`, 서비스 묶음 `Online/Observability/ServiceMetrics`), 로그 문맥(Core `LogContext` —
  요청 머리의 추적 id 128 비트 → 처리기 · 저장소 일의 줄 꼬리표). 관측 남은 것: 서버 로그 JSON lines 출력 장치, OpenTelemetry 내보내기, 감사 줄의 추적 id 칸.
  남은 기반: `EphemeralServerBus` 를 호스트의 캐시 라우터(`EphemeralStoreRouter`) 위로(지금은 자기 캐시 앞을 혼자 쓴다), PostgreSQL · RESP 계약 시험을 실제 서버로 한 번(`SW_TEST_POSTGRES_URL` · `SW_TEST_RESP_URL` — Valkey(WSL) · Garnet(Windows) 각각 — 이 PC 에 서버가 없어 아직 돌리지 않았다, Windows · WSL), 마이그레이션 SQL(`Resource/common/sql/servicestore`)을 Shipping 서버가 읽는 길(지금은 디스크 폴더를 훑는다 — 팩에는 폴더 목록 API 가 없다) —
  계약 시험(`ServiceStoreContract.h`)을 SQL 구현에도 같이 돌린다.
  **계정**: 서버 키트 `GF_Server_Account`(`Kits/Feature/Online/Account/Server`)에 로그인 서비스 본체(`LoginService` — 저장소 일로 맡기고 거둠 · `LoginStoreLogic` · `LoginTicketAuthority`),
  공유 `GF_Account`(와이어 타입 · `AccountClient`), 게스트 · 연동 · 외부 로그인 자리 · 빌드 판 · 제재 확인 · 탈퇴, 스트림 바인딩(`AccountServer` · `AccountClient`) · UDP 접속
  인증기가 들어갔다. 암호는 `NetSecurityLoginCrypto`(제공자의 Argon2id · HKDF).
  외부 로그인은 공통부(기반 `Online/HTTP` · OIDC 확인기 + JWKS 캐시 · 프로필 API 틀 · 제공자 설정 데이터 · PC 루프백 PKCE 클라이언트)와 가짜 제공자까지 — 실제 제공자 설정
  (구글 · 애플 · 카카오 · 네이버 · 스팀 — 앱 등록 · client id 는 쓰는 게임이 생기면), 호스트 이름 해석(Core 주소는 IPv4 뿐 — 실제 제공자 호스트에 필요), 애플 client secret JWT ·
  탈퇴 때 토큰 철회(`Provider/Apple/`), 모바일 SDK 클라이언트, OS 브라우저 열기(`IExternalBrowser` 구현 — 게임 몫)가 남았다. 텔레메트리의 `IHTTPClient`(Engine, 막는 창구 · 기본 Null)를
  이 HTTP 클라이언트로 잇는 일도 남았다(Engine 은 GameFramework 를 모른다 — 게임이 어댑터). 서버 실행 파일에 계정 서비스 조립(주 키는 서버 설정 비밀 — 키 배포 · 교체 절차는 아래 "서버 여럿").
  **채팅**: `GF_Chat`(타입 · `ChatProtocol` · `ChatClient`) · `GF_Server_Chat`(거르개 · 도배 막이 · `ChatService` · 바인딩 `ChatServer`)이 들어갔다. 남은 것: 서버 실행 파일에
  채팅 조립(계정 키트 창구 · 친구 키트 차단 표 · 금칙어 경로), 신고(메시지 스냅숏 → GM 도구), 채널 샤딩(world 채널 수천 명 — 부하 봇 수치로 정함, 거르개 비용은 `ChatService.send` p99),
  클라이언트 UI 위젯, 실제 금칙어 목록(운영).
  **서버 디렉터리**: 기반 `Online/Directory`(등록 · 하트비트 · 읽기 캐시 · 고르기)와 `GF_ServerDirectory` · `GF_Server_ServerDirectory`(점검 · 공지 · 배정 · 바인딩 ·
  클라이언트)가 들어갔다. 남은 것: GM 도구 패널(GF_Admin · 에디터 확장 지점 뒤)에 점검 · 공지 바꾸기 잇기, 서버 고르기 UI 위젯, 서버 실행 파일에 디렉터리 서비스 ·
  등록 조립(서버 설정에 종류 · 지역 · 공개 주소), 오케스트레이터(Agones · 쿠버네티스) 상태와 서버 등록 잇기(지금은 서버가 스스로 캐시에 하트비트).
  **친구 · 길드**: `GF_Social` · `GF_Server_Social`(친구 · 차단 · 접속 상태 · 이름으로 신청 · 길드 · 바인딩 · 클라이언트)이 들어갔다. 남은 것: 채팅 정책(차단)과 길드 채널을
  서버 조립에서 잇기(`SocialService::isBlockedLocal` · `GuildService::setEventDelegate` → 채팅 키트), 길드 창고(원장 맡김 `esc/guild/<id>`) · 길드 레벨 · 공개 길드 가입 신청,
  친구 목록 UI 위젯, 부하 봇의 친구 접속 상태 팬아웃 측정(친구 200 명 × 캐시 읽기).
  **순위표**: `GF_Leaderboard` · `GF_Server_Leaderboard`(순위표 · 통계 · 업적 · 시즌 정산 · 바인딩 · 클라이언트)가 들어갔다. 남은 것: 정산을 페이지 단위 일 여럿으로
  (지금은 시즌 하나가 저장소 워커 하나를 잡는다 — 참가 수십만이면 부하 봇 숫자로), 지난 기간의 캐시 정렬 집합 지우기, 친구 순위표(친구 목록 × 점수 — 게임 조립),
  업적 진행률 표시 · 숨김 업적 목록, 서버 조립에서 `setScheduler` · 표 정의 데이터 읽기.
  **매칭**: `GF_Matchmaking` · `GF_Server_Matchmaking`(매처 · 파티 · 로비 · 대기열 권한 서버 · 전용 서버 배정 · `MatchServerAgent` · 바인딩 · 클라이언트)이 들어갔다.
  남은 것: 서버 실행 파일에 매칭 조립(모드 표 · 실력 공급 `IMatchRatingSource` · 게임 서버의 `MatchServerAgent` 를 UDP 접속 인증기에 잇기), 백필(경기 중 빈자리),
  오케스트레이터(Agones · 쿠버네티스) 할당 · 서버 띄우기, 재접속 유예 중 파티 · 로비 유지(지금은 끊기면 바로 뺀다), 파티 채팅 채널 잇기(게임 조립 — 파티 알림 → Chat),
  다른 서버에서 깨진 파티 표는 낸 서버에 Cancelled 를 버스로 알리지만 권한 매처에서는 모든 모드의 빼기로 뺀다(모드를 모른다). 팀 나누기 · 채우기는 그리디다 — 나눌 수 없는 조합은 그 닻을 건너뛴다;
  작은 n 전수 탐색은 부하 봇의 대기 시간 p99 를 보고 정한다.
  **라이브 운영**: `GF_LiveOps` · `GF_Server_LiveOps`(기간 이벤트 · 원격 설정 값 · 경계 알림 · 푸시 제공자 계약 · 기기 등록 · 발송기 · 가짜 제공자 · 바인딩 · 클라이언트)가
  들어갔다. 남은 것: 실제 푸시 제공자 둘(`Push/Provider/<제품>/` — 공통 HTTPS 클라이언트 위 HTTP/2 · 토큰 인증, 인증서 · 서비스 계정 키는 운영 비밀 — 외부 계정 · 비용이라
  사용자 결정 전에는 하지 않는다), A/B 실험(원격 설정 출시 비율 + 지표), 이벤트 운영 화면(GF_Admin 패널), 서버 실행 파일에 라이브 운영 조립, 우편 · 이벤트에서 "오프라인이면 푸시" 잇기(게임 조립).
  **부하 시험 봇**: `Tools/OnlineLoadBot`(키트 클라이언트 그대로 · 공유 끝점 연결 수천 · 시나리오 JSON · 지연 백분위 · `--local-server` 한 프로세스 서버 조립)이 들어갔다.
  남은 것: 첫 측정 — Release · 서버 한 대 · 봇 1000/5000 으로 `chat_and_match` · `login_storm` 을 돌려 동작별 p99 와 서버 틱 p99 를 적을 것(채팅 world 채널 수천 명의
  알림 수 · 정산 일의 길이 · 매처 대기 p99 가 위 키트 항목의 판단 근거). 참고(2026-10-06, Debug · `--local-server` — 봇과 서버가 한 스레드라 서로의 시간을 잰다):
  `quick_check` 봇 300 → 4200 동작 · 28 초 · 오류 0, chat_send p50 377 ms · p99 1081 ms, 채팅 알림 185548. 전용 서버(`Server`)가 온라인 서비스를 조립하면 `--server` 로
  같은 시나리오를 돌린다(봇 머신 따로).
  **거래**: 공유 `GF_Trade` · 서버 `GF_Server_Trade`(`TradeService` — 상태 기계 · 거래 레코드 · 원장 분개 하나로 정산 · 재시작 복구 · 시한)와 스트림 바인딩이 들어갔다.
  **서버 여럿**: 접속 상태(`OnlinePresence` — 캐시 `presence:` 키, 시한 60 초 · 30 초마다 다시 적기)와 버스(`account.revoke` 로 다른 서버의 옛 세션 닫기 · `push.<서버>` 로
  다른 서버의 계정에게 알림)가 들어갔다(`OnlineMultiServerTest`). 계정 · 거래 몫의 남은 것: 게임 UDP 접속 표 서명 키(로그인 · 게임 서버 공유, 지금 서버 설정 파일)의
  배포 · 교체 절차, 인스턴스 아이템(지금 거래 다리는 가산 자산만 — 인벤토리 인스턴스 상태 칸은 거절), 실제 지연(루프백이 아닌 망)에서의 버스 · 캐시 왕복 측정.
  **경제 · 우편함 · GM**: `GF_Economy`/`GF_Server_Economy`(화폐 · 상품 카탈로그 · 구매 · 영수증 계약 + 가짜 제공자 · 서비스 · 클라이언트 · Wallet/Inventory 거울 — 로컬 지갑은
  원장 사본), `GF_Mailbox`/`GF_Server_Mailbox`(수령 = 원장 이동 · 모두 받기 · 만료 쓸기 · 전체 우편 캠페인 — 기반 `Online/Mail/ServiceMailCampaign`), `GF_Admin`/`GF_Server_Admin`
  (권한 등급 넷 · 모든 조작이 효과와 같은 트랜잭션의 감사 줄 · 환불 회수 빚)이 들어갔다. 남은 것 — ① 영수증 제공자 셋(`Receipt/Provider/<제품>/`): Apple(App Store Server API —
  서명된 거래 JWS 의 x5c 사슬을 Apple 루트로 + 거래 조회), Google(Play Developer API `purchases.products` — 서비스 계정 JWT → OAuth 토큰), Steam(`ISteamMicroTxn`
  InitTxn → FinalizeTxn — 서버가 주문을 여는 흐름) — 외부 스토어 계정 · 비용이 들어 계약 + 가짜까지만(사용자 결정), 호스트 이름 해석(Core 주소는 IPv4 뿐)이 먼저다
  ② 환불 · 결제 취소 알림 받기(Apple 서버 알림 v2 · Google RTDN · Steam 환불 → GM 회수와 같은 `refund.revoke` 빚 이동) · 유상 재화 청약철회(7 일 · 미사용분) 규칙
  ③ 인스턴스 아이템(내구도 · 강화 — 원장은 개수만, `item.<정의>#<id>` 개수 1 + 상태 레코드) ④ 잔액 · 새 우편 알림(서버 → 클라이언트 `sendPush` — 지금은 응답에 실린 잔액 · 다음 목록)
  ⑤ 원장 보존 검사 · 만료 쓸기를 예약 작업(`ServiceScheduler`)으로(지금 쓸기는 우편함 서비스 틱 주기), 분개 · 감사 보존 기간 정리, `mail_sent` · 멱등 기록 · `econ_purchase` 정리
  ⑥ GM: 에디터 패널(1-4 C 확장 지점 뒤), 접속하지 않은 계정 이름 찾기(계정 키트 저장 부분을 기반 계약으로), 첫 관리자 · GM 전용 호스트 포트를 서버 설정에(`AdminService::seedRole`)
  ⑦ 서버 실행 파일에 경제 · 우편함 · GM 서비스 조립(카탈로그 경로 · 영수증 등록부 · 지표 등록부를 서버 설정으로) ⑧ 발행 · 소각 총량 지표(보존 검사가 `economy_issued_total{asset}`).
  상용 비교: 언리얼은 Online Subsystem/EOS 등 외부 백엔드에 맡기고, 자체 MMO 서버는 IOCP/epoll 서비스 서버를 따로 둔다.
- **네트워크 보안(2026-10-06 사용자 결정 — "하지 않기로 한 것" 에서 거둠).** 스트림(서비스)은 TLS 1.3, 게임 UDP 는 연결 수립 때 키 교환(X25519) 뒤 패킷마다 AEAD(AES-GCM 또는
  ChaCha20-Poly1305 · 패킷 번호를 nonce 로 · 재전송 방지 창) — Valve GNS · 언리얼 AESGCM PacketHandler 와 같은 모양. 세션 키는 로그인 키트가 발급한 토큰에 묶는다(UDP 접속 = 토큰 제시).
  암호 구현은 직접 짜지 않는다 — 라이브러리 하나(OpenSSL 3.6 — vcpkg 에 넣었다, 감싼 폴더는 GameFramework/Base/Online/Security/OpenSSL)를 엔진 인터페이스 뒤에 두고 격리 게이트(`CheckThirdPartyIsolation`)에
  올렸다. Core 창구(`Network/Security/`) · OpenSSL 구현(`GameFramework/Base/Online/Security/OpenSSL`, `NetSecurity`) · 스트림 TLS 1.3(`StreamEndpointSettings::_security`) ·
  UDP 보안(`NetHostSettings::_security` — X25519 + 패킷 AEAD + 재전송 창 + 토큰 결속, 인증기 없는 암호화는 개발 빌드만)은 있다. 남은 것: 암호화 켠 하니스로 서버 틱
  시간을 재어 [결정 기록](09_Decisions.md) 3절에 숫자 한 줄(N18a 벤치에 `_security` 를 켠 판), 서버 호스트가 `ServerConfig::_tlsCertificateFile` · `_tlsPrivateKeyFile` · `_tlsPrivateKeySecretEnvironment`(→ `ServerSecret::read`)를 `NetSecurity::createServerTLSContext` 에 넘기는 배선.
- **패킷 압축(2026-10-06 사용자 결정).** 코덱 틀은 Core `Compression`(코덱 id 등록부), LZ4 · zstd · zlib 은 Engine 이 등록한다 — Core 네트워크는 id 로만 쓴다. 작은 UDP 패킷은 일반 압축의 이득이
  작다. 측정 벤치(`NetCompressionBenchTest`, Release 3 회 가운데 값, 패킷마다 봉투 3 B · 줄지 않으면 원문): 스냅숏(평균 1009 B) LZ4 1 %/1.1 us ·
  zstd1 15 %/11.7 us · zstd1+사전 16 KB 34 %/6.0 us, 파괴(302 B) LZ4 3 %/0.37 us · zstd1 4 %/6.4 us · 사전 35 %/1.6 us, 채팅(43 B, 합성) LZ4 1 % · zstd 0 % · 사전 27 %/0.74 us
  (us = 압축 + 해제/패킷). 켜는 규칙(15 % 이상 줄고 2 us 이하)을 등록부 코덱(LZ4 · zstd)은 어느 흐름에서도 못 넘는다 — UDP 패킷 압축은 끈다. 넘는 것은 학습 사전뿐(파괴 · 채팅):
  사전 배포(판 · id 협상 · 사전 갱신)를 하면 다시 본다. 채팅 숫자는 채팅 키트가 실제 기록을 내면 다시 잰다. 남은 것: WSL Release 로 같은 벤치 한 번(압축 시간은
  컴파일러 · libc 에 따라 다르다). 스트림 프레임 압축은 있다(`StreamEndpointSettings::_compression` — 봉투 `NetCompressionUtil`, 기본 꺼짐, 받는 쪽은 푼 크기를 몸 상한으로
  보고 풀기 전에 거절) — 채팅 기록 · 거래 내역을 실을 키트가 zstd 로 켤지 정한다.
- **MMO 규모 서버의 UDP 소켓 계층.** 지금은 호스트당 논블로킹 UDP 소켓 하나 + 전용 스레드 하나(`poll`/`WSAPoll`), 데이터그램마다 `recvfrom`/`sendto`, `NetHost` 잠금 하나.
  UDP 는 소켓이 하나라 IOCP · epoll 은 지렛대가 아니다. ① 측정(`NetUDPBenchTest`, 호스트 스위트 — 서버 20 Hz 200 B · 클라이언트 30 Hz 40 B, 서버 · 클라이언트가 한 PC,
  Release 3 회 가운데 값, 다른 빌드로 CPU 20~46 % 부하): Windows 500 — 받기 10.7k · 보내기 10.7k pkt/s, 서버 스레드 14 %, 6.6 us/패킷, RTT p50 48 · p99 48 ms /
  2000 — 42.1k · 42.5k pkt/s, 54 %(51~82 %), 6.5 us/패킷, RTT p50 68 · p99 70 ms, 커널 버림 0(RTT 는 같은 PC 의 클라이언트 드라이버 지연이 대부분). 리눅스(WSL Release,
  `ulimit -n 8192` 뒤 같은 필터)는 아직 — 숫자를 나란히 놓는다. 패킷당 6.5 us 의 대부분이 `recvfrom`/`sendto` 라 다음 일은 ②.
  ② 시스템 호출이 지배적이면 `INetTransport` 일괄 받기 · 보내기 하나의 계약 — 리눅스 `recvmmsg`/`sendmmsg`(+ GSO/GRO), Windows RIO(+ USO/URO, 없으면 `WSARecvMsg`)
  ③ 스레드 하나가 차면 포트 샤딩(두 플랫폼 같은 계약 — 로그인 토큰이 포트를 준다, 리눅스는 `SO_REUSEPORT` 를 선택) + 샤드마다 `NetHost`. 둘 다 `NetUDPBenchTest` 숫자로 전후를 잰다.

### 1-8. 성능 (재고 나서 정할 것)

- **파괴 잎 셰이프를 플레이 첫 프레임에 짓는 비용.** `FractureBenchTest.ShowcaseBeginPlay`(Release, beginPlay + 첫 틱 — 첫 물리 스텝 앞에서 상태 · 잎 셰이프를 세운다)
  p50 20.2 · 27.2 · 20.7 ms(쇼케이스 파괴물 여섯 · 잎 312, 200 조각 벽 하나가 볼록 껍질 ~11 ms = 잎당 ~55 us). 파괴물이 많은 맵이면 선형으로 는다.
  워커로 나누기는 졌다(결정 기록 3절) — 남은 후보는 쿠킹 때 Jolt 셰이프를 직렬화해 `.fracture` 에 싣기(Chaos 가 지오메트리 컬렉션에 충돌을 같이 굽는 자리) 또는
  잎 셰이프를 처음 깨질 때까지 미루기. 지금 깨지는 프레임은 200 조각 벽 4 ~ 7 ms(그중 사건 처리 2 ~ 4 ms).

- **8000 무버의 `components`(onTick) ~325 us.** 남은 비용은 오브젝트 → 틱 항목 → 컴포넌트 포인터 추적이다. 더 줄이려면 오브젝트 모델 밖 배치 경로
  (언리얼 Mass · 유니티 DOTS 자리)나 트랜스폼 SoA 2 단계가 필요하다 — 큰 구조 변경이라 할지부터 정한다(1-11 의 구조 후보).
- **직렬화기(이름 대조 · 텍스트 파싱)와 리소스 로드의 리플렉션 비용, 비동기 씬 로드 중 최악 프레임을 재지 않았다.** 큰 씬 · 쿠킹본으로 잰다(Dev 는 `Cooked/`
  를 마운트하지 않는다).

- **조건부 후보 묶음.** 병렬 틱 문턱의 교차점 · GameObject 레이아웃 · 적응형 틱 문턱 · 스폰 비용(~1.1 us, 잠금 여섯) · 시퀀서 성능 수치. TickItem 인라인
  재시도는 오브젝트의 틱 부기 49 B 를 먼저 줄여야 한다. 측정해서 이기면 한다.
- **월드 공간 UI 를 대량으로 그리는 비용을 재고, 넘으면 GPU 로 옮긴다.** RTS 이름표 · HP 바 2,000 개 시나리오로 게임 스레드(마커 투영 · 그리기 목록)와 렌더 스레드
  비용을 Release 로 잰다. 게임 스레드가 0.5 ms 를 넘으면 위치 투영 · 컬링 · 간접 드로우를 컴퓨트로 옮긴다(메시의 GPU 커맨드 생성과 같은 구조; 언리얼은 대량
  머리 위 표시를 Niagara 로 권한다). 데미지 숫자처럼 시간 함수로만 움직이는 UI 의 GPU 애니메이션도 같은 측정으로 본다.
- **글리프 SDF 를 컴퓨트로 만들지 재고 정한다.** 한글 장문을 처음 띄우는 프레임(새 글리프 수백 개)의 최악 시간을 Release 로 잰다. 히치가 4 ms 를 넘으면 FreeType 은
  외곽선만 읽고 SDF 는 컴퓨트(점프 플러딩)로 만든다.
- **FrameRenderer 진단 세터**(`setMeshMorphDiag` · `setDrawMergeEnabled` · `setVertexPoolEnabled`)는 Shipping 제외 후보.

### 1-9. 빌드 · 린트 · CI · 테스트

- **규칙 예외 감사(2026-10-07)에서 정한 셋의 적용** — 두 플랫폼 공통 명시적 경고 목록, `(void)` 이유 주석은 `[[nodiscard]]` 실패 가능 함수에만,
  출처를 모르는 리소스 교체.
  결정과 근거는 [결정 기록](09_Decisions.md) 5-2.
- **PCH 를 끈 Windows Debug 에 `-Wsign-conversion` 경고 31 건**(2026-10-07, `9f595052e`): `Core` 의 `Memory.h:296` · `vector.h:525/897/1004/1010` ·
  `unordered_map.h:551` 템플릿 본문이 `char` → `unsigned char` · `int` → `uint32/size_t` 로 인스턴스화되는 곳. PCH 를 켜면 안 보인다. 부르는 쪽(인스턴스화한 TU)을
  찾아 고친 뒤 `RunBuildWarnings.py --preset Ninja-Debug` 를 PCH 끈 빌드 폴더로 확인한다.
- **커버리지 안내 퍼저(`LoaderFuzzer`)는 아직 한 번도 지어지지 않았다**(2026-10-06 들임, 리눅스 전용 — 이 PC 는 WSL 을 쓰지 않았다). 첫 `fuzz.yml` 실행
  (밤 또는 `workflow_dispatch`)이 구성 · 링크(`-fsanitize=fuzzer-no-link` 엔진 + ASan, `-fsanitize=fuzzer` 실행 파일)와 대상마다 60 초가 끝나는지 본다.
  지면 그 실행의 주석 · 아티팩트(`fuzz-findings`)로 고친다.
- **Windows CI Debug · STL 잡의 키트 시험 22 건 실패**(run 37251047781, `a380e2ee3` — 유니티 켬 + PCH 끔 Debug 에서만): 재현 안 됨 — 이 PC 의 같은 구성
  (`CI-Debug -DSW_ENABLE_PCH=OFF`)에서 통과했다. B4 로 실패 보고(`CiFailureReport.py`)가 나아진 뒤 다음 CI 주석으로 본다.
- **imgui-node-editor vcpkg 오버레이**(`ThirdParty/imgui-node-editor/vcpkg-port/`, `<exception>` 패치)는 업스트림이 같은 고침을 받으면 지운다(2026-10-06 확인: 업스트림 vcpkg port-version 4 · 원본 master · develop 모두 아직 없음. vcpkg PR 은 내지 않는다 — 사용자 결정, 외부 공개).
- **include · 전방 선언 남은 후보.** ① OS 헤더(`Core/Common/PlatformOsHeaders.h` — `Windows.h` · `DbgHelp.h` · `Xinput.h` …)가 `EngineMinimal.h` 를 거쳐
  PCH 에 남아 있다(TU 2746 · `windows.h` 1671). 빼려면 먼저 `NOMINMAX` · `WIN32_LEAN_AND_MEAN` 을 CMake 정의로 옮기고(서드파티가 `windows.h` 를 먼저 include 해도
  min/max 매크로가 안 생기게), Win32 · POSIX API 를 쓰는 파일이 직접 include 한다 — 글자 그래프로 찾은 후보 26 개(`Core/Common/Macros.h` · `ModuleCompiler.cpp` ·
  `WindowsFileWatcher.h` · `InputManagerWin32.cpp` · `XInputGamepadDevice.h` · `TestFramework.cpp` …, 오탐 섞임)를 빌드로 하나씩 확인하는 단계다.
  ② `GameObjectManager.h` 의 값 멤버 서브시스템 8 개(`ScenePhysics` · `SceneNavigation` · `SceneAudio` · `SceneOverlapWorld2D` · `PrimitiveRegistry` ·
  `ComponentRegistry` · `CameraRegistry` · `AnimationSystem`)를 `unique_ptr` 로 바꾸면 GOM 을 include 하는 234 TU 에서 헤더 32 개(약 4,700 줄, 서드파티 없음 —
  Physics/* · Navigation/* · Animation 보조)가 빠지지만, getter 를 쓰는 85 파일이 직접 include 해야 해 실제로 덜어지는 것은 약 150 TU × 4.7k 줄이다 — 보류
  (2026-10-05 재측정, `_store` · `_transformHierarchy` · `_structuralChangeBuffer` · `_tickScheduler` 는 헤더의 인라인 · 템플릿이 써서 포인터로 못 뺀다).
  이득은 `ninja -t deps` 전후 TU 수로 판정한다.
- **CI 린트 잡이 처음으로 린트 전부를 리눅스에서 돈다**(`RunLintSuite`, 2026-10-06 — 그 전엔 `CheckCodeConventions` 하나). 첫 실행에서 지는 게이트가 있으면
  그 게이트의 결함(경로 대소문자 · 줄끝 · 외부 도구)이다 — 고치고, 급하면 그 단계에 `continue-on-error` 를 한 번 두고 여기 적는다. 훅 상한
  `kHookBudgetSeconds`(1: 8 · 10: 12 s)는 이 PC 기준 짐작이다 — 첫 `lint-timing` 아티팩트를 보고 러너 값의 1.5 배로 고친다.
- **Scripts 정리의 남은 후보**(2026-10-10, `CodeText` 도우미 · `lint/conventions/` 나누기 뒤). 커밋 훅이 staged 1 개 9.5 s 로 아직 상한 8 s 위다 —
  남은 바닥은 트리 전체 게이트 `CheckConfigReference`(단독 ~7~14 s, 생성 문서를 통째로 다시 만들어 견준다)와 `CheckDuplicateTypeNames` ·
  `CheckKitNamespaces`(각 ~5~9 s)다. 파일 단위로 바꿀 수 있는지(staged 가 PROPERTY · gv · 설정 파일을 건드릴 때만 돌기) 먼저 본다.
  ① `CheckOutParameterNames`(맨이름 `out`)는 `CheckCodeConventions` 의 `Naming/OutParameter` 와 같은 주제다 — 줄 규칙 하나로 합치면 게이트 하나가 준다
  (CTest 이름 · `mapExemption` 표를 옮기는 일). ② `dev/MoveEngineFolders.py` · `MoveGameFrameworkLayout.py` 는 "이동 표 + git mv + 경로 치환" 이 같은
  모양이다 — 두 계획(`docs/plans/EnginePartition.md` · `GameFrameworkLayout.md`)이 끝나면 지우거나 공용 이동기 하나로. ③ `CheckCoreLayers._kHiddenLogUse` 는
  `mapExemption` 밖의 예외 표다(이름이 `CheckExemptionTables` 의 패턴을 비켜 간다) — 옮기면 OK 줄에 "예외 5 줄" 이 붙는다.
- **훅의 `CheckHeaderSelfContained` 도 뒤에서 띄울지.** staged 헤더마다 ~1.3 s CPU(헤더 6 개 1.9 s 벽 / 9 s CPU) — 지금은 이 프로세스에서 다른 파일 단위 게이트와
  차례로 돈다. 헤더가 든 커밋에서 트리 전체 게이트와 겹치면 ~1.5 s 를 더 벌 수 있다. `LintGate.preCommitRunsInBackground` 같은 선언 하나로 — 재고 넣을 것.

### 1-10. 관찰 중 — 다시 보이면 원인을 판다

- **Vulkan(FIFO) + VSync 에서 가끔 한 프레임이 여러 주기를 놓친다**(Release 큐브 100, 165 Hz). 2026-10-06 조사: 46 판 중 4 판에서 600 프레임 창 안에 한 프레임이
  12~24 ms(제안서 측정의 146 ms 정지는 재현되지 않았다), DXGI(DX12 · DX11)는 같은 조건에서 판당 한 주기 이하. 평소 Vulkan 은 `vkAcquireNextImageKHR` 에서 ~5.2 ms 를
  기다리고(DXGI 는 Present 안), 느린 프레임을 시각으로 쪼개면 둘로 갈렸다 — 이미지가 한 주기 늦게 돌아온 것(acquire ~10 ms)과, GPU 일이 0.3 ms 인 프레임의 펜스가
  13 ms 뒤에 신호된 것(창 모드 Vulkan 프레젠트가 합성기를 거치는 길). 그 프레임에 엔진 CPU 일은 없었다. 스왑체인 이미지를 4 개로 늘리면 오히려 잦아졌다(결정 기록 3절).
  다시 보이면 `-gv_tracy=1` 로 acquire · 펜스 · present 를 시간축으로 보고, 전체 화면(독점) · NVIDIA "Vulkan/OpenGL 프레젠트 방식" 설정을 바꿔 가른다.

- **CI Windows Debug `EngineTest_NoGPU_Shard3` 가 모두 통과한 뒤 0 이 아닌 종료 코드로 졌다**(run 37457443235 · `28d5627e3`, 한 번 — 직전 실행은 통과).
  로그는 `TaskManager Shutdown cleanly` 에서 끝나고 크래시 스택 주석이 없었다. 이 PC Debug 로 같은 조각(`--test_shard=2/3 --host_suites=exclude`) 15 회 순차 ·
  12 회 4 개 동시 — 재현 안 됨. 하네스가 이제 마지막 줄 `[TestHost] shut down - exit code N` 을 찍는다: 다시 나면 그 줄이 없으면 `TestHostRuntime::stop`
  안(부트스트랩 · 로거 · 메모리 프로파일러 종료), 있으면 정적 소멸자다.
- **Shipping `EngineTest_NoGPU` · HostOnly 간헐 세그폴트**(09-20 · 21 · 22 에 한 번씩, 2026-10-06 nogpu 3 회 · host 3 회 재실행 깨끗). 다시 나면: 크래시 핸들러의
  스택 파일(`Bin/Saved/Logs/crash_<세션>.stack.txt` — Shipping 도 이제 PDB 가 있어 함수 이름), CI 는 그 파일을 아티팩트로 · 스택을 주석으로 올린다(`CiFailureReport.py`).
- **WSL lavapipe 가 가끔 서피스를 잃는다**(`AppTest_HostOnly` 43 회 중 3 회, 첫 `vkAcquireNextImageKHR` 가 `VK_ERROR_SURFACE_LOST_KHR`) — 이제 서피스 · 스왑체인을
  다시 만들고(사양대로) `Vulkan surface lost at acquire|present (N time(s) …)` 경고를 남긴다. WSL 에서 50 회 돌려 경고 수 · 실패 수를 본다(복구가 되면 항목을 지운다):
  `cd build/WSL-Debug/Bin && for i in $(seq 50); do ./AppTest --host_suites=only --test_filter=AppSmokeTest.* || echo FAIL $i; done` 와 `Saved/Logs` 의 경고 줄 수.
- **CI Windows 러너(WARP)의 픽셀 시험 실패**(`RenderPassGPUTest` 를 host 스위트로 빼서 우회) — 이 PC 에서 `SW_RHI_SOFTWARE_ADAPTER=1`(`-gv_rhiSoftwareAdapter=1`)로
  같은 래스터라이저를 고를 수 있다. 2026-10-06 이 PC 의 WARP(DX12 · DX11)로 `RenderPassGPUTest.*` 71 개 픽셀 시험이 모두 통과했다(Vulkan 은 CPU 디바이스가
  없어 빠지고 GL 은 하드웨어) — WARP 자체는 컴퓨트 컬링 · 인디렉트를 한다. 러너 쪽(WARP 판 · 창 없는 세션)을 다음 CI 실패의 주석으로 가른다.
- **리눅스에서 아직 자동으로 안 도는 것**: X11 입력(좌표 · `XkbSetDetectableAutoRepeat` — WSLg 의 `DISPLAY=:0` 이 있으니 손으로 한 번), yad 파일 대화상자의 두 번째
  `--file-filter`(man 으로만 확인), 리눅스 CI 가 초록인지 · IPO 가 실제로 켜졌는지(2026-10-05 실행은 네 잡 모두 Configure 의 vcpkg 설치에서 졌다 — 다음 실행부터
  `CiFailureReport.py configure` 가 포트 로그 끝을 주석으로 올린다). 2026-10-05 WSL-Debug 로 돌려 확인한 것(net-ci-rest 제안서 조사): POSIX `pipe2` · `close_range` ·
  `launchDetached`, `alarm` 시한, dlsym 도장, `parseWriteTime`(int64 를 넘는 스탬프 시각), 리눅스 ThinLTO(`llvm-ar`) 링크.

### 1-11. 결정이 필요한 것

- **전용 서버의 지형 레이어 질의는 레이어 0 이다** — 스플랫 가중치가 텍스처(.dds)라 서버 패키지에서 빠진다(쿠킹 표 `Texture`). 서버 게임 로직이 지형 레이어
  (발소리 판정 · 표면 마찰 등)를 쓰게 되면 스플랫을 데이터 종류로 쿠킹하거나 그 경로를 서버 패키지에 남긴다.
- **(보류 — 사용자 결정 2026-10-03) `hashed_string` 에 FName 숫자 꼬리를 둘지.** 지금은 비교 · 표시 인덱스 두 칸(8 바이트)이라 `"Enemy_12"` · `"Enemy_13"` 이
  이름 표에 각각 영구 적재된다. 런타임에 번호 붙은 이름을 대량으로 만드는 경로(복제 · 스폰 이름 자동 부여)가 생기면 다시 본다 — 넣으면 `_숫자`(앞자리 0 제외)를
  떼어 정수 칸에 두고 비교는 (인덱스, 숫자) 쌍.

### 1-12. 낮은 우선순위 · 조건이 오면

- **월드 공간 위젯의 입력 · 가려짐(runtime-ui 4-6 뒤)** — World 위젯은 그리기만 한다: 레이로 사각형을 맞혀 위젯 좌표로 사건을 보내는 길(언리얼
  `WidgetInteractionComponent`)이 없다. 화면 마커의 "벽에 가려지면 숨김" 은 레이캐스트 질의 · 깊이 버퍼 읽기 둘 다 없다. World 렌더 텍스처는 내용이 바뀔 때만 다시
  그리지만 화면에 안 보일 때도 칠하기는 돈다(보일 때만 칠하기 — 절두체 판정은 조건: 월드 위젯이 수십 개를 넘을 때).

- **상호작용 대상의 공유 공간 격자**(조건: 하는 쪽 · 대상이 모두 수천 — 지금 하는 쪽은 등록된 대상 목록을 한 번 돈다, 대상 16 · 10k 오브젝트에서 +2~6 us).
  대상이 움직일 수 있어 격자를 프레임마다 맞추는 비용이 목록을 도는 비용과 같으므로, 하는 쪽이 여럿일 때만 씬당 하나(`SpatialHashGrid2D`, 셀 = 최대 상호작용 반경)를 틱 앞에 맞춘다.

- **씬 · 프리팹 파일을 넘는 오브젝트 참조**(조건: 레벨 스트리밍 · 하위 레벨 · 다른 씬의 오브젝트를 가리키는 데이터가 생기면). 그때 오브젝트마다
  영속 GUID 를 싣는다(언리얼 `FSoftObjectPath` 의 하위 오브젝트 경로 자리). 지금은 파일 안에서 엔티티 `id` 로 가리키고, 파일을 넘는 데이터 · 코드가 0 이다.
- **150 % 모니터 실물 확인** — 대체 시험(`dpi.monitorScaleFollows`: 창 DPI 질의를 +0.5 로 바꿔 끼움 · WM_DPICHANGED)이 글자 · 여백 · 창 · 백버퍼를 본다.
  남은 것은 글자 래스터 선명도 — 150 % 모니터에서 에디터를 띄워 눈으로 한 번(2026-10-06 이 PC 의 기동 로그가 `Editor UI scale 1.5 (monitor DPI)` 였다).
- **100 줄 넘는 함수 정리.** 분해는 총량을 줄이지 않는다. 중복을 먼저 없애고, 그래도 문제면 본다. 목록이 필요하면 여러 줄 시그니처를 중괄호 깊이로 재는 스크립트로
  뽑는다(단순 정규식은 틀린다).
- **타일맵 칸 데이터를 일반 레이어로**(두 번째 장르가 칸마다 다른 값 — 지형 비용 · 발소리 — 을 원하면): 지금 레이어는 0/1(`kArrTileFlagLayerInfo`)이고 워프 · 역할 · 스폰은
  전용 원소다. 값 종류를 정수로 넓히고 에디터 페인트 · 형식 시험을 같이 바꾼다(Godot TileSet custom data 모양).
- **걸음 조우 판정 둘**(Overworld `shouldEncounterOnStep` 의 결정적 주기 · ClassicJRPG `JRPGEncounterWalker` 의 확률 + 유예) — 오버월드 위에 JRPG · 몬스터 수집 게임이 서면 기반 `World/` 로 하나를 올린다.
- **MMO 갱신 확인을 `NetConnection` 전달 통지로**: 패킷 확인 → 메시지 전달 통지가 Core 에 생기면 키트 확인 메시지(`kUpdateAck`)를 지우고 그 통지로 판정한다(언리얼 NAK 자리).

- **스크린 리더**(위젯 접근성 이름 · 역할 → OS 내레이터 — 언리얼 Slate 접근성 · Xbox 접근성 지침 107): 런타임 UI 의 접근성은 글자 크기 · 자막 · 색각 · 고대비까지다.
- **MSDF 글리프**(직접 — 윤곽 모서리 칠하기 · 채널별 거리, 큰 글자의 모서리가 날카롭다) — 지금은 단일 채널 SDF(`Engine/Text/GlyphCache`, FreeType `sdf` 렌더러,
  결정 R2). 아틀라스 페이지가 R8 이라 MSDF 는 RGB 페이지 · 셰이더 median 이 함께 든다.
- **서드파티 빈자리(2026-10-05 후보 중 사용자가 고르지 않은 것).** 리눅스 오디오 출력 없음(`XAudio2System` 만, 리눅스는 `NullAudioSystem`) → miniaudio(퍼블릭 도메인/MIT-0) ·
  `gv_renderScale` 을 읽는 업스케일 없음 → AMD FidelityFX FSR(MIT) · 아랍어 셰이핑 없음(양방향은 `Engine/Text/TextBidi` 단순판 — 포개진 방향 제어 문자 ·
  숫자 앞뒤 기호 규칙 미지원) → HarfBuzz(MIT) + SheenBidi(Apache 2.0)
  (꽂을 자리는 `Engine/Text/ITextShaper` — 런 하나, 같은 글꼴 바이트 `IFontRasterizer::findFaceBytes` 로 `hb_face` 를 만든다. SheenBidi 는 `TextBidi` 를 대신한다). 들이면 Jolt · Recast · Tracy 처럼
  엔진 인터페이스 뒤 + 격리 게이트, vcpkg 변경은 main 에서 먼저.
- **임포터가 1 채널(R8) DDS 를 내는 규칙**(조건: 마스크 · 1 채널 텍스처 에셋이 생기면) — RHI · 로더는 `R8_UNORM`(DXGI 61, DX10 머리)을 읽는다. `App --import-textures` 의
  규칙 표(`TextureImportConfig.json`)에 R8 출력이 없다.
