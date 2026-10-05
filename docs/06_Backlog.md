# 작업 백로그 — 남은 일과 참고

> 여러 PC · 여러 세션이 함께 쓰는 **할 일 목록**이다. 여기에는 **아직 열린 일**과 **그 일을 하는 데 필요한 지식**만 둔다.
>
> - 일을 끝내면 그 항목을 **지운다**(같은 커밋에서). 남길 교훈이 있으면 3절에 한두 줄로 옮긴다 — 무엇을 고쳤는지의 사연은 적지 않는다.
> - 새로 찾은 결함 · 미룬 일은 1절의 해당 영역에, 결정을 기다리는 질문은 1-11 에 적는다.
> - 지나간 일의 이력은 `git log` 에 있다. 2026-10-03 까지의 전체 이력이 든 옛 백로그(약 19,400 줄, "최근에 끝낸 일" 포함)는 커밋 `7ce95fc8` 의
>   `docs/06_Backlog.md` 다 — `git show 7ce95fc8:docs/06_Backlog.md`. 코드 주석 · 커밋 메시지가 옛 날짜 항목("2026-09-17 참고" 등)을 가리키면 거기서 찾는다.
>
> 마지막 정리: 2026-10-03 (기준 커밋 `7ce95fc8`).

---

## 0. 손대기 전에 — 검증 규칙

숫자 기준선(테스트 수 · 창 수 · 시간)은 적어 두지 않는다. 금방 낡는다. **비교가 필요하면 바꾸기 전과 후를 그 자리에서 잰다.**
통과 기준은 "전부 통과, 스킵은 Dev 전용 케이스 · 그 플랫폼에 없는 타깃뿐" 이다.

```powershell
cmake --build --preset Ninja-Debug            # 경고 0 이어야 한다
cmake --build --preset Ninja-Shipping         # Debug 가 숨기는 결함이 여기서 드러난다
ctest --preset Ninja-Debug-lint               # 컨벤션 · include 순서 · 린트 자기 시험
ctest --test-dir build/Ninja-Debug -L nogpu --output-on-failure
ctest --test-dir build/Ninja-Shipping -L nogpu --output-on-failure

# CI 가 못 도는 부분 — GPU 있는 PC 에서 일을 끝내기 전에 반드시 돌린다.
# AppSmokeTest 가 네 백엔드 + 에디터(dx12 · gl)를 띄워 종료 코드 0 · 로그 [Error] 0건을 본다.
ctest --test-dir build/Ninja-Debug -L hostgpu --output-on-failure
ctest --test-dir build/Ninja-Shipping -L hostgpu --output-on-failure

# ASan (Windows)
cmake --preset Ninja-Debug-ASAN
cmake --build --preset Ninja-Debug-ASAN
ctest --test-dir build/Ninja-Debug-ASAN -L nogpu

# 정적 분석 — 게이트가 아니라 판단 자료다(3절 "빌드 · 린트 · 도구" 의 clang-tidy 참고)
py -3 Scripts/lint/report/RunClangTidy.py [--filter Core] [--clang-tidy <경로>]

# 눈으로 봐야 할 때 — 종료 코드 0, 로그에 [Error] 0건
cd build/Ninja-Debug/Bin
./App.exe -gv_profileFrames=40 -dx12 -EnableEditor
./App.exe -gv_profileFrames=40 -dx11 -EnableEditor
./App.exe -gv_profileFrames=40 -vk   -EnableEditor
./App.exe -gv_profileFrames=40 -gl   -EnableEditor
```

**단위마다 이 순서로 한다.** 시험 먼저(이전 코드에서 지는 것을 확인) → 고침 → 변이 검사(고침을 되돌리면 시험이 지는가) →
Debug · Shipping · hostgpu → 커밋. 변이로 죽지 않는 시험은 아무것도 지키지 않는다.

**에디터 패널에는 단위 테스트가 없다.** 컴파일이 통과해도 화면이 비어 있을 수 있다. 패널 · 위젯을 건드렸으면 실행해서 본다.
화면을 볼 수 없으면 `-gv_editorPanelDump=N` — N 번째 ImGui 프레임에 창 하나당 한 줄(이름 · 크기 · 정점 수 · 상태)을 남긴다.
**보이는데 정점이 0 인 패널**이 빈 패널이다. 의미 있는 신호는 "내용 없는 패널 0개" 쪽이고, 창 수는 패널이 늘면 바뀐다.
도구 패널까지 보려면 `-gv_editorOpenPanel=all` 을 같이 준다(모두 열고 · 저장된 도킹을 쓰지 않고 · 첫 크기 900×620, 그 실행의
레이아웃은 저장하지 않는다).

```powershell
cd build/Ninja-Debug/Bin
./App.exe -gv_profileFrames=60 -dx12 -EnableEditor -gv_editorOpenPanel=all -gv_editorPanelDump=40 > before.log
# ... 고친 뒤 같은 명령으로 after.log 를 떠서 비교한다
```

**기본 실기동은 테스트 씬을 연다.** `GameConfig._startupScene` 이 `game/empty/maps/editortest.scene.xml` 을 가리킨다(배포본도 같다).
그 씬의 메시는 `_meshId` 가 비어 **화면에 기하가 없다** — 픽셀 비교에는 벤치 큐브(`-gv_benchMeshes=N`)를 쓴다.
다른 씬은 `"-gv_editorStartupScene=<경로>"` 로 연다. **PowerShell 은 점이 든 인자를 쪼갠다 — 따옴표로 감쌀 것.**
씬 · 프리팹 에셋은 **엔진 직렬화기로 만든다**(손으로 쓴 XML 은 깨진다 — 3절 "직렬화" 참고).

**렌더 결과는 픽셀로 잰다.** `-gv_screenshot=out.ppm`(`-gv_screenshotFrame=N`)은 톤맵까지 든 최종 화면이다. 눈으로 보지 말고
배경과 다른 픽셀 수 · 채널 평균을 센다. `-gv_viewMode=<0|1|2>`(Lit · Unlit · Wireframe)는 에디터 없이도 고른다.
**벤치는 `-gv_benchAnimate=0` 이어야 결정적이다**(네 백엔드 0 바이트 차이). 애니메이션을 켠 채 차이를 주장하려면 같은 조건
두 판으로 잡음 바닥을 먼저 잰다.

```powershell
./App.exe -dx12 -gv_benchMeshes=8 -gv_benchAnimate=0 -gv_profileFrames=20 -gv_viewMode=2 "-gv_screenshot=wire.ppm"
```

**함정**

- 백엔드는 `-dx11 / -dx12 / -vk / -gl` 로 고른다. 명시한 백엔드를 쓸 수 없으면 폴백하지 않고 에러로 선다(`RHIBackendUtil::findCommandLineBackend`
  한 자리 — `-gv_rhiBackend=Vulkan`(열거자 이름 또는 숫자, 모르는 이름이면 기동이 멈춘다)도 같은 판정을 지난다). 실제로 뜬 백엔드는 로그 `Initializing RHI with backend:` 로 확인한다.
- 에디터는 `-EnableEditor` 를 줘야 뜬다. 없이 돌린 검증은 에디터 OFF 검증이다 — 로그로 실제로 로드됐는지 본다.
- Shipping 테스트 바이너리는 `build/Ninja-Shipping/TestBin/` 에 있고 작업 디렉터리는 `Bin/` 이다. `Bin/` 에 남은 낡은 테스트 exe 사본을
  실행하지 말 것.
- 셰이더 소스(.hlsl/.hlsli)를 고쳤으면 다시 쿠킹한다. 빌드는 HLSL 을 다시 쿠킹하지 않는다 — `App.exe --cook-shaders` 뒤에 재고 비교한다.
  Shipping 쿠킹은 `cook.stamp` 의 내용 해시로 검증하고 어긋나면 빌드를 세운다(`Scripts/generate/CookAssets.py --verify-shaders`).
- 비결정적 실패는 한 번 재현으로 "확정" 이라 부르지 않는다. 3~5 회 다시 돌려 재현율을 본다. 간헐 실패를 보면
  `--output-on-failure` 출력 전체를 파일로 남긴다(요약만 남으면 어느 케이스인지 모른다).
- 직접 실행으로 통과해도 CTest 에서 질 수 있다(병렬 부하 · 시한). 끝내기 전 검증은 CTest 로 한다. 멈추면 `LastTest.log` 의 마지막 `[ RUN ]` 을 본다.

---

## 1. 남은 일

영역별로 묶었다. 영역 안에서는 위에 있을수록 먼저 볼 것이다. 줄 번호는 2026-10-03 기준이라 어긋날 수 있다 — 함수 이름으로 찾는다.
"확인 필요" 가 붙은 항목은 열려 있는지부터 확인하고 시작한다.

### 1-1. 직렬화 · 리플렉션

- **씬 · 프리팹 파일을 넘는 오브젝트 참조가 없다.** 파일 안에서는 엔티티 `id` 로 가리킨다. 파일을 넘는 참조가 필요해지면 오브젝트마다 영속 GUID 를 싣는다.

- **XML 문자열 속성은 읽을 때 끝 공백(줄바꿈)을 잘라 왕복이 고정점이 아니다**(2026-10-06, `SerializationRoundTripTest` 가 찾음). `MissingComponent::_originalText`
  가 줄바꿈으로 끝나 다시 쓰면 `&#10;` 이 빠진다 — 지금은 오브젝트 상태 직렬화기가 원문을 그대로 다시 써서 데이터는 잃지 않고, 시험은 이 타입을 뺀다.
  끝 공백이 뜻을 갖는 문자열 칸이 생기면 XML 읽기의 자르기를 속성 값에서 걷어낸다.

- **JSON 소유 포인터 원소가 `{ "타입이름": {...} }` 꼴이 아니면 말없이 건너뛴다**(`JsonSerializerInternal::ContainerReader::readOwnedPointer`). 실패로 알리면 `_listComponent`
  칸 전체가 실패하므로 그 원소만 orphan 으로 남기는 길이 필요하다(XML 은 태그가 곧 타입이라 이 모양이 없다).

### 1-2. 오브젝트 · 씬 · 틱 · 물리

- **프리팹 오버라이드의 남은 모서리 셋** — (1) 인스턴스에 더한 컴포넌트는 로드 때 목록 끝에 붙어, 가운데 있던 것은 저장 · 로드 뒤 순서가 바뀐다 (2) 물려받은
  컴포넌트의 이름표를 바꾸면 제거 + 추가로 기록돼 그 컴포넌트에 프리팹 수정이 더는 닿지 않는다(UE 도 물려받은 컴포넌트 이름 변경을 막는다 — 에디터에서 막을지)
  (3) 프리팹에서 사라진 컴포넌트의 오버라이드는 경고와 함께 버려지고 다음 저장에서 사라진다.
- **중첩 프리팹이 정식으로 없다.** 프리팹은 GameObject 하나(컴포넌트 트리)라 메시 · 이펙트 여럿을 담을 수는 있지만, 프리팹 안에 다른 프리팹의
  인스턴스를 오버라이드와 함께 두는 형식은 없다 — 지금은 컴포넌트가 로드 뒤 스폰하는 우회뿐이다(순환 스폰 방어는 있다, `PrefabTest.CircularReferenceSpawnProtection`).
  아이템 부품 · 탈것 · 건물처럼 조립하는 것이 늘면 필요하다: 상태 안의 프리팹 인스턴스 노드(경로 + 오버라이드), 원본 프리팹을 고치면 모든 상위가
  따라감, 순환은 로드 오류, 쿠킹은 펼치거나 참조 유지(로드 시간으로 고름), `collectReferencedPrefabPaths` 가 프리로드 수집에 그대로 쓰임.

- **`getAllGameObjects()` 값 반환이 6 곳에 있다(모두 일회성).** 프레임 경로에 들어오면 `getAllGameObjects( out )` 또는 `forEachGameObject` 로 바꾼다(조건부).
- **강체 물리의 다음 조각.** (1) 볼록 껍질 · 삼각 메시 셰이프를 `.mesh` 에서 채우는 길이 없다 — 지금은 셰이프 서술자에 점 · 인덱스를 직접 적는다
  (`MeshCache` 의 CPU 정점이 필요하다) (3) 에디터에 물리 컴포넌트의 셰이프 시각화 · 기즈모가 없다(`gv_physicsDebugDraw` 가 게임 뷰의 디버그 선으로만 그린다)
  (4) Box2D 는 한 스레드로 돈다 — 2D 바디가 수천이 되면 전용 워커를 붙인다 (5) 3D 질의는 가장 가까운 것 하나 · 겹침 목록뿐이다(레이의 모든 닿음 ·
  스윕 다중 닿음이 필요해지면 더한다) (6) 겹침 월드(`PhysicsWorld` · `BoxCollider2DComponent`)는 강체 씬과 따로 돈다 — 키트의 투사체 · 근접 판정이
  Box2D 센서로 옮겨 가면 겹침 월드를 걷어낸다.
- **3D 내비메시(Recast & Detour, `Source/Engine/Navigation/README.md`)의 남은 것.** 베이크(쿠킹 · 런타임) · 경로 · 레이캐스트 · 군중 · 장애물 · 파괴 재베이크 ·
  디버그 선 · Shooter3D 적은 들어갔다. (1) 오프 메시 링크(사다리 · 점프 · 문)와 그 애니메이션 (2) 지형 높이장 · 식생을 베이크 기하로(`INavGeometrySource`)
  (3) 장애물이 많고 자주 움직이면 TileCache(압축 층) — 지금은 타일을 통째로 다시 베이크 (4) 에디터의 내비메시 보기 · 베이크 버튼 (5) 행동 트리 이동 노드가
  `INavMover` 를 쓰게(격자 · 내비메시 공통) (6) 다른 시험 게임(AbilityArena 등)의 적도 내비메시로.
- **`MeshInstanceBatch` 의 한계.** 항목 수가 만들 때 정해지고(resize 없음, `setEntryVisible` 로 숨기기만), 배치 하나 = 메시 · 머티리얼 하나라 항목별
  머티리얼 · 투명 정렬이 없다.

### 1-3. 그래픽스 · RHI · 셰이더

- **추가 뷰(CCTV · PiP · 렌더 텍스처)의 투명 순서는 주 카메라 기준이다.** 투명 순서의 정본이 CPU `sortTransparent` 하나가 되면서(twod-basics) 시선 축 · 깊이를
  주 카메라로 한 번 정하고, GPU `instancesort` 는 뷰마다 인스턴스 번호로 되돌리기만 한다. 추가 뷰가 주 카메라와 크게 다른 쪽을 보면 겹친 투명 물체의 앞뒤가 틀린다 —
  뷰마다 CPU 정렬 키를 따로 두거나 뷰별 키를 GPU 정렬에 다시 넣는다.

- **`.hdr` 원본 임포트가 없다** — 지금 임포트는 `.hdr` 을 만나면 8 비트로 자르지 않고 실패로 알린다. HDR 원본이 필요해지면 DirectXTex `LoadFromHDRFile` → BC6H.

- **창(백버퍼)을 읽는 창구가 없어 창 쪽 반전을 시험이 못 본다.** `-gv_screenshot` 은 오프스크린 텍스처를 읽으므로 창으로 옮기는 단계(GL 캡처 블릿)의
  상하 반전은 캡처로 보이지 않는다(`3b9a6bdc`). 지금은 `Scripts/dev/CompareWindowToCapture.py` 로 손으로 잰다. 스왑체인에 백버퍼 읽기를 두면 hostgpu 시험으로 바꿀 수 있다.
- **2D 의 남은 것(2026-10-04 twod-basics)** — (1) 파이프라인을 게임이 데이터로 고르는 자리(지금은 `-gv_renderPipeline` 뿐 — 게임 프리셋 · gamesettings 에)
  (2) 픽셀 퍼펙트의 Upscale Render Texture(기준 해상도 타깃 + 정수 업스케일 패스) (3) 2D 빛 텍스처 · 자유 모양 빛 · 부드러운 그림자 · 빛 블렌드 스타일
  (4) 타일맵 청크(한 레이어 65536 칸)와 편집 중 미리보기(지금은 플레이 때 배치를 만든다), 에디터 칸에 아틀라스 그림 (5) 테두리를 픽셀로 적는 9-슬라이스.
- **점광 · 스폿 그림자** — RHI 텍스처 차원(배열 · 큐브, 면 단위 타깃 · 올리기 · 읽기)은 있다. 남은 것: 그림자 패스 다중 뷰(면 여섯) → 셰이더 쪽(DX12 · Vulkan
  큐브 · 배열 bindless 테이블, DX11 · GL TextureCube 슬롯) + `swSampleShadowAtWorld`. 3 단계 전에 큐브 대신 2D 아틀라스(Unity URP · Godot — RHI 변경 없음)로 갈지 먼저 정한다.
- **컷 준비(프리웜)의 선행 조건 셋**(1-6 카메라 항목의 5 단계가 기다린다) — LOD 시스템이 없다, 밉 단위 텍스처 스트리밍이 없다(`AssetStreamingQueue` 는 에셋
  단위 비동기 읽기), PSO 를 미리 만드는 창구가 없다(DX12 PSO 생성 히치).
- **환경(지형 · 식생 · 물, `Engine/Environment`)의 남은 것** — 들어간 것과 계약은 [Environment/README.md](../Source/Engine/Environment/README.md).
  (1) Jolt 가 들어오면 지형 → `HeightFieldShape`(`TerrainHeightfield::getHeightSamples` · 구멍 칸) · 부력(`WaterBodyComponent::computeSurfaceHeight` · `isUnderwater`, 시간은
  `getWaveTime`)을 잇는다. (2) 강 경로가 점 목록이다 — 스플라인 컴포넌트가 들어오면 그것을 경로로 받는다. (3) 하늘 · 시간 · 높이 안개 + 물속 안개 패스(값은
  `WaterBodyComponent::findUnderwaterFog` 가 이미 준다) — 다중 뷰 병합 뒤. (4) 물의 굴절 · 화면 공간 두께는 반투명 패스가 장면 색 사본 · 장면 깊이를 입력으로 받는
  계약이 있어야 한다(지금은 지형 깊이를 정점에 굽는다). (5) 흔드는 식생의 그림자는 흔들리지 않는다(`shadowdepth.hlsl` 이 머티리얼 정점 변형을 모른다 — 풀은 그림자를 끔).
  (6) 지형 LOD 가 바뀌면 메시 집합이 바뀌어 정점 풀을 통째로 다시 만든다 — Release 에서 바뀌는 프레임의 최악 시간을 재 보고, 크면 LOD 메시를 미리 만들어 두거나
  청크 정점을 풀에서 부분 갱신한다. 지오모프(LOD 튐) · 레이어 다섯 이상(두 번째 스플랫 — 머티리얼 텍스처 칸이 넷이다) · 에디터 칠하기 도구가 없다.
- **툰 머티리얼(`toon.hlsl`, MToon 1.0 체계)의 남은 것** — 노멀 맵(정점에 탄젠트가 없다) · UV 스크롤 애니메이션 · 셰이딩 시프트 / 림 곱 / 외곽선 두께 텍스처(머티리얼 텍스처 칸이 넷이라 기본 · 그림자 · 발광 · 맷캡만 받는다) · 디퍼드의 계단 셰이딩(G버퍼는 표면만 적어 램버트로 칠해진다) · 그림자 패스의 알파 컷오프
  (`shadowdepth.hlsl` 은 픽셀 스테이지가 없어 머리카락 카드가 사각형 그림자를 드리운다 — 모든 컷오프 머티리얼이 같다).
- **VRM 임포트의 남은 것** — 머티리얼(MToon) · 구간 메시 · 스켈레톤만 옮긴다. 표정(모프 타깃 · `blendShapeMaster`) · 스프링 본(`secondaryAnimation`) · humanoid 본 표 · firstPerson 은 읽지 않는다(0.x · 1.0 모두). 본 메시(`<이름>.mesh`)는 구간들을 다시 합친 것이라 디스크에 두 벌이다(VRoid 34k 삼각형 7 MB × 2) — 엔진 메시에 머티리얼 구간이 생기면 하나로 줄인다. VRoid 텍스처는 BC3 이다(Debug DirectXTex 의 BC7 은 512×256 한 장도 10 분이 넘는다 — Release 로 BC7 임포트를 다시 할 것). `ModelImporterTest.SkinnedModelImportsSkeletonClipsAndAttachments` 는 Debug 에서 혼자 31 초라 EditorTest 한도를 30 → 120 초로 올려 두었다 — 임포트를 줄이면 되돌린다.
- **반해상도 후처리** — 첨부별 `_resolutionDivisor`(1 · 2 · 4)는 있다. 남은 것: 반해상도 패스가 읽는 입력의 텍셀 크기(`g_OutlineParams.yz` 는 프레임 텍셀),
  `deferredpipeline.xml` 블룸을 반해상도로 나누기, Release 로 p50 · p99 측정.

### 1-4. 에디터

- **에디터를 켠 실행은 종료 보고에 `Editor` 태그 256 B(1 블록)가 남는다**(2026-10-06, `App.exe -dx12 -EnableEditor -gv_profileFrames=5`). 에디터 없는 실행은 0 이고
  `AppSmokeTest.ShutdownReturnsEveryTagToTheBaseline` 이 지킨다. 프로세스 정적 저장소가 기동 뒤 자란 몫일 것 — 기준선 직후 `setDetailedTrackingEnabled( true )` ·
  종료 보고 직전 `getTopCallStacks( LiveBytes )` 임시 진단으로 자리를 찾아 종료 끝에서 놓는다.

- **에디터 · 개발 편의 기능(2026-10-04 사용자 승인, 순서대로).** 이미 있는 것(gv 표 · 커맨드 팔레트 · 핫 리로드 · Undo · PIE 재생/한 프레임 ·
  InputReplay · 기즈모 · 미니덤프 · RenderTargetPanel)은 다시 만들지 않는다.
  - **C 확장 지점** — 등록부(`EditorRegistry<T>` · `IEditorPanel` · `IInspectorComponent` · 시각화 · `EditorCommandRegistry`)를
    EditorFramework SHARED 로 떼어 내보내고, 키트 · 게임이 Dev 전용 `<Module>Editor` 모듈로 패널 · 인스펙터 · 시각화를 등록한다.
    지금은 EditorModule DLL 안의 함수 정적이라 다른 모듈이 못 쓴다. 첫 사용자는 ThemePark 배치 시각화.
  - **D 콘솔 · 치트(남은 것)** — 게임 · 키트의 치트 명령(무적 · 아이템 주기 · 돈 …)을 각 게임 · 킷에 `SW_DEV_COMMAND` 로 단다(등록부 · 콘솔 ·
    엔진 명령은 들어갔다 — `Source/Engine/README.md` "개발 콘솔"). 리눅스 오버레이(`X11DevConsoleWindow`)는 실기로 띄워 보지 않았다.
    게임 창 콘솔은 셸 InputMap 액션 + `InputManager` 키보드 포커스로 받는다(`DevTools/DevConsoleController`). 남은 것: 패드는 포커스 밖이라 콘솔이 열린 동안
    패드 A · B · 십자키가 게임에도 간다(shooter3d 는 패드 `Back` 이 `CycleCamera` 와 겹친다), 플레이어별 재배치(`InputMap::loadUserBindings`)가 셸 맵에 걸려
    있지 않다, X11 그리기는 `XDrawString`(Latin-1)이라 한글이 깨진다.
  - **F 카탈로그 편집기** — 카탈로그 계약 하나(ResourceDataSchemaTest 의 종류 표를 대체) · enum 이름 표(`CityCatalog.cpp` 의 하드코딩 개수 포함
    25 곳) · DataTablePanel 확장 편집기 · 저장 시 검증 · "어디서 쓰이나" 역색인 → 이름 바꾸기 시 참조 고침.
  - **G 프로파일링 · 캡처** — 구조는 섰다: 자체 패널(`ProfilerPanel` — CPU 구간 · GPU 패스 · 카운터 표(최근 N 프레임 p50 · p99 · 최대, 정렬 · 검색) +
    GT · RT · GPU 프레임 그래프, 집계는 ImGui 없는 `ProfilerScopeHistory`) + 시간축 분석은 외부 Tracy 뷰어(패널의 "Open Tracy" 가 같은 판 0.13.1 을 띄워
    localhost 에 붙인다 — 언리얼 에디터 → Insights 방식. 뷰어를 도킹 창으로 넣지 않은 이유는 `Source/Engine/Utility/Profiling/README.md`). GPU 타임스탬프는 네
    백엔드 모두 엔진이 모은다. 남은 것: RenderDoc 캡처 버튼 · 스크린샷 버튼 · 오버드로 · 노멀 · 깊이 보기, 패널에 스레드별 미니 타임라인(지금은 표 · 그래프뿐 —
    타임라인은 Tracy).
  - **H 품질 · 작업 흐름** — assert 무시 대화상자(이번만 / 계속) · 버그 리포트 한 방(스크린샷 + 로그 + InputReplay + 씬) · 시험 패널.

- **에디터 자체 시험(`SW_EDITOR_SELF_TEST`)이 입력을 흉내 내지 못한다** — 그래프 패널 ↔ 저장 커맨드 배선, 인스펙터 콤보 직접 편집, 툴팁 호버 · 드래그 드롭은
  ImGui 입력 이벤트를 넣는 창구(`ImGuiIO::AddMousePosEvent` 류를 프레임 단계에서 주입)가 있어야 덮인다.

- **DPI 150 % 모니터와 모니터 사이 이동을 실물로 보지 않았다.** 96 DPI 기계에서 `-gv_editorUiScale=1.5` 로만 봤다. 글자 선명도 · 창 · 스왑체인 크기 ·
  `io.ConfigDpiScaleFonts` · `ConfigDpiScaleViewports` 를 본다.

### 1-5. 핫 리로드 · 모듈

- **바깥 빌드(터미널 · IDE)의 리로드 트리거는 여전히 mtime 디바운스뿐이다** — 에디터가 시킨 빌드는 성공 뒤에만 올린다(`LiveReloadManager::notifyBuildStarted/Finished`).
  바깥 빌드도 "빌드 성공" 신호(ninja 종료 · 스탬프 파일)를 받으려면 빌드 쪽 협조가 필요하다.
- **모듈이 렌더 패스를 등록하는 창구가 없다.** `FramePassContext` · 커맨드 리스트 · 트랜지언트 풀을 모듈 경계 밖으로 내야 하고, 그것은 RT 안전 계약까지
  내보내는 일이다. 쓰는 모듈이 생기면 그때.

### 1-6. 게임프레임워크 · 킷 · 게임

- **아무도 이름을 부르지 않는 메시가 45 개다**(에셋 검증 `orphans` 경고 — NileCity 건물 · StarSkirmish 탈것 · ThemePark 코스터 조각 등 Kenney 키트에서 들여온 것).
  쓸 계획이 없으면 `models_raw/` 원본과 함께 지우고, 코드가 이름을 조립해 부르는 것이면 규칙의 `exclude_patterns` 에 적는다.

- **사용자 설정(옵션 메뉴 백엔드)의 남은 것** — 백엔드 · 바인딩 API 는 `Source/Engine/UserSettings/README.md`. (1) 메뉴 UI(런타임 UI 프레임워크 뒤) ·
  `UserSettingsPanel` 대신 키를 눌러 받는 리바인딩 창. (2) 값만 있고 읽는 곳이 없는 대상: `gv_renderScale`(업스케일 패스) · 그림자 · 시야 거리 · 후처리 ·
  텍스처 · 이펙트 품질 · 모션 블러 · `gv_colorVisionMode`(톤맵 패스에 색각 행렬 — 지금 톤맵에 상수 버퍼가 없어 미뤘다) · UI 배율 · 글자 크기 · 자막,
  카메라 `gv_cameraFieldOfView` · `gv_cameraShakeScale` · `gv_cameraHeadBob`(cam-views 가 읽을 자리). (4) 해상도 선택지를 모니터 모드에서(선택지 공급자) · GPU 사양 조회(RHI 어댑터 · 전용 메모리)로 품질 자동 선택.
  (5) 게임 스키마에 키 바인딩 설정 — Shooter3D 는 입력 맵(`data/shooter.input.xml`)을 쓰니 그 액션부터. 다른 시험 게임은 아직 키를 직접 묻는다(입력 맵으로 옮길 것). (6) X11 `setDisplayMode`(EWMH 전체 화면)는 리눅스 실기 미확인.
- **GameFramework 구조 정리(2026-10-04 리뷰, 사용자 승인).** 남은 것 —
  - 중간: Overworld `TileMap` 의 칸 손셈(`indexOf` · `isInBounds` — 크기는 파일 스키마 `TileMapXmlData` 가 든다) · NetConnection 메시지 버퍼 재사용 ·
    기반의 같은 손셈(NavGrid 4 · FlowField 4 · GridInventory 3 · PlatformTileMap 2 · GridReachability `% 너비` 1, 클래스마다 자기 `isInside` · `computeIndex` ·
    `toIndex` 사본 — NavGrid · GridReachability · ElementGrid)도 `GridTopology` 멤버로 · 게임 손셈(HarvestValley `FarmCropComponent` · `FarmSoilComponent`,
    NileCity `NileDirectorComponent` 의 `index % getWidth()`)은 키트가 `getTopology()` 를 열면 같이.

- **카메라 — 프리셋 데이터 · 블렌드 · 시퀀서(사용자 승인 로드맵).** 1~3 단계(프리셋 XML · 블렌드 · 디렉터, 모드(직교 · 궤도 · 따라가기 · 1인칭 · 3인칭 ·
  CCTV) · 입력 · 카메라 매니저(뷰 타깃 블렌드), 흔들림 · 제약 · 프레이밍 · 스프링 암)와 4 단계(다중 뷰 렌더 · 컷 프레임 신호 · 초상화 굽기)는 들어갔다
  (`Source/GameFramework/README.md` "카메라" 절, `Source/Engine/Graphics/Renderer/README.md` "다중 뷰"). 남은 것 —
  - 2 단계에서 남은 것: 직교 리그 네 게임(ThemePark · Harvest · Nile · StarSkirmish)은 리그 값이 곧 데이터다(모드 · 블렌드는 공유) — 리그를 지우고 디렉터 + 프리셋
    XML 로 옮기려면 게임 디렉터의 `setViewOverride` · `findGroundPoint` 를 디렉터 창구로 바꿔야 한다. 프레이밍의 가로 존은 16:9 로 센다(모드가 화면 비율을 모른다).
    2D(XY 평면) 카메라는 디렉터 밖의 `Follow2DCameraComponent` 다 — 2D 게임이 프리셋 · 블렌드를 원하면 디렉터 모드에 "XY 평면 따라가기" 를 더하고 그 컴포넌트를 지운다.
  - 4 단계에서 남은 것: 예산 · 보임 판정이 거칠다 — 보임은 "지정한 오브젝트가 주 카메라 절두체 안" 하나(가려짐 · 화면 크기 안 봄), 예산은 프레임당 뷰 수
    (`gv_renderViewBudget`)지 시간이 아니다. 화면 사각형 뷰는 에디터 GameView(ImGui 이미지)에서 검증하지 않았다. 해상도 배율 뷰의 TAA 기록 · 풀은 뷰마다 따로라
    뷰가 많으면 메모리가 뷰 수에 비례한다(공유 풀 없음). 초상화 굽기는 동기(렌더 스레드를 멈추고 그린다) — 에디터 썸네일처럼 많이 구우려면 큐로. GL 기본 프레임버퍼의 뷰포트
    y 뒤집기(`OpenGLRHICommandContext::setViewport`)는 자동 시험이 없다 — 시험의 화면 사각형은 캡처(오프스크린 FBO)에 그려지고 RHI 에 백버퍼 되읽기가 없다.
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
  ACL 코덱은 `Engine/Animation/Codec/Acl`(쿠킹 때 압축 → 코덱 id + 불투명 블롭). vcpkg 를 바꿀 때는 **다른 워크트리가 빌드 중이
  아닐 때** — 설치 폴더를 나눠 써서, 옛 매니페스트의 워크트리가 configure 하면 새 패키지를 지운다. Jolt 소프트 바디(천 · 헤어 카드)는 아직 감싸지 않았다.
  2026-10-05 사용자 결정으로 **Recast & Detour**(zlib, 정적 — `RecastNavigation::Recast` · `Detour` · `DetourCrowd` · `DetourTileCache`)와
  **Tracy**(BSD-3, 클라이언트만 · 기능 끔 — `Tracy::TracyClient`, Windows 는 공유 TracyClient.dll)를 vcpkg 로 들였다(`ThirdParty/{recastnavigation,tracy}`).
  Tracy 는 clang-cl 트리플릿의 C++14 기본값에 서지 못해 C++17 오버레이 포트(`ThirdParty/tracy/vcpkg-port/tracy`)를 둔다. Tracy 는 엔진 프로파일러의 두 번째
  출력으로 감쌌다(`Source/Engine/Utility/Profiling/README.md` — 헤더 경계는 같은 게이트, Shipping 은 링크하지 않는다). 남은 것: 배포물에 넣을
  서드파티 고지 목록이 저장소에 없다 — Shipping 패키지에 `THIRD_PARTY_NOTICES` 를 만들어 vcpkg `share/*/copyright` 를 모은다. Tracy(BSD-3)는 Shipping 에
  들어가지 않지만 **개발 빌드(TracyClient.dll)를 남에게 줄 때** 고지가 필요하다: "Tracy Profiler (https://github.com/wolfpld/tracy) is licensed under the
  3-clause BSD license. Copyright (c) 2017-2025, Bartosz Taudul <wolf@nereid.pl>" + BSD-3 본문(`share/tracy/copyright`) — 뷰어(tracy-profiler.exe)는 저장소에 넣지 않는다.
- **애니메이션(로드맵).** 지금 있는 것은 `Source/Engine/Animation/README.md`(임포트 · 코덱 · 재생 · 상태 기계 · AnimationSystem · GPU 스키닝 · 2D/3D 공용 재생).
  알림 디스패치(구간 알림 · 처리기 등록부 · `*.notifies.xml`)는 `Source/Engine/Character/README.md`.
  남은 것 — ① 그래프의 블렌드 스페이스 노드(지금 `BlendSpace` 는 행렬 하나라
  포즈 블렌드 스페이스로 다시 짓는다) · 그래프에 레이어 · 동기 그룹을 데이터로(지금 레이어는 `addLayer` 코드) · 에디터 그래프 패널이 조건 · 블렌드를 편집
  ② 후처리 리그 — 들어갔다(`PoseModifierComponent`, `Source/Engine/Animation/README.md` 5 절 · `Source/Engine/Character/README.md`). 남은 것: 시퀀서 트랙이 `setSlotWeight` 를 쓰기(칸은 있다),
  해석된 소켓 표의 표면 기준 소켓 체형 보정을 리그 대상에도, 에디터 리그 패널(노드 목록 · 대상 · 기즈모) ③ 애니메이션 LOD(가시성 · URO · 보간 · 본 LOD · 예산 · 2D 스프라이트는 들어갔다 —
  `AnimationLod.h`) — 남은 것: 거리별 IK/물리 끔을 `AnimationLodState`(화면 크기)로(지금 스프링 본은 거리 기준점) · 메시 LOD 가 생기면 본 LOD 를 메시 LOD 와 묶기 ④ 군중 공유(묶음 · 사본 풀 · VAT 쿠킹은
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
- **다중 월드 · 에디터 툴 창(로드맵, 카메라 4 단계와 한 덩어리).** 지금 `EngineLoop` 는 활성 씬 하나만 틱하고 그리며, GpuScene 빌더 · 스냅샷도 하나다 —
  그래서 프리팹 격리는 활성 씬을 빌리고(씬이 더러우면 막는다) 머티리얼 미리보기는 메인 뷰포트의 선택 오브젝트에 입혀 보인다. ① 다중 월드(미리보기 월드를
  따로 살림, 월드마다 시간 · 틱 정책 · 조명 환경 · 물리 월드) ② 월드별 렌더(씬마다 GpuScene, 뷰 목록의 뷰가 "어느 월드 · 카메라 · 렌더 타깃" 을 고름,
  안 보이는 창은 안 그림 — 카메라 4 단계의 다중 뷰를 다중 월드 × 다중 뷰로) ③ 툴 창 틀(미리보기 월드를 가진 도킹 창, 자기 렌더 타깃 · 에디터 카메라 ·
  기즈모, 창마다 선택 · Undo 범위 — 지금은 에디터 전체에 하나, 문서 계약은 있는 것, 키트 · 게임이 등록하도록 1-4 의 C 확장 지점 위에) ④ 위에 올릴 창:
  프리팹(격리 월드) · 머티리얼(미리보기 구체) · 메시/모델(LOD 비교) · 애니메이션(스켈레톤 · 타임라인 · 압축 오차) · 카메라 프리셋(블렌드 미리보기) ·
  래그돌/물리 에셋(관절 한계) · 이펙트 · 장르 도구(코스터 트랙 · 리듬 차트). 언리얼 FPreviewScene · 애셋 에디터 툴킷, 유니티 PreviewRenderUtility · Prefab Stage.
- **캐릭터 외형 편집(로드맵).** 지금: 형상 쪽(아래 ①~④ · ⑤ 의 소켓 이름 공간)은 `Source/Engine/Character` 에 있다(README "통합이 할 일"), GPU 모프 풀(`Mesh::setGpuMorphEnabled`)은
  있다, 스켈레톤 에셋(`.skeleton.json`)은 본 · 레퍼런스 포즈 · 역 바인드와 임포트가 적은 본 부착 메시 표(소켓 파일을 처음 만들 근거)뿐이고 편집 창구가 없다.
  ①~④ 남은 것 — **통합**: `Mesh` · 포즈 ↔ `AppearanceGeometry` · `CharacterBoneArray` 변환, 체형 모프 · 피팅 델타(`FitPartResult::_listVertexDelta`)를 GPU 모프 풀에
  싣기(스키닝 앞), 병합 결과(`MeshMerger`)를 인덱스 · 정점 버퍼와 구간 그리기로, 애니메이션 시스템이 본이 움직인 프레임에만 `SocketBindingComponent::updateSocketTransform`, 표면 상태(`CharacterSurfaceState`)를 머티리얼 파라미터 · 마스크 텍스처로. **쿠킹**: 장비 정점 → 몸 전이
  (`SurfaceTransferUtil` — 모프 · 스킨 가중치)를 임포트 · 쿠킹 단계에, 아틀라스 굽기(`IMeshMergeHooks` 구현). **핫 리로드**: 소켓 · 레퍼런스 포즈 · 체형 · 피팅 표 · 부품
  피팅 · 표면 채널 파일을 고치면 외형을 다시 조립 — 소비자(외형 컴포넌트)가 `IAssetCache` 로 올린다(로더는 다 있다). **나중**: 천 시뮬레이션(Jolt 소프트 바디)이 같은 겹
  정보를 충돌체로(Mutable 의 Clip with Mesh · Clip Deform 이 같은 문제를 푼다). 참고: 언리얼 Mutable(Customizable Object) ·
  Skin Weights 전이, Character Creator 스마트 핏, Daz 오토핏 ⑤ 장비 해석 — 데이터 · 해석기는 있다(`GameFramework/Appearance` — 슬롯 표 · 세트 · 아이템 외형 · 꾸미기 스키마 · 규칙 · `CharacterAppearance`
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
  - **작음(S)**: 에디터 H(assert 대화상자 ·
    버그 리포트 · 시험 패널) · 단축키 편집기 · 환경설정 창 · 모듈 켜고 끄기 · DPI 실물 확인.
  - **중간(M)**: 대역폭 프로파일러 · 게임플레이 디버거 · 비주얼 로거 · 모듈 패키지 관리 · 점광/스폿 그림자 · SSAO ·
    HZB 가림 컬링 · 메시 LOD(meshopt) · PSO 미리 만들기 · 에디터 G(RenderDoc · 보기 모드 — 프로파일러 표 · GPU 타임스탬프 · Tracy 는 들어갔다) · 에디터 C(확장 지점) · 에디터 F
    (카탈로그 편집기) · 공용 커브 편집기 · 공용 노드 그래프 틀 · 인스펙터 개선 · 에셋 브라우저 · 맵 검사 패널 · UI 시험 입력 흉내 · 패키징 UI · 에디터 자동화 ·
    로딩 흐름 · 입력 확장 · 에셋 DCC 내보내기 · QA 봇 · 포토 모드 · 리플레이/킬캠 · SSR · 업스케일러 · HDR 출력 · 데칼 · 하늘/시간대/높이
    안개 · 2D 스켈레탈 · 학습용 몫(장르 시작 템플릿 · 튜토리얼 · API 문서) · 옵션 메뉴 · 알림/토스트 · 튜토리얼 힌트 · 월드 마커
    [넷 다: 런타임 UI].
  - **큼(L)**: 런타임 UI 프레임워크(폰트 · 글자 · 위젯 · 레이아웃 · 게임패드 탐색 · 현지화 · 화면/월드 공간) · 제약 ·
    파티클/VFX · 텍스처
    밉 스트리밍 · 카메라 5 · 6 단계 · 에셋 레지스트리 · DDC · 증분 쿠킹 · 월드 편집 도구 · 탈것/말 ·
    천/머리카락 · 전술 AI · 볼류메트릭 안개/빛/구름 · 캐릭터 셰이딩 · 모션 캡처 공정 · 대규모 좌표 · PCG 저작 그래프 · GI/반사 프로브 · 플랫폼 서비스 ·
    패치/DLC · 모드/UGC.
  - **아주 큼(XL)**: 비주얼 스크립팅 · 월드 파티션/스트리밍/HLOD · 음성 채팅(온라인 구성은 1-7 "네트워크 서비스 계층").
- **오디오 엔진(2026-10-04, `Engine/Audio/README.md`)의 남은 것.** 믹서 · DSP · 공간화 · 이벤트 · 스냅샷 · 적응형 음악 · 씬 묶기는 들어갔다. (1) 데이터 핫 리로드 —
  `loadEventLibrary` · `loadMixer` 는 같은 이름이면 바꾸지만 파일 감시(에디터 `FileWatchDispatcher`)에 걸려 있지 않다. (2) 에디터 — 믹서 패널(버스 미터 · 음소거/솔로),
  이벤트 브라우저 · 미리 듣기, 보이스 · 가상화 프로파일러. (3) 긴 음악 스트리밍(지금은 클립을 통째로 디코드해 메모리에 든다 — 3 분 스테레오 ≈ 69 MB float).
  (4) 출력: HRTF(바이노럴) · 5.1/7.1 · 다중 리스너 믹스(지금은 가장 크게 들리는 리스너 하나), 리눅스 출력 백엔드(지금 Null — 오프라인 렌더). (5) 2D 물리(Box2D) 가림 ·
  포털/방 기반 가림(Wwise Rooms & Portals) · 회절. (6) 사이드체인 덕킹(대사 때 음악 낮추기 — 지금은 스냅샷으로), 컨볼루션 리버브, 그래뉼러 · 절차 소리
  그래프(MetaSounds 급). (7) 일곱 시험 게임 중 Shooter3D · StarSkirmish 만 이벤트로 바뀌었다 — 나머지는 아직 `GameSound::play( 경로 )`.
- **NPC 하루 일정(`GameFramework/AI/Schedule`, 2026-10-04) — 연결할 것.** 일정 데이터 · 런타임 · 화면 밖 LOD · 잠 · 저장 · 추적은 들어갔다(`AI/Schedule/README.md`).
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
- **Shooter3D 핫 리로드 · 세이브는 처치 수만 잇는다.** 웨이브를 페이싱 감독(`AiDirector`)이 내게 되면서(ai-telemetry) 웨이브는 감독의 주기라, 되살린 판은
  적을 걷고 감독을 처음부터 돈다(quick-gf 의 상태 v1 은 웨이브 번호를 실었다 — v2 는 처치 수뿐). 감독 상태(주기 · 시간 · 단계 · 풀 쿨다운 · 예산 · 난수)를
  `writeState` · `readState` 로 싣고 Shooter3D 가 같이 쓰면 웨이브까지 이어진다.

- **Shooter3D 외형 데이터(`game/shooter3d/data/appearance/`)가 없는 메시 · 프리팹 · 소켓 · 머티리얼 27 곳을 가리킨다**(char-appear 의 자리 채움 데이터).
  에셋 검증 `references-exist` 가 이 폴더를 빼 두었다(`Config/Editor/AssetValidationRules.json`) — Shooter3D 통합이 실제 에셋을 넣으면 그 제외를 지운다.

- **병합된 시험 게임 일곱의 눈 확인** — 일곱 게임 × 네 백엔드 자동 플레이(1200 프레임)는 종료 0 · `[Error]` 0 이다. 남은 것은 스크린샷으로 볼 것:
  스프라이트 조준선 · 복셀 청크 · 코스터 레일 방향 · 직교 카메라 그림자 범위. 복셀 청크가 프레임마다 GPU 버퍼를 새로 잡는지(`Mesh` 재사용).
- **GameFramework 리뷰에서 미룬 것(빌드가 있어야 안전하다).** **하지 않기로 한 것:** HP 바 · 피해 숫자의 매 틱 배치 예약을 "바뀔 때만" 으로 줄이는 것 — 월드 변환이 틱 **뒤에** 적용되므로 틱 안에서
  자리 변화를 보면 움직이는 막대가 한 프레임씩 건너 늦는다. 줄이려면 변환 적용 뒤의 훅이 필요하다.
- **어빌리티 시스템의 다음 조각(쓰는 게임이 생기면).** 언리얼 GAS 에 있고 여기 없는 것: 이펙트가 주는 어빌리티(장비가 스킬을 준다), 걸린 동안의 태그 조건
  (`OngoingTagRequirements` — 기절 중 버프 정지), 태그가 붙을 때 발동(`OwnedTagAdded` 트리거), 큐를 데이터로 이어 주는 큐 매니저(큐 태그 → 프리팹 · 사운드),
  어트리뷰트를 `SaveGame` 에 싣는 도우미, 에디터의 런타임 상태 패널(걸린 이펙트 · 태그 개수 · 쿨다운). 넣을 때마다 `AbilitySystemTest` 에 시험 하나.
- **리눅스에서 yad 만 깔린 기계에는 "All files" 필터가 없다**(`LinuxFileDialog.cpp`). `yad --file --file-filter='A | *.txt' --file-filter='All files | *'`
  가 뜨는지 확인한 뒤에만 `buildGtkStyleCommand( ..., true )` 로 바꾼다 — yad 가 인자를 거부하면 다이얼로그가 아예 안 뜬다.

- **장르 공통 기반 · 새 키트의 첫 엔진 빌드(2026-10-03).** 위 두 항목과 같은 이유로 하네스로만 돌렸다. 기반의 새 폴더 —
  `AI/` · `Navigation/` · `Combat/`(Shooter 키트에서 옮긴 무기 포함) · `Input/`(예전 `Control/`) · `Inventory/` · `Match/` · `Movement/` · `Progression/` · `Quest/` · `World/`,
  `Input/TimingJudge` · `Utility/TimerQueue` · `Data/StatBlock` — 와 키트 `GF_CityBuilder` · `GF_RealTimeStrategy`. 하네스 통과: `NavigationTest` · `AiTest` ·
  `CityBuilderTest` · `RealTimeStrategyTest` · `CombatTest` · `InventoryTest` · `ProgressionTest` · `QuestTest` · `WorldTest` · `MatchTest` · `ControlTest` · `PlatformerTest` ·
  `GameFrameworkUtilTest`, 네트워크 — `Core/Network`(`NetworkTest` 6, 실제 UDP 로컬 송수신 포함) · 키트 `GF_NetClientServer` · `GF_NetLockstep` · `GF_NetTurnRelay` · `GF_NetMmo`
  (`NetClientServerTest` · `NetLockstepTest` · `NetTurnRelayTest` · `NetMmoTest`). Windows 소켓 경로(`PlatformSocketUtil` 의 winsock 분기)는 **구문 검사도 못 했다**(리눅스뿐). 할 일: 재구성(새 폴더는 GLOB 이라 CMake 수정 없음 — `GF_Shooter` 가 빠졌으니 낡은 빌드의 `GF_Shooter.dll` 을 지운다) → Debug · Shipping →
  `EngineTest --test_filter=` 위 스위트들. `GameFramework.dll` 이 커졌으니 `SW_GF_API` 내보내기 누락(링크 오류)부터 본다.
- **새 장르 키트 22 개 · 시험 게임 둘의 첫 엔진 빌드(2026-10-03, 대기열은 모두 만들었다).** 하네스로만 돌렸다 — `GF_TacticsSrpg` · `GF_CardGame` · `GF_OpenWorldWestern` ·
  `GF_WitcherRpg` · `GF_CreatureLife` · `GF_RestaurantSim` · `GF_SurvivalHorror` · `GF_SideScrollConquest` · `GF_Fighting` · `GF_ActionAdventure` · `GF_GhostHunt` ·
  `GF_BattleRoyale` · `GF_CoopScavenger` · `GF_MonsterCollector` · `GF_ClassicJrpg` · `GF_Metroidvania` · `GF_ActionPlatformer` · `GF_KartRacing` · `GF_MechArena` ·
  `GF_AsymmetricHorror` · `GF_PartyArena` · `GF_Rhythm`, 시험 게임 `NileCity` · `StarSkirmish`(화면 · 마우스 집기 · 카메라는 본 적이 없다 — 에디터 게임 뷰에서는
  창 크기를 써서 커서가 어긋날 수 있다). 할 일: 재구성 → Debug · Shipping → 각 `<Kit>Test`, `SW_ACTIVE_GAME=NileCity` · `StarSkirmish` 로 한 번씩 띄워 보기.
  **같은 빌드에서 GameFramework 폴더 재배치(2026-10-03)도 처음 확인한다** — `Base/` 를 `Framework` · `Components` · `Stage` · `Utility` · `Input` 으로 쪼개고
  (`Transition` · `Control` 은 합침, `ItemBag` → `Inventory`, `GameFlags` → `World`), 키트를 `Kits/<장르 묶음>/<키트>` 로 옮겼다. 재구성(`cmake --preset`)으로
  GLOB · 리플렉션 헤더 목록을 다시 모아야 한다 — 낡은 빌드 폴더의 `generated/**/Base/*.gen.cpp` 가 남아 같은 타입이 두 번 등록되면 그 폴더를 지운다.
- **로컬라이제이션 — 남은 것(데이터 쪽 파이프라인은 끝, `Engine/Localization/README.md`).** UI 글꼴 렌더러가 생기면: `getFontFallback( culture )` 의 가족 목록으로 CJK ·
  아랍 글리프 대체를 고르고, `isRightToLeft()` 로 배치를 뒤집고, `getTextRevision()` 이 바뀌면 글을 다시 묻는다(언리얼 FText 처럼 키를 든 UI 글 컴포넌트 —
  `Meta = "Localizable"` 프로퍼티 + `getStringByText`). 아직 없는 것: `selectordinal`(서수) · 화폐 · 시간대 · XLIFF · 쿠킹된 이진 표(언리얼 `.locres` — 지금은 JSON 을
  그대로 읽는다) · 아랍어 이외 RTL 문화권 데이터 · `ja` 번역. 아이템 · 무기 이름(Shooter3D)은 표에 모이지만 화면에 쓰는 코드가 아직 `getStringByText` 를 거치지 않는다.

- **상호작용 · 기믹(2026-10-04 들어감 — `GameFramework/Interaction` · `Gimmick` · `Spline`) 병합 뒤 남은 것.**
  - 물리: 기믹 프리팹의 `BoxCollider2DComponent` 에 3D 게임용 3D 트리거 · 강체 콜라이더 변형을 더한다(`Resource/common/prefabs/gimmicks`). 월드 질의 · 카메라 암 ·
    집기 · 눌림판 무게 · 발사대 · 컨베이어는 강체 물리에 이어졌다(`GameFramework/Interaction` · `Gimmick` README).
  - 애니메이션: 상호작용 단계에 몽타주(클립)를 이름으로 잇기 — 맞춤 마커 → 워프 목표는 들어갔다(`InteractorComponent` 가 시작할 때 넣는다).
  - 렌더러: `InteractableComponent::getHighlightRequest`(Outline · Sense)를 읽는 외곽선 · 감각 모드 패스.
  - 에디터: 기믹 회로 그래프 편집 창(노드 · 배선 · 검증 오류 표시, 대상 오브젝트 고르기) — 지금은 인스펙터의 목록 편집뿐.
  - 네트워크: 회로 상태 바이트(`GimmickCircuit::saveState`)를 `NetClientServer` 스냅샷 · `RollbackSession` 상태에 싣기(모양은 준비됨, 배선 없음).
  - `InteractorComponent` 는 틱마다 씬의 `InteractableComponent` 를 모두 훑는다 — 하는 쪽이 많아지면 공간 등록부로. `SmartObjectComponent` 는 틱도
    `onPropertyChanged` 도 없어 상호작용 표를 고쳐도(핫 리로드) 자리 정의를 다시 찾지 않는다 — `InteractionCatalog::getSharedReloadCount` 를 볼 자리를 정한다.
  - 카트 트랙(`KartTrack`)은 거리를 수평(XZ) 길이로 재서 공용 `SplinePath` 로 옮기지 않았다(옮기면 랩 · 고스트 값이 바뀐다 — 옮길지 정한다).

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

### 1-7. Core · 태스크

- **네트워크 — 파괴 · 가상 서버에서 남은 것**(2026-10-05, `GF_NetSimulation` · `GF_NetDestruction`). ① 파괴 사건은 "신뢰 · 순서 없음" 채널(N21b) — 250 ms · 손실 15 % 의 사건 지연
  평균 · 최대 전/후는 `NetSimDestructionMatrixTest` 로그(`max event lag … mean …`)로 잴 것(빌드 뒤). 남은 최대는 한 메시지가 거듭 잃는 몫이다. 덩어리 멈춤 확정(신뢰 자세)도
  받는 쪽이 틱으로 끼우므로 옮길 수 있다 — 재고 나서. ② 롤백(파괴 상태 저장 · 되돌리기, `RollbackSession` 에
  `makeNetworkSnapshot` 바이트 싣기)은 하지 않았다. ③ 부서지기 전 움직이는 파괴 오브젝트(상자 · 드럼통)의 자세는 파괴 키트가 보내지 않는다 — 게임이
  `ReplicationServer` 엔티티로 보낸다(아니면 클라이언트 조각이 클라이언트의 그 자리에서 태어난다). ④ 전용 서버 프로세스 모드(창 · 렌더러 없는 App 서버 +
  UDP 클라이언트, WSL 리눅스 서버 ↔ Windows 클라이언트로 파괴 해시가 컴파일러 · 플랫폼을 넘어 같은지)는 하지 않았다.

- **네트워크 — 복제 키트에서 남은 것**(2026-10-05, N5~N7 뒤). ① MMO 비신뢰 갱신을 잃어도 서버는 모른다 — 보낸 순간 `_listSentState` 를 바꿔 "안 바뀜" 으로 보고
  가속하지 않으니 다음 차례(누적 우선도)까지 옛 상태가 보인다. 메시지 전달 통지(`NetConnection` 패킷 확인 → 메시지)가 생기면 확인 기준으로 바꾼다. ② 클라이언트-서버도
  우선도를 보낼 때 0 으로 돌려, 실린 스냅숏이 순서만 채널에서 다음 것에 밀리거나 잃으면 낮은 우선도는 한 차례(우선도 비 만큼) 더 기다린다(하니스: 11 → 최대 20 틱).

- **네트워크 — 포화된 연결의 비신뢰 줄**(N21a 뒤). 대역폭 몫을 다 쓰면 메시지가 기다리는데, 비신뢰 줄(`NetConnection::_listOutgoingUnreliable`)에는 상한이 없어 오래 포화되면
  옛 비신뢰가 쌓였다 몰려 간다. 언리얼은 포화면(`IsNetReady` 거짓) 액터 복제를 건너뛴다 — 키트가 `NetHost` 에 "이 연결이 포화인가" 를 묻거나 줄에 나이 상한을 둔다. 재고 나서.

- **네트워크 리팩토링 남은 단계(2026-10-05 사용자 요청 — 결함 단계 N0~N12 · 키트 결함 D1~D19 는 끝남).** 공통 부품을 Core `Network/Replication/` 에 두고 키트는
  조립만 하게. 단계마다 커밋 하나, 스레드를 건드린 단계는 `--test_repeat=50`, 파괴 네트워킹 시험(`NetSimDestruction*` · `DestructionSnapshot`)을 매 단계 지킨다.
  N18b · N18c **측정 뒤 결정**(벤치 `NetReplicationBenchTest` 는 들어감 — Release 3 회로 서버 복제 할당 > 5 000 / 틱 또는 p50 > 1 ms 면 재구성 · 해독 버퍼
  재사용 · 델타 이분 탐색, 그 뒤 연결 · 메시지 할당 > 1 000 / 틱이면 메시지 풀. 미달이면 3-12 에 숫자).
  하지 않기로 한 것: NAT · 리플렉션 속성 복제(Iris) · 복제용 RPC · 외부 네트워크 라이브러리(암호 라이브러리는 아래 "네트워크 보안" 항목의 예외).

- **네트워크 서비스 계층(2026-10-06 사용자 결정 — 로그인 · 채팅 · 거래, MMO 가 아니어도 쓰는 키트).** 상태 복제(UDP)와 달리 순서 있는 신뢰 스트림 · 긴 연결 · 요청-응답이다.
  ① Core `Network/Transport/` 에 스트림 전송(TCP) — 수락 · 읽기 · 쓰기 완료를 받는 이벤트 루프 인터페이스 하나, 구현은 Windows IOCP · 리눅스 epoll(io_uring 은 측정 뒤), 연결마다 송수신 버퍼 · 배압 ·
  유휴 시한 ② Core `Network/Message/` 에 길이 접두 프레이밍 + 요청-응답(요청 id · 시한 · 취소 · 멱등 키) — 복제용 RPC 와 다르다(서비스 호출)
  ③ 구성은 `docs/` 가 아니라 이 항목이 정본 — **공통 기반**(GameFramework 기반 `Online/`): 서비스 틀(등록 · 라우팅 · 인증 문맥 · 오류 코드 · 판 협상) · 요청 보호(도배 제한 · 멱등 키 · 크기 상한) ·
  신원 원형(`AccountId` · 세션 토큰 검증) · 저장 계약(`IServiceStore` 영속 · `IEphemeralStore` 캐시 · `ILocalStore` 로컬 — 파일 백엔드는 바이너리/JSON/XML · 원자적 쓰기 · 체크섬 · 선택 압축/암호화) ·
  마이그레이션 적용기 · 감사 로그 · 서버 간 버스 · 예약 작업 · 원격 설정/기능 플래그 · 관측(지표 · 구조화 로그 · 추적 id). **드라이버 키트**: `GF_SqlStore`(SQLite · PostgreSQL) ·
  `GF_CacheStore`(메모리 · RESP). **기능 키트**: `GF_Account` · `GF_ServerDirectory` · `GF_Economy`(원장 · 지갑 · 상점 · 영수증 검증) · `GF_Trade`(원장 위) · `GF_Mailbox` · `GF_Chat` · `GF_Social` ·
  `GF_Leaderboard` · `GF_Matchmaking` · `GF_LiveOps` · `GF_Admin`(GM 도구 · 제재). 제품 이름은 드라이버 · 제공자 폴더에만. 부하 시험 봇은 시험 도구.
  ④ **DB 결정(사용자)**: 영속 PostgreSQL(서버) · SQLite(개발 단독 서버 · 클라이언트 로컬), 캐시는 RESP 드라이버 하나 — 리눅스 Valkey(BSD-3), 윈도우 Garnet(MIT)(Redis 7.4+ 는
  RSAL/SSPL — "오픈소스 · 무료" 조건 밖). 캐시를 잃어도 영속 데이터는 맞아야 한다(거래 정본은 영속 트랜잭션). DB 호출은 전용 워커 + 연결 풀 + 비동기 완료.
  **서버는 윈도우 · 리눅스 둘 다 1 급**, 서버 전용 모듈은 클라이언트 Shipping 에 넣지 않는다(전용 서버 타깃 — Game · Client · Server).
  **순서**: 기반 + 드라이버 → Account · ServerDirectory → Economy → Trade · Mailbox · Admin · 관측 → Chat · Social → Leaderboard · Matchmaking · LiveOps.
  상용 비교: 언리얼은 Online Subsystem/EOS 등 외부 백엔드에 맡기고, 자체 MMO 서버는 IOCP/epoll 서비스 서버를 따로 둔다.
- **네트워크 보안(2026-10-06 사용자 결정 — "하지 않기로 한 것" 에서 거둠).** 스트림(서비스)은 TLS 1.3, 게임 UDP 는 연결 수립 때 키 교환(X25519) 뒤 패킷마다 AEAD(AES-GCM 또는
  ChaCha20-Poly1305 · 패킷 번호를 nonce 로 · 재전송 방지 창) — Valve GNS · 언리얼 AESGCM PacketHandler 와 같은 모양. 세션 키는 로그인 키트가 발급한 토큰에 묶는다(UDP 접속 = 토큰 제시).
  암호 구현은 직접 짜지 않는다 — 라이브러리 하나(후보 OpenSSL: TLS · AEAD · X25519 를 한 의존으로, Apache-2.0)를 엔진 인터페이스 뒤에 두고 격리 게이트(`CheckThirdPartyIsolation`)에
  올린다. vcpkg 변경은 main 에서 먼저(라이브러리 선택은 제안서에서 근거와 함께 사용자 확인). 인증서 · 키 관리(개발용 자체 서명, 배포 설정)와 시험(변조 · 재전송 · 잘못된 키 거절)을 같이.
- **패킷 압축(2026-10-06 사용자 결정).** 코덱 틀은 Core `Compression`(코덱 id 등록부), LZ4 · zstd · zlib 은 Engine 이 등록한다 — Core 네트워크는 id 로만 쓴다. 작은 UDP 패킷은 일반 압축의 이득이
  작으니 **측정 먼저**: 실제 스냅숏 · 파괴 사건 · 채팅을 모아 (양자화 · 비트 패킹 · 델타 뒤) LZ4 · zstd(학습 사전 포함)의 크기 · 시간을 잰다 → 이기는 종류만 켠다(패킷 머리에 코덱 표식,
  압축 뒤 암호화 순서, 압축 폭탄 상한). 스트림(채팅 기록 · 거래 내역 · 큰 메시지)은 zstd 가 기본 후보.
- **MMO 규모 서버의 UDP 소켓 계층.** 지금은 호스트당 논블로킹 UDP 소켓 하나 + 전용 스레드 하나(`poll`/`WSAPoll`), 데이터그램마다 `recvfrom`/`sendto`, `NetHost` 잠금 하나.
  UDP 는 소켓이 하나라 IOCP · epoll 은 지렛대가 아니다 — ① 측정 먼저(실제 UDP 에 가짜 클라이언트 500 · 2000, 초당 패킷 · 패킷당 CPU · 지연 p99 — N18 은 루프백이라 소켓 비용을 안 본다)
  ② 시스템 호출이 지배적이면 일괄 I/O(리눅스 `recvmmsg`/`sendmmsg` → UDP GSO/GRO, Windows RIO — 완료 통지는 IOCP) ③ 한 스레드가 차면 `SO_REUSEPORT` 수신 분산 + 연결 샤딩(`NetHost` 잠금 분할).
- **순서**: 보안의 UDP 부분은 N13~N21 뒤(연결 수립 · 패킷 머리가 정리된 위에), 스트림 전송 → 프레이밍/요청-응답 → TLS → 로그인 → 채팅 · 거래. 압축은 측정 벤치가 서면 어느 때나.

- **sw 할당자 밖 누적 할당의 85 % 는 `FileUtil` 의 `std::filesystem` 이다**(기동 ~670 KB / 1 만 회 — collectFiles · fileExists · 디렉터리 순회). 할당자 인자가 없는
  표준 API 라 줄이려면 Win32 · POSIX 순회로 바꾼다. 상주량은 1 KB 미만이라 전역 operator new 교체는 하지 않는다(사용자 결정).

- **`runParallel` 합류 대기가 남의 태스크(IO 등)를 도와 실행할 수 있다.** 프로파일에 보이면 IO 레인을 따로 둔다(조건부).
- **TaskManager 스테이지 디버그 이름** — 프로파일러에 연결할 때 넣는다(지금은 연결돼 있지 않다).
- **`fixed_string` 의 해시가 FNV(`computeHash64`)다.** 느리지만 프로파일에 안 보여 두었다(낮음).

### 1-8. 성능 (재고 나서 정할 것)

- **DX12 · Vulkan Present 히치.** 큐브 100 · 600 프레임 중 40 프레임이 1~18 ms 다(DX11 은 없다). 다음 후보는 DXGI 대기 가능 스왑체인
  (`FRAME_LATENCY_WAITABLE_OBJECT` + `SetMaximumFrameLatency` + 대기). 함정: 플래그는 `ResizeBuffers` 에도 같게. 재기 전에 VSync 가 정말 꺼졌는지 보고 p99 로 본다.
- **파괴 잎 셰이프를 플레이 시작에 짓는 비용.** 잎마다 Jolt 볼록 껍질 약 80 us(Release) — 쇼케이스(파괴물 여섯 · 잎 312)가 첫 프레임에 ≈ 25 ms 를 쓴다.
  파괴물이 많은 맵이면 선형으로 는다. 후보: 쿠킹 때 Jolt 셰이프를 직렬화해 `.fracture` 에 싣기(Chaos 가 지오메트리 컬렉션에 충돌을 같이 굽는 자리) 또는
  워커에서 `ShapeSettings::Create`(순수 계산) 후 게임 스레드에서 핸들만 등록. 지금 깨지는 프레임은 200 조각 벽 4 ~ 7 ms(그중 사건 처리 2 ~ 4 ms).
- **DX12 `releaseOnlineBlocksDeferred` 의 `_onlineBlockMutex` 경합.** 병렬 기록 중 RT `mutex::lock` 의 79 % 였다. 후보는 워커별 대기 목록. 고치기 전에 다시 잴 것.
- **에디터 모드 `GT.Editor.updateUi` ~5 ms(큐브 8000)를 쪼개 보지 않았다.** 창을 전면에 두고 잰다(가려지면 RT.BeginFrame 이 67 ms 를 기다려 5.8↔75 ms 로 흔들린다).
- **8000 무버의 `components`(onTick) ~325 us.** 남은 비용은 오브젝트 → 틱 항목 → 컴포넌트 포인터 추적이다. 더 줄이려면 오브젝트 모델 밖 배치 경로
  (언리얼 Mass · 유니티 DOTS 자리)나 트랜스폼 SoA 2 단계가 필요하다 — 큰 구조 변경이라 할지부터 정한다(1-11 의 구조 후보).
- **직렬화기(이름 대조 · 텍스트 파싱)와 리소스 로드의 리플렉션 비용, 비동기 씬 로드 중 최악 프레임을 재지 않았다.** 큰 씬 · 쿠킹본으로 잰다(Dev 는 `Cooked/`
  를 마운트하지 않는다).
- **`GpuInstance` 96 → 128 B(VAT 위상 · `pixelSnap` 칸)의 비용을 재지 않았다.**
- **조건부 후보 묶음.** 병렬 틱 문턱의 교차점 · GameObject 레이아웃 · 적응형 틱 문턱 · 스폰 비용(~1.1 us, 잠금 여섯) · 시퀀서 성능 수치. TickItem 인라인
  재시도는 오브젝트의 틱 부기 49 B 를 먼저 줄여야 한다. 측정해서 이기면 한다.
- **Core 에서 미룬 결정.** 전역 소형 블록 할당자(프레임당 할당이 0 근처가 된 뒤 로드 시간으로 판단 — 지금 ~10 회/프레임).
- **필드 재배치로 8 B 이상 줄일 수 있는 타입이 남아 있다**(`RunPaddingReport.py` 로 보고만 함). 많이 만들어지는 것: GameObject 200→192, Mesh 112→104,
  MaterialInstance · Material · InputMap::ActionEntry 16, MaterialProperty · ShaderBindingSlot · InlineSuccessorList · GlobalVariableInfo/Registrar 8. 싱글턴(InputManager ·
  Logger 64 등)은 이득이 작다. PROPERTY 필드는 직렬화 순서라 옮기지 않는다. 위치 초기화 표(EditorAssetTypeInfo · AssetMatchRow · CommandRow)는 모든 행을 같이 바꿔야 한다.
  FrameRenderer 진단 세터(`setMeshMorphDiag` · `setDrawMergeEnabled` · `setVertexPoolEnabled`)는 Shipping 제외 후보.
- **ReflectionParser 강제 include PCH**(`CoreMinimal.h` 를 PCH 로 — 타깃당 ~0.4 s). 캐시 위치 · 무효화가 필요하다. 값이 작아 보류.

### 1-9. 빌드 · 린트 · CI · 테스트

- **Shipping 은 심볼 없이 링크한다(/DEBUG · PDB 없음).** 그래서 배포본 크래시 묶음의 `buildId` 가 비고(덤프의 모듈에 RSDS 서명이 없다) 덤프를 심볼과 짝지을
  수 없다. 상용 엔진처럼 Shipping 도 `/Z7`(또는 `/Zi`) + `/DEBUG:FULL` 로 PDB 를 만들고 패키지에서는 빼서 심볼 저장소에 넣는 단계가 필요하다
  (`Source/Engine/Telemetry/README.md` "심볼 · 빌드 id 짝짓기"). 빌드 시간 · 캐시에 닿는 결정이라 미뤘다 — 정하면 `ModuleBuildIdTest` · `CrashBundleTest` 의
  `SW_SHIPPING` 예외를 지운다.
- **커버리지 안내 퍼징(libFuzzer)은 Windows 에서 엔진과 링크되지 않는다** — `clang_rt.fuzzer-x86_64.lib` 가 정적 CRT(/MT)뿐이라 동적 CRT(/MD) 엔진과 LNK2038.
  `LoaderFuzzTest`(시드 고정 변이)가 같은 대상 표(`Test/EngineTest/LoaderFuzzTargets.cpp`)를 돈다. 리눅스 clang 에서 `LLVMFuzzerTestOneInput` 하나로 그 표를 붙이고
  ASan 과 같이 돌린다(서드파티 디코더만 떼어 /MT 로 돌리면 Windows 에서도 된다 — stb_vorbis 를 그렇게 확인했다).
- **Windows CI 시험 단계 실패(10-02 부터 Debug, 10-03 부터 Shipping)의 원인은 이 PC 에서 재현하지 못했다** — CI-Debug · CI-Shipping 을 같은 라벨로,
  TEMP 를 8.3 짧은 이름으로 바꿔서도 돌렸다(부하로 인한 시간 초과 말고는 통과). CI 의 시험 단계가 이제 진 시험을 주석으로 올리므로 병합 뒤 첫 실행의
  주석(`/check-runs/<job id>/annotations`, 로그인 없이 읽힌다)에서 시험 이름 · 실패 줄을 보고 고친다.
- **골든 이미지 기준은 한 PC(RTX 3070 Ti Laptop · 그 드라이버)에서 뜬 것이다** — 다른 GPU · 드라이버는 허용 오차를 넘을 수 있다. 다른 기계에서 지면 그 기계에서 `--record` 로 뜬
  기준과 견줘 차이가 드라이버인지 회귀인지 가른 뒤, 기계별 기준(`<백엔드>.<기계>.json`)이 필요한지 정한다.


- **시험 공백 목록** — `StringBuilder` 할당 실패(주입 창구 없음), 팩과 낱개 파일의 우선순위, 컴포넌트 풀 키, `syncAfterSceneGenerationChange`,
  `RenderGraph::executeParallel` 의 제출 실패 경로, `_materialCb` 병합 키(그래픽스).
- **imgui-node-editor vcpkg 오버레이**(`ThirdParty/imgui-node-editor/vcpkg-port/`, `<exception>` 패치)는 업스트림이 같은 고침을 받으면 지운다.
- **include · 전방 선언 남은 후보.** ① OS 헤더(`Core/Common/PlatformOsHeaders.h` — `Windows.h` · `DbgHelp.h` · `Xinput.h` …)가 `EngineMinimal.h` 를 거쳐
  PCH 에 남아 있다(TU 2746 · `windows.h` 1671). 빼려면 먼저 `NOMINMAX` · `WIN32_LEAN_AND_MEAN` 을 CMake 정의로 옮기고(서드파티가 `windows.h` 를 먼저 include 해도
  min/max 매크로가 안 생기게), Win32 · POSIX API 를 쓰는 파일이 직접 include 한다 — 글자 그래프로 찾은 후보 26 개(`Core/Common/Macros.h` · `ModuleCompiler.cpp` ·
  `WindowsFileWatcher.h` · `InputManagerWin32.cpp` · `XInputGamepadDevice.h` · `TestFramework.cpp` …, 오탐 섞임)를 빌드로 하나씩 확인하는 단계다.
  ② `GameObjectManager.h` 의 값 멤버 서브시스템 8 개(`ScenePhysics` · `SceneNavigation` · `SceneAudio` · `SceneOverlapWorld2D` · `PrimitiveRegistry` ·
  `LightRegistry` · `CameraRegistry` · `AnimationSystem`)를 `unique_ptr` 로 바꾸면 GOM 을 include 하는 234 TU 에서 헤더 32 개(약 4,700 줄, 서드파티 없음 —
  Physics/* · Navigation/* · Animation 보조)가 빠지지만, getter 를 쓰는 85 파일이 직접 include 해야 해 실제로 덜어지는 것은 약 150 TU × 4.7k 줄이다 — 보류
  (2026-10-05 재측정, `_store` · `_transformHierarchy` · `_structuralChangeBuffer` · `_tickScheduler` 는 헤더의 인라인 · 템플릿이 써서 포인터로 못 뺀다).
  이득은 `ninja -t deps` 전후 TU 수로 판정한다.

### 1-10. 관찰 중 — 다시 보이면 원인을 판다

- **GL 백엔드 `[Error] bindGraphicsContext failed - the context is held by another thread`**(2026-10-04, NileCity 자동 플레이 320×180 Debug, 골든 기록 중
  한 번 — 같은 인자 5 회 재실행은 깨끗). GL 컨텍스트를 렌더 스레드와 다른 스레드가 같이 잡는 순간이 있다. 골든 러너가 진 판의 App 출력을
  `%TEMP%/sw_golden_<백엔드>_<회차>_app.log` 로 남기니, 다시 보이면 그 로그로 어느 스레드 · 단계인지 본다.

- **ReflectionParserTest 는 파서 프로세스를 케이스마다 1~4 번 띄운다**(2026-10-04 여섯 조각으로 나눔 — 파서 케이스가 25 개로 늘었다). 더 줄이려면 파서 실행
  비용을 깎는다: CoreMinimal.h 를 PCH 로 미리 컴파일해 `-include-pch` 로 쓰는 것. 함정: PCH 에서 온 헤더를 `clang_getInclusions` 가 의존으로 내는지 먼저
  확인할 것(안 내면 depfile 이 비어 반사되지 않은 헤더가 바뀌어도 단계가 다시 돌지 않는다). 실행 하나의 0.5~1 초는 프로세스 생성 · 종료라 파서 탓이 아니다.

- **Shipping `EngineTest_NoGPU` · HostOnly 간헐 세그폴트**(09-20 · 21 · 22 에 한 번씩). 09-23 에 고친 DX11 기록 컨텍스트 결함과 모양은 같지만 단정하지 않았다.
  이제 시험 실행 파일에 크래시 핸들러가 있어 다음에는 스택이 남는다 — 직접 실행해 전체 출력을 파일로 받는다.
- **Shipping `CoreTest` 의 `Failed to deserialize config from: shipping_host_generated`**(한 번, 3 회 재실행 통과). `ConfigManager::loadConfigFromJson`.
- **WSL lavapipe 의 첫 `vkAcquireNextImageKHR` 가 가끔 `VK_ERROR_SURFACE_LOST_KHR`** 로 진다(`AppTest_HostOnly`, 43 회 중 3 회, 환경 탓으로 판단 — 미확정).
  다시 보이면 기준선과 번갈아 돌려 가른다. App 로그는 `build/WSL-Debug/Bin/Saved/Logs`.
- **CI Windows 러너(WARP)에서 픽셀 시험이 지던 원인은 판정하지 않았다**(`RenderPassGpuTest` 를 host 스위트로 빼서 우회). 실패 값이 `좌 0, 우 0` 이면 WARP 가
  컴퓨트 컬링 · 인디렉트를 못 하는 것이니 초기화에서 끊고, 어중간하면 허용 오차를 본다.
- **리눅스 전용 경로는 이 PC 에서 돌려 보지 않았다** — `parseWriteTime`, POSIX `pipe2` · `close_range` · `launchDetached`, `alarm` 시한, X11 입력(좌표 · `XkbSetDetectableAutoRepeat`),
  리눅스 LTO 를 진짜 `llvm-ar` 로 끝까지 링크하기, `verifyModuleBindings` 의 dlsym 도장 갈래(`727b872c` 뒤). 리눅스 CI 가 초록인지 · IPO 가 실제로 켜졌는지를 본다.
- **수동 확인이 안 된 에디터 동작** — Hierarchy `tag:` 필터, 검색 0 건 힌트, Classic Dark 테마의 대화상자 편집 경로.
- **DbgHelp 외부 샘플러 소스가 저장소 밖에 있다**(세션 스크래치였다). 다른 PC 에 남아 있는지 확인하고, 필요하면 `Scripts/dev/` 로 들인다.

### 1-11. 결정이 필요한 것

- **(보류 — 사용자 결정 2026-10-03) `hashed_string` 에 FName 숫자 꼬리를 둘지.** 지금은 비교 · 표시 인덱스 두 칸(8 바이트)이라 `"Enemy_12"` · `"Enemy_13"` 이
  이름 표에 각각 영구 적재된다. 런타임에 번호 붙은 이름을 대량으로 만드는 경로(복제 · 스폰 이름 자동 부여)가 생기면 다시 본다 — 넣으면 `_숫자`(앞자리 0 제외)를
  떼어 정수 칸에 두고 비교는 (인덱스, 숫자) 쌍.

### 1-12. 낮은 우선순위 · 조건이 오면

- **100 줄 넘는 함수 정리.** 분해는 총량을 줄이지 않는다. 중복을 먼저 없애고, 그래도 문제면 본다. 목록이 필요하면 여러 줄 시그니처를 중괄호 깊이로 재는 스크립트로
  뽑는다(단순 정규식은 틀린다).
- **MonsterCollector 의 파티 · 박스 세이브**(쓰는 게임이 생기면) — `MonsterStorage` 를 세이브에 싣는 길이 없다. `MonsterInstance` 를 REFLECT 로 하거나 상태 바이트(`writeState`)로.
  맵 · 타일 자리 · 플래그는 `OverworldSaveGame` 이 든다.
- **타일맵 칸 데이터를 일반 레이어로**(두 번째 장르가 칸마다 다른 값 — 지형 비용 · 발소리 — 을 원하면): 지금 레이어는 0/1(`kArrTileFlagLayerInfo`)이고 워프 · 역할 · 스폰은
  전용 원소다. 값 종류를 정수로 넓히고 에디터 페인트 · 형식 시험을 같이 바꾼다(Godot TileSet custom data 모양).
- **걸음 조우 판정 둘**(Overworld `shouldEncounterOnStep` 의 결정적 주기 · ClassicJrpg `JrpgEncounterWalker` 의 확률 + 유예) — 오버월드 위에 JRPG · 몬스터 수집 게임이 서면 기반 `World/` 로 하나를 올린다.

---

- **서드파티 빈자리(2026-10-05 후보 중 사용자가 고르지 않은 것).** 리눅스 오디오 출력 없음(`XAudio2System` 만, 리눅스는 `NullAudioSystem`) → miniaudio(퍼블릭 도메인/MIT-0) ·
  `gv_renderScale` 을 읽는 업스케일 없음 → AMD FidelityFX FSR(MIT) · 아랍어 셰이핑 · 양방향 없음 → HarfBuzz(MIT) + SheenBidi(Apache 2.0). 들이면 Jolt · Recast · Tracy 처럼
  엔진 인터페이스 뒤 + 격리 게이트, vcpkg 변경은 main 에서 먼저.

## 2. 작업 방식 — 정해진 방향

- **쪼개기보다 공통부 추출.** 긴 함수를 나누면 코드가 옮겨 갈 뿐 총량은 그대로다. 중복은 증상이고 원인은 "매번 다시 만들어야
  하는 구조" 다. 원인을 없앤다.
- **추가 · 변경이 쉬운 구조를 먼저 만든다.** 판단 기준은 "하나 더하려면 몇 곳을 고쳐야 하는가" 다. 복사해야 할 것이 남아 있으면 그
  자리가 다음 리팩터 대상이다. 목록이 여럿이면 한쪽만 늘어난다 — 목록은 하나(표 · X-macro · 폴더 열거)로 둔다.
  여러 파일이 같은 enum 을 `switch` 하는 자리를 셀 때는 "백엔드 · 스테이지마다 정말 다른 일을 하는가" 를 먼저 묻는다(그러면 정당하다).
- **한 곳에 넣은 고침은 형제에게도 넣는다.** 지금까지 가장 잦았던 결함 모양이다(한 함수만 상한을 안 봄 · 셋 중 하나만 폴백 없음).
  고칠 때 같은 일을 하는 형제를 grep 으로 찾는다. 원인이 같은 결함이 여럿이면 그 원인을 구조(한 창구 · 단언 · 게이트)로 막는다.
- **실패는 조용하면 안 된다.** 버린 값 · 못 읽은 칸 · 못 만든 폴더는 그 자리에서 무엇이 왜인지 알린다. 실패할 수 있는 bool 은
  `[[nodiscard]]` 이고, 일부러 버릴 때는 `(void)호출();` 과 이유 한 줄.
- **상용 엔진의 같은 자리와 견준다.** 새 구조를 정할 때 언리얼 · 유니티 · Godot 의 같은 자리를 먼저 보고 그 모양을 따르되, 일부러
  다르게 한 것은 이유를 적는다(예: `hashed_string` 은 FName 규칙이지만 숫자 꼬리는 없다).
- **성능 주장은 Release 숫자로만 한다.** 숫자 없는 최적화는 하지 않는다. 이전 · 이후를 같은 조건에서 번갈아 재고, 평균이 아니라
  p50 · p99 · 최악 프레임을 본다(3절 "측정" 참고).
- **주석 · 커밋 메시지는 한국어.** 문서 주석(`/** */` · `///<`)은 "~합니다" 체, 함수 본문 `//` 주석은 "~다" 체로 한 블록 안에서 통일한다.
  직역어 대신 표준 용어(thundering herd · 락 컨보이 · 브로드캐스트 · 조인 · continuation · 오버플로 · 분기)를 쓴다. 식별자 · 로그 ·
  assert 문자열 · `NOLINT` 줄은 그대로 둔다. 주석을 고치기 전에 구현을 읽는다 — 복사해 붙인 설명이 실제 동작과 다른 곳이 많았다.
  규칙은 [AGENTS.md](../AGENTS.md) 와 [04_CodingGuidelines.md](04_CodingGuidelines.md).
- **자산 처리 용어는 넷이다(상용 엔진 기준).** Cook = 배포 · 실행용 플랫폼 데이터로 바꾸기(셰이더 · 씬 · 프리팹 · 팩, `--cook-*`), Import = 원본 → 엔진 형식(텍스처, `--import-textures`), Generate = 빌드가 만드는 코드 · 헤더, Bake = 미리 계산한 결과(라이트맵 · 내비 — 지금은 없다). 한국어 "굽다" 는 쓰지 않는다. 이름을 바꿀 때 별칭(`Alias` · 옛 CLI 철자 · 옛 형식 리더)을 두지 않고 데이터를 다시 쓴다.
- **이 문서는 같은 커밋에서 고친다.** 끝낸 항목은 지운다. 남길 교훈이 있으면 3절에 한두 줄로 옮긴다. 여러 PC 에서 고치는 문서라
  작업 중에 pull · 충돌이 날 수 있다 — 커밋 전에 받아서 합친다.

### 안 하기로 한 것 (다시 제안하지 말 것)

- **지금 하지 않는 구조 후보 — 다시 볼 조건과 함께**(2026-10-03 상용 엔진 비교로 결정): 트랜스폼 SoA 2 단계(UE 액터도 AoS, 측정 근거가 생기면) ·
  선행 조건 스케줄러(시스템이 서로의 결과에 기대기 시작하면 — UE `AddTickPrerequisite` 모양) · 에셋 로더 등록제(종류가 대여섯이 되면 — UE `UFactory`) ·
  참조 카운트 RHI 핸들(한 리소스를 여럿이 나눠 들기 시작하면 — UE `TRefCountPtr`) · Mesh/Material `SlotHandle`(하지 않는다 — `shared_ptr` 이 수명과 RT 안전을 한 번에
  준다) · `ResourceUtil` 소유 객체화(하지 않는다 — UE `FPaths` 도 정적) · 링크 단위 분할(증분 링크 시간이 문제가 되면 별도 PR) — GameFramework 기반(약 58k 줄)을 `GFS_*` DLL 여럿으로 쪼개는 안도 같은 이유로 하지 않는다(2026-10-05 재확인). 기반 폴더의
  층은 폴더 층 게이트(`CheckGameFrameworkLayers`)로 지킨다. · `UnitStatsComponent` 를 기반(`Combat/`)으로(2026-10-05 — 쓰는 게임 · 씬 · 프리팹 0, 옮기면 데미지 숫자(UI 층 4) · `MonsterDef` · `DamageAppliedEvent` 셋을 끊어야 층이 맞는다. 체력 읽기는 `Combat/HealthSourceComponent` 가 맡는다. 다시 볼 조건: 다른 키트 · 게임이 피해 입구(`takeDamage` · 무적 · `DamageAppliedEvent`)를 이 키트 없이 쓰려 할 때 — 그때는 `DamageAppliedEvent` 도 기반으로, `setStats( MonsterDef )` 는 키트의 도우미로, 데미지 숫자는 `HealthListenerComponent` 파생으로 내리고, `CheckGameFrameworkLayers` 자가 시험의 키트 헤더 예를 바꾸고, `generated/GF_ActionCombat/UnitStatsComponent.gen.*` 을 지운 뒤 re-configure) · 피해 입구 인터페이스(19 곳 — 인자 모양이 넷이다. 키트 투사체 · 공격 판정을 다른 체력 모델(어빌리티 시스템 · 게임 컴포넌트)에 쓰는 게임이 생기면 `Combat/` 에 `takeDamage( 양, 쏜 쪽 )` 하나를 `HealthSourceComponent` 옆에 — 언리얼 `AActor::TakeDamage`)
  · 키트 아이템/효과 처리기 등록부(2026-10-05 전수: enum switch 4 곳 · 19 case, 두 키트가 나누는 종류 enum 0 — 키트마다 하는 일이 다르고 `-Werror=switch` 를
  잃는다. 데이터로 효과를 더할 게임은 기반 GAS 의 `registerExecutionClass` — 언리얼 GameplayEffect 자리 — 를 쓴다. 두 키트가 같은 종류 집합을 나누게 되면 다시 본다) ·
  `TimedModifierSet`(시간 제한 수정자는 이름 붙은 `Countdown` 슬롯이고 쌓기는 `extendTo` · `start` 로 이미 정해져 있다, 목록형은 위쳐 물약 한 곳 — 일반형은 GAS 의
  지속 이펙트 · 스택 정책. GAS 밖 키트 둘 이상에 목록형이 생기면 다시 본다) · API 통합 남은 판단(다음 훑기).

- **도구 버전을 "최신 자동" 으로 두는 것.** 네트워크 의존이 생기고 빌드 재현성이 떨어진다. 버전 키 하나로 고정하고 올릴 때만 의도적으로
  올린다. clang-format 은 버전이 곧 출력이라 고정이 아니면 안 된다.
- **`EditorContext` 의 소유 구조를 더 쪼개는 것.** 조회 · 생명주기를 두 TU 로 갈라 조회만 하는 코드가 ImGui 없이 링크되게 해 둔 것으로
  충분하다(그래서 `Test/EditorTest` 가 성립한다). 매니저 소유를 밖으로 빼는 것은 영향이 크고 얻는 것이 없다.
- **clang-tidy 의 `bugprone-throwing-static-initialization` 남은 건에 `noexcept` 를 붙이는 것.** 전역 변수 등록자 · 설정 싱글턴은
  `string` · `variant` 를 들어 실제로 던질 수 있다. 분석기를 침묵시키는 대신 정보를 지우는 거래다.

### 편집 함정

- 한 함수에서 **여러 구간을 빼낼 때는 뒤쪽 구간부터** 한다. 앞쪽을 먼저 빼면 뒤쪽 줄 번호가 밀린다.
- 파일을 스크립트로 고칠 때 CRLF 를 보존한다. 이 저장소는 CRLF 다.
- **bash heredoc 은 `\` 를 뭉갠다**(`'\0'` 이 널 바이트가 된 적이 있다). 백슬래시가 든 내용은 파일로 써서 넘긴다.
- **파서를 고친 뒤 "`.gen.cpp` 가 다시 만들어졌나" 를 산출물 시각으로 판단하지 말 것.** 내용이 같으면 파일을 다시 쓰지 않는다.
  다시 만들었는지는 옆의 `<이름>.gen.cpp.stamp` 시각으로 본다.
- `grep -v` 로 거를 때 이름이 비슷한 다른 것(`TestArchive` 등)까지 걸러지지 않는지 본다.
- **Windows PowerShell 5.1 의 `Get-Content` · `Set-Content` 로 소스를 고치지 말 것** — UTF-8 한국어 주석을 CP949 로 읽어 되돌릴 수 없게 깨고 BOM 을 붙인다. 파이썬(`encoding='utf-8'`)으로 고친다.

---

## 3. 참고 — 다음 작업에 필요한 것

끝낸 일에서 남긴 교훈만 모았다. 사연은 `git log` 에 있다 — 여기에는 다시 물릴 함정, 지켜야 할 계약, 되돌리면 안 되는 결정, 재는 법만 적는다.

같은 내용이 `AGENTS.md` · 폴더 `README.md` · 코드 주석에 정본으로 있으면 그쪽을 가리킨다.

- **`drainEvents` 는 붙이고 비운다(2026-10-03 통일).** 바꿔치기(`swap` · `std::move`) 하던 18 곳을 맞췄다 — 바꿔치기는 받는 쪽 목록의 앞 내용을 지우고, 붙이기에
  익숙한 호출부는 같은 알림을 매 프레임 다시 받는다(StarSkirmish 에서 패배 로그가 두 번). 매 프레임 같은 목록을 쓰는 쪽이 먼저 `clear()` 한다.
- **키트 커밋은 저장소가 고정한 clang-format(`Tools/LLVM/bin/clang-format`, 20)으로.** 시스템의 18 은 멤버 포인터(`float32 Foo::*_pMember`) 줄을 다르게 맞춰 린트가 막는다.

### 3-1. 측정 · 프로파일

- **물리 벤치: `PhysicsBenchTest`**(Release) — 먼 이동 바디가 있는 step p50 1124~2468 → 319~330 us(없는 step 은 319~328 us 그대로).

- **AppSmokeTest 의 Unknown 태그 상한(8 KB)은 파일 수에도 걸린다** — 태그 없는 호출자(App 스플래시)가 공유 캐시(`ResourceUtil` 경로 캐시)의
  재해시를 일으키면 그 버킷 배열이 Unknown 으로 센다. 데이터 파일 몇 개를 더하자 18 KB 가 넘었다. 공유 캐시는 넣는 자리에서 자기 태그를 건다.
- **성능은 Release 로 잰다.** Debug 는 레이스 검출기 · 이터레이터 프록시로 컨테이너 코드를 과장한다(668 vs 87 us). 이전 · 이후 바이너리를 같은 스크립트로
  **번갈아** 2~3 회 잰다(`git stash -u` → 빌드 → 복사 → `stash pop` → 빌드). 아침 기준선과 오후 결과를 견주면 기계 상태가 결과로 읽힌다.
- **측정 기계**: i5-8500(6 코어 6 스레드). 게임 · 렌더 스레드 + 워커 넷이 코어를 나눠, 나눠도 벽시계가 잘 안 준다 — 틱이 쓰는 CPU 총량이 벽시계를 정한다.
  프로파일러가 없으면 `build/Ninja-Release-Prof`(Release+PDB, 프리셋 아님) + 외부 DbgHelp 샘플러. ICF 로 함수가 남의 이름으로 보인다.
- **표 읽기.** 열은 avg · p50 · p99 · min · max · per_frame 이고 카운터 값은 per_frame 열에 있다(시간 열 0 을 "죽은 경로" 로 읽지 말 것). 평균이 히치를 가린다 —
  p50 · p99 · 최악 프레임을 본다. 백분위는 옥타브 × 8 칸 히스토그램의 아래 끝(±9 %)이다. 구간 표는 스레드마다 **일한 시간**이고, 프레임이 빨라졌는지는
  `[Profile] wall N frames … us/frame` 와 `startup N ms` 로 본다. 중첩 합이 바깥보다 크면 표부터 의심한다.
- **`RT.Frame` = `RT.BeginFrame`(펜스 대기 = GPU 백프레셔) + `RT.ExecutePacket` + `RT.Present`.** `GT.Packet.submit` 이 크면 GT 가 RT 를 기다린다. `GT.Frame` 은
  `EngineLoop::tick` 만 재고 게임 모듈은 `App::run` 의 `GT.Game.update` · `GT.Game.fixedUpdate` · `GT.Editor.updateUi` 다. 2026-09-13 이전 RT 수치는 실제보다 작다.
- **병목은 씬 크기에 따라 뒤집힌다.** 큐브 2000 은 GPU 대기, 8000 은 게임 스레드다. 어느 쪽을 깎을지는 재고 나서 정한다. GT 가 병목이면 RT 구간이 늘어 보여도 경합일 뿐이다.
- **타임라인은 Tracy 로 본다**(`-gv_tracy=1` + 같은 판 0.13.1 뷰어, `Source/Engine/Utility/Profiling/README.md`). 표(`-gv_profileFrames`)는 구간마다 접은 숫자라
  "어느 스레드가 무엇을 기다렸나" 는 Tracy 의 스레드 타임라인으로 본다. 계측은 `SW_PROFILE_SCOPE` 하나가 둘 다에 남긴다. GPU 줄도 쿼리는 한 벌이다
  (엔진 타임스탬프 → Tracy 수동 GPU 컨텍스트). DX11 · Vulkan 은 GPU 시계를 컨텍스트를 열 때 한 번만 맞춰(큐를 기다린다) 긴 실행에서 GPU 줄이 조금씩 밀린다.
- **GPU 비용은 `GPU.<패스>` 타임스탬프로 나눈다.** 패스를 지워서 나누면 타깃 사슬이 바뀌어 답이 뒤집힌다(추정 38 us, 실측 123 us). `RT.BeginFrame` 은 GPU 시간의 대리값이 아니다.
- **재기 전에 VSync 가 꺼졌는지 본다** — 1/RT.Frame 이 주사율과 같으면 VSync 다. DXGI 는 스왑체인 생성과 `ResizeBuffers` **둘 다**에 `ALLOW_TEARING` +
  `Present( 0, DXGI_PRESENT_ALLOW_TEARING )`(짝이 안 맞으면 `INVALID_CALL`, `RHI/DX/RHIDxgiTearing.h`). Vulkan 은 present 모드. CLI 는 `-vsync`.
- **셰이더를 고쳤으면 재기 전에 `App.exe --cook-shaders`.** 빌드는 HLSL 을 다시 쿠킹하지 않는다.
- **벤치 스위치**(`Source/Games/Empty/BenchScene.cpp`): `-gv_benchMeshes=N` · `-gv_benchLights=N` · `-gv_benchGround=1` · `-gv_benchMovePercent=%` · `-gv_benchInstanced=1` ·
  `-gv_benchTickMovers=N`(틱 **안** 세터 — 실제 게임플레이 경로) · `-gv_benchSpawnChurn=N` · `-gv_benchMeshVariants` · `-gv_benchMeshShapes=N` ·
  `-gv_benchMaterialChurn*` · `-gv_benchAnimate=0`(컴퓨트 회전과 `update` 사인파를 **둘 다** 멈춘다), `-gv_deferred=1`, `-gv_useRenderThread=0`.
- **`-gv_profileFrames=N` 은 프레임 수다** — VSync 가 꺼진 가벼운 장면은 1500 fps 라 90000 프레임이 1 분에 끝난다(시간 상한은 없다). 시간으로 재려면
  `-gv_profileSeconds=S`(먼저 닿는 쪽이 끝낸다 — soak 이 쓴다).
- **프레임당 힙 할당**은 `-gv_profileFrames` 보고의 `alloc/frame`, 콜스택은 `-gv_profileAllocSites=N`(Debug App — 횟수는 최적화와 무관, 시간은 같이 재지 말 것).
- **씬 로드 측정**: `Scripts/dev/GenerateStressScene.py` 로 큰 씬(도형 섞기) → 게임 프리셋(`Config/Game/Empty.json`) `_startupScene` → `[SceneLoad]` 줄. Dev 는 `Cooked/` 를 마운트하지
  않으므로 쿠킹 효과는 `--cooked-dir=<repo>/Resource` 로 쿠킹하고 재고 지운다. `[SceneLoad]` 가 `.xml` 을 가리키면 쿠킹본을 안 읽은 것이다.
- **벤치가 상태를 공유하면 단계 순서를 잰다.** 손대지 않은 대조군이 움직이면 하니스를 의심한다. 벤치 메시가 공유라 배치 결함을 가린 적이 있다 — 씬에서 온 메시로도 본다.
- **GPU 업로드 비용은 호출당이다**(DX12 ~3.3 us) — 쪼개면 느려진다. 구간을 배열로 묶어 한 번에.
- **워커가 쓴 데이터를 다른 코어가 읽으면** 캐시 이동이 항목당 일(~40 ns)보다 비싸다. 쓰는 스레드와 읽는 스레드를 같게 둔다.
- **워커는 공유 카운터 · 비트필드에 쓰지 않는다.** `fetch_add` 한 줄이 병렬 플러시를 직렬보다 느리게 했다. "하나라도" 플래그는 프레임에 한 번 쓰고 읽기만 한다.
- **도구**: `RunDuplicateCode.py --filter <dir> --no-headers`(머리말에 "합칠 대상 아님" 목록), `RunEngineLayerGraph.py`, `Scripts/dev/BackendSmoke.py`(4 백엔드 평균 RGB),
  Release 로 읽는 `TaskManagerBenchTest` · `GameObjectBenchTest` · `ContainerBenchTest` · `NetReplicationBenchTest`(클라이언트 16 × 엔티티 1000), `Test/TestFramework/TestBench.h`.

### 3-2. 검증 · 시험 쓰기

- **`--test_filter` 의 구분자는 쉼표다**(`A.*,B.*`). `:` 로 이으면 패턴 하나가 되어 아무것도 맞지 않는다 — 고르는 패턴이 등록된 케이스 하나와도 맞지 않으면
  실행이 진다(`TestFrameworkTest.FilterThatSelectsNothingFails`). 그 전에는 0/0 으로 통과해 제안서들의 확인 명령이 아무것도 돌리지 않았다.
- **광선이 두 삼각형이 나누는 모서리를 정확히 지나면 Möller–Trumbore 가 양쪽을 다 놓칠 수 있다** — 같은 각도로 나뉜 합성 원기둥 두 겹에서 실제로 났다
  (`CharacterGeometryUtil::intersectRayTriangle` 은 무게중심 여유 1e-5 로 막는다). 합성 형상 시험은 분할 수를 서로 다르게 하고, 면 모양(다각형)이라 반지름이 면 가운데서
  `r · cos(π/n)` 로 준다는 것도 기댓값에 넣는다.
- **App 의 종료 코드 77 = 이 기계 · 빌드가 그 RHI 백엔드를 못 돌린다**(`RHIInitResult` 의 `BackendNotBuilt` · `DriverUnsupported`). 시험 · `AppRun.py` 는 로그 문구가 아니라 이것으로 건너뛴다 —
  환경 탓으로 물러나는 새 경로는 백엔드가 `_initResult` 를 적어야 건너뜀이 된다(안 적으면 결함으로 진다).
- **백엔드마다 디바이스를 세우는 시험은 `test::RHIBackendSweep`** — `for ( test::RHITestDevice& device : sweep )`, 건너뛰기는 케이스가 `sweep.getReadyCount() == 0` 으로.
  오프스크린 · 한 프레임 도우미(`makeSingleTargetPsoDesc` · `beginOffscreenRenderPass` · `countPrimaryColorPixels` · `renderSceneFrame`)를 먼저 볼 것.
- **macOS 는 지원 대상이 아니다** — 코드는 10-04 에 지웠고 `CheckTargetMacros` 가 `SW_PLATFORM_MACOS` 를 막는다. 되살리려면 그 전 git 기록에서. `Vcpkg.cmake` 의 APPLE 갈래 ·
  레거시 스탬프 이관과 `arm64-osx` · `x64-osx` 트리플릿은 손대지 않는 파일이라 사용자가 정리한다.
- **ThreadSanitizer 는 이 리눅스 환경의 clang 18 에 런타임이 없다** — clang 으로 `-fsanitize=thread` 컴파일하고 링크만 gcc 의
  `/usr/lib/x86_64-linux-gnu/libtsan.so.2` 를 직접 붙이면 돈다(`setarch -R` 으로 ASLR 을 끈다). 일부러 만든 경합을 잡는 것까지 확인했다(2026-10-03,
  `NetworkThreadTest`). 잠금 · 스레드를 바꾼 뒤 그 시험만 이렇게 돌린다.
- **고정 틱 창 끝에서 "모두 같다" 를 보는 네트워크 시험은 서버 물리가 그 창 안에 가라앉는다고 가정한다** — 물리 궤적은 구성마다 달라 리눅스 Shipping 에서는
  파괴 벽의 사건이 480 틱 창 너머까지 와 수렴 단언이 졌다(Debug 는 269 틱에 멈춤). 수렴은 창 뒤 상한 안에서 같아질 때까지 돌려 본다(`ScenarioOptions::_settleTickLimit`).
  해시가 갈리면 먼저 사건마다 적용 전 · 후 해시를 서버 · 클라이언트에 찍어 같은 (오브젝트, 번호) 끼리 맞춰 본다 — 결정성 결함인지 시험 가정인지 한 번에 갈린다.
- **느린 시나리오는 조건마다 한 케이스로 쪼갠다** — 조각(`SHARDS`)은 스위트 안의 케이스를 번갈아 나누므로, 한 케이스에 조건 둘을 넣으면 한 조각이 둘 다 진다
  (`NetSimDestructionMatrixTest` 는 회선 둘을 두 케이스로 나눠 호스트 스위트에서 nogpu 로 왔다 — WSL Debug 케이스마다 17 초).

- **CoreTest 는 엔진을 쓰지 않는다** — include 경로로는 막을 수 없다(`TestFramework` 가 Engine 을 PUBLIC 링크, `TestFramework.h` → `EngineMinimal.h`).
  `CheckTestSuites` 규칙 6 이 CoreTest 파일의 직접 Engine · GameFramework · Editor include 와 `engine::` 호출을 막는다. 엔진 타입이 필요하면 지역 대역을 쓰거나 EngineTest 에.
- **레이어 때문에 지금 자리가 가장 낮은 합법 자리인 파일 넷**(`SpriteClipCache` · `PackCompressionUtil` · `ObjectUndoUtil` · `GpuLight.h`)은 README 에
  이유가 있다 — 다시 "잘못 놓였다" 로 옮기지 말 것.

- **호스트 스위트 케이스는 예상 밖 `[Error]` 로그 하나로 진다** — 아직 못 고친 엔진 Error 는 `SW_TEST_KNOWN_ERROR_LOG( 스위트, 문구, 이유 )` 로만 허용하고, 고치면 그 줄을
  지운다(실행 끝에 남은 선언이 출력된다). 골든 이미지는 `AppSmokeTest.BenchFrameMatchesGoldenImage`(`Test/AppTest/Golden`, `SW_UPDATE_GOLDEN=1` 로 다시 뜬다).
- **Debug 의 단언 경로는 `test::ScopedAssertCapture` 안에서 시험한다**(멈추지 않고 센다).
- **모듈 코드 정리 시험에 이미지 전체를 범위로 주지 말 것** — Shipping 은 엔진까지 한 exe 다. 스텁 · vtable 하나씩으로 좁힌다.
- **큰 `reserve` 는 실패하지 않을 수 있다** — 상한 시험은 할당 바이트로 단언한다.

- **시간으로 움직이는 GPU 픽셀 단언은 `FrameRenderer::setAnimationTimeOverride` 로 시각을 고정한다**(음수 = 벽시계). 단위 큐브는 모프 위상이 π 근처에 몰려
  t ≈ π/2 + kπ 에서 변위가 다 함께 0 이 된다 — 벽시계로 찍으면 부하에 따라 간헐로 진다.
- **에디터 확장의 순서는 등록 순서가 아니라 `SW_EDITOR_*` 의 order 키다.** 등록이 빠지거나 순서가 바뀐 것은 `AppSmokeTest.EditorRegistriesKeepTheirOrder`
  (`-gv_editorRegistryDump=1`, hostgpu)만 잡는다 — 패널 · 메뉴를 고치면 hostgpu 를 돌릴 것.

- **`ScopedDefensiveTestLog` 범위 안의 로그는 메시지 앞에 `"[Expected Defensive Test] "` 가 붙는다** — 문구 비교는 startsWith 가 아니라 포함 여부로.
- **워크트리와 vcpkg 스탬프:** 스탬프 해시는 트립릿 파일(`cmake/Modules/Toolchain/Vcpkg/*-{windows,linux,osx}.cmake`)의 **바이트**로 계산한다. 작업 사본의
  줄끝이 체크아웃과 다르면(git 은 같은 파일로 본다) main 과 워크트리가 서로의 스탬프를 "매니페스트 변경" 으로 보고 configure 마다 vcpkg install 이 돈다.
  워크트리를 만들기 전에 `git diff --quiet` 로 같은지 보고, 다르면 그 파일을 지우고 `git checkout` 으로 다시 받는다.

- **진단 도구 표는 `docs/01_GettingStarted.md` §5** 가 정본이다(`-gv_dumpReflection=…`, `ReflectionParser --dump`, `-gv_dumpRenderGraph=1`, `'A' waits on 'B'`,
  `경로:줄:열: 이유`, `[SW_ASSERT]` stderr, `-gv_screenshot`). 시험 도우미도 거기 있다.
- **렌더 변경은 스크린샷까지 봐야 검증이다.** 로그 · 시험이 다 초록인데 화면만 찢어진 적이 있다. 정적 씬은 바이트까지 결정적이라 4 백엔드 전후 sha 로 비교한다.
  4 백엔드 대조는 Debug 로(Shipping 은 `SW_SHIPPING_RHI_BACKEND` 하나만 링크하고 `-vk` 를 거절한다).
- **픽셀 지표는 튼튼하게.** "특정 색 픽셀 수" 는 클리어 색 · 톤맵에 무너진다 — 모서리 기준 배경 제거 + 평균(R−B) 대소, 또는 "고유 색 ≥ 2". sin(시간) 구동
  지표는 "달라진 픽셀 수" 로. define 이 GPU 에 닿았는지는 PSO 디스크립터가 아니라 픽셀로 본다. 막히면 PPM 을 덤프한다.
- **`-gv_screenshot` 은 PrintWindow 가 아니다** — PPM 이 정본이다. `-gv_screenshotFrame=N` 으로 프레임을, `-gv_screenshotAttachment=<이름>` 으로 첨부를 고른다.
  "가끔 튄다" 는 한 장으로 안 잡힌다 — `-gv_screenshotCount=30 -gv_screenshotInterval=2` 로 연속으로 찍어(`_000` … 이 붙는다) 장을 나란히 본다.
- **"조용한 프레임" 버그는 벤치가 가린다**(회전 큐브라 매 프레임 dirty). 에디터 정지 화면으로 본다: `-EnableEditor -gv_benchMeshes=1 -gv_screenshot -gv_screenshotFrame=60`.
  그리는 것이 의심스러우면 `-gv_gpuCulling=0` · `-gv_drawMerge=0` · `-gv_vertexPool=0` 으로 경로를 하나씩 뗀다.
- **백엔드 교체는 헤드리스로 재현한다**: `-gv_rhiSwapAtFrame=N -gv_rhiSwapTo=<backend>`. 투명 큐브가 카나리아다. 텍스처 경로는 `-gv_defaultMaterial=engine/materials/benchtextured.material`.
  전역 변수는 `GlobalVariableInfo::setValueAsInt` 로 넣는다 — **C++ 대입은 변경 콜백을 부르지 않는다.**
- **실제 백엔드는 로그 `Initializing RHI with backend: <이름>` 으로 확인한다**(설정값에 덮여 "네 백엔드 확인" 이 DX12 네 번이던 적이 있다). "인자가 적혔는가" 는
  `CommandLineManager::isArgumentProvided` 로(`getArgument` 는 기본값이 있으면 안 적어도 true).
- **리사이즈는 창을 실제로 흔들어야 검증된다.** 블로킹을 걷을 때는 그것이 무엇을 가리던지 먼저 찾는다(에디터 모드가 DX12 DEVICE_HUNG 을 가렸다).
- **레이스 시험은 "돌았다" 가 아니라 결과를 대조한다**(직렬 1 판 · 병렬 3 판 평균 ±1 %, 검출률 ~7/8). 이 시험은 hostgpu 라 CI 가 못 돈다 — 병렬 기록을 건드리면
  로컬 hostgpu 를 여러 번 돌린다. 레이스는 deferred 파이프라인으로 돌려야 드러난다. 겨루는 시험은 상대가 **돌기 시작한 뒤** 겨루고 "겹친 횟수 ≥ N" 을 단언한다.
- **변이 검사 함정** — 조건을 지우는 변이는 방향에 따라 우연히 통과한다. 백업을 `Copy-Item` 으로 되돌리면 mtime 이 과거라 ninja 가 안 짓는다(touch). 변이한 코드를 태우는
  스위트가 어느 실행 파일에 있는지 먼저 본다. "깨끗하게 실패했는가" 까지 본다(부정 단언 메시지는 `SW_EXPECT_FALSE_MSG`). 성공 쪽이 조용한지도 본다. 저장소 밖
  `mutlib` 은 기준선을 돌리지 않는다 — 시험이 먼저 통과하는지 확인한다. 변이 뒤에는 전체를 다시 빌드한다(부분 빌드는 모듈 DLL ABI 가 섞인다).
- **새 시험만 걸러 돌리고 커밋하지 말 것.** 빌드가 깨진 채 돈 ctest 는 **옛 바이너리**로 통과한다. 빌드가 진 프리셋(DLL 잠김 — `Scripts/setup/AddDefenderExclusions.py`)에서
  시험을 돌리지 말 것.
- **간헐 실패를 다룰 때** — `ConsoleLogOutput` 은 Error 만 flush 해서 ctest `--output-on-failure` 의 마지막 `[ RUN ]` 은 크래시 위치가 아니다. 같은 명령을 직접 돌려 파일로
  받는다. 시험 실행 파일에도 크래시 핸들러가 있다(`Test/TestFramework/main.cpp`). 단독으로 통과하고 스위트에서만 지면 전역 상태 오염부터(VFS 는 `GlobalVfsScope`).
- **검증은 한 번에 하나만 돌린다.** 빌드 · 스모크 · 벤치 · GPU 스위트를 겹치면 가짜 실패(파일 잠김 · CMake 프로브 레이스 `LNK1107`)와 수치 오염이 난다. 백그라운드 셸은
  `taskkill /T` 로 트리째 내린다. 창을 만드는 GPU 시험을 Bash 로 돌리면 타임아웃에 걸린 것처럼 보인다 — PowerShell `Start-Process`.
- **Shipping 에서만 나는 것** — Info 로그 · `SW_ASSERT` 가 사라지고(반응이 로그 한 줄뿐인 기능은 배포본에서 없는 것과 같다), 리소스는 팩에만 있고 `.hlsl` 원본이 없으며,
  쿠킹한 바이너리 씬만 읽는다(씬 · 프리팹 시험은 `SceneDocument::saveBinary` 로 `.bin` 도 쓴다). 풀 블록을 힙으로 반납하는 손상은 Shipping 에서만 터진다 — 풀 자유 목록이
  LIFO 인 것을 써서 "다음 할당이 같은 주소" 로 검사하면 Debug 에서도 잡힌다.
- **시험 도우미**: `test::makeTempPath` · `makeTempDirectory`(케이스 폴더, 끝나면 지우고 못 지우면 진다), `test::ScopedLogCollector`, `test::ScopedFailureCapture`,
  `test::ScopedDefensiveTestLog`, `SW_ASSERT_TRUE_MSG`, `test::runThisExecutableAsChild`, `test::RHITestDevice`(`kArrAllRhiBackend`), `test::RHITestImage`,
  `test::FakeRHIDevice`(병렬 기록 nogpu), `LitCubeScene` · `renderPresentCaptureOf` · `compareCaptures`(TestRenderPassGpu.cpp), 에디터 지역 서비스 `Test/EditorTest/EditorTestServices.h`, 네트워크 호스트 묶음 `test::LoopbackCluster`(`TestFramework/TestLoopbackCluster.h` — 루프백 +
  끝점마다 흉내, 손 시각 `step` · 호스트 스레드, CoreTest 도 쓴다. 씬 · 라우터까지 필요하면 `NetSimHarness`).
- **시험 실행기 규칙** — 필터로 고른 스위트의 케이스가 전부 스킵되면 실패다(`--allow_empty_suite`). 테스트는 `Bin` 에 쓰지 않는다. `RUN_SERIAL` 은 이유와 함께만.
  PowerShell 에서 쉼표가 든 `--test_filter` 는 따옴표로 감싼다(안 감싸면 앞 토큰만 먹고 오류도 없다).
- **단언 규칙** — 공개 API 는 단언 뒤에 진짜 if 가드를 둔다(Release 에서 단언이 사라진다). 그런 경로의 시험은 Debug 에서 skip 하고 Release · Shipping 에서 잰다.
  작업 스레드 안의 `SW_ASSERT_*` 는 그 람다만 끝낸다. future 시험에서 `wait()` 를 부르지 말 것(회귀가 정지가 된다) — `isValid()` 와 짧은 `waitFor`.
- **EditorTest 는 ImGui 를 링크하지 않고 정해 둔 Editor 파일만 링크한다**(`EditorAssetCommands.cpp` 는 빠져 있다). 시험할 로직은 ImGui 없는 함수로 꺼낸다 —
  `alignObjects( listObject, … )`, `EditorPlaySession::captureSnapshot( PlaySessionData& )`, `EditorSessionPolicy`. `EditorContext::get()` 을 읽는 함수는 시험할 수 없다.
- **할당 0 을 보는 시험은 여러 번 재어 최솟값을 본다**(누계는 프로세스 전체 값). 누수 시험은 `MemoryProfiler::getLiveAllocationCount` 로, 노드를 동시에 여럿 쥐고.
- **공유 시험 픽스처의 등록**(`makeMockComponentTypeInfo` 등)은 짝 `.cpp` 한 TU 에. 헤더에 두면 TU 마다 `TypeInfo` 가 중복 등록된다. 시험 본문(전역 스코프)에서는 `sw::` 로 한정한다.

### 3-3. 환경 · 툴체인

- **리눅스 빌드는 WSL 안의 클론(`~/LearningTemplate`)에서 한다.** 그 클론의 변경은 사용자 것이라 현재 상태를 덮어 빌드해도 된다. 가져올 때는 그 클론에서
  `git fetch /mnt/d/Projects/Personal/LearningTemplate main`. **`WSL-*` 프리셋을 Windows 체크아웃(`/mnt/d`)에서 돌리지 말 것** — 같은 `build/vcpkg_installed` 를
  리눅스 트리플릿이 덮어 Windows 트리가 통째로 선다(복구 27 분). DrvFs 에서는 configure 자체가 `Operation not permitted` 로 죽는다.
- **`wsl.exe -- bash -c "cd X && …"` 의 `cd` 실패는 조용하다** — 뒤의 `cmake -B` 가 저장소 루트에서 돌아 `toolchain_config.json` 과 공유 vcpkg 를 덮는다. `set -e` 나
  절대 `-S`/`-B`. 중단 뒤 `waiting to take filesystem lock…` 이 끝없으면: 남은 vcpkg · cmake · ninja 프로세스 종료 → `vcpkg-running.lock` 셋 삭제
  (`build/vcpkg_installed/vcpkg/`, `Tools/vcpkg/buildtrees/`, `Tools/vcpkg/packages/`) → `toolchain_config.json` 복원 → Windows 프리셋 재구성.
- **vcpkg 설치 폴더 · 스탬프는 워크트리 모두가 나눠 쓴다**(`build/vcpkg_installed` junction). 해시만 보고 설치하던 때는 옛 vcpkg.json 의 워크트리가
  configure(빌드 중 GLOB 로 도는 재구성 포함)하면 다른 워크트리가 쓰는 포트를 지웠다(recast · tracy). 지금 게이트(`Vcpkg.cmake` 5 절)는 스탬프가 다르면
  `vcpkg install --dry-run` 계획을 보고 — 지을 것이 없으면 skip, 지우게 되면 멈추고 경고(스탬프도 덮지 않는다), 빠진 포트가 있을 때만 install. 재현:
  `git show <옛 커밋>:vcpkg.json > vcpkg.json` → `cmake --preset Ninja-Debug` → "Install skipped … would remove" 경고 · 포트가 남는지 · `VCPKG_MANIFEST_INSTALL=OFF`
  → vcpkg.json 되돌림. 스탬프가 다를 때만 dry-run 이 돈다(약 20 초).
- **sccache 서버 포트(4226)를 Windows 와 WSL 이 나눠 쓴다.** 동시에 빌드하면 모든 컴파일이 `failed to fill whole buffer` 로 깨진다(코드 오류처럼 보인다). WSL 은
  `export SCCACHE_SERVER_PORT=4227`, 아니면 상대 서버를 `sccache --stop-server`.
- **WSL 에는 GPU 가 없다**(Vulkan 은 `llvmpipe` 하나, GL 은 `ARB_gl_spirv` 가 없어 빠진다) — API 오용은 잡지만 드라이버 거동은 못 본다. `libwayland-dev` 가 필요하다.
  **gdb 가 없다** — `/proc/<pid>/task/*/stat` 의 utime · stime 을 두 번 떠 사용자/커널을 가르고, SIGUSR1 처리기(`backtrace_symbols_fd`)를 임시로 넣어 `tgkill` 로 스레드마다
  스택을 뜬다. 리눅스 파일 하나는 `clang++ -fsyntax-only -DSW_PLATFORM_LINUX … -include Source/Core/pch.h <file>` 로 검사할 수 있다.
- **Ubuntu 26.04 는 `libxml2.so.2` 가 없어 번들 `ld.lld` 가 뜨지 못한다** — 시스템 lld 를 `--ld-path=/usr/bin/ld.lld` 로 EXE · SHARED · MODULE 세 링커 플래그 모두에
  (`SetupLinuxDevEnvironment.py` 가 안내한다). 리눅스 LLVM 은 `/usr/lib/llvm-*` glob 자연순 내림차순으로 찾는다(손목록은 새 배포판을 비켜간다).
- **X11 헤더는 X11 을 쓰는 `.cpp` 에서만**(`Core/Common/X11Headers.h`, 게이트 `CheckX11Isolation.py`). `PlatformOsHeaders.h` 가 X11 을 들고 있을 때 PCH 로
  모든 TU 에 `Convex` · `None` 같은 매크로가 퍼져 Jolt(`EShapeType::Convex`)가 리눅스 다섯 잡을 세웠다 — Windows 빌드는 원리상 못 본다. 유니티 빌드는 X11 `.cpp` 를
  include 줄을 보고 묶음에서 뺀다(`sw_skipUnityForX11Sources`).
- **플랫폼 스텁도 인터페이스를 따라간다** — `IWindow` 에 가상 함수를 더하면 `Win32Window` 의 비-Windows `#else` 스텁에도 정의를 둔다(빠지면 리눅스 링크만 진다).
  올라온 이미지는 이름이 아니라 주소로 찾는다(`ModuleBuildId::find( &함수 )._modulePath`) — `Engine.dll` 을 글자로 찾던 시험이 리눅스(`Lib/libEngine.so`)에서
  늘 건너뛰어 "아무것도 검증하지 않은 스위트" 로 졌다.
- **CI 가 끝까지 돌게 하는 세 가지**(`.github/workflows/ci.yml`). ① main 은 `cancel-in-progress: false` — push 가 실행 시간보다 잦으면 끝나는 실행이 0 건이 된다(10-04 7 시간).
  ② vcpkg 바이너리 캐시는 구성 직후 `actions/cache/save` 로 저장한다 — `actions/cache` 의 post 저장은 잡이 성공할 때만 돌아, 시험 하나가 지면 1 시간 지은 포트를 버렸다
  (Configure 50~90 분이 매번). 키는 OS 별 하나(트리플릿이 OS 당 하나). ③ Windows 는 `SW_ENABLE_PCH=OFF` — sccache 는 clang-cl 의 `/Yu` · `/Fp` 를 캐시하지 못해 적중률 0 % 였다
  (`sccache --show-stats` 의 "Non-cacheable reasons: /Fp"). PCH 를 끄면 PCH 가 가리던 오류가 드러난다(템플릿 본문의 `-Wcovered-switch-default`) — PCH 를 끈 구성도 짓는다.
  작업 로그 · 아티팩트는 API 로 403 이라 진 시험은 주석(annotation)으로 올린다 — 실행 목록 · 잡 단계 · 주석은 로그인 없이 읽힌다(시간당 60 회 한도를 여럿이 나눠 쓴다).
- **WSL 의 sccache 적중은 빈 의존 파일(.d)을 남길 수 있다** — 적중한 오브젝트의 `ninja -t deps` 가 `#deps 0` 이면 그 TU 의 소스 · 헤더를 고쳐도 `ninja: no work to do`
  다(유니티 TU 에서 봤다). 낡은 빌드가 의심되면 그 오브젝트를 지우거나 `SCCACHE_RECACHE=1` 로 다시 짓는다.
- **리눅스 CI 는 ubuntu-22.04 의 `libclang-dev`(16 미만)다.** 파서에 새 libclang API 를 쓰면 리눅스 잡만 선다 — `CINDEX_VERSION` 으로 가른다. CI 러너 파이썬은 3.10 이라
  f-string 식 안의 백슬래시 · 여러 줄 식이 configure 를 죽인다(`CheckPythonMinimumVersion.py`). GH Windows 러너는 cp1252 라 한글을 print 하는 스크립트가 빌드째 죽는다
  (증상: `sccache stats: 0 hits, 0 misses`) — 진입점은 `Scripts/common` 을 **모듈 수준에서** import 한다(UTF-8 stdout, 게이트 `CheckScriptEntryPoints` —
  함수 안의 import 는 치지 않는다: 한 갈래에서만 끌어오면 다른 갈래의 print 가 죽는다). 재현은 `PYTHONIOENCODING=cp1252`(cp949 는 `—` 에서 죽는다).
- **CI 실패는 실패한 잡과 같은 프리셋으로 재현한다.** Debug(Engine SHARED)는 Shipping 정적 링크 결함을 원리상 못 낸다. Windows CI 러너의 TEMP 는 8.3 짧은 이름
  (`RUNNER~1`)이라 경로를 글자로 비교하면 틀린다. 빨간 CI 는 다음 결함을 숨긴다(55 런 연속 실패를 아무도 몰랐다) — 런 상태:
  `curl -s "https://api.github.com/repos/sswgame/LearningTemplate/actions/runs?per_page=30&branch=main"`. 스킵은 실패보다 조용하다.
- **clang-format 은 고정 바이너리 `Tools/LLVM/bin/clang-format.exe`(20.1.8)** 로만 센다(`clang_format_version` 키로 LLVM 과 따로 고정, `Scripts/common/ClangFormat.py`).
  PATH 의 것으로 세면 틀린다(정답은 0 개). `--dry-run` 에 파일 여럿을 한꺼번에 주면 보고가 조용히 잘린다 — 파일마다 한 번씩 센다:

  ```bash
  CF=Tools/LLVM/bin/clang-format.exe
  find Source Test Tools/ReflectionParser \( -name '*.cpp' -o -name '*.h' -o -name '*.inl' \) -print0 \
    | xargs -0 -P 8 -n 1 -I{} sh -c "\"$CF\" --dry-run --ferror-limit=0 \"{}\" 2>&1 | grep -q warning: && echo {}"
  ```
- **clang-tidy 는 버전마다 다른 숫자를 낸다.** `RunClangTidy.py` 가 실행 파일 경로와 버전을 머리에 찍고 `--clang-tidy <경로>` 로 고정한다(VS 의 `VC/Tools/Llvm/x64/bin` 도 찾는다,
  PCH 는 `/Y-`). 남은 `bugprone-throwing-static-initialization`(전역 변수 등록자 · 설정 싱글턴 ~30 건)은 고치지 않는다 — 2절 참고. 숫자가 늘면 **종류**를 먼저 본다(전역
  변수 개수를 따라간다). 새 지적은 고치거나 `NOLINTNEXTLINE` + 이유(진단이 붙는 줄 바로 위). 끈 검사의 근거는 `.clang-tidy` 에 있다. `performance-enum-size`(열거형 폭에
  ABI · 직렬화가 달렸다)와 `performance-no-int-to-ptr`(Win32 API)는 기각했다.
- **ASan (Windows)** — SmokeTest 만 `report_globals=0`(DLL 을 내려도 전역 등록이 안 지워진다), `detect_odr_violation=0`(1 이면 1800 s+), `/MD` 강제, ASan 런타임 DLL 은
  `Bin` 과 `BuildTools` 양쪽(`cmake/Engine/TargetRules.cmake`). Windows 의 memcpy 는 겹쳐도 맞게 옮겨 겹친 복사 버그가 안 보인다 — 리눅스 ASan 이 잡는다.
- **TSan** 은 `SW_SANITIZER_KIND=thread` · `CI-Debug-TSAN`(GNU/Clang 전용, ASan 과 동시 불가). 크래시 자식 시험은 ASan · TSan 에서 건너뛴다. 새 CI 검사는 매트릭스에
  `reportOnly: true` 로 들여 보고를 추린 뒤 막는 잡으로 바꾼다.
  원자 연산으로 스스로 동기화하는 서드파티(Jolt · Box2D)는 트리플릿 `x64-linux-tsan`(`cmake/Modules/Toolchain/VcpkgTsan/`)이 계측해 짓는다 — 계측 안 된 정적 라이브러리는 동기화가
  안 보이는데 헤더 인라인 함수는 링커가 우리 TU 의 계측된 사본을 골라 거짓 경쟁 수백 건이 났다. 트리플릿 파일은 ABI 해시에 들어 고치면 포트를 다 다시 짓고(WSL 약 35 분),
  기본 CI 캐시 키가 보는 `Toolchain/Vcpkg/**` 밖에 둔다. 포트 컴파일러는 프리셋의 `CC=clang` 이 정한다(빠지면 vcpkg 가 GCC 로 짓는다).
- **유니티 빌드는 `CI-*` 프리셋에만 켜져 있다.** `Ninja-*` 가 초록이어도 익명 네임스페이스 충돌이 없다는 뜻이 아니다 — 헬퍼 · 상수는 `XxxInternal` 구조체로 감싼다.
  `sw_skipUnitySources` 가 다시 길어지면 규칙이 깨지고 있다는 신호다.
- **LLVM 을 다시 깔면 PCH 가 전부 낡는다**(`… has been modified since the precompiled header was built`). `.pch` 와 짝 `cmake_pch.cxx.obj` 를 같이 지운다(`SetupLlvm.py` 가 한다).
- **LTO 함정** — clang `-flto` obj 는 MSVC `lib.exe` 가 못 읽는다(LNK1107). 아카이버는 "지금 컴파일러 옆" 을 먼저 본다(리눅스 `/usr/bin` 에는 llvm-ar 이 없어 LTO 가 조용히 꺼진다).
  CMake 는 IPO 아카이브 명령을 `project()` 때 정해 두고, `check_ipo_supported` 는 거짓 NO 를 내서 직접 판정한다(`cmake/Environment/ToolchainBinaries.cmake`). `SW_ENABLE_LTO` 하나가 Release · Shipping.
- **임포트 · 수집 산출물은 줄끝 변환을 받지 않는다**(`.gitattributes` 의 `Resource/**/models{,_raw}/** -text` · `Resource/**/localization/** -text`). 스탬프는 원본 · 결과를 바이트 해시로 대조하는데,
  `core.autocrlf=true` 체크아웃이 `*.skeleton.json` 을 CRLF 로 바꾸면 그것을 임포트한 워크트리 밖에서만 "손으로 바꿨다" 가 된다. 텍스트 산출물을 새로 만들면 같은 규칙에 든다(`App --check-text` 도 CRLF 체크아웃에서 "OUT OF DATE" 였다).
- **GPU · 드라이버** — 반복 TDR 은 어댑터를 망가뜨린다(재부팅 필요). DX12 는 실패 지점에서 InfoQueue · DRED 를 강제로 뽑는다. 이름 없는 객체("Unnamed")가 보이면 `SetName` 부터 붙인다.
  비동기 로거는 크래시 직전 메시지를 잃는다 — 직접 진단은 `fopen` + `fflush` + `fclose`.

### 3-4. 빌드 · CMake · 린트 · 스크립트

- **파이썬 도구의 단위 시험은 `Test/PythonTest/Test*.py`** — 파일을 놓으면 CTest 항목(`PythonTest_<이름>`, `nogpu`)이다. Blender 애드온처럼 바깥 모듈(bpy)을
  쓰는 것은 그 import 를 한 파일에 가두고 나머지를 시험한다(`TestBlenderExporter` 가 빈 패키지 모듈을 세워 읽는다).

- **주석 정리에서 마커(예전 · 날짜 · 백로그)로만 뽑으면 과거형 경위("~를 각자 들고 있었습니다")가 영역마다 ~10 % 남는다** — `(었|았|였)(는데|다|습니다)` 로 한 번 더 훑는다.
  빌드가 도는 동안 헤더를 고치면 PCH 크기 불일치("modified since the precompiled header")로 빌드가 진다 — 편집과 빌드를 겹치지 말 것.

- **명령줄 철자는 인자마다 하나다** — `ArgumentList.xxx` 의 줄에 적은 것만 키이고 열거자 이름(`WIDTH` · `COOK_SHADERS`)은 키가 아니다(`CommandLineTest.EnumeratorNameIsNotACommandLineKey`). RHI 백엔드 줄만 쿠킹 표(`CookContract.json`)의 별칭 여럿을 받는다.

- **`git mv` 로 옮긴 시험 파일은 pre-commit 의 `CheckIncludeOrder` · `CheckTestSuites` 가 "변경 없음" 으로 건너뛴다** — 옮긴 뒤에는 `ctest -L lint` 로 확인할 것.
- **폴더를 옮기기 전에 옮길 파일의 include 를 티어 표와 대조한다**(2026-10-05 폴더 정리). 계획한 자리(`Animation/` · `Utility/Console/` · `Localization/`)가
  위층을 include 하는 파일을 받을 수 없어 `Character/Pose/` · `Character/AnimNotify/` · `DevTools/` 로 갔다. 엔진 루트는 `CheckEngineRootFiles` 허용 목록,
  폴더 크기 · 파일 하나짜리 폴더는 `RunFolderFileCount.py`(보고서). 시험은 소스 폴더를 따른다(`Test/README.md`). 옮긴 헤더의 옛 `.gen.cpp` 는 생성 폴더에서 지운다.
- **병합 커밋의 훅은 어느 부모와도 내용이 다른 파일만 파일 단위로 본다**(한쪽 부모와 같은 파일은 그 부모 커밋 때 검사됐다) — 부모 둘에서 따로 온 파일끼리의 관계는
  병합 뒤 `ctest -L lint` 로 확인할 것.

- **같은 클래스가 `#if` / `#else` 로 헤더에 두 번 있으면 `CheckCodeConventions` 의 헤더 기본값 검사가 그 클래스를 건너뛴다** — D3D11 · D3D12 비Windows 스텁을 지우자
  숨어 있던 위반 9 건이 드러났다. 다른 플랫폼 스텁이 있는 헤더도 같은 사각일 수 있다.

- **enum switch 는 LLVM 방식**: 모든 열거자를 다루면 `default:` 없음(`-Werror=switch` 가 빠진 case 를, `-Werror=covered-switch-default` 가 다 다룬 switch 의
  default 를 잡는다), 일부만 다루면 `default:`(`-Wno-switch-enum` · `-Wno-switch-default`). 파일별 `#pragma` 로 switch 경고를 바꾸지 않는다. 외부 헤더는 `SYSTEM`
  include 여야 이 규칙이 그 안에 걸리지 않는다(ReflectionParser 의 nlohmann-json 이 일반 `-I` 였다).
- **플랫폼 · 아키텍처 · 컴파일러는 SW_ 매크로로만 묻는다** — 컴파일러 내장 매크로(`_M_X64` · `__clang__` · `_MSC_VER` …)를 읽는 곳은
  `Core/Common/TargetMacroCheck.h` 하나(CMake 판정과 대조해 `#error`), 나머지는 `CheckTargetMacros` 게이트가 막는다. 아키텍처 판정은
  `CMAKE_CXX_COMPILER_ARCHITECTURE_ID` 기준(교차 컴파일에서 `CMAKE_SYSTEM_PROCESSOR` 는 틀린다). "MSVC 확장을 쓸 수 있는가" 는 `SW_PLATFORM_WINDOWS`
  로 묻는다(clang-cl 도 그렇다 — `SW_COMPILER_MSVC` 로 물으면 clang-cl 이 다른 갈래로 간다). MinGW 를 지원하면 이 전제와 검사 헤더를 함께 바꾼다.
  ReflectionParser(libclang)는 CMake 를 거치지 않으므로 `ParserConfig::load` 가 대상 매크로를 넘긴다. Linux arm64 · macOS 갈래는 실제 빌드로 확인된 적이 없다.

- **패딩은 `RunPaddingReport.py`(libclang + 컴파일 DB 플래그) 로 본다** — clang-cl(MS ABI)은 `-Wpadded` 를 내지 않고 `-fdump-record-layouts` 는 필드 위치를 안 준다.
  libclang 에는 `-resource-dir` 를 직접 줘야 한다(안 주면 MSVC `offsetof` 가 상수식이 아니어서 constexpr 표가 오류로 무너진다). 줄인 타입의 회귀는 "크기 ≤ 필드 합을
  정렬로 올린 값" static_assert 로 막는다(DrawCandidate · SpriteAnimatorComponent).
- **생성자 초기화는 `Style/ConstructorInitializesEveryField` 가 막는다** — 기본값 없는 스칼라 · 포인터 · 열거형 · atomic · 비트필드만 대상(컨테이너 · 문자열은 스스로 초기화).
  MSVC STL 은 atomic 을 값 초기화해 Windows 시험만으로는 빠뜨림이 안 드러난다.

- **소유하지 않는 포인터 등록부는 `Core/Container/RegistrationList<T>`**(중복 · 이름 거절, 순서, 이름 찾기, 이름 사본) — 슬롯 인덱스로 O(1) 빼기를 하는 등록부
  (Primitive · Tick · TransformHierarchy · 콜라이더)와 모양이 다른 것(TypeRegistry · GlobalVariable · 코덱 · RHIBackend)은 예외다.
- **`SW_ENABLE_DEADLOCK_DETECTION` 은 CI Windows Debug 잡이 `RunBuildWarnings.py --define SW_ENABLE_DEADLOCK_DETECTION --fail-on error` 로 지킨다.**

- **빌드 출력을 `| head` 로 자르지 말 것** — 파이프가 닫히면 빌드가 중간에 죽고 낡은 바이너리가 남는다. 파일로 받은 뒤 본다.

- **린트의 제외 폴더 비교는 저장소 아래 경로의 폴더 이름으로** — 절대 경로 부분 문자열로 비교하면 경로에 "build" 가 든 워크트리에서 파일을 하나도 안 본다
  (실제로 그랬다). 짓지 않는 소스는 CMake 가 `sw_declareUnbuiltDirectory` 로 적고 `CheckSourceGlob` 은 그 목록만 본다.
- **생성자 초기화의 반복자 쌍은 소괄호** — 중괄호면 initializer_list 생성자로 빠진다(`Style/IteratorPairBraces` 가 막는다).
- **실패한 커밋 뒤에는 스테이징이 남는다** — 다음 커밋 전에 `git status`. Git Bash heredoc 은 `\\` 를 뭉갤 수 있다 — 스크립트는 파일로 써서 돌린다.

- **RHI 백엔드 표(이름 · 별칭 · 셰이더 폴더 · 포맷)와 쿡 접미사 표의 정본은 `Config/Engine/CookContract.json`** — `GenerateCookContract.py` 가 C++ X-macro
  (`sw/config/CookContract.gen.h`)를 만들고 Python 은 `Scripts/common/CookContract.py` 로 읽는다. `CheckCookContract` 게이트가 쿠커 함수를 표와 대조한다.
  두 언어에 목록을 따로 적지 말 것(`PackFormat.json` 과 같은 모양).

- **소스 목록 중 손 목록이 셋 있다** — `Source/Core/CMakeLists.txt`(`cfSources`, 빠지면 ReflectionParser 링크에서 깨진다), `cmake/Engine/RhiBackendSources.cmake`(빠지면
  Engine GLOB 이 주워 **모듈의 미정의 심볼**로 나타난다), `Test/EditorTest/CMakeLists.txt`. `CheckSourceGlob` 이 디스크와 대조한다. 구성이 일부러 짓지 않는 소스는
  `sw_excludeUnbuiltSources` · `sw_declareUnbuiltSources` 로 적는다(`<빌드>/generated/sw/config/UnbuiltSources.txt`). 파일을 옮기면 경로를 문자열로 적은 곳은 컴파일러가 안 잡는다.
- **동적 모듈은 타깃을 만드는 자리에서** `sw_registerDynamicModule( <타깃> rhi|kit|game|gameframework|editor )` 로 등록한다. `sw_verifyDynamicModuleRegistry` 가 루트부터 훑어
  미등록 MODULE 이면 FATAL_ERROR.
- **생성 상수는 `Scripts/common/Constants.py` 의 `k*` 전부를 기계 변환한다**(`kDirSourceEngine` → `SW_DIR_SOURCE_ENGINE`). 경로 조립은 소비 쪽(`ConfigConstants.h.in`)이 한다.
  `configure_file` 은 빈 값을 조용히 넣는다. `toolchain_config.json` 은 CMake 가 파싱하지 않는다(`GenerateToolchainCMake.py` → `SW_TOOLCHAIN_<KEY>`) — 예외는 vcpkg 가 부르는
  `FindLlvmBin.cmake` · `VcpkgPortsToolchain.cmake` 둘이고, 거기서 `CMAKE_SOURCE_DIR` 은 vcpkg scripts 폴더다. 상수만 필요한 CMake 는 `LoadConfigConstants.cmake` 를 **파일
  스코프에서** include 한다(함수 안에서 처음 include 하면 상수가 그 함수에만 생긴다).
- **`sw_addVcpkgConfigLib` 는 패키지를 못 찾으면 빈 타겟으로 조용히 대신한다.** 반드시 있어야 하는 것은 `ThirdParty/<이름>/CMakeLists.txt` 에서 `find_package( … CONFIG REQUIRED GLOBAL )`.
  OBJECT 라이브러리(`Core_objects`)는 링크를 전파하지 않는다 — 외부 라이브러리는 `Core` 와 `Engine` 양쪽에. vcpkg 업스트림 결함은 `ThirdParty/<pkg>/vcpkg-port/` 오버레이로
  (`.gitattributes` 의 `*.patch -text`).
- **Shipping 정적 링크는 열거형만 든 `.gen.cpp` 를 버린다**(증상: `findEnum` 이 null, enum 2 개) — `sw_linkWholeArchive` 가 링커별 whole-archive 를 고른다.
- **지연 로드 훅(`__pfnDliNotifyHook2`)은 모듈마다 따로다.** 엔진 모듈 DLL 을 지연 로드하는 kit · SWGame 에만 넣는다(Engine 에 넣으면 시스템 DLL 로드마다 ERROR).
- **쿠킹** — `CookAssets` 는 App 에 의존하고 Shipping 에서는 `all` 에 든다. App 경로는 CMake 가 `--app $<TARGET_FILE:App>` 로 넘긴다(빌드 폴더를 뒤지면 다른 프리셋의 낡은 App
  을 집는다). 산출물은 `build/<preset>/Cooked/`. 소스 트리에 남은 옛 `.bin` 은 경고와 함께 팩에서 빠진다 — 지운다. 팩 코덱은 `Config/Engine/PackConfig.json`(LZ4 · Zstd 는 pip
  선택 의존성, 없으면 그 자리에서 멈춘다 — zlib 으로 조용히 물러나면 안 된다), 팩 계약 SSOT 는 `Config/Engine/PackFormat.json`(고치면 configure) · 파이썬 `PackFormatSpec`
  (`PackStruct.pack()` 은 필드 **이름으로만**).
- **경고를 재기 전에 그 프리셋을 한 번 빌드한다**(생성 헤더 `FlagOps.gen.h` 가 없으면 `-fsyntax-only` 가 가짜 오류 32 건). 파일을 옮긴 뒤 옛 compile DB 면 "no such file".
  주석만 바뀐 TU 는 sccache 가 캐시를 재생해 `-Wdocumentation` 이 안 보인다 — `RunBuildWarnings.py --preset Ninja-Debug`. 리눅스 전용 경고는 Windows 의 `RunBuildWarnings` 가 못 본다
  (WSL-Debug 로그). 한 플랫폼에서만 읽는 필드는 `[[maybe_unused]]`.
- **include 를 지울 때는 단독 컴파일로 확인한다**(PCH 가 가린다). `.xxx` X-매크로 include 는 빼도 컴파일되지만 함수 본문이 빈다 — 기계로 지우지 말 것. 헤더 안 `= default`
  소멸자가 `unique_ptr<T>` 멤버를 파괴하면 전방 선언으로는 안 선다.
- **전방 선언 후보의 이득은 `ninja -t deps` 로 전후를 센다**(그 헤더에 의존하는 오브젝트 수). `RunForwardDeclarationCandidates` 후보 40 건 중 실제로 준 것은 13 건이었다 —
  짝 `.cpp` 하나뿐인 후보는 include 가 그 `.cpp` 로 옮겨 갈 뿐이라 0, 값으로 거쳐 받던 헤더 · 인라인 멤버 접근 · 인라인 생성자의 `unique_ptr` 소멸자는 깨진다.
  강제 include `FlagOps.gen.h` 의 `*.gen.h` 는 `Core/Common/BitFlagTrait.h`(`<type_traits>` 만)만 든다 — 여기에 무엇을 더하면 모든 TU 의 누락이 가려진다.
- **PCH 안 헤더의 include 하나가 TU 수를 정한다** — `RHITypes.h`(PCH)가 쓰지도 않는 `RHIBackendType.h` 를 들어 1987 TU 가 그 헤더에 의존했다(끊은 뒤 약 474).
  끊을 때 직접 include 를 받는 것은 그 이름의 **값**(`RHIBackend::DirectX12`)을 쓰는 파일뿐이다 — 타입 이름만 쓰는 헤더는 불투명 선언(`enum class RHIBackend : uint32;`)으로
  선다. 거쳐 받던 파일은 글자 include 그래프에서 그 간선을 끊어 후보를 좁힌 뒤 `-fsyntax-only`(PCH 없이)로 확인한다.
- **D3D · DXGI · D3DCompiler · MF · XAudio2 헤더는 PCH 에 넣지 않는다**(`EngineMinimal.h` 는 OS 헤더만, 쓰는 파일이 `EnginePlatformHeaders.h` 를 직접) — 넣으면
  1,654 TU 가 `d3d12.h` 를 파싱했다(뺀 뒤 약 43). `#if defined( SW_HAS_DXC_API )` 처럼 정의 여부로 읽는 매크로는 정의 헤더가 빠지면 **조용히** 꺼진다(시험은 "컴파일러
  없음" 으로 건너뛴다) — `ShaderCompiler.cpp` 의 Windows `#error` 가 막는다.
- **헤더 자립은 정기 실행 + 커밋 훅이 나눠 막는다** — CI `header-self-contained.yml`(매일 · 수동)이 `RunHeaderSelfContained --fail-on-violation` 으로 트리 전체를,
  게이트 `CheckHeaderSelfContained` 가 커밋 훅에서 빌드 폴더가 있을 때 staged 헤더만 본다(`ctestSkipReason` 으로 CTest 린트에서 빠진다 — 전 트리 3~10 분). 판정은
  `common/HeaderSelfContained.py` 한 자리. 유니티 빌드(CI 프리셋)의 컴파일 DB 는 TU 가 빌드 폴더 안이라 `CMakeFiles` 앞을 소스 경로로 옮겨 씨앗 TU 를 고른다(안 그러면
  모든 헤더가 첫 TU 의 플래그를 받는다). 구성만 한 폴더(`FlagOps.gen.h` 자리 표시자)는 검사 불가로 친다 — 가짜 오류 수십 건.
- **X-매크로 목록 `.xxx` 의 정본은 `Core/Predefined/`** 이고 죽은 사본은 `CheckDataFileReferences` 가 막는다. `PredefinedNameType.xxx` 의 줄 순서가 곧 intern 인덱스다(중간 삽입
  금지, 대소문자만 다른 이름 금지).
- **`CheckCodeConventions` 알아 둘 것** — 명명 판정은 `kMapContainerVocabulary` × `kMapNamingSubject` 표 하나. `Style/BitfieldBoolean` · `Naming/DuplicateInternalHelper` ·
  `Style/HeaderMemberInitializer` 는 전체 스캔에서만 돈다. `Naming/OutParameter` 는 `out` 이 든 지역 변수(`arrOutput`)를 오탐한다. 게이트는 파일을 동시에 훑으니 규칙
  객체에 상태를 들지 말 것. 자기 시험 조각은 그 검사가 **통과하는** 바탕(`_kCleanFixture`) 위에 위반 하나만 얹는다.
  `Style/ConstructorOrder` 는 헤더에서 읽은 멤버 순서와 비교하므로, 멤버 선언을 못 읽으면(예전엔 `Widget* const* _ppWidget`) 순서가 맞아도 위반으로 건다 —
  멤버 정규식 `_kClassMemberRe` 를 넓히면 `_kWholeScanCleanCase` 의 `RangeTable` 조각으로 확인한다.
- **린트 정규식에 `(식별자+ … \s*)+` 모양을 쓰지 말 것** — 빈 구분자로 식별자를 몇 조각으로든 나눌 수 있어 맞지 않는 줄에서 역추적이 지수로 는다(한 줄 7 초,
  커밋 훅이 부하에서 수십 분). 식별자 뒤에 `(?![A-Za-z0-9_:])` 를 붙인다. 느린 게이트는 파일별 시간부터 정렬해 볼 것 — 평균이 아니라 몇 파일이 지배한다.
  줄 규칙의 `"글자" in line and 정규식` 앞 검사는 그 정규식이 반드시 품는 글자다 — 정규식을 바꾸면 같이 본다.
- **`CheckIncludeOrder` 는 첫 `#if` 를 경계로 삼는다.** include 가 전부 `#if` 안인 파일(`DelayLoadNotifyHook.cpp` · `PlatformOsHeaders.h` · `X11MacroUndef.h`)과 새 플랫폼 전용
  `.cpp` 는 손으로 순서를 지킨다(Core → Engine).
- **`CheckFunctionVocabulary` 는 헤더 선언만 본다**(호출부를 보면 `vk*KHR` 를 잡는다). 대문자 규칙은 "셋 이상은 어디서든, 둘은 이름 끝에서". `hashed_string` 은 리터럴에서만
  암묵 변환, `string_view` 판과 `const hashed_string&` 판을 함께 두면 NamePair 위반, `setX` 의 게터는 `getX`(BareGetter), `calculate` · `calc` · `init*` 축약 금지(`initRhi` 예외).
- **`FormatBranchBraces` 의 case 규칙** — `break;` 도 한 문장, 본문에 전처리기 지시문이 있으면 손대지 않는다. 플랫폼 전용 파일이나 `#if` 가 든 코드를 텍스트로 변환했으면 그
  플랫폼에서 빌드한다. clang-format `RemoveBracesLLVM` · `InsertBraces` 는 켜지 말 것(반복문까지 벗긴다).
- **일괄 이름 바꾸기는 파일 범위를 정한 규칙 표로**, 문자열 · 문자 리터럴(직렬화 키 · 로그 문구)은 건드리지 않는다. 새 이름 충돌은 `-Wshadow` 가 잡는다. `Win32Window.cpp` 는
  리눅스에서도 컴파일된다(스텁 구간) — 창 API 이름을 바꾸면 스텁 · X11 · Cocoa 까지.
- **Scripts 규칙** — 폴더는 하는 일로(`generate/` 생성기 · `setup/` 외부 도구 · `lint/*`), 모두 `common` 만 import(`common` → `setup` 역방향 금지). `common` 은 무거운 표준
  모듈을 쓰는 함수 안에서 import 한다. 동시 처리는 `Scripts/common/Parallel.py` 한 자리 — 파일 읽기 · 하위 프로세스 대기는 스레드, 정규식이 무거우면 `flatMapInProcesses`
  (코어 수만큼의 덩어리). `App.exe` 찾기 · 실행은 `Scripts/common/AppBinary.py` 하나. 파일을 한 번 읽어 나눠 쓰는 캐시는 CRLF 를 LF 로 바꿔야 결과가 같다.
- **헤더 멤버 초기값은 금지, 정본은 생성자**(면제는 `AGENTS.md`). 네임스페이스 스코프 상수는 `inline constexpr`(`inline static constexpr` 은 TU 마다 사본).

### 3-5. 직렬화 · 리플렉션 · 파서

- **엔진 데이터는 별칭을 쓰지 않는다**(사용자 결정 2026-10-03 — 실제 게임 데이터가 없다). 이름을 바꾸면 `Resource/` 데이터를 다시 쓴다. 모르는 키 · 타입 · 열거자는
  안쪽 원소까지 orphan 경고가 나고, `ResourceDataSchemaTest` 가 Resource/ 의 데이터 파일 전부(종류 표에 없는 파일도 실패)를 실제 로더로 읽어 경고 0 을 단언한다.
  Alias · ValueAlias 기능과 그 시험은 실제 게임 데이터가 생긴 뒤의 창구로 남긴다. 판 체계(registerXmlMigrator · 바이너리 판 필드)도 기능으로 남고, 지금 판만 읽는다.

- **orphan 정책은 `SchemaMigrate.h` 의 계약** — XML · JSON(사람이 고치는 저작 파일)은 `Ignore`(로드마다 경고, UE `FPropertyTag` · Unity YAML), 바이너리(쿠커 ·
  빌드 산출물)는 `Reject`(UE `FPackageFileSummary` 판 검사). 필드를 버려도 되는 오브젝트 상태는 그 migrate 함수가 말한다(`skipFieldsTheTypeNoLongerHas`).

- **바이너리 Archive 읽기는 읽은 만큼 자리를 옮긴다**(이어 쓴 객체를 차례로 읽는다). 같은 자리를 다시 보려면 새 Archive 를 만든다.
- **set 원소 편집은 `replaceElement`(지우고 다시 넣기)로만** — 같은 값이 되면 하나로 합쳐진다. 맵은 `forEachMutable` · `eraseAt`, 고정 배열은 `appendElement` 가
  원소 순번의 칸을 채운다(칸보다 많으면 실패).
- **머티리얼 enum · 플래그 글은 `EnumInfo::tryParseText`** — 모르는 이름 · 표식 값(`Count`)은 경고하고 값을 쓰지 않는다. enum 타입 이름은 `findInterned` 로만(에셋 글일 때). 코드가 준 이름(전역 변수의 `#enumType`)은 `hashed_string` 생성자로 찾는다 — 아직 아무도 인턴하지 않은 기동 시점에 `findInterned` 는 빈 해시를 내고, 시험 프로세스는 다른 시험이 먼저 인턴해 두어 그것을 가린다.

- **저장되는 상태는 PROPERTY 이고, 모든 PROPERTY 타입은 직렬화기가 실어 나를 수 있어야 한다**(`SerializerUtil::canCarryProperty`,
  `ReflectionSerializationTest.EveryPropertyHasATypeTheSerializersCanCarry`, 모듈판은 SmokeTest 의 `ModuleApiTest`). enum 에 `ENUM()` 이 없으면 `"null"` 로 저장된다.
  런타임 핸들(`void*`)은 `Transient`. `PROPERTY()` 를 빼먹은 필드는 매 실행 "모르는 필드" 경고를 내고 값은 기본값으로 돈다.
- **모르는 칸 · 모르는 열거자는 그 칸만 실패한다**(컨테이너면 그 원소, 맵이면 그 항목) — 세 형식이 같은 규칙이다. 기록 타입 해시를 모르는 칸(지운 enum · 타입)은 크기로
  짐작해 읽지 않는다. 모르는 타입의 컴포넌트는 `MissingComponent` 가 원문을 맡아 같은 형식으로 다시 쓴다. 프리팹을 못 찾은 엔티티는 `SceneDocument::SceneObjectNode` 로 보존한다.
- **바이너리는 enum 을 열거자 이름 해시로 싣는다**(플래그는 켜진 이름 수 + 해시, 이름 없는 값만 `0 + int64`). 열거자 이름을 바꾸면 데이터를 다시 쓴다(옛 바이너리는
  읽히지 않는다 — 실제 게임 데이터가 생긴 뒤라면 `ValueAlias`). 한 enum 안의 `Red` · `RED` 는 해시가 같다 — `registerEnum` 이 알린다. 판 `BinaryWireVersion` 은 스트림 머리마다 있다. `kObjectReflectedSchemaVersion` 은 일부러
  올리지 않았다(올리면 옛 상태가 모두 거절된다).
- **버전 절차는 `runVersionedDeserialize` 한 벌**이고 형식 사이 차이는 `SchemaVersionSource` · `SchemaOrphanPolicy` 두 enum 뿐이다. 버린 orphan 은 로드마다 한 줄 알린다 —
  새 이관도 `findOrphan` · `findOrphanHash` · `applyOrphanTo…` 로 찾아야 경고에서 빠진다(`_bClaimed`). 이관은 기록 타입이 같을 때만 제자리, 스칼라 → 스칼라는 텍스트를 거쳐
  (`tryCoerceBinaryPayload`), 비트 재해석은 금지다. 판단은 전선이 싣고 온 타입 해시로(payload 크기로 짐작하면 `1.5f` 가 `1069547520` 이 된다).
- **실패는 버릴 수 없다.** 실패할 수 있는 동사(load · save · read · write · parse · (de)serialize · apply · restore · import · export · cook · compile · revert · convert ·
  try · open · attach · spawn · instantiate · reload · remove · copy · create · delete · move · rename)의 bool 은 `[[nodiscard]]` — `-Werror=unused-result` + `CheckFallibleNodiscard`.
  의도된 버림은 `(void)` + 이유. `Archive` 읽기 연산자는 끈적한 `isError` 상태라 버려도 된다.
- **제자리 로드는 원자적이다**(`ObjectLoadContext::_bRestorePreviousOnFailure`). `BinarySerializer::deserialize` 자체는 실패해도 되돌리지 않는다 — 원자성이 필요한 쪽이 스냅샷을 뜬다.
  틱 중 제자리 상태 읽기는 거절된다(`executeOrDeferPostTick` 으로 감싼다).
- **반사 값을 직접 쓴 쪽은 `Component::notifyStateWritten()` 을 부른다.** 값 하나는 `SerializerUtil::copyPropertyValue` · `arePropertyValuesEqual` · `formatPropertyText` ·
  `applyPropertyText` 한 벌(비트필드는 그 비트만). 표는 `Source/Engine/Object/README.md`. 상태를 다 읽으면 `finishLoad` 가 `onPostLoad` 를 부른다 — 비동기 씬 로드에서는 워커에서
  불리므로 거기서 닿는 API 는 잠가야 한다.
- **밖에서 온 바이트를 믿지 않는다.** 경계는 뺄셈으로(`count > size - offset` — 덧셈은 넘쳐서 통과한다), 개수는 남은 바이트 / 최소 항목 크기로 상한, 해제 크기는
  `CompressionStream::kMaxUncompressedSize`(1 GiB), 32 비트 축소는 `narrowToUint32` 한 곳, bool 은 바이트를 `!= 0` 으로, 리소스 id 의 `..` 거절. 손상된 개수 칸 하나로 `reserve`
  가 72 초를 쓴 적이 있다. 넘침 회귀 시험은 위치를 먼저 옮긴 뒤 되감기는 크기를 줘야 문다.
- **디스크 · 네트워크로 나가는 바이트는 결정적이어야 한다** — 해시맵은 키 순 정렬, 구조체는 필드 순서대로(패딩 쓰레기), 판 번호는 읽는 쪽이 대조한다. 다형 소유 포인터는
  원소마다 `[이름][본문크기][본문]`(본문 크기가 있어야 모르는 타입을 건너뛴다). `serializeCompact` 는 프로퍼티를 이름이 아니라 **인덱스**로 짝짓는다.
- **에셋 · 파일 글로 `hashed_string` 을 만들지 말 것.** intern 표는 줄지 않고(상한에 닿으면 그 뒤 엔진의 **모든** 새 이름이 None), 대소문자를 무시하며 영구 적재된다. 조회는
  `hashed_string::findInterned`, 해시만 필요하면 `computeHash( string_view )`. 진단은 `getInternedCount`. 같은 이름 번호는 밑 이름별로 되쓴다(`SameNameChurnKeepsInternPoolBounded`).
- **`hashed_string` 은 FName 규칙이다** — 같음 · 해시는 대소문자 무시, `c_str()` 은 적은 철자, `operator<` 없음(`HashedStringLexicalLess` · `HashedStringFastLess`), 대소문자만 바꾸는
  이름 변경은 `isEqual( …, NameCase::CaseSensitive )`. FName 과 일부러 다른 셋은 되돌리지 말 것: 해시는 실행마다 같은 FNV(저장된다), 철자 보존은 모든 구성, 숫자 꼬리 없음.
- **문자열 해시 식을 바꾸지 말 것.** `StringUtil::computeHash64`(FNV-1a, 문자를 `make_unsigned_t<CharT>` 로 넓힌다)에 쿠킹 산출물 · 셰이더 쿠킹 스탬프 · 파이썬 쿠커가 걸려 있다.
  `RuntimeStringHash` 는 프로세스 안 전용이다. `computeHash64( "리터럴", false, seed )` 는 포인터 오버로드에 묶여 키가 상수가 된다 — `string_view` 로 넘긴다.
- **`sw::unordered_map` 은 밀집 배열, `sw::map` 은 정렬 벡터다**(`SW_ENABLE_STL_CONTAINER` 꺼짐). 삽입 · 삭제가 원소를 옮기므로 표 안 원소의 포인터를 잠금 밖으로 내주지 말 것 —
  복사로 주거나 값을 `unique_ptr` 로 든다. 짧은 이름(SSO) `string` 의 `c_str()` 도 이동 뒤 빈자리를 가리킨다(프로파일러 이름은 intern 아레나에).
- **`TypeInfo` 주소는 고정이다**(레지스트리의 `unique_ptr` + 모듈 해제는 묘비 `_bAlive`, 같은 FQN 재등록은 같은 객체를 되살림, 묘비는 `clearContent()`). 조회 캐시는 등록 배치
  끝에 한 번(`buildLookupCaches`). `castTo` 는 `_pParentType` + `TypeLookupCache`(세대)이고 포인터가 다르면 FQN 을 한 번 더 견준다 — 시험 목의 `StaticType()` 이 레지스트리 밖 사본이다.
  부모 사슬은 순환할 수 있다(`registerClass` 는 값을 검사하지 않는다 — `markVisitedOrStop`). 상속 캐시는 `clearInheritedProperties`.
- **`TypeInfo::forEachProperty` 의 기본은 `bIncludeBase=false`** 다 — 직렬화기는 `true` 를 넘겨야 상속 PROPERTY(트랜스폼)가 저장된다. `applyTypeDefaults` 는 뿌리 → 파생 순.
  벡터 기본값 · JSON float4 텍스트는 쉼표 구분이다(공백이면 파싱이 조용히 실패한다).
- **비트플래그는 `ENUM( Flags )` 로만 말한다**(값 모양 자동 감지는 지웠다). 클래스 안에 중첩된 `ENUM( Flags )` 는 `FlagOps.gen.h` 가 전방 선언할 수 없어 코드젠이 일부러 실패한다.
  enum 메모리는 `readValueFromMemory` · `writeValueToMemory` 로만(`getValuePtr<int32>` 는 uint8 enum 뒤 3 바이트를 덮었다). enum 값은 생성 코드가 컴파일러에게 계산시킨다.
- **파서에 레이아웃을 재게 하지 말 것** — 파서 인자에는 `SW_DEBUG` · `_DEBUG` 가 없다. 비트필드 자리는 런타임 `PropertyInfo::resolveBitField` 가 찾는다. 범위는 경계마다
  (`_bHasMinRange` · `_bHasMaxRange`). `PROPERTY()` 를 `T&` 를 돌려주는 인자 없는 메서드에 붙이면 값이 객체 밖에 있는 프로퍼티가 된다(`Reflection/README.md` 5),
  `Name = "_옛이름"` 으로 씬 파일을 고치지 않고 이어 쓴다.
- **ReflectionParser 구조** — 애노테이션 필드 하나 = `AnnotationFields` 표 한 줄(`Emit` 열이 코드젠), 철자 표 `AnnotationMeta.txt` 와 어긋나면 시작에서 멈춘다(`validateBindings`),
  모르는 토큰은 빌드를 세운다. 선언 소속은 `findTargetIndex`(전개 위치 파일) — `clang_Location_isFromMainFile` 은 매크로 위치를 늘 "아님" 으로 답한다. 별칭은 `resolvePropertyAlias` ·
  `getBaseClassDeclaration` 으로 풀고, 컨테이너는 바깥 템플릿 이름이 **같을 때만**(`outerTemplateName`). 헤더 여럿은 한 TU 로 묶는다(`parseBatch`, 실패하면 헤더별 재시도).
- **파서 증분 판정** — 스탬프에 `input <읽기 전 시각>` 과 `dep <시각> <경로>`, `--depfile` → CMake `DEPFILE`. 파서 실행 파일 · 템플릿 · builtins · AnnotationMeta 의 시각도 본다.
  산출물은 내용이 같으면 다시 쓰지 않으므로 다시 만들었는지는 `<이름>.gen.cpp.stamp` 로 본다. `git mv` 는 mtime 을 안 바꾼다(산출물 머리말 `// Source:` 경로를 대조한다).
  생성 파일 이름은 소스 파일 이름만으로 지으므로 한 모듈 안 같은 이름의 헤더 둘은 빌드를 세운다. 리눅스 libstdc++ 파일 시계는 지금 시각이 음수다(부호 없는 64 비트로 읽는다).
- **파서 변경의 검증은 생성물 바이트 비교다** — 새 · 옛 파서로 133 파일(열 타깃 + `ReflectBuiltins.gen.cpp`)을 만들어 `diff -r` 0. 로컬 `parser_config.json` 은 기계 키
  (`paths.*` · `parser_args.extra` · `parser_args.force_include`)만 받는다. `findReflectionParserExecutable` 은 `BuildTools` 를 먼저 본다 — `Bin` 의 옛 사본을 돌린 결과는 지금 답이 아니다.
  Shipping 은 Info 로그가 없으므로 도구의 사용법 · 덤프는 stdout 으로.
- **단위는 `PROPERTY( Units = m )`** — 단위 표(`ReflectUnits.h`)로 철자를 검사한다. `Meta = "Units=…"` 는 표에 없는 글자(`HP` · `dB` · `px` · `BPM`)만 받고, 표에 있는
  단위를 `Meta` 로 적으면 파서가 거절한다(`ReflectionParserTest.DisplayMetadataIsValidated`). 가속도는 `m/s2`(`m/s^2` 아님). `/` 가 든 단위는 따옴표로
  (`Units = "m/s"`) — clang-format 이 `m/s` 를 `m / s` 로 띄우고, 따옴표 없는 값은 첫 공백에서 끝나 `m` 이 된다. 파서는 공백이 든 따옴표 없는 값을 거절한다.
- **`AnnotationMeta.txt` 의 `flag.X` 한 줄이 단독 토큰과 `X = true` 를 함께 등록한다.** `ArgumentList.xxx` 의 `bUseDefaultValue` 를 켜면 주지 않은 인자에도 `getArgument` 가 true 다.
- **XML** — 쓰기는 `XmlNode::toString` 하나, float 는 `std::to_chars` 최단 왕복(그래서 되돌리기 스냅샷을 바이너리로 바꾸지 않는다), 긴 줄 접기는 시작 태그 속성에만(pugixml 은 텍스트
  안 `"` 를 이스케이프하지 않는다), 태그는 `sanitizeTag` 가 `::` → `__`. 정수 속성은 `tryGetAttributeIntInRange`, 불리언 글은 `StringUtil::tryParseBool`(관대한 `parseBool` 은 실패를
  알아야 하는 자리에 쓰지 말 것). Windows 헤더가 `small` 을 매크로로 정의한다.
- **JSON** — `JsonValue` 는 빌린 포인터다: 같은 부모에 `set( 새 키 )` · `pushBack()` 을 하면 앞서 꺼낸 형제 핸들이 죽는다("하나 받아 다 채우고 다음"). nlohmann
  `is_number_integer()` 는 부호 없는 수에도 참 — unsigned 를 먼저 본다. `JsonSerializer::loadFile` 은 실패해도 그 앞까지 읽힌 값이 남는다. `SerializeContext` 는 `deriveFromDefault()`.
- **컨테이너를 어떻게 채울지는 컨테이너가 정한다** — 역직렬화는 `appendElement`, 인스펙터는 `allowsInPlaceElementWrite()`. 왕복 시험은 세 형식 모두, 값은 정렬되지 않은 순서로.
- **컨테이너 순회는 `ContainerVisitor` 하나다**(`Serialization/Core/ContainerVisitor.h`). 원소 모양(중첩 · 소유 포인터 · 값 구조체 · 스칼라)은 컨테이너마다 한 번
  `ContainerElementPlan` 이 정하고, 형식은 `IContainerWriter` · `IContainerReader` 만 구현한다(형식 TU 의 `…Internal::ContainerWriter` · `ContainerReader`). 실패는 세 형식이
  `ContainerReadResult` 로 같다 — 자리를 알면 그 원소 · 항목만 빼고 칸 실패(`FieldFailed`), 모르면 멈춘다(`StreamBroken`: 바이너리의 enum 아닌 값 실패 · 넣을 칸 없는 원소).
- **직렬화 출력이 그대로인지는 덤프로 본다** — `SW_SERIALIZATION_DUMP_DIR=<폴더>` 로 `SerializationRoundTripTest.DumpEveryResourceObjectState` **하나만** 돌리면 씬 · 프리팹의
  오브젝트 상태를 세 형식으로 덤프한다. 고치기 전 · 후 덤프의 `diff -r` 이 비어야 한다. 실제 데이터의 쓰기 → 되읽기 → 쓰기 고정점은 `…EveryResourceComponentRewritesToTheSameBytes`.
- **경로** — 리소스 id 하나로 든다(`ResourceUtil::toResourceId` — 루트 밖 · `..` 는 빈 글). 쓰기 경로는 `ResourceUtil::getWritePath` 하나 + `ensureParentDirectoryExists`(bool, 실패하면
  그 자리에서 경로 · OS 이유를 알린다). 맵 키는 `normalizePath`(소문자), 여는 경로는 `normalizeSeparators`(`collectFiles` 는 대소문자를 보존한다). 배포 빌드는 `.meta` 를 쓰지도
  GUID 를 지어내지도 않는다 — 배포본 GUID 표는 쿠커가 도메인마다 넣는 `assetregistry.txt` 다. `ensureMeta` 는 루트 밖 절대 경로면 null GUID.
- **쿠킹 이름은 `AssetCookPath`(`Engine/Resource/AssetFormat.h`) 표 하나** — `.scene.xml` → `.scene.bin`, `.prefab.xml` · `.prefab.json` → `.prefab.bin`, `toSourcePath` ·
  `isCookableSource`. 로더 · 쿠커 · 에디터 판정 · `SceneManager::saveActiveScene` 이 모두 지난다. 쿠킹은 엔진이 한다(`PrefabCache::cookAllPrefabs` · `App --cook-scenes` ·
  `AssetDatabase::writeRegistryFiles`), 파이썬 `CookAssets.py` 는 스테이징만. 쿠킹은 왕복 검증한 엔티티만 바이너리로 바꾸고 나머지는 XML 로 남기며 WARNING 을 낸다.
- **압축** — 팩 enum `PackCompressionType` 과 스트림 enum `CompressionCodecType` 은 독립된 디스크 포맷이다(`static_cast` 로 잇지 말 것). `CompressionCodecRegistry` 는 `EngineLoop` 가 소유하고
  Core 에는 슬롯만 있다. 모듈이 등록한 코덱은 그 모듈 shutdown 에서 `unregisterCodec`. zlib 은 Windows 에서 4 GB, LZ4 는 2 GB 가 한계다.
- **"헤더 혼자 빼도 선다" 는 "아무도 안 쓴다" 가 아니다** — `RunForwardDeclarationCandidates --verify-unused` 가 고른 120 건을 지우자 소비자 TU 에서 오류 2790 개가
  났다(거쳐 받던 `RHIBackend` · `IRHIResourceFactory`, NOMINMAX 가 `windows.h` 보다 먼저 오던 순서가 깨져 `std::max` 가 매크로에 먹힘). 보고는 "후보" 로만 쓰고, 지울 때는
  그 헤더를 include 하는 TU 전부를 다시 지어 본다.
- **리눅스에서만 지은 코드를 들일 때** — 윈도우 매크로 `near` · `far` · `small` 와 겹치는 이름, DLL 이 내보내지 않은 타입(`SW_GF_API` — 리눅스 .so 는 다 내보낸다),
  같은 이름의 타입이 두 모듈에 있는 ODR 위반(`CheckDuplicateTypeNames` 가 막는다)을 먼저 본다. 병렬 본문은 컨테이너를 만지지 않고 나누기 전에 받은 포인터로만
  접근한다(`NetParallelScratch` · `engine::runParallel` — 컨테이너 `operator[]` 는 경합 검출기에 쓰기로 세어진다).
- **GPU 메모리는 `RHIMemoryLedger` 가 센다** — 생성은 핸들 표에 넣는 자리, 해제는 지연 해제 콜백에서만 적는다(destroy 요청 시점이 아니다). 새 자원 경로를
  더하면 거기서 `recordAllocation` / `recordFree` 를 부른다. Vulkan `heapUsage`(이 AMD 드라이버)는 `vkAllocateMemory` 합뿐이라 "엔진 밖" ≈ 0 — 스왑체인 몫은
  DX12 · DX11 수치로 본다. GL 은 벤더 확장이 없으면 사용량이 "모름" 이다. DX11 · GL 은 할당 크기 API 가 없어 논리 크기다.
- **씬 엔티티는 0 이 아닌 `id` 가 필수다** — `SceneDocument::loadXml` · `saveXml` · 쿠커가 거절한다. 손으로 씬 XML 이나 `SceneObjectNode` 를 지을 때 `_fileId` 를 빠뜨리지 말 것.
  이름만 남은 부착(id 0 + 이름)은 찾지 못한 부모 참조를 다른 id 공간으로 옮겨 적은 **지금 형식**이라(`SceneComponent::syncAttachSerializeFields`) 지우면 안 된다.
- **씬 · 프리팹 손 XML 을 쓰지 말 것** — 임베디드 오브젝트 XML 은 리플렉션 산출물이다. 머티리얼 XML 에서 `_permutations` 를 빼먹으면 네 백엔드가 제각각 무너져 렌더러 버그로 오인한다
  — 실제 에셋 + `setPropertyValue` 로 간다.

### 3-6. 오브젝트 · 씬 · 틱

- **모델 임포트의 옆 폴더(`models/<모델>/`)는 임포트마다 통째로 지워진다**(`ModelImporter::importModel`) — 손으로 쓴 캐릭터 데이터(소켓 · 알림 표 · 물리 에셋 ·
  몸 영역)는 `game/<게임>/characters/<캐릭터>/` 처럼 임포트 산출물 밖에 둔다. 클립 알림은 원본 옆 `<모델>.clips.json` 에 적고 `App --import-models`.
- **물리는 DuringPhysics 와 PostPhysics 틱 사이에 돈다**(그 앞에 틱 결과 적용 → 내비게이션 → 애니메이션 — 프레임 순서의 정본은 `Object/GameObject/SceneFrameStepList.xxx` 한 표, 단계를 더하면 줄 하나 + `runFrameStep*` 본문 하나). PostPhysics 이후 틱은 이번 프레임의 바디 자세 · 겹침을 보고,
  그 그룹에서 쓴 애니메이터 파라미터 · 트랜스폼은 다음 프레임의 포즈 · 물리에 든다(`PhysicsComponentTest.PostPhysicsTickSeesThisFramesBodyPose`).
- **한 오브젝트의 두 번째 씬 컴포넌트는 첫 씬 컴포넌트(루트)에 붙는다** — 저장하면 `_attachComponent="CameraComponent#0"` 처럼 남는다. 카메라와 같은 오브젝트의 뷰 모델 ·
  조준선은 로컬 자리(카메라 기준)로 다룬다 — 월드 자리를 `setLocalPosition` 에 넣으면 카메라 자리만큼 두 번 밀린다(`FirstPersonCameraComponent`).
- **씬 작성 코드의 `createEmptyActiveScene` 은 `GameCamera` 엔티티를 둔다.** 자기 카메라를 들고 오는 씬(1인칭 플레이어)은 저장 전에 지운다 — 같은 역할 · 우선순위의
  카메라가 둘이면 어느 쪽이 활성일지가 등록 순서에 걸린다.
- **이름으로 컴포넌트를 만드는 길은 `TypeInfo::_addComponent` 하나다**(팩토리 표 없음 — UE `UClass`). 코드젠이 구체 컴포넌트마다 채우고 모듈 해제가 비운다.
  손으로 만든 시험 TypeInfo 는 이 칸을 채워야 `addComponentByName` · 씬 로드가 만든다. 스레드별 이름 캐시는 `TypeRegistry::getGeneration` 으로 무효화된다.
  전체 상태가 실린 옛 프리팹 엔티티는 원형을 짓지 않는다(프리팹이 있는지만 본다).

- **씬의 프리팹 엔티티 = 프리팹 경로 + `<PrefabOverrides>`**(프리팹 쪽 `이름표#n` 키, 다른 필드만) — `<GameObject>` 전체 상태를 든 엔티티는 옛 형식이고 그 상태가
  이긴다(다음 저장에서 오버라이드로). 프리팹 원형 상태는 별도 `GameObjectManager` 에서 만든다 — 한 매니저에만 `registerComponentType` 한 시험용 목 타입은 원형에서
  MissingComponent 가 되니 시험은 실제 타입으로. 원형은 `forEachGameObject` 안에서 만들 수 없다(순회 전에). 씬 판 1, SCN1 판 3(v0~v2 읽음).
- **컴포넌트 이름표(`_componentName`)는 저장되고 컴포넌트 키(`ComponentStableKey`)는 이름표로 센다**(없으면 타입 이름) — 옛 키(`SceneComponent#n`)도 맞는다.

- **`onPostLoad` 는 `ObjectStateBatch::finish` 에서 이름 → 부착 → 핸들이 풀린 뒤에 온다**(언리얼 PostLoad 순서). 플레이 중 재로드는 `onBeginPlay` 를 다시
  부르므로, 흐른 시간 · 적용한 오프셋 같은 진행 상태는 저장하고 `onBeginPlay` 가 되돌리지 않게 한다.

- **경로 프로퍼티로 에셋을 여는 컴포넌트는 `onBeginPlay` 만으로 부족하다** — 상태 읽기는 `onPostLoad` 만 부르고 플레이 전이면 BeginPlay 가 없다.
  `onPostLoad` · `onPropertyChanged`(그 경로) 양쪽에서 연다(`SequencePlayerComponent` · `DialogueRunnerComponent`).
- **물리 후보 범위는 셀 하나 이내로 움직인 바디만으로 넓히고, 먼 이동 바디(`isFarMover`)는 하나씩 잰다** — 하나가 멀리 끌리면 모든 연속 바디가 전체를 훑었다.

- **저장된 상태는 이름으로 다른 오브젝트를 가리키지 않는다**(`AGENTS.md`). 부모는 `_attachOwnerId`, 핸들 PROPERTY 는 `ObjectSaveOptions::getSavedObjectId`(프리팹은 0). 상태를 읽는
  길 아홉은 모두 `ObjectStateBatch` 를 지나고 `finish()` 가 이름 되찾기와 부착을 한 번에 한다. 못 푼 참조는 `keepUnresolvedAttach` 로 보존한다. 표는 `Source/Engine/Object/README.md`.
- **핸들 PROPERTY 는 바이너리에서도 파일 id 로 적는다.** `GameObjectHandle` 은 내장 타입이라 기본 문맥에 8 바이트 처리기가 있다 — 글 처리기만 바꾸면 XML 은 맞고 바이너리
  (쿠킹한 씬 · 세이브)는 런타임 id 를 실어, 쿠커 왕복 검증이 그 엔티티를 XML 로 남겼다. `ObjectStateSerializer` 가 글 · 바이너리 처리기를 함께 덮는다
  (`ActionCombatTest.ObjectReferencesSurviveBinaryFileState`).
- **빌리기는 포인터(이번 호출 · 프레임), 보관은 `GameObjectHandle` · `ComponentHandle` + `resolve*`.** 이름 기반 `GameObjectPtr` · `ComponentPtr` 를 되살리지 말 것. 게임 모듈은
  컴포넌트를 생포인터로 들지 않는다(상태 복원이 씬을 갈아엎는다). 되살릴 때 id 보존: `createGameObjectWithId`, `ComponentIdRestoreScope` 는 `onRegister` **전에**, 프로세스 토큰이
  다르면 id 를 버린다. 오브젝트 id 는 프로세스 전역이다 — 시험에서 "다음 = +1" 을 가정하지 말 것.
- **틱 중 구조 변경은 큐 하나(`deferStructuralChange`)에 부른 순서대로**, 게임 쪽 `deferPostTick` 은 그 뒤다. 이름 · 틱 설정 · 서브틱 · 태그 쓰기는 `Component::deferIfStructureFrozen` 을
  지난다. 틱 중 `addComponent` 는 `nullptr` — `executeOrDeferPostTick`. 시험은 틱 **안에서** 본 값을 목 훅으로 기록한다(`getComponentCount` 는 미룸과 해제를 가르지 못한다).
- **틱 중 트랜스폼 쓰기** — 자기 오브젝트를 틱하는 스레드면 칸의 대기 자리에, 남의 오브젝트면 쓰기 큐로(`SceneComponent::queueTickWrite` 하나). 남의 값은 늘 틱 전 값을 읽는다.
  큐는 (대상, 쓴 오브젝트 id, 순번)으로 정렬해 적용하므로 결과가 스레드 배정에 달리지 않는다. 워커가 쓰는 더티 플래그는 바이트 · `atomic<uint8>` relaxed(비트필드는 이웃
  비트를 덮고, 같은 값을 겹쳐 쓰는 것도 데이터 경쟁이다). 예외는 서브틱 선행 조건의 스테이지 경계다 — 기다리는 스테이지 앞에서 그때까지의 쓰기를 적용 · 플러시한다
  (`SceneTickScheduler::applyStageTransforms`). 그 단계에 attach · detach 가 미뤄졌으면(`deferHierarchyChange`) 그 뒤로는 앞당기지 않는다 — `KeepWorld` 부착이 먼저 적용된 쓰기를 덮는다.
- **월드 합성은 `updateWorldTransformFromParent` 한 곳이고, 렌더 더티를 찍는 곳은 `onWorldTransformUpdated` 하나다.** 합성 경로를 하나 더 만들면 메시가 화면에서 얼어붙는다.
  트랜스폼 값은 전역 `SceneTransformStorage`(256 칸 페이지, 옮기지 않는다)에 있고 컴포넌트는 칸 번호만 든다. 쓰기 알고리즘은 `SceneTransformHierarchy` 가 갖는다.
- **부착** — 오브젝트의 루트 씬 컴포넌트는 하나(둘째는 primary 아래로), `AttachRule { KeepRelative, KeepWorld }`, `canAttachTo`(다른 매니저 · 소켓을 거친 순환 · 파괴 대기 부모)는
  미루기 **전에** 묻는다. 자식의 정의는 `getParent` 하나(소켓 자식 포함). 월드 값은 `setWorldPosition` · `setWorldTransform`, 크기는 월드 상자 하나(`getWorldBox` · `AABB::transformedBy`),
  경계는 컴포넌트가 선언한다(`getWorldBounds`). `transformVector` 는 방향(w=0) 변환이지 법선 변환이 아니다 — 법선은 `invert().transpose()`, 셰이더는 `swComputeWorldNormal`.
- **`forEachGameObject` 콜백에서 생성 · 파괴 · 이름 변경 · 풀 생성을 하지 말 것** — `shared_mutex` 가 재진입하지 않아 교착한다(`WalkScope` 가 Debug 에서 단언). 그런 순회는
  `getAllGameObjects( out )`. 컴포넌트 목록을 범위 for 로 도는 중에 붙이면 반복자가 풀린다 — 인덱스로. 콜백이 형제를 지울 수 있는 걷기는 핸들로 모으고 매번 다시 푼다.
  소멸자에서 미루는 경로를 타지 말 것(`~SceneComponent` 는 `detachFromParentImmediate()`).
- **`tick()` 에서 `TaskManager::waitAll()` 을 부르지 말 것** — 렌더 기록 · 스트리밍 · 오디오까지 기다린다. 자기 스테이지를 `waitStage` 로.
- **틱 뒤 적용 순서는 `StructuralChangeBuffer::drain` 하나가 갖는다**: 동결 해제 → 구조 변경(부른 순서) → 틱 중 트랜스폼 쓰기 → 틱 뒤 큐 → `mergePendingAdds` → 시작 줄. 그 뒤 dirty 면 재 flush → 지연 파괴. 큐마다 자기 시점에 비우게 나누지 말 것.
- **컴포넌트 해체는 `destroyComponentInstance` 한 곳이고 `removeComponent` 는 순서를 지킨다**(swap-remove 금지 — 첫 일치 · primary · 안정 키가 순서에 기댄다). 컴포넌트는 `_pPool`
  (나온 풀)과 `_pTypeInfo` 를 든다 — 이름표 `_componentName` 은 런타임 라벨일 뿐이라 조회 키로 쓰면 안 된다(Shipping 에서만 힙이 깨졌다). 풀 키는 FQN 이다. 컴포넌트는 풀에서
  제자리에 생기므로 이동 연산을 되살리지 말 것. 멤버 없는 파생 컴포넌트도 `REFLECT_BODY` 가 필요하다.
- **`markPendingDestroy()` 는 묘비만 세운다** — 파괴 목록에 넣는 것은 `destroyObject` 다. 동시 파괴는 `tryMarkPendingDestroy()`(exchange)가 true 인 스레드 하나만 진행한다.
  이름은 살아 있는 오브젝트만 차지한다(`isNameTakenUnlocked`).
- **활성** — 계층 활성 재계산은 값이 그대로면 멈추므로 부모가 바뀌는 모든 자리가 그 자리에서 다시 맞춘다. `Component::isActive` 는 소유자 계층 활성을 포함하므로 오브젝트 `setActive`
  가 컴포넌트 비트를 복사하면 안 된다. 변화는 `onOwnerActiveInHierarchyChanged` 로 알린다. `TickRegistry` 의 목록에 들어 있는 것이 곧 활성이다.
- **플레이 수명주기** — 컴포넌트의 "시작됨" 비트가 시작 · 끝을 한 번씩 짝짓는다. 플레이 중 붙은 컴포넌트는 다음 틱 단계(`GT.Scene.tick.beginPlay`)에서 시작한다. 월드 플레이 상태는
  `SceneManager::setWorldPlaying`. 다시 만든 인스턴스(되돌리기 · 핫 리로드 · DontDestroyOnLoad)는 onBeginPlay 를 다시 받는다 — `markPersistent` 는 같은 id 로 **다시 만든다**.
- **틱 선언** — 기본은 "`onTick` 을 오버라이드했는가"(`HasOnTickOverride_v<T>`), 우선순위는 `kMaxTickPriority`(63). 다른 그룹의 선행 조건이 있으면 뒤따르는 쪽을 그 그룹으로 옮긴다.
  **선행 조건을 가진 서브틱만 스테이지로 간다**(그룹마다 보통 길 뒤) — 나머지는 선행 조건이 씬에 있어도 보통 길이다(무버 8000 의 틱 p50 600 → 140 us). 단계
  (`TickPhase`)는 한 오브젝트 안의 순서일 뿐이다. 선행 조건 핸들은 등록이 준 것을 쓴다 — 소유 오브젝트 id 가 없는 손수 만든 핸들은 거절된다.
- **기본값은 만들 때 한 번이다**(CDO 자리, `DefaultPatch`). 모듈 등록 · 리로드에서 살아 있는 값에 다시 찍지 말 것. `ComponentDefaults` 의 기본값 파일은 없어도 되는 파일이다
  ("시도했는가" 로 한 번만 연다). 프리팹 형식은 읽을 때 정해 든다(`PrefabStateFormat`), 쓰기는 `PrefabAsset::saveToFile` 하나, 오버라이드는 `타입#n` 키.
- **찾지 말고 등록받는다.** 매 프레임 씬을 훑던 주광 조회가 GT 의 38 % 였다. 빛은 `LightRegistry`(종류별 칸), 카메라는 `CameraRegistry::selectCamera`(동률은 컴포넌트 id — 등록
  순서로 가르면 되돌리기마다 뒤집힌다), 그림자 빛은 `Scene::findShadowCastingDirectionalLight`. 프리미티브 등록부에 섞지 않는다(그릴 수 있는 것만 담는 계약).
- **씬 로드** — `SceneManager` 대기열은 한 자리다. 밀려난 요청 · `shutdown` · 취소도 약속에 `nullptr` 을 채워야 `future.get()` 이 영원히 멈추지 않는다. 로드 중 모듈 팩토리가 바뀌면
  (`getFactoryHeadSerial`) 다시 짓는다. `SceneManager::shutdown` 은 씬을 내리기 **전에** 활성을 비운다.
- **씬 쿠킹은 활성 게임 팩만 엄격하다.** 다른 게임 팩(`game/<다른 게임>/`)의 씬이 이 빌드에 없는 게임 모듈의 컴포넌트를 쓰면 건너뛴다(정보 줄) — 실패로 세면 다른
  게임을 고른 빌드의 쿠킹이 모두 선다. 엔진 · 공용 타입만 쓰는 다른 팩의 씬은 그대로 쿠킹한다(`AppCookTest` 가 `game/empty` 를 본다). 활성 팩의 모르는 타입은 여전히 실패다
  (`SceneTest.SceneCookFailsOnAComponentOfUnknownType`).
- **물리** — `stepPhysics` 는 틱과 트랜스폼 적용 뒤에 한 번 돈다. 콜라이더는 틱하지 않고 틱 안의 질의는 지난 step 을 본다. `onOverlapBegin( const OverlapInfo& )` 안에서는 스폰 ·
  파괴해도 된다. 순간이동은 `teleportTo` · `BodyMoveType::Teleport`(아니면 연속 바디가 그 길을 쓴다). 셀 범위는 `CellRange` 하나, 셀 순회 변수는 int64(`MaxInt32` 로 접히면 안 끝난다),
  `toCellCoord` 는 float64 로 나눈 뒤 접는다. 공간 색인 규약은 `Source/Engine/Spatial/README.md`.
- **강체 물리** — `ScenePhysics::step` 이 겹침 월드 다음에 돈다(고정 스텝 → 보간 자세를 트랜스폼에 → 이벤트). 컴포넌트가 쓴 자세와 다른 트랜스폼은 코드가 옮긴 것(순간이동)이다.
  vcpkg Jolt 는 설치 헤더가 부동소수 예외 비트를 켜고 라이브러리는 끈다 — `JPH::RegisterTypes()` 는 abort 하므로 백엔드가 라이브러리의 ID 로 등록한다(그 비트만 허용).
  Jolt 임포트 타깃의 `-mavx2` 는 Jolt 백엔드 소스에만 붙인다(`$<LINK_ONLY:>` + 소스 속성). Box2D 의 `totalNormalImpulse` 는 이완 반복까지 더해 약 두 배다.
  `RigidBodyComponent` 는 오브젝트의 루트여야 몸의 자세가 오브젝트를 옮긴다(메시가 루트면 파괴 · 기믹 시험이 조용히 안 움직인다). 상한에 붙어 돌던 바디의 각속도를 새
  바디에 넘기면 반올림으로 상한을 넘을 수 있어 `createBody` 가 줄인다.
- **파쇄(평면 자르기)** — 모서리 교점은 끝점을 자리 순으로 정렬해 구한다(이웃 칸이 같은 모서리를 반대 방향으로 자르면 비트가 달라 틈이 생긴다). 세 칸이 만나는 곳에는 거의 같은
  점이 생겨 그때만 용접 + 퇴화 삼각형 정리(`cleanPiece`)를 돈다(늘 돌리면 느리다). 귀 자르기는 일직선 점을 삼각형 없이 버리면 안 된다(T 자 틈). 안쪽 면 다시 짓기는 그 점을
  쓰는 **모든** 면이 안쪽 면일 때만 뺀다. 쪼개기 결과가 바뀌면 `MeshFractureUtil::kAlgorithmVersion` 을 올린다(임포트 해시).
- **"바뀌었나" 검사는 제곱 거리를 `MathUtil::EpsilonSquared` 와 비교한다**(`Epsilon` 이면 프레임당 1e-3 아래 움직임이 영원히 삼켜진다). `GpuSceneBuilder::bCamSame` 의 이력은 의도다.
  `float4x4::invert` 는 행렬식이 정확히 0 · NaN 일 때만 항등을 돌려준다(절대 임계값은 작은 부모 · 큰 직교 카메라를 깨뜨렸다).
- **시퀀서** — "지나갔는가" 는 `previousFrame < start <= frame`, 이전 프레임 없음은 `kNoPreviousFrame`(INT32_MIN — -1 은 frameMin 0 과 겹친다). 프레임은 배 정밀도로 곱하고 천분의 일을
  얹어 자른다. 반복 재생 시간은 한 바퀴 안으로 감는다(float32 는 10^6 초 근처에서 0.016 을 더해도 안 움직인다).
- **`getTags()` 와 `getOrCreateTags()` 는 다르다** — 공용 빈 상수를 `const_cast` 로 돌려주면 한 번 쓰는 순간 모든 오브젝트가 그 태그를 갖는다. `TagID::computeId` 하나가 리터럴 · 런타임
  태그 ID 를 만들고, 계층 비교(`Faction` → `Faction.Player`)에는 문자열이 같이 필요하다.

### 3-7. 그래픽스 · RHI · 셰이더

- **한 `FrameRenderer` 로 두 씬을 번갈아 그리면 옛 배치가 나온다** — 씬 빌더의 수집 캐시(프리미티브 집합 세대)는 씬마다가 아니라서, 다른 매니저의 같은 세대
  번호를 "그대로" 로 본다. 픽셀 비교 시험은 씬마다 렌더러를 둔다(`RenderPassGpuTest.SkinnedMeshFollowsPaletteLikeCpuSkinning`).
- **Debug App 의 `--import-textures` 는 BC7 1024² 한 장에 20 분을 넘긴다**(CPU 압축기가 최적화 없이 돈다) — 색 칸 아틀라스(KayKit)는 BC1 규칙
  (`TextureImportConfig.json` 의 `Character_Atlases`)이라 몇 초다. 큰 BC7 은 Release App 으로 굽는다.
- **D3D11 `UpdateSubresource` 에 상자가 없으면 버퍼 전체 길이를 원본에서 읽는다** — 용량을 남겨 둔 버퍼에 짧게 올릴 때는 상자를 준다(원본 뒤를 넘어 읽어
  드라이버 안에서 죽는다 — `RenderPassGpuTest.PartialStructuredBufferUploadReadsOnlyTheSourceRange` 가 가드 페이지로 지킨다).
- **애니메이션이 튀면 본 하나의 프레임 사이 이동량을 재 본다** — Shooter3D 의 튐은 셋이 겹친 것이었다: 반복으로 돌린 겨누기 레이어의 끝 → 처음(1 초마다 32 cm),
  대각선에서 상태가 오가며 클립을 처음부터 다시 틀기, 끊긴 크로스페이드가 한 칸을 버리기. 튐의 간격이 클립 길이와 맞는지부터 본다.
  몸 전체가 튀면 프레임별 CSV(Shooter3D `-gv_shooterMotionTrace=<경로>`)로 몸 = 발 자리 · 루트 본 · 요 각속도 · 카메라 이동을 나눠 본다 — 자동 조준이 표적을 바꿀 때
  20 rad/s 로 돌던 요 스냅이 원인이었다(각속도 상한 `OrientationUtil::turnTowardAngle`).
- **스킨 팔레트는 `AnimationSystem::getUnits()` 에서 모은다, 레벨이 아니다** — `unregisterUnit` 은 레벨을 다음 평가까지 비운다. 레벨로 모으면 시체 하나를 걷는 프레임에
  모든 스킨드 메시가 팔레트 없이(바인드 포즈 = T 포즈) 한 번 그려진다(`GpuSceneTest.SkinPalettesSurviveAUnitLeavingTheFrame`).
- **스킨드 메시는 모프 풀의 뒤 구간이다** — 팔레트는 GT 의 `AnimationSystem` → `GpuSceneBuilder::collectSkinPalettes`(수집 건너뛰기와 무관하게 매 프레임) →
  스냅샷 → `GpuMeshMorphPool::uploadSkinPalettes`(풀 순서) → meshskin.hlsl. 팔레트 행은 행벡터 4x4 의 **열** 셋이다(행을 넣으면 전치된 회전).
  모프 타깃은 같은 컴퓨트에서 **스키닝 앞에** 더한다(가중치는 팔레트 행 뒤) — 스키닝 뒤에 더하면 민 방향이 본과 같이 돌지 않는다
  (`RenderPassGpuTest.MorphWeightsDeformBeforeSkinningLikeCpu`).
- **애니메이션 되감기는 평가를 멈추고 기록된 포즈를 건다**(`AnimationRewind.h`, Shipping 에 없음) — 기록 요청은 프로세스 전역(`-gv_animationRewind` ·
  콘솔 `anim.rewind` · 에디터 Animation Rewind 패널)이고 씬마다의 기록기가 평가 앞에서 따른다. 시험은 요청을 바꾸면 되돌릴 것(`ScopedRecording`).
  군중 묶음과 나누는 유닛에는 포즈를 걸지 않는다(포즈가 묶음의 것) — 기록 · 뼈대 그리기만 된다.
- **다중 뷰(`FrameRendererViews.cpp`)의 함정 셋.** ① 디스패치마다 쓰는 상수버퍼(컬링 · 정렬)는 뷰마다 따로다 — 정렬 CB 하나를 주 뷰 · 추가 뷰가 나눠 쓰면 마지막
  기록만 남는다(`RenderView::_sortCb`). ② 직렬 경로의 패스는 `_frameCtx._pCmd` 리스트에 기록한다 — 프리패스 리스트가 이미 닫힌 뒤라 그 자리를 뷰의 리스트로 바꿔
  두지 않으면 Vulkan 이 죽고 나머지는 0 을 그린다. ③ D3D 의 `CopyResource` 는 같은 포맷 · 크기만 받는다 — 컷 프레임은 원본을 기록에 복사하지 않고 기록 자리에
  원본을 건다, 캡처를 백버퍼로 옮기는 것은 출력이 백버퍼 크기일 때만. GL 기본 프레임버퍼는 아래 원점이라 `setViewport` 가 y 를 뒤집는다(오프스크린 FBO 는 그대로).
- **GPU 자원을 든 객체의 마지막 소유는 게임 스레드가 아무 때나 놓는다 — 핸들 반환은 `IRHIDevice::releaseHandle` 로.** GpuScene 후보 · 걷은 뷰가 마지막 소유가 되면
  소멸이 수집 잡 안에서 일어나고, 그때 렌더 스레드가 병렬 기록 중이면 bindless 표가 바뀐다(핫 리로드한 StarSkirmish · VoxelCraft · Shooter3D 가 Debug 단언으로 죽었다).
  `releaseHandle` 은 렌더 스레드가 프레임을 들고 있으면 그 프레임 뒤(RT 의 `flushDeferredHandleReleases`)로 미룬다(언리얼 `FDeferredCleanupInterface`).
  `Material` · `MaterialInstance` · `Texture2D` · `Mesh` 가 쓴다 — 새로 GPU 자원을 드는 객체도 팩터리를 직접 부르지 말고 이것으로 내린다(`Mesh` 만 빠져 있어 지형 LOD 교체가
  DX11 버퍼 SRV 표를 기록과 겹쳐 썼다). 주의: `sw::unordered_map::erase` 는 없는 키여도 쓰기다(DataRaceDetector 가 잡는다).
- **기본 포워드 파이프라인의 톤맵(Reinhard `c/(c+1)`)은 흰색을 0.5 로 누른다** — 2D 화면이 회색으로 죽는다. 2D 는 `forward2dpipeline.xml`(`-gv_renderPipeline`).
  씬의 `_localRotation` 은 라디안이다(`Units=rad`) — "0,0,-90" 은 조용히 엉뚱한 방향이다.
- **머티리얼이 쓰는 새 셰이더는 `ShaderCookRequest.cpp` 의 엔진 셰이더 목록에도 넣는다** — 머티리얼 쿠킹은 퍼뮤테이션 해시가 붙은 변형만 굽는데 머티리얼은
  define 없는 변형의 리플렉션을 묻는다. Debug 는 런타임 리플렉션으로 넘어가 모르고, Shipping hostgpu 만 "매니페스트에 없다" 로 실패한다(sprite2dlit).
- **투명 순서의 정본은 CPU 의 `sortTransparent` 하나다**(정렬 레이어 키 → 깊이 → 후보 번호). GPU `instancesort.hlsl` 은 압축된 목록을 인스턴스 번호
  오름차순으로 되돌릴 뿐이다 — 거기서 깊이를 다시 재면 정렬 레이어 · 직교 시선 축을 모르고 같은 깊이를 불안정하게 갈라 CPU 와 다른 순서를 낸다.

- **bindless 표를 바꾸는 일은 렌더 스레드의 병렬 기록과 겹치면 안 된다**(`IRHIDevice::setParallelRecording`). 게임 스레드의 `MaterialCache::initializePending`
  (씬 로드 · 처음 쓰는 머티리얼의 스폰)은 렌더 스레드가 지난 프레임을 기록하는 동안 돈다 — `EngineLoop` 는 올릴 것이 있는 프레임(`hasPendingInitialize`)만
  `RenderThread::waitIdle` 로 기다린다. 증상은 `registerBindlessResource 이(가) 병렬 패스 기록 중에…` 단언 · 크래시이고, 단언은 플래그를 경합으로 읽어 재현율이
  바이너리마다 다르다(같은 실행이 0 · 100 %). 의심되면 `assertRegistryMutableNow` 에 콜스택을 파일로 남겨 본다(로거는 크래시 직전 줄을 잃는다).

- **주의: DX12 `enqueueGpuRelease`(`_fenceValue`)** — 다른 스레드의 `waitForQueueDrain` 이 같은 값을 먼저 Signal 하면 기록 중인 프레임이 제출되기 전에 해제가 돌 수 있다.
  기존 DX12 해제 경로 전부에 해당한다(열린 일).

- **런타임에 바꾸는 정적 스위치는 `bShaderFeature="0"` 이어야 Shipping 에 바이너리가 있다** — 쿠커는 에셋 상태 + `bShaderFeature="0"` 스위치의 켬/끔 조합만 쿠킹한다(유니티 shader_feature / multi_compile, 런타임 스위치 넷까지). 코드가 `setStaticSwitch` 로 바꾸는 변형은 Dev 의 실시간 컴파일이 가려 Shipping hostgpu 에서만 진다 — `ShaderCookRequestTest.EveryRuntimeStaticSwitchCombinationIsRequested` 가 조합을 패스마다 대조한다. 멀티 컴파일(`_multiCompiles`)은 아직 고른 값만 쿠킹한다.
- **정적 스위치의 `keywordOff`(꺼지면 내는 define)를 쓰는 에셋은 지금 없다** — `MaterialTest.StaticSwitchOffKeywordWhenDisabled` 만 지킨다. 스위치를 지우거나 켜고 끄면
  퍼뮤테이션 해시가 바뀌므로 `--cook-shaders` 를 다시 한다.
- **셰이더 쿠킹은 패스 종류 표 전체 × (머티리얼 없음 + 머티리얼) × `RenderViewMode` 를 쿠킹한다** — 파이프라인 XML 에 나오는 패스만 곱하면 런타임
  (`ensurePassResources`)이 만드는 변형이 빠진다. 뷰 모드 define 의 정본은 `FrameRendererUtil::findViewModeDefine`, `ShaderCookRequestTest.CookedManifestHoldsEveryRequest` 가
  커밋된 매니페스트를 대조한다. Vulkan 최소 판과 쿠킹 타깃(`-fspv-target-env`)은 `VulkanRHIApiVersion.h` 하나 — 1.3 미만 디바이스는 고르지 않는다(SPIR-V 1.6).

- **텍스처는 들일 때 임포트한다(사용자 결정 2026-10-03 — UE 임포트 방식).** 런타임은 DDS 만 읽고, 원본은 `<domain>/textures_raw/` 에만 둔다(`CheckTextureFolders`).
  원본 ↔ DDS 대조는 원본 폴더마다 `import.stamp`(원본 바이트 + 해석한 규칙 + 임포터 버전의 해시, DDS 해시) — `App --import-textures` · `--check-textures`(헤드리스로
  에디터 모듈을 올린다, Shipping 은 이유를 남기고 실패), CI 대조는 `TextureImportStampTest`. 함정: 임포트 동작을 바꾸면 `TextureImporterInternal::kImporterVersion` 을 올려야
  모든 스탬프가 어긋남이 된다. Debug 의 DirectXTex BC7 은 블록당 수백 ms 라 큰 원본은 Release App 으로 임포트한다. 밉 · 변환은 `TEX_FILTER_FORCE_NON_WIC`(결정적).
- **모델도 같은 스탬프 절차다** — `models_raw/` 의 glTF → `models/*.mesh`(`App --import-models` · `--check-models`, `AssetImportStampUtil` + 종류마다
  `IRawAssetImporter`). glTF → 엔진은 X 반전 + 삼각형 감김 뒤집기(노드 행렬식 < 0 이면 한 번 더). `.mesh` 는 지금 형식만 읽는다 — `RHIVertex` 를 바꾸면
  `MeshAssetFormat::kVersion` 을 올리고 다시 임포트. 메시 캐시는 약한 참조라 쓰는 쪽이 없으면 리로드할 것도 없다(다음 `acquire` 가 새로 읽는다).
  원본 glb 는 내려받은 그대로 둔다 — 비표준 씬 뿌리는 임포터가 받고, 배치 오프셋은 `ModelImportConfig.json` 규칙으로 지운다. 경계 상자 중심
  (`recenter: xz`)은 모양이 치우친 모델을 옮기므로 원점이 정해진 키트에는 `translation` 이 맞다.

- **머티리얼 캐시는 잡을 때 `.meta` 를 지어 붙인다(`AssetDatabase::ensureMeta`)** — 임포트 결과 옆 폴더(`models/<이름>/`)에 머티리얼을 쓰면 첫 실행이 실행마다 다른 GUID 의 `.meta` 를 만들어 스탬프가 "손으로 바꿨다" 가 된다. 임포터가 경로에서 정해지는 GUID 로 `.meta` 를 미리 쓴다(`ModelImporterInternal::makeImportedGuid`).
- **디바이스 종료 순서는 `IRHIDevice::shutdown`(비가상 템플릿 메서드) 하나가 정한다** — releaseAllFor → `waitIdleInternal` → `detachCommandRecordingInternal` →
  `shutdownInternal`. 백엔드는 훅만 채우고 앞부분을 다시 적지 않는다(네 벌일 때 DX12 · DX11 이 이미 어긋나 있었다). 리스트 떼기는 `RHILiveCommandListUtil::detachAll`.
- **트랜지언트 크기를 따르는 자원(TAA 히스토리 · Present 캡처)은 `releaseTransientResources` 만 놓는다** — 패스 자원만 다시 세우는 셰이더 리로드는 이것을 다시 만들지
  않는다(놓으면 리사이즈 전까지 히스토리 0). 컴퓨트 상수버퍼는 `collectComputeConstantBuffers` 표 하나로 만들고 놓는다.

- **텍스처 슬롯 샘플러의 정본은 `bindingslots.hlsli` 의 `SW_ENGINE_TEXTURE_SAMPLER`(t0..t3, 선형 · 클램프)와 `SW_MATERIAL_TEXTURE_SAMPLER`(t5..t8, 선형 · 랩)** —
  GL 은 유닛마다 샘플러 객체, DX11 은 정적 세트. 이로써 네 백엔드 벤치 프레임이 바이트까지 같다(골든 이미지 백엔드마다 같은 그림).
- **기록 중의 CB 갱신은 `IRHICommandList::updateConstantBuffer`, 기록 밖(에셋)은 `IRHIResourceFactory::updateConstantBuffer`** — 한 버퍼는 프레임에 한 번만 쓴다.
- **배열 · 큐브 텍스처는 bindless 등록이 거부된다**(셰이더 테이블이 Texture2D 뿐). 면은 `_arrColorTargetSlice` · `_depthTargetSlice` 로 고른다.
- **첨부 `_resolutionDivisor` 를 쓰면 패스는 출력 첨부 크기로 열린다** — 한 패스의 출력은 같은 나눗수여야 한다(검증이 본다). Vulkan PSO 는 셰이더가 읽는 정점 속성만 건다.
- **머티리얼 · 인스턴스 형식 판정은 `AssetFormatRegistry::upgradeXmlWithActiveRegistry`**(씬 · 프리팹과 같다) — `AssetManager` 없이 돈다. 본문의 enum 글은 `TypeRegistry` 가 필요하다.

- **거울 변환(월드 3x3 행렬식 < 0)은 컬을 뒤집은 PSO 변형으로 그린다** — 배치 키 · 정렬 키 · 투명 병합에 `_bReverseCulling` 이 들어 있고 PSO 변형 키의 한 축이다
  (언리얼 `bReverseCulling`). 트랜스폼만 바뀐 프레임도 부호를 다시 구한다.
- **깊이 첨부는 렌더 그래프의 쓰기다.** 그래프는 선언 순서상 앞선 쓰기를 생산자로 고르므로, 불투명 깊이를 읽을 패스(SSAO · 외곽선)는 투명 패스보다 **먼저 선언**한다.
  D3D11 은 첨부 전이(`prepareTextureForRenderTarget`)가 그 텍스처가 걸린 PS SRV 슬롯을 뗀다(`D3D11RecordingState::_arrPixelSrvTexture`).
- **머티리얼 텍스처 슬롯(t5..t8)의 샘플러는 `shaderslot::kMaterialTextureSampler`(LINEAR_WRAP) 하나** — GL 은 샘플러 객체를 유닛에 `glBindSampler`, DX11 은 정적 세트.

- **패스 종류 하나 = `RenderPassType` 한 값 + `RenderPassTypeInfo.cpp` 의 case 하나**(기본 셰이더 · define · 포맷 · 클리어 · 입력 계약 · 플래그). 전용 실행이
  필요할 때만 `executePass` 의 switch 에 case. 런타임 PSO 와 쿠커가 같은 `selectRenderPassShader` 를 부른다. 마지막 열거자를 바꾸면
  `kRenderPassTypeCount` 를 직접 고친다(`RenderPassTest.TypeInfoTableCoversEveryEnumValue` 가 잡는다). 리플렉션 매니페스트는 키 순서로 쓴다(결정적).
- **패스가 머티리얼로 배치를 거르면(메시 외곽선의 `_pRequiredMaterialDefine`) 드로우 · 머티리얼 PSO 변형 · 쿠커가 같은 `drawsMaterialInPass` 를 본다** — 하나라도 빠지면 쿠킹 안 된 변형을 런타임이 찾거나(Shipping 매니페스트 미스) 외곽선을 모르는 셰이더가 앞면 컬링으로 그려진다. 툰 구 시험은 정점 색을 흰색으로 둔다(생성기의 검증 색이 계단 위에 그라데이션을 얹는다).
- **인스펙터 위젯 · CallInEditor 인자는 `ReflectBuiltins.xxx` 를 펼친 표 하나**(`InspectorBuiltinValue.h`) — 내장 타입을 더하면 `InspectorWidgetFor<T>` 특수화가
  없으면 컴파일이 선다. .xxx 의 문자열 줄은 `std::string`, 프로퍼티는 `sw::string`(`InspectorBuiltinCppType` 이 메운다).

- **셰이더 이름 규칙은 C++ 와 같다**(AGENTS.md "### HLSL", `CheckShaderConventions` 게이트): 함수 camelCase(공유 헤더는 `sw…`), 타입 PascalCase
  (공유 헤더는 `Sw…`, `_t` 없음), 필드는 C++ 멤버 이름에서 `_` 를 뺀 것. `g_*` · cbuffer · 시맨틱 · 진입점(`VSMain` · `PSMain` · `CSMain`)은 C++ 가
  문자열로 묶으므로 바꾸면 같은 커밋에서 C++ 도 바꾼다. **셰이더 쪽 개명은 계약 검사를 조용히 끌 수 있다**(`validate` 는 표에 없는 이름을 건너뛴다) —
  `ShaderBindingValidatorTest.EveryBoundNameIsInCookedReflection` 이 C++ 가 아는 이름이 쿠킹된 매니페스트에 있는지 본다.

- **렌더 스레드는 씬을 못 본다** — 런타임 경로의 `_pScene` 은 늘 null, `GpuSceneSnapshot` 이 유일한 통로다. CPU 폴백을 다시 만들지 말 것. 스레드 경계 타입은 `GpuSceneBuilder`(GT, GPU
  핸들 0) / `GpuSceneSnapshot` / `GpuScene`(RT)이다. 스냅샷은 GT 가 만든 것만 옮긴다 — RT 가 파생하는 값(`_indirectCommandCount`)을 실으면 GT 의 0 이 덮는다(증상: "카메라를 움직일
  때만 메시가 보인다"). 패킷은 자기완결이어야 하고 소유(`shared_ptr`)를 싣는다 — 생포인터는 `CheckRenderOwnership` 이 막고 예외는 `// SW_OWNERSHIP_RAW_OK: <이유>`.
- **셰이더 바인딩 계약의 정본은 `bindingslots.hlsli` 하나**(HLSL · C++ 같은 파일 include). 백엔드는 shaderslot 상수만 쓰고 바인딩 숫자 리터럴은 금지, `ShaderBindingValidator` 가 쿠킹된
  바이너리를 대조한다(nogpu). `draw()` 에 CB 인자는 없다. 백엔드 간 공유 상수는 `RHITypes.h` `constant` 블록 하나 — 지금 값이 같아도 바뀔 수 있으면 공유하고 별칭도 금지.
- **상수버퍼를 드로우 · 디스패치가 나눠 쓰면 안 된다**(같은 함정이 두 번: 패스 CB, 컬링 CB). 값이 바뀔 때만 쓰는 CB 는 `RHIConstantBufferMirror` 로 링의 모든 칸에 채운다(안 그러면
  DX12 · Vulkan 이 세 프레임 중 둘을 0 으로 그린다). `updateConstantBuffer` 크기는 만든 크기를 넘으면 안 된다(GL 만 막는다) — 셰이더를 다시 구우면 머티리얼 CB 가 커질 수 있어
  `MaterialInstance` 가 `_constantByteSize` 로 다시 만든다. CB 칸 크기는 리플렉션, 쓰는 크기는 XML `shaderType` — 어긋나면 옆 프로퍼티 색이 오염된다(`writeBoundedValue`).
- **셰이더 산출물 스테일 판정은 내용 해시 하나다**(`ShaderCooker::computeEffectiveSourceHash`, mtime 금지 — 쿠킹된 바이너리를 커밋하므로 git 이 mtime 을 섞는다). 스탬프 헤더 `SWCOOK 3`,
  `cook.stamp` 는 CR 을 뗀 바이트로. 매니페스트가 바이너리보다 낡으면 바인더가 빈 레이아웃으로 그려 DX12 DEVICE_HUNG 이다 — `CookAssets.py --verify-shaders`. 백엔드 하나만 다른
  그림을 내면 ① 셰이더 산출물 ② 엔진 바깥을 다 되읽었는데 맞으면 셰이더 **코드 모양**(GL 드라이버가 DXC early-return 을 잘못 컴파일했다)을 의심한다. 프로브는 `nointerpolation`
  슬롯에 정수를 싣고, 모프 진단은 `-gv_morphDiag=1|2|3`.
- **쿠킹 바이너리 이름은 `ShaderCooker::computeBinaryFileName` 하나**(퍼뮤테이션 해시 포함 — 빠지면 define 이 GPU 에 안 닿는데 리플렉션은 맞아 보인다). 패스 define 의 정본은
  `FrameRendererUtil::getPassDefine`(런타임 PSO 와 쿠커가 같이 부른다 — 갈리면 Shipping 에서만 그 드로우가 사라진다). "컬러 출력 없는 패스에는 픽셀 스테이지가 없다" 는
  `hasPixelStage` 하나. 실시간 컴파일 요청은 `ShaderCache::makeLiveCompileDesc`(경로 `Saved/ShaderCache/<rhi>/<해시>-<opt|dbg>/`), 쿠커는 늘 최적화다. 셰이더 리로드 대상은
  `ShaderCache::collectCompiledDescs`, 바이트코드가 같으면 로컬 캐시에 쓰지 않는다.
- **정점 입력의 정본은 `constant::arrVertexAttribute`**(POSITION · NORMAL · TEXCOORD · COLOR, 48 B), 셰이더는 `common.hlsli` 의 `SwVertexInput` 만 쓴다(Vulkan · GL 은 선언 순서로
  location 을 매긴다). `SV_VertexID` 는 Vulkan · GL 에서 startVertex 를 포함하고 D3D 는 0 기반이다. 인스턴스 자리는 정점 슬롯 1(`SW_INSTANCESLOT`)로 넘기므로 `SV_InstanceID` 를 쓰지
  않는다. `GpuInstance` 원소 정의는 `instancedata.hlsli` 하나.
- **구조버퍼 원소는 `float4` 단위로 짠다**(float3 을 섞으면 std430 때문에 GL 만 어긋난다). GL(ARB_gl_spirv)은 구조버퍼를 정점 · 픽셀 두 단계에서 읽으면 링크를 거절하고 그 배치는
  조용히 물러난다 — 머티리얼은 픽셀 단계에서만 읽는다. GL 은 SPIR-V 라 bindless 텍스처가 불가, DX11 은 SM5.0 이라 버퍼로 텍스처를 못 넘긴다 — 머티리얼 텍스처는 t5..t8 고정 슬롯.
- **GPU 모프는 구조버퍼 풀 + 정점 셰이더 인덱스 읽기다**(Vertex 버퍼의 UAV · DX12 VB 의 UPLOAD 힙 · DX11 겸용 불가 때문). `Mesh::setVertices` 는 매번 GPU 버퍼를 다시 만들므로 매 프레임
  CPU 정점 갱신은 금지.
- **GPU 가 드로우 커맨드를 만든다** — 컬링이 가시 인스턴스 ID 를 압축해 커맨드를 생성한다(개수만 세면 보이는 쪽이 사라진다). 투명은 GPU 바이토닉 정렬, 블렌드 모드는 머티리얼이다.
  투명 배치는 정렬된 순서에서 연속한 같은 키를 묶는다(배치 순서 = 깊이 순서). 병합 키의 `_materialCb` 는 레이아웃이 MaterialCB 슬롯을 가질 때만 넣는다. `RHIDispatchIndirectCommand` ·
  `RHIDrawIndexedIndirectCommand` 는 C++ 참조가 0 이어도 지우지 않는다(인자 버퍼 레이아웃 정본).
- **GpuSceneBuilder 계약** — 전체 · 부분 수집은 같은 `fillCandidateFromPrimitive`. 집합 · 퍼뮤테이션 세대가 바뀌거나 더티가 1/4 을 넘으면 전체 수집. 부분 프레임에는 회수 시계를
  멈춘다(머티리얼 원소 회수는 돈다). 퍼뮤테이션 해시는 `SortKey` 에 직접, 정지한 씬의 재수집 트리거는 `MaterialUtil::getPermutationGeneration()`. 발행한 인스턴스 배열은 다시 고치지
  않는다(`GpuInstanceRing` 은 `use_count()==1` 슬롯에만 짓는다). `rebuildTransparentTail` 은 접두부 갱신과 같은 `runParallel` 의 블록 0 이다. `DrawCandidate` 의 `shared_ptr` 을
  날 포인터로 바꾸지 말 것(같은 주소에 새 메시가 태어나면 ABA). `PrimitiveRegistry` 의 더티는 렌더 상태 · 월드 행렬만 둘이다.
- **GPU 자원 수명은 `RHIRenderResource` 등록부에 통보로 밀어 넣는다**(`Mesh` · `Material` · `MaterialInstance` · `Texture2D`). 디바이스가 살아 있으면 `releaseRhi`, 이미 없으면
  `forgetRhi`(여기서 destroy 하면 UAF), 교체 뒤에는 `initAllFor( device )` 한 줄. `initRhi` 는 멱등. 디바이스 세대 번호 · `shutdownAllGpu` · `reinitializeAll` 을 되살리지 말 것.
  동사 표는 `AGENTS.md`. `Material::forgetRhi` 는 `releaseRhi` 와 같은 상태를 남겨야 한다(빌린 텍스처 목록이 남으면 t5..t8 서수가 밀린다).
- **새 디바이스는 첫 PSO · 버퍼에 옛 것과 같은 번호를 준다** — 디바이스를 넘어 사는 캐시는 `releasePassResources` · `shutdown` 에서 잊는다. 머티리얼 등록부 비우기는 그룹 목록과
  셰이더 경로 → 인덱스 맵을 같이(한쪽만 비우면 투명이 알파 0 으로 사라진다). 재생성 판정은 bindless 인덱스가 아니라 세대가 든 핸들로(DX11 · GL 은 인덱스를 즉시 회수한다).
- **텍스처 리로드는 같은 `Texture2D` 에 새 SRV 인덱스를 준다** — `getReloadGeneration` → `refreshTextureBindings` → `refreshReloadedTextures`. DX11 · GL 은 인덱스를 바로 다시 써서 이 종류가
  숨는다 — DX12 · Vulkan 으로 본다. 머티리얼 인스턴스 텍스처는 에셋 경로로 덮어쓴다(`setTextureParameter`). 디바이스 없이 잡은 머티리얼은 `MaterialCache::requestInitialize` 로 표시한다.
  `MaterialCache` · `TextureCache` 는 일부러 다르다(소유 · 디바이스 기억 · acquire 순서) — 맞추지 말 것. 두 캐시의 `clear()` 는 GPU 자원을 놓지 않는다(RHI shutdown 이 먼저라 안전).
- **`Material` · `MaterialInstance` · `Mesh` 는 Engine 의 `create()` 로만 만든다**(모듈이 `make_shared` 하면 제어 블록이 모듈 DLL 에 살아 종료 세그폴트). 팩토리 안에서는 `sw::make_shared`.
- **`IRHIDevice::waitIdle` 은 비가상이다** — 다른 스레드에서 부르면 렌더 스레드 패킷을 먼저 비운다. 렌더 스레드에서 텍스처 · 머티리얼 캐시를 잠그면 교착이다. `RenderThread` 는 패킷을 실행한 뒤에
  `_tail` 을 올리므로 "큐가 비었다" = "프레임이 끝났다". 리사이즈는 `RenderThread::waitIdle()` 뒤에. GT 버퍼 만들기와 RT 드로우별 맵 읽기는 `_bindlessMutex` 읽기/배타로 나눈다(배타 안에서
  읽기 락을 잡으면 스스로 멈춘다).
- **커맨드 리스트는 디바이스보다 오래 살 수 있다** — 디바이스 종료 앞에 `cmdList.reset()`. 기록 상태 캐시는 "기록 스트림마다 하나" 다(GL 은 하나여야 맞다). 웨이브 배리어는 RT 가 첫 패스
  리스트를 열어 앞머리에 적고 워커가 닫는다 — "연 스레드 ≠ 닫는 스레드" 라 스레드 로컬 묶임이 함정이다.
- **DX11** — `ID3D11DeviceContext` 는 스레드 안전하지 않다. 드로우 경로의 CB 갱신은 워커가 건 자기 Deferred Context 에 `Map(WRITE_DISCARD)`, 즉시 컨텍스트 자리는
  `_immediateContextMutex` 뒤. 기록 컨텍스트의 스레드 로컬은 (디바이스 일련번호 · 슬롯 · 세대) **토큰**만 든다 — 포인터로 되돌리면 해제된 컨텍스트에 Map 한다. 모든 드로우 진입점은
  `bindGraphicsPipelineForDraw()` 를 거친다. 인스턴스 버퍼를 CS UAV 에서 떼지 않으면 D3D11 이 SRV 를 NULL 로 강제한다(해저드는 WARNING 이라 로그에 안 나온다 — 해저드 ID 만 ERROR 로).
  기록 끝난 `ID3D11CommandList` 가 백버퍼를 붙든다(리사이즈). 버퍼의 SRV 는 버퍼 레코드(`BufferRecord`)에 든다 — 기록 경로가 읽는 자료를 따로 된 해시 맵에
  두지 않는다(생성 · 삭제의 재해시를 기록이 읽는다).
- **DX12** — 펜스 대기의 시간 초과는 성공이 아니다. 커맨드 얼로케이터는 리스트마다(공유가 DEVICE_HUNG 의 진짜 원인이었다), 업로드 얼로케이터 Reset 은 그 슬롯의 펜스를 직접 기다린다.
  업로드 복사 리스트는 열어 두고 기록만 하며 `flushPendingUploads` 가 프레임에 한 번 내보낸다(`_uploadSlotMutex`). bindless 인덱스는 `acquireBindlessIndex(lock)` 로 집는 일과 뷰 생성을
  한 임계 구역에. CBV 는 256 B 정렬. drawIndirect 가 메시 VB 를 덮어쓴 적이 있다 — 렌더 변경은 스크린샷까지 본다.
  오프스크린 레코드(`_mapOffscreenTexture`)는 기록 경로에서도 `findOffscreenTargetView`(잠근 채 복사)로만 읽는다 — 이터레이터를 들고 `transitionTexture` 를 부르면 같은 뮤텍스로 교착이다.
- **Vulkan** — acquire 한 이미지는 present 로만 돌려준다(present 없는 프레임마다 acquire 하면 `UINT64_MAX` acquire 로 교착). 리소스 해제는 실제 GPU 펜스(단조 세대)와 이어야 한다.
  일회성 업로드는 `VulkanOneShotCommands` · 전용 풀 · `_queueMutex`. `vulkan1.3` DXC 는 `discard` 를 demote 로 내므로 기능을 켠다 — 쿠킹된 셰이더가 바뀌면 검증 레이어 로그를 다시 읽는다.
  와이어프레임은 `fillModeNonSolid`. 백버퍼 블릿의 이전 레이아웃은 `UNDEFINED`.
- **GL** — 컨텍스트는 렌더 워커가 프레임마다 쥐었다 놓는다(다른 스레드 생성은 `acquireGraphicsContextBlocking`). `ARB_gl_spirv` 가 없으면 초기화에서 끊는다(다른 백엔드로 넘어가지
  않는다). `glClipControl` 은 `#ifdef GL_CLIP_CONTROL`(없는 토큰) 같은 가드 뒤에 두지 말 것(상하 반전이 오래 숨었다). MRT 클리어는 `glClearBufferfv`, `R16G16B16A16_FLOAT` 는
  `GL_HALF_FLOAT`, `drawInstanced` 는 startInstance 를 버린다. 로그 문구에 `[Error]` 같은 레벨 토큰을 쓰지 말 것(스모크가 센다).
- **스왑체인은 진짜 객체다**(`5aea5ef1`) — 가상 인터페이스로 되돌리지 말 것, GL 은 의도적으로 없다. Present PSO 는 대상 포맷(`getBackBufferFormat`)으로, BGRA 는 `-gv_rhiBackBufferFormat=1`
  로 검증. `IRHIDevice` 에 백엔드 전용 API 를 두지 않는다 — 에디터 같은 외부 모듈은 네이티브 핸들을 판 번호 든 `RHINativeHandles` 로 받는다
  (`IRHIDevice::queryNativeHandles` 가 판 · 크기를 대조). 구체 디바이스로 캐스팅하지 말 것. 에디터 능력을 `RHICapabilities` 에
  넣지 말 것(`createRendererBackend` 가 모르는 백엔드에 `nullptr`). 백엔드 능력은 이름이 아니라 `getCapabilities()` 런타임 값으로.
- **백엔드 하나만 고쳐진 모양이 계속 나온다**(`createStructuredBuffer` 는 DX12 만 64 비트로 곱한다). 시험은 백엔드별 계약으로 쓴다. 안 쓰이는 경로는 조용히 썩는다 — 폴백은 지우고
  "아직 안 쓰는 기능" 은 시험과 함께 남긴다(인덱스 드로우의 유일한 검증은 `RHIDeviceTest.IndexedIndirectDrawReadsInstanceSlotStream`, `createIndexBuffer` 는 순수 가상).
- **렌더 패스** — 패스 입력은 선언이 곧 바인딩이다(`RenderPassInputSignature` 역할 표, 첨부 역할은 `_role` 선언 → 정본 이름 → 포맷). `findTransient` 의 핸들 0 은 백버퍼다 — 없는 첨부를
  열면 씬이 백버퍼로 간다. `beginColorPass` 가 false 면 그리지도 닫지도 않는다. WAR 간선은 "생산자 다음 쓰기", 같은 이름 패스는 거절, `executeParallel` 은 모든 레벨의 리스트를 먼저
  마련하고 못 하면 false(직렬 폴백은 앞 레벨을 두 번 그린다). 풀스크린 패스의 컬은 `None` 고정. 후처리 효과는 패스가 아니라 함수(`postchain.hlsl` + 퍼뮤테이션, 기준은
  `forwardpipelinestaged.xml` · `FusedPostChainMatchesStaged`). 깊이 프리패스는 `SW_PASS_DEPTH_PREPASS` · LessEqual, DX11 은 VS 만.
- **그림자** — 직교 투영 깊이 범위는 눈 기준 `[거리-반경, 거리+반경]`(아니면 그림자 항이 늘 1), 샘플은 `swSampleShadowAtWorld`, 회귀는 행렬로(`ShadowMatrixDepthRangeContainsScene`) ·
  픽셀로 보려면 바닥(`-gv_benchGround=1`). 그림자 시험은 그림자 깊이 쓰기를 끈 판과 **달라야** 한다. 렌더 차이가 같은 프로세스 안에서는 결정적이고 프로세스마다 갈리면 배치 순서를 의심한다.
- **타임스탬프 계약** — 칸은 패스 인덱스로 고정(흐르는 카운터는 병렬 기록에서 경쟁), 기다리지 않고 링 슬롯이 펜스를 지난 뒤에만 읽는다, 안 적은 칸은 음수. 계측 게이트는
  `SW_PROFILE_COMPILED` — Shipping 에서는 통째로 빠진다(로그에만 쓰는 값은 `[[maybe_unused]]`).
- **`GpuUploadQueue`** 는 GT 가 `buildFromScene` 뒤 · 스냅샷 전에 동기로 flush 한다. 워커 생성은 `RHICapabilities::_bThreadSafeResourceCreation`(GL 은 큐가 받지 않는다 — 렌더 스레드가 그 프레임에 만든다. 게임 스레드가 GL 자원을 만들면 렌더 스레드가 쥔 컨텍스트를 기다리다 시간을 넘긴다). 비상 스위치 `-gv_gpuUploadQueue=0`.
- **디퍼드의 고정 비용은 채움률이다**(1280×720 2503 us · 640×360 864 us, 라이트 256 개 몫 ~600 us) — 타일/클러스터 컬링은 측정이 가리키는 자리가 아니다. GBuffer 는 같은 머티리얼
  셰이더에 `SW_PASS_GBUFFER` 를 얹는다(출력은 양쪽 다 구조체).
- **RHI 백엔드에 .cpp 를 더하면** `cmake/Engine/RhiBackendSources.cmake` 에도. 파일은 `<Backend>RHIDevice` · `…DeviceInit` · `…DeviceSubmission` 축으로. 백엔드는 별도 MODULE DLL 이라 Engine
  전역 변수를 extern 으로 못 쓴다 — 정책은 Engine, 메커니즘은 디바이스.
- **DDS 의 `dwFourCC` 는 D3DFMT 정수일 수 있다**(레거시 부동소수점). 스플래시는 32bpp 비압축만 받는다 — `splash.dds` 를 BC 로 저장하지 말 것. `.hdr` 은 임포트하지 않는다(8 비트 경로).

### 3-8. 에디터

- **오른쪽 클릭 메뉴의 확장 지점은 `EditorCommandRegistry` / `SW_EDITOR_*` 하나다** — 등록이 하나도 없던 `EditorActionMenuManager` 는 지웠다.

- **에디터가 UI 스레드에서 놓는 GPU 자원(ImGui 텍스처 · 게임 뷰 렌더 타깃)은 `EditorDrawReleaseQueue` 에 맡긴다** — 렌더 스레드는 같은 draw 스냅샷을 여러 패킷에
  다시 그리므로 UI 스레드에서 읽은 펜스 값으로는 모자란다. 그 스냅샷 번호 이상을 그리는 프레임에서 `IRHIDevice::enqueueGpuRelease`(렌더 스레드에서만)로 넘긴다.
  새 렌더 타깃은 그리기 전 패킷이 샘플링할 수 있어 만들 때 클리어 색으로 채운다(Vulkan UNDEFINED 레이아웃).

- **Undo 의 오브젝트 편집은 엔진 데이터 명령(`ObjectUndoUtil` — 오브젝트 id · 이름 · 스냅샷)으로 기록한다** — 리로드를 넘어야 할 기록은 Engine 코드로 만든다.
  모듈 람다 명령은 에디터 리로드 때 `CommandStack::releaseCodeWithin` 이 뗀다(묶음은 안쪽 하나라도 걸리면 통째로). 대상 조회는 id, 같은 프레임에 지우고 되살린
  오브젝트는 지연 파괴 때문에 새 id 를 받으므로 이름으로 다시 찾는다. 선택 · dirty 는 `ObjectEditListener` 로.
- **에디터 동작 검증은 에디터 안 자체 시험** — `SW_EDITOR_SELF_TEST` 로 등록하고 `AppSmokeTest.EditorSelfTestsPassInsideTheEditor` 의 기대 목록에 한 줄 더한다
  (`-gv_editorSelfTest=<패턴>`, 실행 중에는 사용자 `imgui.ini` 를 읽지도 쓰지도 않는다). 워크스페이스는 오브젝트 GUID · 프리팹 경로 사본을 들지 않는다(id · 씬이 정본).

- **DebugDrawQueue 는 `endFrame` 에 비워진다** — 에디터 UI 보다 먼저 채운 것(게임 업데이트)만 보인다(`debug_draw` 시각화). 틱에서 채우는 생산자가 생기면
  이중 버퍼로. `ActionRoom::drawDebug` 를 부르는 곳은 아직 없다. 메뉴 경로는 `EditorCommandRegistry::validate` 가 "그려지지 않는 경로" 를 잡는다.

- **패널 · 팝업 · 인스펙터 · 시각화는 자기 .cpp 의 `SW_EDITOR_PANEL` · `SW_EDITOR_POPUP` · `SW_EDITOR_INSPECTOR` · `SW_EDITOR_VISUALIZER` 한 줄로 등록한다**
  (`EditorRegistry<T>`, (order, id) 정렬, 같은 id 거절). 메뉴 배치는 커맨드 표 줄의 `_menuPath` · `_menuOrder`(백의 자리가 바뀌면 구분선). 매니저 · 메뉴바에
  손 목록을 다시 만들지 말 것. 시각화 마스크 비트는 등록 순서의 index 라 순서 키를 바꾸면 비트 자리도 바뀐다(지금은 저장하지 않아 무해).

- **에셋 종류 하나 = `EditorAssetType` 한 값 + `EditorAssetType.cpp` 의 `kArrAssetMatch`(판정 · 핫 리로드 칸) · `kArrKindInfo`(이름 · 라벨 · 패널 · 아이콘 · 색 ·
  임포트) 각 한 줄 + 필요하면 `Source/Editor/AssetActions/<Kind>AssetTypeActions.cpp`(썸네일 · 열기 · 드롭, 정적 등록).** 종류별 if-체인을 다시 만들지 말 것 —
  칸이 빠지면 static_assert 가 막는다. 도구 문서 IO 는 `loadToolDocument` / `saveToolDocument<TAsset>` + `ToolDocumentDesc` 하나.

- **에디터는 `-EnableEditor` 로 켜야 뜬다.** 에디터 스모크에는 `-gv_profileFrames` 를 꼭 붙인다(`-gv_editorPanelDump` 는 스스로 끝나지 않는다). 창 수가 모자라면 코드보다 로컬
  `Config/Editor/windows.ini` · `imgui.ini` 를 먼저 본다(추적하지 않는 파일 — 세션 간 픽셀 비교도 이것 때문에 안 된다). `-gv_editorOpenPanel=<id|all>`.
- **에디터 커맨드 정본은 `Common/Gui/EditorCommandGui.cpp` 의 표 하나**(메뉴 · 단축키 · 팔레트, `EditorCommandRegistry::validate` 가 중복 조합을 잡는다). 한 줄짜리 래퍼는 이유가 있어
  남았다(파일 머리) — "마저 정리" 하지 말 것. 확장자 정본은 `EditorAssetTypeRegistry`(`kArrAssetMatch` 한 줄), 핫 리로드 경로도 같은 줄의 칸(`_pCacheKindName` · `_pfnImportSource`)이다. 복합 접미사
  `.prefab.xml` 은 접미사 비교로(`hasExtension` 은 마지막 점 뒤만 본다).
- **nullable 조회는 받아서 확인하고 쓴다** — `editor::getService<T>()` · `game::getService<T>()` · `EditorContext::get()`. 나중에 불리는 람다 안에서는 다시 받는다. `getService<…>()->` 꼴은
  `CheckNullableServiceUse` 가 막는다. `game::areGameServicesBound()` 는 SceneManager 슬롯 하나만 본다. 진단용 서비스는 `OPT` 로 등록한다(required 면 `areEngineServicesBound()` 가 영영 false).
- **문서 저장 계약** — dirty 비트는 `IEditorPanel` 이 든다(패널이 자기 `_bDirty` 를 만들면 Ctrl+S · 종료 확인에서 빠진다). `saveDocumentAndClearDirty` 가 성공했을 때만 지운다. 로드 실패는
  `markDocumentLoadFailed` 로 저장을 막는다. 씬 dirty 는 되돌리기 · 다시 하기 · 스냅샷 되읽기에서도 찍는다. 커맨드 스택이 없어도 `markActiveSceneDirty()` 는 찍는다.
- **플레이** — 스냅샷은 활성 씬의 세대 · 이름 · 소스 경로를 함께 적는다(Stop 때 세대가 다르면 로드를 거두고 편집하던 씬을 다시 세운 뒤 되돌린다). 플레이 중 인스펙터 직접 편집은 바로
  적용한다(의도 — Stop 이 되돌린다). 씬을 여는 중의 Play 는 `Starting` 으로 미뤘다가 로드가 끝난 프레임에 시작한다.
- **되돌리기** — 자식 있는 오브젝트는 서브트리를 후위 순서로 한 트랜잭션에(`recordDestruction`), 생성 · 삭제는 `recordObjectLifetime` 한 절차. 제자리 로드는 지우기 전에 다른 오브젝트의
  자식을 (자식 핸들, 부모 **안정 키**)로 적고 되붙인다. 오브젝트 → GUID 표와 GUID → 오브젝트 표는 서로의 역이어야 한다(`EditorWorkspace::setGuid`). 모듈 DLL 주소(람다)는 모듈이 내려가기 전에 걷는다.
- **인스펙터** — 타입 사슬 전부의 확장을 기반 → 파생 순으로(`collectForType`), 확장은 자기가 그린 프로퍼티만 알린다. 각도는 라디안으로 저장하고 에디터만 도로 보인다(`Units=rad`),
  0..1 비율은 `Units=ratio`, `PropertyUnitsTest.UnitsMatchHowValuesAreStored` 가 본다. 검색은 `EditorListFilter`, 0 건 안내는 `drawNoSearchResultHint`(손으로 쓴 `stristr` 술어는 빈 필터에서
  목록을 지운다).
- **ImGui 수명 짝** — 플랫폼 백엔드 `shutdown()` 은 `BackendPlatformUserData` 를 확인한 뒤에만, 초기화 실패 경로도 전역을 걷는다, 팝업에 `p_open=&_bOpen` 을 넘기지 말 것(X 버튼이 `onClose`
  를 건너뛴다). 모달이 떠 있으면 키가 `InputManager` 까지 오지 않는다. 에디터 draw 스냅샷은 획득 → present **또는 포기**(`abandonPendingDraw`)로 끝난다. 입력 위젯은 `drawTextField` 하나.
- **에디터 상태 · 설정** — 설정 파일 경계는 "앱이 다시 쓰는가": `EditorConfig.json`(전체 재생성 — 테마만), 손으로 정하는 경로는 읽기 전용 `editortooldefaults.json`. Game View 클리어 색은
  `_clearColor`. 상태를 소유자에게 옮길 때는 그 소유자가 언제 서는지부터 본다(테마가 `EditorContext::initialize()` 전에 읽혀 조용히 버려졌다). DPI: 96 DPI 기준값 × 배율, 테마에서 곱하고
  되읽을 때 나눈다(짝이 깨지면 이중 배율). 에셋 핫 리로드는 에디터 소유(`FileWatchDispatcher`), 감시 접두어는 절대 경로.
- **기계 훑기의 알려진 오탐** — 델리게이트로 묶인 `&Class::method` 는 "죽은 함수" 로 잡힌다. `EditorThemeUtil` 팔레트 · 킷의 소비자 없는 세터 · 게터는 정상이다. 쓰이는지는 `= delete` 로
  바꾸고 빌드해 센다.
- **패널 시각 검증 사각** — 피킹 클릭 · 기즈모 우선순위는 사람이 눌러야 보인다. 그리기 회귀는 `Game View` 정점 수로 전후를 비교한다.

### 3-9. 핫 리로드 · 모듈 · 엔진 서비스

- **지연 import 는 첫 호출로 묶이게 두지 않는다 — 첫 float 인자가 망가진다**(2026-10-06). lld 20 의 x64 지연 로드 썽크 `__tailMerge_<dll>` 은
  `push rcx … r9; sub rsp,48h; movdqa [rsp],xmm0; movdqa [rsp+10h],xmm1; …; call __delayLoadHelper2` — `[rsp..rsp+1Fh]` 가 그 호출의 홈 공간이라 헬퍼가
  rcx · rdx 를 흘려 저장된 xmm0 을 덮는다(키트의 `DamageMath::applyArmor( 25, 5, 0, 1 )` 첫 호출이 damage = 0 을 받았다). 지연 로드 훅 TU 가
  `bindDelayLoadImports`(이미지의 지연 import 를 `__HrLoadAllImportsForDll` 로 전부)를 내보내고, `LiveReloadManager` 의 커밋 · `ModuleHost` · 엔진 기동
  (`bindDelayLoadImportsOfLoadedModules` — 시험 실행 파일이 링크한 키트)이 모듈 코드가 돌기 전에 부른다. `/DELAYLOAD` 는 `TargetRules.cmake` 의 두 함수로만
  (`CheckDelayLoadSites`) — Engine 의 시스템 DLL(D3DCompiler · MF · XAudio2 · Tracy)은 미리 묶지 않으니 첫 인자가 float 인 함수를 부르지 않는다.
  Shipping 은 키트 · 게임을 정적으로 링크해 모듈 지연 로드가 없다. 시험: `DelayLoadBindTest` · `ArchitectureTest.ReloadedDependentsBindToTheCurrentImages`.

- **게임 인스턴스는 핫 리로드 · 백엔드 교체마다 다시 선다 — `onInitialize` 의 "처음 한 번" 일은 되살린 월드를 덮는다.** 첫 씬 요청이 그랬다(되살린 씬을 몇 프레임
  뒤 새 첫 씬이 바꿔 디렉터 상태가 사라졌다) — `requestFirstScene` 은 살아 있는 씬 위에서는 아무것도 하지 않는다. PROPERTY 가 아닌 디렉터 상태는
  `ComponentStateStore`(봉투 v3 의 세 번째 섹션)로 넘기고, 손 없이 확인은 `App -gv_reloadGameAtFrame=N`.
- **모듈 켜기/끄기 · 의존 · 적재 순서는 매니페스트(`<모듈>.module.json`)가 정본이다**(CMake `ModuleManifest.cmake` 와 App `ModuleCatalog` 가 같은 규칙 — 둘의 답을
  `ModuleCatalogTest.BuildAndRuntimeAgree` 가 견준다). 새 동적 모듈은 매니페스트가 없으면 `sw_registerDynamicModule` 에서 구성이 선다. 게임 매니페스트는 활성 게임 것만
  빌드가 읽으므로 다른 게임 것은 `ModuleCatalogTest.EveryRepositoryManifestParses` 가 본다. 꺼진 모듈의 낡은 DLL 은 `Bin` 에 남아도 올리지 않는다.

- **올라온 모듈 이미지를 다루는 코드는 `Core/Module/ModuleImageUtil` 한 곳이다**(이름 · 올리기 · 심볼 · 범위 · 의존 고정 · import 결속 · 코드 떼기 · 내리기). `FileUtil` 에 되돌리지 말 것. 섀도 복사본 **파일 바이트**(`ModuleImagePatch`)와 리로드 정책(`ShadowCopyName` · 리눅스 도장 결속 검사)은 쓰는 곳이 하나라 `LiveReloadManager` 에 있다.
- **핫 리로드가 아닌 곳에서 모듈 이미지를 내릴 때는 `ModuleImageUtil::unloadModuleImage`(Core) 하나로** — 게임 · 에디터 모듈과 RHI 백엔드 모듈이 같은 창구다(RHI 층은 Module 층을 include 할 수 없어 Core 에 둔다. 로그 이름은 적재 때 받은 경로로 — 종료 중 서비스 소멸자에서 리플렉션 조회(`RHI::getBackendTypeName`)를 부르면 정리 중인 TypeRegistry 를 읽어 죽는다) — `releaseModuleCode` 로 그 이미지 코드를 쥔 등록(디스패처 채널 등)을 떼고,
  떼지 못하면 내리지 않으며, 끌어온 의존 이미지는 고정한다(리눅스는 DT_NEEDED 가 함께 내려가 종료 때 남은 채널 deleter 로 SEGFAULT, Windows 는 /DELAYLOAD 가
  GameFramework 를 프로세스 끝까지 잡아 가려졌다). 섀도 사본 이름은 `<모듈>_temp_p<pid>_…` — 정리는 다른 살아 있는 프로세스의 사본을 남긴다(`Bin` 은 CTest `-j` 로 같이
  도는 프로세스들 — AppCookTest 가 띄운 App 등 — 이 함께 쓴다).

- **씬은 기동 단계 `ModuleTypes` 뒤에만 읽는다** — `TypeRegistry::areAllModuleTypesRegistered()` 가 거짓이면 `SceneManager::requestLoadFuture` · `SceneCooker::cookAllScenes` 가
  거절한다. 모듈 이미지 · 타입 등록은 그 단계에서 App 로더(`ModuleHost::loadModuleImages`)가 하고, 인스턴스는 RHI 뒤에 **게임 → 에디터** 순(그래야
  `-gv_editorStartupScene` 이 마지막 요청이 된다). Shipping 통째 링크(/WHOLEARCHIVE) 목록은 `sw_configureAppDependencies`(모듈 등록 뒤)에서만 읽는다 — App 이 GF · 게임보다
  먼저 add_subdirectory 되어 GF · 킷 · 게임의 등록기가 배포본에서 빠져 있었다. 쿠킹은 `ContentSource::SourceTree`(팩은 산출물이라 입력이 아니다)이고, MissingComponent 가
  든 씬은 쿠킹하지 않고 실패(종료 코드 → CookAssets)로 센다.

- **엔진 기동 · 종료 순서는 `EngineInitStepList.xxx` 의 의존 칸이 정하고, 표는 그 순서대로 적는다**(UE `USubsystem` 의존 선언). 의존은 식별자 목록
  `{ A, B }` 라 오타 · 아래 줄 의존은 컴파일 오류(static_assert), 정렬은 의존만 보고 동점은 이름 순, 그 결과가 줄 순서와 같은지
  `EngineInitSequenceTest.TableIsWrittenInStartupOrder` 가 본다(의존을 빼먹으면 진다). 종료는 초기화한 단계만 역순.
  새 단계는 호스트(EngineLoop · 시험 하네스 `Test/TestFramework/main.cpp`)마다 `<단계>StartupStep` 구조체 하나(initialize · shutdown · destroy, 기본은 no-op)를 더한다 —
  빠지면 `EngineInitStepTable` 이 컴파일 오류. 해제(destroy)는 **표의 모든 단계**를 역순으로 돈다(실패 · 건너뜀 · 닿지 못한 단계 포함)라 본문은 null 안전이어야 하고,
  백엔드 교체는 `shutdownDependentsOf( RHI )` · `restartStoppedSteps()` 로 같은 initialize 본문을 다시 돌리므로 본문은 다시 설 수 있어야 한다(객체가 있으면 다시 쓰고
  디바이스 설정은 매번 건다 — 교체 뒤 `setMergeBatchesAcrossMaterials` 가 옛 디바이스 값으로 남던 결함이 이것). 로거 · 명령줄 · 크래시 핸들러는 두 호스트가 `EngineBootstrap`
  하나를 쓴다(로거 스레드는 메모리 프로파일러보다 먼저, 로거 객체는 맨 마지막 — 해제 중의 진단이 남는다). 서비스 표 칸은 낱말(`Required/Optional` ·
  `GameVisible/HostOnly` · `EngineCreated/HostCreated`, RuntimeAPI `ServiceListColumns.h`), `destroyAll` 순서는 `makeDestroyOrder()` 로 시험한다.

- **리로드 거절 사유는 옛 이미지를 내리기 전에 본다** — `LiveReloadManager::setOnValidateImage`(ABI · API 표)와 배치 콜백(`OnBeforeCommitBatchDelegate` 가 false
  면 아무것도 내리지 않음, 게임 상태 찍기 실패 포함)이 적용 전 실패를 막아 옛 모듈이 계속 돈다. 적용 뒤 결함만 `markGraphBroken`(UE Live Coding 과 같다).

- **모듈 코드를 쥘 수 있는 등록부는 `IModuleUnloadListener` 를 상속해 스스로 등록한다**(`releaseModuleCode` 에 손 목록을 다시 만들지 말 것). 보유자 객체는
  엔진(또는 App) 코드가 만들고 생성자를 .cpp 에 둔다 — 모듈 안에서 만든 보유자가 모듈보다 오래 살면 훑기가 내려간 vtable 로 뛴다.

- **에셋 핫 리로드의 경계: 임포트 · 감시 · 씬 알림은 에디터, 런타임 파일의 제자리 다시 읽기는 엔진 캐시.** `AssetHotReload` 에 종류별 코드를 넣지
  말 것 — 새 종류는 엔진에 `IAssetCache` 등록 + `EditorAssetTypeRegistry` 줄의 `_pCacheKindName`(· 임포트하는 종류는 `_pfnImportSource`).
  컴포넌트 알림은 `AssetHotReload::notifyAssetUsers` 가 `PROPERTY( AssetPath )` 값으로 찾아 `onPropertyChanged` 를 부른다 — 에셋에서 계산한 상태는
  `onPropertyChanged` 가 **값이 같아도** 다시 맞춰야 한다. 리로드 전용 컴포넌트 훅 · `#if !SW_SHIPPING` 가드는 두지 않는다.

- **모듈 리로드는 App 의 것이다.** Engine 에는 `IModuleHandleProvider` 창구만 두고 공개 헤더에 리로드 콜백을 두지 않는다. `LiveReloadManager` 는 `Source/App/Module/`(Shipping 에서 빠진다).
  Shipping 은 모듈을 내리지 않는다. 검증은 SmokeTest.
- **핫 리로드는 섀도 복사본을 올린다.** Windows 는 지연 로드 훅(`DelayLoadNotifyHook.cpp`)이 `GameFramework.dll` import 를 지금 복사본으로 돌린다(빼면 원본이 한 벌 더 올라와 정적 상태가
  둘). 리눅스는 SONAME 을 같은 길이로 제자리에서 고친다(`ModuleImagePatch`). 결속은 `verifyModuleBindings` 가 본다.
- **옛 이미지는 바로 내리지 않는다**(`deferImageUnload`, 배치 4 개, 배치 안에서는 의존하는 쪽부터). 다른 코드가 구독 중인 채널을 만든 이미지는 프로세스 끝까지 올려 둔다(언리얼도 같다 —
  되돌리지 말 것).
- **`ModuleImageUtil::releaseModuleCode` 는 델리게이트 스텁 주소로** 그 이미지가 단 등록을 뗀다. 뗀 것이 있다는 경고는 모듈의 손 정리가 빠졌다는 뜻이고 늘 0 이어야 한다. 시험 함정: 몸통이 같은
  람다는 ICF 가 접어 주소가 겹친다.
- **엔진 ABI 도장**(`Scripts/generate/GenerateEngineAbiStamp.py`, 엔진 헤더 + `.xxx` + RuntimeAPI + GameFramework 의 SHA-1)은 섀도 복사본을 올리기 **전에** 파일 바이트에서 대조한다. 주석만
  바꿔도 바뀌는 것은 의도다. 도장 없는 모듈은 거절한다. RHI 모듈은 `kRHIModuleAbiVersion` == 도장 `v<N>`(static_assert), GameAPI · EditorAPI 표를 바꾸면 `kModuleAbiVersion` 을 올리고
  App · 모듈을 같이 빌드한다. 컨테이너 인라인 코드는 모듈마다 복사된다 — 내부를 바꾸면 모든 모듈을 다시 빌드한다(새 Engine.dll + 옛 App.exe 는 로그 없이 -1).
- **`ModuleCallGuard` 는 `onAfterReload` 한 호출만 지킨다**(Windows SEH 는 접근 위반 · 잘못된 명령 · 0 나누기만). 잡은 뒤는 온전하지 않다 — 저장하고 재시작할 시간을 버는 장치다.
- **모듈 등록** — GameFramework 는 키트 · SWGame 보다 먼저, 제 이름으로. 팩토리의 모듈은 등록을 모으는 모듈(`_activeModuleName`). 모듈 전역 변수는 `GlobalVariableRegistrar::getHead()` 에
  매달려 commit 에서 모듈 이름으로 오른다(`SW_GVM_MODULE_HEAD` 류를 되살리지 말 것). 게임 모듈 리로드가 실패하면 씬 저장을 막는다(`setSaveBlockReason`). 모듈 언로드는
  `destroyComponentsOfModule` 을 팩토리를 걷기 **전에**, DLL 은 "씬은 사라지고 서비스는 살아 있는" 구간에서만(`setOnScenesReleased`). 모듈 에셋 캐시는 `registerAssetCache` /
  `unregisterAssetCache` 짝(이름은 등록 때 복사). 로드 모듈의 코덱 · 로그 리스너(`releaseListenerCodeWithin`)도 내리기 전에 뗀다.
- **절차 생성물은** `onBeforeStateSerialize` 에서 걷고 `onAfterStateDeserialize` 에서 다시 만든다(스냅샷에 실리면 메시 없는 유령). 리로드 전용 API 를 엔진에 넣지 않는다.
- **서비스 표(`EngineServiceList.xxx`)는 RuntimeAPI(`Service/`)에 있고 Engine 이 include 한다** — 서비스 id 가 호스트 ↔ 모듈 계약이라서다. 타입 이름은 전방 선언만 만든다. RuntimeAPI 는 Engine · App 헤더를 include 하지 않는다(`CheckEngineLayers`). 표를 id 표와 바인딩 표로 나누지 않는다(목록 둘이 된다).
- **엔진 서비스는 `EngineServiceList.xxx` 의 `owned` 열에서** `EngineServiceCollection::createAll()` / `bindInto()` 로 생성된다(호스트가 먼저 만든 것은 덮지 않는다, 정의는 `.cpp`, 자리는
  `Source/Engine/` — `Common` 이면 `CheckEngineLayers` 가 막는다). 호스트 대조는 `CheckEngineServiceBinding`. 시험의 서비스 흔들기 창구는 `test::rebindEngineServices` 하나.
  `EngineServiceTest` 의 기대값도 같은 X-매크로라 `gameAllowed` 값 자체가 틀린 것은 못 잡는다.
- **Engine 폴더 include 그래프는 DAG 다**(`RunEngineLayerGraph.py`, 다시 제안하지 말 목록은 `docs/07_EngineStructureVsCommercial.md` 4절). 일부러 그 층에 둔 것: 핸들 · `TagID` 는 Core,
  `CommandStack` 은 `EngineLoop` 소유(핫 리로드를 넘어 산다), `TileMapXml.h` 는 Engine. 엔진 창은 `WindowResizeEvent` 를 발행하지 않는다(델리게이트). 상태가 살아남아야 하면 Engine · App 에 둔다.
- **기동 순서**: 로거 · 크래시 핸들러 → `ResourceUtil::initialize()`(로거 뒤라야 진단이 남는다) → 설정 → `AssetManager::initialize()` + `mountContent`. 종료는 `_rhi->shutdown()` 이
  `AssetManager::shutdown` 보다 먼저, 오디오는 TaskManager 보다 먼저(`_voiceMutex` 로 `_bInitialized` 를 먼저 내린다), 로거를 세운 뒤 `MemoryProfiler`. 시험 호스트도 앱과 같은 순서
  (리플렉션 등록 → 설정 → AssetManager).

### 3-10. Core · 태스크 · 메모리

- **순서만 채널(`UnreliableSequenced`)은 메시지 종류(첫 바이트)마다 흐름이다** — 예전엔 연결 전체가 흐름 하나라 한 보내기 간격의 다른 종류 메시지가 서로 지웠다.
  같은 종류로 여러 조각(오브젝트 · 부분)을 보내는 것은 여전히 서로 지우므로 비신뢰 + 받는 쪽 틱 정렬로 보낸다(`DestructionReplication` 의 자세).
- **도는 덩어리는 질량 중심으로 보간한다** — 그룹 원점(오브젝트 원점)은 덩어리에서 수 미터 떨어질 수 있어 원점을 직선으로 이으면 오차가 1 m 를 넘는다(p99 0.44 → 0.07 m).
- **`quaternion::inverse()` · `conjugate()` 는 const 가 아닌 값에서 제자리 버전(void)이 골라진다** — 식 안에서는 const 참조로 받아 부를 것(`RigIkSolver::makeInverse`).
  **`quaternion::fromToRotation` 은 코사인 차 1e-6(약 0.08°) 안쪽을 단위 회전으로 버린다** — 반복 IK 의 마지막 몇 mm 가 그 안이라 CCD 가 멈춘다(`RigIkSolver::makeFromToRotation`).

- **Debug 기동은 CRT 누수 보고를 stderr 로도 낸다**(`EngineBootstrap` 의 진단 갈래가 `MemoryProfiler::enableMemoryLeakChecks` 를 부른다 — 누수 덤프가 콘솔 · CI 로그에
  나온다, `MemoryTagTest.DiagnosticBootstrapEnablesPlatformLeakChecks`).
- **Windows UDP 는 돈다**(2026-10-05, `NetworkTest.UdpTransportSendsDatagramsOverLocalhost` · `NetworkThreadTest.UdpHostsRunOnThreadsOverLocalhost`, 닫힌 포트로 보낸 뒤
  받기 포함). `SIO_UDP_CONNRESET` 끄기와 받기 고리의 "오류는 건너뛰고 다음 것" 을 둘 다 빼도 시험은 통과한다 — 루프백 ICMP 리셋을 이 시험이 재현하지 못하니 두 방어를 지우지 말 것.
- **회선 나쁨은 `NetEmulationTransport` 하나다**(N17) — 루프백 망은 보낸 순서대로 다음 `update` 에 배달만 한다. 흉내는 보내는 쪽 줄이라 호스트를 한 스레드에서
  차례로 돌리는 시험은 프레임마다 흉내를 **모두 먼저** `update` 한다(아니면 뒤에 도는 호스트가 보낸 것이 한 프레임 늦다 — `NetSimHarness` 와 시험 도우미가 그렇게 한다).
  깨짐 난수는 깨짐을 켰을 때만 뽑아 다른 조건의 수열을 바꾸지 않는다(하니스 · 파괴 시험이 바이트까지 그대로인 이유).
- **큰 신뢰 메시지는 Core 가 조각으로 나른다(64 KB, N20) — 그래도 조각마다 창 한 칸이다.** 메시지를 묶어도 창 몫(바이트 ÷ 1 KB)은 줄지 않으므로, 나누는 까닭이
  창(앞부분이라도 나가게)이면 키트의 쪼개기를 남긴다(MMO 나감 · 턴 대기 줄). 묶어서 이득은 엔티티마다의 머리 · 창 칸이 작은 것이 많을 때다(MMO 들어옴).
- **조각이 패킷을 채우면 비신뢰가 굶는다** — 1 KB 조각 뒤에는 150 B 남짓이라 600 B 스냅샷이 못 들어간다. `NetConnection::writePacket` 은 남긴 쪽부터 번갈아 싣는다.
- **대역폭 상한(N21a)은 빚 모양 토큰 버킷이다** — 몫이 0 보다 크면 꽉 찬 패킷 하나, 쌓는 몫은 보내기 간격 두 번어치. 다 써도 머리(확인 · 유지)는 가므로 타임아웃보다 오래 포화돼도
  끊기지 않는다. 상한 없이 "한 차례에 여럿" 만 넣으면 회선을 넘친다 — 둘은 같이 간다. 키트 예산은 `NetSendBudget::computeTickBudget`(설정과 상한 × 틱 × 0.5 중 작은 것).
- **신뢰 · 순서 없음으로 옮긴 메시지는 같은 채널의 다른 메시지와의 앞뒤도 잃는다(N21b).** 파괴 사건은 번호로 서로 줄 세웠지만 "청한 스냅숏보다 뒤 사건은 늦게 온다" 는
  순서 채널이 주던 것이었다 — 스냅숏을 기다리는 동안 사건을 쌓지 않으면 스냅숏이 앞서 적용한 사건을 덮어 영영 빠진다. 채널을 바꿀 때는 받는 쪽이 기대던 앞뒤를 모두 적는다.
- **신뢰 재전송 간격을 짧게 고정하면 꼬리는 줄지만 회선을 먹는다**(2026-10-05 측정). 0.1 초 고정은 250 ms · 15 % 에서 사건 최대 지연 1.0 초였지만 메시지당
  재전송 3.5 번 · 서버 올림 3 배로 파괴 시험(덩어리 오차)이 졌다. RTT + 50 ms 와 빠른 재전송(뒤 패킷 셋 확인)이 재전송 1.9 배 · 올림 +9 % 로 4.1 → 1.3 초다.
- **복제 예산은 메시지 전체를 정확히 센다**(`NetSendBudget`) — 1024 B 를 넘는 메시지는 `sendMessage` 가 버리고 확인이 안 와 기준이 그대로라, 다음 틱도 같은
  크기로 다시 버려지는 라이브락이 된다(사라진 id 400 개 = 1200 B 에서 클라이언트 틱이 멈췄다). 길이는 `writeBlob` / `readBlob( 상한 )` 하나로 — 자르는 쪽과 쓰는 쪽이 어긋났다.
- **비신뢰 입력을 "최근 N 개" 만 겹쳐 보내면 N 을 넘는 연속 손실이 영구 빈틈이 된다** — 롤백은 30 틱 끊김 뒤 두 쪽이 120 프레임에서 영원히 멈췄다. 상대가 확인한
  다음 틱부터 보낸다(GGPO) — Core `NetInputSendWindow` 가 구조로 그렇게 하고, 예산이 모자라면 **오래된 것부터** 싣는다(새 것부터 실으면 못 실은 옛 것이 같은
  빈틈이 된다). 받는 고리(`NetInputReceiveBuffer`)는 아래 · 위 창을 둘 다 둔다 — 위 창이 없으면 고리 한 바퀴 뒤의 먼 틱이 받아 둔 입력을 덮는다.
  권위 서버 입력도 같은 부품이다 — 확인은 스냅숏의 `_firstMissingInputTick`, 서버가 꺼낸 틱은 받는 창 아래로 놓아 확인이 넘어간다(`InputBurstLossLeavesNoGap`).
- **틱 · 프레임으로 찾는 고리는 `TickRingBuffer` 하나다**(예측 · 스냅숏 · 보낸 재구성 · 랙 보정 · 롤백 기록 · 락스텝 입력 · 체크섬 · 파괴 최근 해시). 키 전체를 적어
  감김 · 건너뛴 칸 비우기가 없고, 무엇이 낡았나는 쓰는 쪽이 넣기 전에 본다 — 창 너비를 고리 크기로 두면 창 안끼리 덮지 않는다(락스텝 입력 256 · 체크섬 512).
  `acquire` 는 옛 값을 비우지 않는다(버퍼를 다시 쓴다 — 처음 넣는 틱이면 쓰는 쪽이 비운다). 16 비트로 감기는 패킷 시퀀스는 `SequenceBuffer`.
- **렌더 지연 규칙은 `NetClock` 하나다 — max( 최소 지연, 표본 간격 × 2 )**(유니티 Netcode for Entities 기본 2 틱과 같은 수). 한 간격뿐이면 표본 하나를 잃을 때마다
  보간할 뒤 표본이 없어 멈춰 선다(파괴 덩어리가 미터 단위로 어긋났다). 따르는 방식은 둘 — 복제 클라이언트는 Smooth(가장 새 스냅숏 − 지연을 초당 ±10 % 로
  따라가고 지연 × 4 를 넘으면 바로), 파괴는 Monotonic(추정을 흐르는 시간으로 밀고 뒤로 가지 않는다 — 멈춘 덩어리가 많아 자세가 끊긴다). 하나로 맞추는 것은
  행동이 바뀌는 일이라 수치(덩어리 오차 · 보간 단조 · 지연)로 정한다. 구간 규칙(앞 이하 · 뒤 초과 · 끝이면 멈춤 — 외삽 안 함)은 `InterpolationBuffer` ·
  `NetInterpolationUtil::computeAlpha` 하나.
- **확인만 담은 패킷이 확인을 부르면 한가한 연결이 30 Hz 로 핑퐁한다**(`NetConnection` 확인 요청 비트의 이유). 요청을 끄면 거꾸로 두 쪽 유지 시각이 맞물려 한쪽은
  늘 답만 보내 RTT 표본이 0 이 된다 — 그래서 답이라도 마지막 요청에서 유지 간격이 지나면 요청한다. RTT 는 "요청 패킷이 가장 새 확인으로" 돌아올 때만 잰다(묶음으로 늦게
  확인된 것은 상대가 기다렸다 보낸 시간이 섞인다).
- **상한은 쓰는 쪽도 본다** — 받는 쪽만 상한을 보면 넘는 메시지가 보낸 쪽에선 "보냈다", 받는 쪽에선 깨짐으로 조용히 사라진다(턴 행동 900 B) · 락스텝은 보내기가 실패해도
  내 입력을 이미 예약해 나만 진행했다. 상한 상수는 메시지 구조체에 하나(`NetTurnRelayMessage::kMaxActionBytes` · `NetLockstepMessage::kMaxInputBytes`)를 두 쪽이 같이 쓴다.

- **보고의 "(sw 할당자 밖)" 은 CRT 합 − 태그 합**이라 프로파일러보다 먼저 잡힌 sw 블록도 들어간다 — MemoryProfiler 는 부트스트랩 맨 앞에서 선다. 새 스레드는 Unknown
  에서 시작하므로 띄운 쪽의 태그를 인자로 넘겨 첫 줄에서 건다. 배열은 `sw_new_array` · `make_unique<T[]>`(맨 `new` 는 `Style/RawNew` 가 막는다). 외부 라이브러리는
  공개 설정 지점으로만 sw 할당자에 잇는다(pugixml `set_memory_management_functions`, nlohmann 할당자 인자, zlib zalloc · zstd advanced · LZ4 extState, stb STBI_*).

- **메모리 태그(UE LLM 식)는 배포본이 아닌 구성 모두에 있고 추적은 Debug · 시험만 켜져 있다**(Release 는 `-gv_memoryTracking=1`). 거는 자리는 셋 — 기동 단계 표(`EngineInitStepList.xxx`)의 태그 칸, 서비스 생성의 `kServiceMemoryTag<Type>`,
  하위 시스템 진입점의 `SW_MEMORY_SCOPE`. 태스크 · 병렬 청크는 **만든 쪽의 태그를 상속**한다(`TaskNode` · `ParallelGroup` 의 패딩 자리, 크기 그대로). 분포와 sw 할당자 밖
  몫은 `-gv_profileFrames` 보고의 "memory by tag" 와 ProfilerPanel 에서 본다 — Unknown 이 커지면 진입점이 빠진 것이다. ImGui 는 `SetAllocatorFunctions` 로 sw 할당자를
  지나므로 에디터 실행의 alloc/frame 에 ImGui 할당이 들어간다. GPU 메모리는 대상이 아니다(CPU 힙만).

- **프로세스 정적 캐시(`ShaderReflectionLibrary` 매니페스트 같은 것)는 엔진 종료 단계가 비운다** — 안 비우면 기동 뒤에 채운 몫이 종료 누수 검사(기준선 대비 바이트 ·
  블록 수)에 남는다(백엔드 교체 뒤 ~1.1 MB). 진단은 MemoryProfiler 세부 추적을 켜고 `destroyAll` 뒤 `getTopCallStacks( LiveBytes )`. 교체 전 백엔드의 매니페스트는
  종료까지 상주한다(상한 4 개라 둔다). 모듈 인스턴스 내리기는 에디터 · 게임 모두 타입을 걷은 **뒤** 서비스를 뗀다(`ModuleHostInternal::destroyInstance`).
  프로세스 정적 저장소(이름 풀 · 트랜스폼 페이지 · 경로 캐시)는 `EngineBootstrap::shutdown` 이 놓는다 — 컨테이너의 `clear()` 는 버킷 · 밀집 배열 · 용량을 남기므로 타입을 적은 빈 객체를 대입한다(`= {}` 는 initializer_list 대입이 골라져 남는다). 이름 풀 블록은 넣는 쪽 태그가 아니라 `EngineMisc` 로 센다. 종료 보고 0 은 `AppSmokeTest.ShutdownReturnsEveryTagToTheBaseline` 이 지킨다.

- **STL 구성(`SW_ENABLE_STL_CONTAINER=ON`, CI `CI-Debug-STL`)은 C++17 이라 std 해시 컨테이너에 이종 조회 · `contains` 가 없다** — sw 쪽 얇은 클래스가 메운다.
  커스텀 컨테이너 전용 시험은 그 구성에서 건너뛴다.

- **완료를 모으는 줄에 고정 용량 큐(`ConcurrentQueue`)를 쓰지 말 것** — 가득 차면 `enqueue` 가 false 를 돌려주고 그 완료는 말없이 사라진다. 에셋 스트리밍 큐가 그랬다(한 경로에 편승한
  콜백 1024 개 초과분 유실, `AssetStreamingTest.ManyCallbacksOnOnePathAreAllDelivered`). 상한 없는 잠금 + deque 로 둔다.
- **비동기 IO 의 완료 콜백은 IO 스레드가 아니라 태스크 워커에서 돈다(엔진 설정)** — 팩 해제 · CRC 가 IO 스레드를 막지 않게. 그래서 `AsyncFileIo::shutdown` 은 Task 보다 먼저(기동 단계
  `FileIo` 가 Task · ModuleImages 에 의존), 콜백 안에서 자기 큐의 잠금을 쥔 채 IO 를 걸지 말 것(내린 뒤 요청은 그 스레드에서 바로 완료된다 — `AssetStreamingQueue::issueDataRead`).
- **스트리밍 I/O 를 `TaskPriority::High` 에 싣지 말 것** — 그 줄은 병렬 그룹의 청크 사이에서도 비우므로 파일 읽기가 프레임 일을 막는다(High · Immediate → Normal,
  Low · Normal → 백그라운드).

- **TaskManager 약속** — 워커는 High 전역 큐 → 자기 덱 → Normal 전역 → 훔치기 → Low 순으로 본다(RT 가 곧바로 기다리는 기록 태스크는 High). 깨우기는 넣은 만큼만 — 묶음은
  `submitWithoutWake` 후 끝에 `wakeSleepingWorkers( n )` 한 번(세대도 올리지 않으므로 반드시), `notify_all` 은 2 배 손해, 모두 깨우기는 처음 읽은 유휴 마스크 안에서만(다시 잠든 워커를 쫓으면
  WSL 에서 1~90 초). `runParallel` 의 둘째 인자는 문턱이지 청크 크기가 아니다. `addTask` 는 제출 **전에**. `clear()` 는 아무것도 돌지 않을 때만(시험 전용 — 활성 수가 0xFFFFFFFF 로 감긴다),
  `_activeTaskCount` 감소는 스테이지 통지 **앞**. 이름으로 찾는 스테이지는 없다. 병렬 본문 스코프는 "현재 태스크" 를 바꾸기 **전에** 만든다.
- **`waitStage` · `waitAll` · `runParallel` 의 `tryHelpAndExecute` 는 렌더 · 로더 스레드도 지나며 남의 잡을 실행한다.** 스크래치는 `getCurrentThreadScratchSlot()` · `getScratchSlotCount()`
  (워커 + 도우미 8). 렌더 스레드는 끝날 때 `releaseCurrentThreadHelperSlot`. 멈춤 표시는 대기 쪽과 같은 락 안에서 세운다(밖에서 세우면 알림을 잃어 `join` 이 멈춘다). 자기 락을 쥔 채
  콜백을 부르지 않는다. 기다림은 횟수가 아니라 시간으로 끊는다(yield 1024 번 상한이 느린 CI 에서 프로세스를 죽였다).
- **`TaskHandle` 은 refcount 라 `const&` 로 넘긴다.** 스케줄러 시험의 모든 대기에는 타임아웃을. 프레임마다 도는 태스크는 메서드 델리게이트로(인라인 인자 칸은 노드를 키운다).
  `TaskFuture::then()` · 콤비네이터는 무효 입력에 무효를 돌려준다. "락 안에서 완료 표시, 알림 · 이어받기는 락 밖" 은 `SharedFutureSignal` 한 벌.
- **레이스 탐지기(Debug)** — 워커의 비-const `sw::vector::operator[]` 를 쓰기로 잡는다: `std::as_const(v).data()` 나 const 참조로 읽는다. `sw::array` 를 락-프리 버퍼에 쓰지 말 것(드물게
  Fatal). 데드락 탐지기를 피하려면 실패 기록 같은 곳은 `std::mutex`. 잠금은 `std::scoped_lock`. `compare_exchange_weak` 은 실패 순서도 명시한다(기본 `seq_cst`).
- **`SW_ASSERT` 는 Release · Shipping 에서 사라지고 `SW_LOG_ASSERT` 는 Debug 에서 `SW_DEBUG_BREAK` 까지 한다**(디버거 없는 CI 에서는 프로세스가 죽는다 — 방어 경로 시험은 Release ·
  Shipping 에서). 배포 구성의 `SW_LOG_ASSERT` 는 진행하므로 뒤가 앞에 달린 전제는 하드 단언으로.
- **크래시** — 엔진이 만드는 스레드는 시작할 때 `CrashHandler::initializeCurrentThread()`(빠뜨리면 스택 오버플로 덤프가 0 바이트). Windows 덤프는 보고 스레드가 `PssCaptureSnapshot` 으로
  쓴다(살아 있는 자기 프로세스를 `MiniDumpWriteDump` 하면 로더 락에 멈춘다 — 재현은 자식 12 개 × 400 회). 크래시 경로 로그는 `Logger::flushGlobalForCrash`(락을 못 잡으면 포기).
  시한은 `setReportDeadline`(20 초), POSIX 는 `alarm` + SIGALRM, `backtrace()` 예열. 실물 확인 `-gv_crashTest=1..5`. 크래시 보고 본문은 `CrashContext.cpp` 의 `writeCrashReport`.
  묶음 · 올리기는 다음 실행의 `CrashReportService`(Engine/Telemetry). 보고 프로세스는 `setReporterExecutable` 을 정한 호스트(App)만 띄운다 — 경로를
  `getExecutablePath` 로 잡았더니 시험 실행 파일이 `--crash-reporter=` 를 모른 채 자기를 끝없이 다시 띄웠다(실측).
- **프로세스** — 자식 상속은 Windows `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`, POSIX CLOEXEC + `close_range`(아니면 동시에 띄운 자식이 서로의 파이프를 문다). 띄우기만 할 때는 `Process::launchDetached`
  (`execute` 는 UI 스레드를 세운다, `explorer.exe /select,` 는 성공해도 1). POSIX: `isRunning` 은 `waitid(..., WNOWAIT)`, `terminate` 는 그룹째, pid 는 한 번만 읽는다(0 이면 `kill(-0)`).
- **파일** — 쓰기는 원자적이다(`writeAtomically` → 같은 폴더 임시 파일 → `replaceFile`, Windows 는 `FileRenameInfoEx` POSIX 의미). Windows 읽기는 Win32(`readRange`) — `fopen_s` 는 ANSI 라
  한글 경로가 깨진다. 실행 파일마다 `WindowsProcess.manifest`(UTF-8 코드페이지 · longPath · PerMonitorV2)를 `sw_embedProcessManifest` 로 박는다(새 exe 에 빠뜨리면 한글 경로를 못 읽는다).
  `getFileTimestamp` 는 초 단위, 셰이더 소스 캐시는 (크기, 시각) `getFileStamp`. 워처가 알림을 잃으면 빈 `_filename` 의 `Modified`(리스캔 신호) — `expandRescanEvents`.
- **문자열** — `formatstring` 은 `string_view` 를 길이로 쓴다(`.data()` 로 풀어 넘기면 뷰 끝을 지나 읽는다 — `CheckLogViewArgument`). `%#` 은 순수 자리표, 모르는 `%…` 는 리터럴, 인자 수 불일치는
  Debug 실행 단언(정본은 `FormatString` 클래스 주석). `setlocale` 이 없다(C 로캘) — 잘못된 UTF-8 은 `escapeInvalidUtf8`. `fixed_string` 은 넘치면 글자 경계에서 자르고 경고한다.
  `StringUtil` 은 비-ASCII 바이트를 `uint8` 로 넓힌다(utf16 에 같은 치환 금지), `stristr` 은 바이트 묶음 "최적화" 금지. Win32 변환은 `utf8ToUtf16`(`ImmGetCompositionStringW` 반환은 바이트 수).
- **메모리 · 컨테이너** — 과정렬 판정은 `kUsesAlignedAllocation<T>` 하나(`max_align_t` 는 MSVC 에서 8 이라 힙 계열이 갈린다). `sw::vector` 는 `is_bitwise_copyable_v` 타입을 `Memory::copy`
  한 번으로(ReflectionParser 도 쓴다). `sw_delete_array` 는 원소 소멸자를 부르지 않는다. 해시 버킷 수는 2 의 거듭제곱, 번호는 `bucketIndexOf` 하나(스무 자리가 쓴다 — 한 곳만 달라도
  원소가 사라진다). 락 없는 읽기 + 주소 안정은 `PagedArray`. `SlotHandleTable` 은 점유 + 세대를 한 워드 `_state` 에 든다(둘로 나누지 말 것 — arm64). 순서 없는 삭제는
  `VectorUtil::removeAtSwap`, 표준에 없는 함수를 `vector.h` 에 붙이지 않는다. 함수 인자 연속 뷰 이름은 `vector_reference<const T>`.
- **락을 잡은 getter 는 자기 멤버의 뷰 · 참조를 돌려주면 안 된다**(값으로). 콜백을 부르는 순회는 인덱스로 돌고 부르기 전에 델리게이트를 복사한다. `MulticastDelegate` 는 복사 · 이동 넷을 직접
  적는다. `Delegate` 의 인라인 람다를 옮기면 원본 소멸자를 부른다.
- **`EventDispatcher`** — 큐(`push`)는 아무 스레드나, 버스(`subscribe` · `publish`)는 `processEvents` 를 부르는 스레드만. 큐에 남은 이벤트는 `destroyQueuedEvents()`.
- **명령줄** — 미등록 `gv_` 키는 `_mapPendingGlobal` 에 남았다가 모듈이 변수를 올릴 때 적용된다. gv_ 선언은 읽는 파일에, `extern` 재선언 금지(`SW_EXTERN_GLOBAL_VARIABLE` 은 정의 파일 밖에서
  읽을 때만 — `CheckGlobalVariableKinds`). 시험용 전역 변수는 `SW_TEST_GLOBAL_VARIABLE`(Shipping 에서 빠진다), 배포본을 스크립트가 조종할 것만 `SW_TEST_GLOBAL_VARIABLE_SHIPPED`. bool 이 아닌 `-gv_*` 에
  값을 빠뜨리면 경고 + 기본값.
- **싱글턴을 옮기지 않는 자리**: `CrashContextStore`(시그널 핸들러가 읽는다), 등록자 헤드 · `TestRegistry`(main 이전 자기 등록), `TagID` · `hashed_string` 인터닝(프로세스 전역이어야 뜻이 있다).
  Core 에 인스턴스가 필요하면 Logger 모양 — 인스턴스는 `EngineLoop`, Core 에는 포인터 슬롯.

### 3-11. 입력 · 오디오 · 게임프레임워크

- **로컬라이제이션의 정본은 원문 표(`*.strings.json`)이고 번역 표(`<culture>.translation.json`)는 번역할 때의 원문 해시를 든다** — 해시가 다르면(원문이
  바뀌었으면) 그 번역은 화면에 나오지 않는다. 표 파일은 프로젝트(`*.locproject.json`)가 이름으로 부른다(폴더를 훑지 않아 팩 안에서도 같다). 코드 · 데이터의 글을 고치면
  `App --gather-text` 결과(원문 표 · 번역 표 · `tm/`)를 같이 커밋한다 — `TextGathererTest.RepositoryProjectsAreUpToDate` 가 막는다. 셸 InputMap 을 못 읽으면
  오류를 알리고 빈 맵이다(손 바인딩으로 바꿔 끼우지 않는다). 입력 리플레이 파일은 `RawInputEvent` 를 통째로 적으므로 배치가 바뀌면 `kReplayVersion` 을 올린다(지금 3).

- **통합 `InputMap` 은 `InputManager::beginFrame` 이 갱신한다** — 게임 코드가 `update()` 를 다시 부르면 한 프레임에 두 번 흐른다(Input README 예제가 그랬다).

- **마우스 `getSmoothDelta` 는 프레임당 한 번 `IInputDevice::onEventsDispatched( dt )` 에서 정해진다** — `setSmoothing(f)` 는 1/60 초 동안 남기는 비율
  (τ = -(1/60)/ln f, 60 Hz 에서 옛 계수와 같다). 이벤트 처리기 안에서 스무딩을 다시 돌리면 폴링 레이트마다 감각이 달라진다. 프레임 이동은 `getMovementDelta()` 하나.
- **2D 콜라이더 판정은 `overlapsBounds`(순수 기하)와 `isTouching`(레이어 반영) 둘** — 둘 다 바디 등록 여부와 무관하게 같은 답(Unity `Bounds.Intersects` · `IsTouching`).
- **일정의 "일찍 나서기" 는 앞 칸의 끝을 빌린다** — 앞 칸의 우선순위가 더 높으면(약속 · 축제) 빌리지 않는다. 빌리면 점심 약속 중에 일하러 나서 약속 출석
  판정이 깨진다. 화면 밖 일정이 화면 안과 같으려면 판정 자리(계획 출발점 · 끼어든 자리)를 LOD 와 상관없는 계획 경로로 잰다(`AI/Schedule/README.md`).
- **엔진 · 킷 컴포넌트는 태그를 붙이지 않는다** — 종류는 `GameObjectManager::forEachComponentOfType<T>` 로 찾는다(UE `GetAllActorsOfClass`). 프레임마다 쓰는
  소비자가 생기면 타입별 등록부(O(해당 타입))를 Release 로 재고 정한다. 글자 입력은 `InputManager::setTextInputCallback` 하나(UE `OnKeyChar`).

- **도구 에셋 종류는 표 하나** — 대화 노드는 `kArrDialogueNodeInfo` 한 줄 + 러너 switch 의 case 하나(`-Wswitch-enum` 이 짚음), 다음 노드는 러너 · 에디터 미리보기가
  같이 쓰는 `DialogueCursor::step`. 핀 번호 `nodeId*100+offset` 은 디스크 포맷. 타일맵 레이어 표(`kArrTileFlagLayerInfo`)의 XML 속성 이름과 줄 순서는 파일 형식이다
  (바꾸면 옛 맵의 그 레이어가 기본값으로 읽힌다 — `TileMapXmlTest.SavedBytesMatchTheExistingFormat`). 레이어를 더할 때 손댈 곳은 `TileFlagLayer` 값과
  이 표 한 줄뿐이다(Overworld `TileMap` 은 `isFlagSet( layer )` 로 묻는다). 맵은 조우가 **일어나는 칸**만 말하고 무엇을 만나는지는 장르 키트의 지역 표
  (`MonsterCollectorCatalog::rollEncounter` · `JrpgEncounterWalker`, 지역 = 존 id · 태그)가 정한다. `SequenceItemKind` 값은 JSON 정수라 번호를 바꾸지 말 것;
  시퀀서 이벤트는 `SequencePlayerComponent::registerSequenceEvent` 로 받는다.
- **월드 플래그는 `GameFlags` 하나다** — 대화 러너(`DialogueRunnerComponent::setFlags`) · 지역 잠금(`AreaGraph`) · 일정이 같은 저장소 · 같은 조건식을 쓴다
  (`a && !b || count>=3` — 이름 하나는 0 이 아니면 참, 비교 오른쪽은 정수나 다른 플래그, 접두어 없음). 0 을 넣으면 지운다. 세이브는 `fillEntries` 의 이름 순 목록이다.

- **`GameEvents.h` 의 이벤트는 프레임워크가 그 자리에서 낸다**(세이브 · 로드 완료 = `GameInstanceBase::save/loadStateToFile`, 레벨 로드 요청 · 완료 =
  `requestFirstScene` · `requestEntranceScene`). `SceneManager` 를 직접 부른 로드는 LevelLoad 이벤트를 내지 않는다.
  낼 자리가 없는 이벤트는 두지 않는다.
- **스프라이트 클립 키(`transformKeys`)는 클립 타임라인의 초이고 루트(primary) 스프라이트에는 적용하지 않는다**(경고) — 움직일 스프라이트는 루트 아래에.

- **입력** — 창 메시지는 큐에만 넣고 장치 상태를 바꾸는 길은 `beginFrame` 의 재생 하나다(포커스 · 포인터 진입도 큐 순서 안). `RawInputEventType` 은 뒤에만 덧붙인다(리플레이 파일이 번호를
  담는다). 입력 시험은 메시지 → `beginFrame` → 조회 → `endFrame`. XInput 트리거도 `setAxis( 4 · 5 )` 로 넣어야 데드존이 먹는다. 리바인딩은 바인딩 종류를 지킨다(`getRebindSlotIndex`),
  바인딩 종류는 `kArrBindingKindInfo` 표 하나(+ `static_assert`, 저장소는 `-Wswitch-default`). 통합 InputMap 은 `InputManager::beginFrame` 이 갱신한다.
  입력 XML 의 액션 `trigger` 는 단일 키 · 조합 · 축 합성(`<axis1d>`, 적지 않으면 `Down`)에 간다 — 연속 값(`vector2d` · `stick` · `mouseDelta`)은 `Down` 고정이라
  다른 값은 로드 경고. 유저 바인딩 저장도 `trigger` 를 싣는다(빼면 다시 읽을 때 종류의 기본값으로 돌아가 Shooter3D 무기가 누르는 동안 매 프레임 바뀌었다).
- **오디오** — 믹스는 전부 `AudioEngine`(플랫폼 무관)이 하고 백엔드는 출력 장치만 연다(XAudio2 는 스트리밍 보이스 하나). 장치가 없으면 `IAudioSystem::update` 가
  흐른 시간만큼 렌더한다. 볼륨 · 음소거는 같은 이름 버스의 사용자 볼륨, 음소거는 master 한 곳. 소리 동작은 `AudioEngine::render` 로 버퍼에 렌더해 숫자로 잰다(`Audio/README.md`).
- **반복 간격(연사 · 스폰 · 자동 공격)은 끝난 걸음에 `Countdown::restart`** — 간격으로 덮으면(`start` · `= 간격`) 지나친 몫을 버려 빈도가 fps · 고정 걸음에
  매이고, float 로 걸음을 빼면 0 에 조금 못 미쳐 한 걸음을 더 기다린다(RTS 0.05 초 걸음에서 1.2 초 → 1.25 초). 잇는 몫은 한 간격까지라 몰아 내지 않는다.
  "원하는 동안 간격마다 한 번" 은 `Countdown::tickRepeat( dt, interval, bWant )` 한 줄이다(Voxel 블록 놓기 · Shooter3D 적 휘두르기).
- **피해 · 월드 UI(킷)** — 피해는 `UnitStatsComponent::applyTakeDamage` 한 자리에서만 깎인다. 방어 식은 `DamageMath::applyArmor`(고정 방어, 최소 1 — 액션 룸의 적도 같은 식)이고, 0 이하 피해는 맞지 않은 것이다(HP · 무적 · 이벤트 없음 — 언리얼 `ApplyDamage`). `DamageAppliedEvent` 는 큐로, 같은 프레임이 필요하면 `registerDamageApplied`. 월드 UI(HP 바 ·
  데미지 숫자)는 저장되지 않는 `SpriteInstanceBatch` 로 그린다 — 자식 컴포넌트로 만들면 씬 · 프리팹 · 스냅샷에 저장돼 다음 시작에 겹친다. 스프라이트 UV · 색은 인스턴스에 싣는다(같은 텍스처는
  한 배치). 체력을 가진 컴포넌트는 `Combat/HealthSourceComponent` 를 상속해 읽기(`getHealthReading` — 지금 · 최대 · 쓰러짐) 하나만 내고, 알림은 `notifyHealthChanged` 한 곳이 비율 · 종류(쓰러짐 포함)를 정해 같은 오브젝트의 `HealthListenerComponent` 에 보낸다 — HP 바는 시작할 때 원천을 읽는다(맞은 뒤 붙여도 맞는 비율). RTTI 가 없어 인터페이스가 아니라 리플렉션 베이스다(`getComponent<HealthSourceComponent>()`). 시뮬레이션 키트(`Vitality` · 정수 HP 배열)는 상속하지 않는다 — 그 유닛에 HP 바를 띄울 게임은 뷰 컴포넌트가 상속해 스냅샷을 읽는다. 보이기 정책은 바의 PROPERTY 다. 확인용 씬 `Resource/game/empty/maps/spriteui.scene.xml`, 글리프 · 클립은 `Scripts/generate/GenerateSpriteTextures.py`.
- **수명이 다하면 지우는 컴포넌트(이펙트 페이드 · 데미지 숫자 · 투사체)는 `LifeSpanUtil` 로 센다** — 흐른 시간은 저장되는 PROPERTY 이고 `onBeginPlay` 에서 0 으로 돌리지
  않는다(되돌리기 · 핫 리로드 때마다 수명을 다시 산다 — 투사체가 그랬다). 끝나는 경계는 `Countdown::tick` 과 같은 "수명 이상", 수명 0 은 지우지 않음.
- **액션 룸의 적은 몬스터 정의다** — 종 id(`grunt` · `boss`)를 게임이 건 `MonsterCatalog` 서비스(`game::bindLocalService`)에서 찾고, 없으면 내장 정의(옛 상수와 같은 값)다. 카탈로그가 걸렸는데 그 id 가 없으면 싸움마다 한 번 경고한다. 사격은 `<Shot angle speed life radius damage/>` 줄마다 한 발(겨냥에서 돌린 각). 방어 식은 유닛 스탯과 같다(`DamageMath::applyArmor`). 룸이 돌려주는 플레이어 피해(`_damageToPlayer`)는 방어 전 값이다 — 게임이 플레이어 `UnitStatsComponent::takeDamage` 로 넣으면 방어가 한 번 빠진다. 룸의 적은 오브젝트가 아니라 `UnitStatsComponent` 를 거치지 않는다. 방 배치는 코드 표(`kArr*Spawn`) — 쓰는 게임이 생기면 맵의 스폰 지점으로.
- **사용자 설정 파일(`usersettings.json`)은 배포된 플레이어 데이터다** — 설정 id · 선택지 이름을 바꾸면 스키마 `version` 을 올리고 `<Upgrade>` 를 더한다(별칭 금지
  규칙의 예외). 화면 변경은 적용기가 요청만 쌓고 App 이 프레임 맨 앞에서 렌더 스레드를 기다린 뒤 한다 — 창 크기는 `App::onResize` 한 길로 스왑체인에 닿는다.
- **GameSettings** 는 `GameInstanceBase::initialize` 가 서비스로 묶는다. 언어 코드는 `LocalizationManager::normalizeLanguageCode` 의 철자 하나. 로컬라이제이션 조회의 `const utf8*` 는 추가 전용
  `LocalizedTextArena` 에 있어 영구 유효하다. 대화 핀 번호(`nodeId * 100 + offset`)는 디스크 포맷이고 주인은 `DialogueGraphAsset` 하나다.
- **어빌리티 시스템** — 다른 오브젝트로 가는 적용 · 이벤트는 `applyGameplayEffectSpecToTarget` · `sendGameplayEventToTarget` 로만(틱 중이면 틱 직후로 미룬다 —
  남의 `applyGameplayEffectSpecToSelf` 를 틱 안에서 직접 부르면 미루지 않는다). 활성 이펙트 · 스펙은 `unique_ptr` 목록이고 콜백 도중 지우기는 표시만 한다
  (`ScopedListLock` 이 풀릴 때 지운다) — 콜백이 목록을 늘리거나 줄여도 도는 포인터가 산다. 게임 모듈의 어빌리티는 컴포넌트의 `IModuleUnloadListener` 가 모듈을
  내리기 전에 거둔다. 카탈로그는 컴포넌트에 박지 말고 게임 서비스로 건다(리로드 뒤 옛 카탈로그를 가리킨다).
- **게임 디렉터는 `GameDirectorComponent` 를 상속한다** — 상태 바이트 보류 · 틱 뒤 플러시 · 대기 소리 · 세운 것 걷기 · 자동 플레이는 베이스에 있고, 게임 인스턴스는
  생성자에서 `registerDirector<T>()` 한 줄로 스냅샷에 올린다(`Source/Games/README.md`). 디렉터의 시뮬레이션은 PROPERTY 가 아니라 `writeState` · `readState` 로만 넘는다.
  뷰 · 컨트롤러를 템플릿 베이스(`DirectorViewComponent<T>`)로 묶지 않는다 — 리플렉션 부모는 등록된 타입이어야 해서 템플릿 중간 층을 둘 수 없다.
- **라운드 묶음은 기반 `Match/RoundSeries` 하나다** — 순위 점수(비면 1 위 1 점 = 선승) · 목표 점수 · 동점 규칙(격투 무승부 · 파티 서든 데스) · 정수 걸음 라운드 시간 · 대기 ·
  상태 바이트. 알림은 내지 않고 결과(`RoundSeriesOutcome` · `RoundSeriesTick`)를 돌려준다 — 키트가 제 이벤트로 낸다. 롤백 상태에 실을 때는 **맨 뒤**에 둔다:
  `readState` 가 맞을 때만 바꾸므로 마지막에 읽으면 키트의 `loadState` 가 통째로 원자적이다. 카트 카운트다운은 라운드가 아니라 한 경기의 출발 대기라 옮기지 않았다
  (그랑프리처럼 여러 경기를 순위 점수로 묶을 때 이것을 쓴다).
- **팀 적대 판정은 `Match/TeamAttitude.h` 하나** — 팀은 판이 매긴 번호(int32, `TeamAttitudeUtil::kNoTeam` = −1 = 누구와도 중립), 적 · 아군은
  `TeamAttitudeUtil::isHostile` · `isFriendly`(언리얼 `ETeamAttitude` 자리, `Combat` 이 아니라 `Match` 인 것은 `MatchState`(층 1)가 쓰기 때문). 키트 팀 enum
  (`ActionTeam` · `ConquestTeam` · `SrpgTeam`)은 상태 바이트에 실리지 않고 역할 이름 · XML 이름 · 페이즈 차례로 쓰여 그대로 둔다. 강타입 `TeamId`(uint8)는
  팀 번호를 배열 첨자로 쓰는 곳이 많고(약 220 줄 · 18 파일) RTS 상태 바이트(int32)를 바꿔서 하지 않았다. 동맹 표 · 팀킬 허용이 생기면 판정기를
  `TeamAttitudeUtil` 에 붙이고 `SrpgBattlefield::isHostile` · `ConquestWorldInternal::isHostile` 도 그쪽으로 옮긴다.
- **턴제 몬스터 전투는 `MonsterCollector` 하나다**(같은 장르의 얇은 `TurnBattle` 키트는 2026-10 에 지웠다 — 레벨 업이 없었고 쓰는 게임이 0 이었다). 전투 연출(단계 타이머 · HUD 한 줄)은 게임 몫이다.
- **`SaveGame::saveToFile` · `loadFromFile` 은 순수 가상이다**(`REFLECT( Abstract )`) — `Archive::serializeObject<T>` 가 정적 타입 `T::StaticType()` 을 쓰므로 기반에서
  `saveGameToSlot( *this )` 를 부르면 `SaveGame` 의 TypeInfo(프로퍼티 0)로 빈 페이로드를 쓰고 성공을 돌려준다. 파생 세이브가 자기 타입으로 부른다(`OverworldSaveGame`).
- **존 역할은 열거가 아니라 맵 `<role>` 의 태그 목록이다**(`ZoneTracker::setFromMap` 이 쉼표 · 공백으로 나눈다, `hashed_string` 이라 대소문자를 가리지 않는다). 클리어 게이트는 `clear_gate` 태그 —
  경로 이름에서 역할을 짐작하지 않는다.
- **2D 근접 질의는 엔진 `SpatialHashGrid2D` 하나** — NetMmo 관심 영역이 쓴다(키는 엔티티 id 를 index 에 담은 `SlotHandle`, 세대 1). `update` 는 덮는 셀이 그대로면
  경계만 바꾸고(PhysicsWorld 와 같은 지름길), 질의 결과는 핸들 순이라 순서가 결과에 실리는 쪽은 스스로 정렬한다. RTS 버킷(걸음마다 다시 짓는 밀집 머리 · 다음 배열,
  결과 순서가 자동 목표 · 채취 · 밀어내기에 실린다)은 옮기지 않는다.
- **칸 격자를 든 클래스는 `GridTopology _topology` 하나를 든다** — `_width` · `_height` 를 따로 두지 않고 칸 번호 · 경계 · 발자국은 `toIndex( x, y )` · `isInside` ·
  `isRectInside` 로만 쓴다(`y × 너비 + x` 손셈 금지). 칸마다 값 저장소 템플릿(`Grid2D<T>`)은 두지 않는다 — 저장소 모양이 키트마다 다르고(칸마다 하나 · 둘 ·
  팀마다 한 벌) 줄어드는 것이 `findTile` 류의 한 줄씩이다.
- **키트 소속은 의존 관계로 판별되지 않는다**(전부 Engine 만 include). 다른 장르도 쓰는 것(HP 바 · 데미지 숫자 · 중력)은 `UI/` · `World/`.
  기반 폴더는 층(DAG)이고 `CheckGameFrameworkLayers` 가 지킨다 — 형식으로 묶은 폴더(옛 `Components/`)는 의존 방향을 숨겨서 두지 않는다. 리플렉션 대상 헤더는 소스와 같은 재귀 규칙으로
  모은다(다르면 새 폴더의 `REFLECT` 타입이 컴파일되고 등록만 안 된다).
- **설정 표의 열쇠는 타입이다**(`ensureConfig<T>( path, generated )`). Shipping 은 디스크의 `Config/` 를 보지 않는다. 고정 스텝 상한은 `EngineConfig::_fixedDeltaTime` · `_maxFixedStepPerFrame`
  (넘친 잔액은 버린다). `ModuleFrameState` 래치 지점이 둘인 것은 의도다(옮기면 에디터 Step 한 칸이 틱 없이 소비된다).
- **리눅스 스플래시** — `XPutImage` 는 1:1 이라 우리가 줄인다, `Expose` 마다 지워지므로 배경 픽스맵, `override_redirect` 창은 XWayland 에서 안 뜬다(EWMH `_NET_WM_WINDOW_TYPE_SPLASH`).
  서버가 "정상" 이어도 화면에 없을 수 있다 — 최종 확인은 사람 눈이다. 창의 `isVisible()`(지금 화면에 있나)과 `isVisibleRequested()`(의도)는 다른 질문이다.
- **데이터 이름 `None` 은 빈 이름이다** — `hashed_string( "None" )` 은 언리얼 `FName` 처럼 `empty()` 다. 고르기 항목 · id 를 `None` 으로 지으면 "이름 없음" 으로
  읽힌다(외형 스키마는 로드 오류로 막는다). 항목은 `Off` · `Bare` 처럼 짓는다.

- **카메라 포즈는 어느 공간 값인지 보고 쓴다** — 대상이 월드(디렉터 · 매니저 · 직교 리그)면 `CameraPoseUtil::applyToCamera`(월드), 대상이 카메라 주인의
  로컬 값(1인칭의 눈 자리)이면 `applyToCameraLocal`. 로컬 값을 월드로 쓰면 부모가 움직여도 카메라 · 손에 든 모델이 원점 근처에 남는다(루트 카메라는 둘이 같아 안 보인다).
- **2D(XY 평면) 따라가기 · 흔들림 카메라는 기반 `Camera/Follow2DCameraComponent`** — 데이터 카메라 디렉터의 모드는 Y 가 위인 땅(XZ) 기준이라 2D 씬을 맡지 못한다.
  흔들림 식은 `CameraImpulse` 하나다.
- **2D 콜라이더 바디는 깊이가 없다(Z 0 한 점)** — 3D 광선 · 상자 질의를 `PhysicsWorld` 에 그대로 던지면 Z 가 0 이 아닌 2D 씬에서 아무것도 맞지 않는다.
  `PhysicsWorldQuery` 는 깊이 없는 바디를 Z 와 상관없이 맞힌다.
- **병렬 틱에서 다른 오브젝트의 상태(센서 피해)를 바로 바꾸면 결정적이지 않다** — 받는 쪽이 이번 틱에 볼지가 스케줄에 달린다(사슬 폭발이 한 프레임에 번지거나 말거나).
  `GimmickDamageUtil` 처럼 틱 뒤(`executeOrDeferPostTick`)로 미루면 늘 다음 틱이다. Windows 헤더는 `near` · `far` 를 빈 매크로로 둔다 — 지역 변수 이름으로 쓰지 말 것.
- **가중치 뽑기에서 0 은 "후보 아님" 이다** — `pickWeightedIndex` 가 −1 이면 아무것도 고르지 않는다(식당 주문 · 손님 도착 · 드롭 · 조우 모두 같다). 실수 가중치(수요 · 배율)는
  `pickWeightedIndex`, 정수 표(조우 · 드롭)는 `pickWeightedIndexInt` — 서로 바꾸면 난수 흐름(`nextFloat` ↔ `nextInt`)이 달라져 같은 씨앗의 결과가 바뀐다.

### 3-12. 기각한 것 — 숫자와 함께 (다시 제안하지 말 것)

- **TaskManager 후보 셋**(Release, 큐브 8000, 16 스레드, 5~8 회 번갈아): 스핀 워커 2 개 제한 + 적응 예산(GT 839→846, RT.ExecutePacket 395→436 us), 워커별 노드
  자유 목록(GT 839→917, RT.Graph 187→237 us), 워커 지정 틱 쓰기 적용(queued 78→80 us, p99 163→212 us). 셋 다 손해 또는 잡음.

- **사용자 결정**: mimalloc · SIMD 수학 · 프리페치는 쓰지 않는다. 사용처가 0 이어도 상용 엔진에 대응이 있는 공개 API 는 남긴다(남긴 기능에는 시험을 붙인다).
- **GpuScene · 렌더**: `DrawCandidate` SoA(구조체를 78 % 키워도 불변, 거의 선형 — 예전의 "28~62 배" 는 GT 가 GPU 를 기다린 착시), 발행 배열 풀(75 vs 76~85 us), 영속 렌더 씬을 **종류** 축으로
  (8 → 1024 에 평평), 인스턴스 채우기 병렬화(모든 크기에서 인라인에 짐), raw 페이로드 워커 쓰기 · `MeshInstanceBatch::updateParallel`, 배치 정렬(잡음), DX12 루트 상수 드로우 ID 주입
  (ExecuteIndirect 2 배 느림 — 인스턴스 슬롯 스트림이 대안), 클리어 `DontCare`(0 us — 타일 GPU 로 가면 다시), 점 샘플러(122 → 121), 머티리얼 CB 워커화(6 us), PSO 병렬 생성(9 %, 드라이버가 직렬),
  `executeCommandLists` 일괄(리스트 수 그대로), 꼬리 재방출 그룹 캐시(98 → 98).
- **히치**: 펜스 시그널을 Present 앞으로 · 백버퍼 수 · 인라인/즉시 제출 — 분포가 그대로였다. 어댑터 강제 선택은 A/B 로 악화.
- **틱 · 오브젝트**: 인라인 `TickItem`(GameObject 192 → 232 B), 적용 단계를 틱에 합치기(칸당 +124 B), 회전 사원수 캐시(+28 B), 축별 sin/cos 건너뛰기(18.0 → 18.7 ns), 쓰기 정렬의
  `id % 버킷`(150 → 450 us) · 키를 건에 넣기(64 → 80 B), 오브젝트 id 표 2 단 디렉터리(1 억 스폰이면 800 MB) · 늘 견주기(5.6 → 6.4 ns), 엔티티당 XML 재파싱 구조 변경(2 ms 뿐).
- **태스크**: 워커 스핀 늘리기(2 → 50 us 면 SMT 형제를 빼앗아 GT 889 → 1305 us), hot pool(이득 없음), 스레드별 목록 머리 패딩(잡음), 공유 풀 위 GT 병렬화를 레인 없이(RT 170 → 261 us),
  워커 깨우기 사슬, `TaskArgs` 인라인 4 칸(기록 122 → 196 us), `PagedArray` 통합의 첫 측정(번갈아 재니 차이 없음 — 측정 착시).
- **도구 · 빌드**: 린트를 파일마다 프로세스로(5.3 → 9.4 s — 덩어리 프로세스는 7.5 → 2.25 s), 커밋 훅 게이트 병렬화(이득 ~1 s), 짝 헤더 메모이즈(차이 없음), `formatstring` 비템플릿 부분
  `.cpp` 분리(41 → 44 s) · 타입 소거 배열, `ContainerTypeMap` 선형 탐색 개선(파서 시간은 libclang), 팩 코덱 Zstd(−0.1 %) · LZ4(+28 %) — 이미 압축된 자산이라 Zlib 유지(압축 안 된 자산이
  들어오면 다시 잰다).
- **구조**: 백엔드 `*RHIResource.h` · `*RHICommandContext.h` 공통 기반(겹침이 전부 override 선언), `ResourceCache<T>`(나머지 39 % 가 소유 방식), 모듈 팩토리 골격 공통화(공통 4 줄),
  RHI · GF leaf CMakeLists 를 부모 루프로(디렉터리 스코프), `FindWindowsTools` 파이썬 이전, `EngineTest` 에 `GF_*` 자동 링크, `SW_ASSERT_NULL`, `formatstring` 인자 수 컴파일 검사의 매크로 판
  (C++20 으로 올리면 `consteval` 포맷 타입으로 옮긴다), "자유 `static` 함수 금지" 린트(오탐), "CommandList 가 RecordingState 를 소유" 린트, `CheckCodeConventions` 매개변수 · 지역변수 사슬,
  `Cb` · `Fbo` 풀어 쓰기, 되돌리기 스냅샷 바이너리 통일, `StringBuilder::appendFormat` 잘림(버퍼를 늘려 다시 포맷한다), `EditorViewportClient` 쪼개기(공통 빼기로 간다),
  RTS 버킷 · MechArena · BattleRoyale 근접 질의를 엔진 `SpatialHashGrid2D` 로(RTS 는 ~40 줄이 순서 계약을 들어 핸들 정렬 격자로 바꾸면 자동 목표 동점 · 첫 빈 광물 ·
  밀어내기 합이 바뀐다, BR 은 근접 질의가 없다, Mech 는 조종사 몇 명 전수 검사가 격자보다 싸다), 키트 칸 저장소 템플릿 `Grid2D<T>`(덮는 자리 4 곳에서 4 줄,
  저장소 모양이 키트마다 다르다).
- **늘 상위에 오는 정당한 중복**(`RunDuplicateCode`): 백엔드 인터페이스 선언 · 레이스 래퍼 전달 · 플랫폼 구현 · enum 레이블 나열 · 서비스 로케이터 둘(`sw::editor` 는 nullptr, `sw::game` 은
  assert) · `MaterialPacking` 숫자 case(`-Wswitch-enum`) · DX12 상태 조회 · `TypeInfo` 생성자 · RLE · 콜스택 관문 · 셰이더 반사 D3D11/12(확인 중 — 1-3) · Win32 마우스 case · include 묶음.

### 3-13. 옛 이름 → 지금 이름 (`git log` 을 읽을 때)

| 옛 이름 | 지금 이름 |
|---|---|
| `PendingKill` · `markPendingKill` | `PendingDestroy` · `markPendingDestroy` |
| 렌더 그래프 wave / 틱 wave | level / stage(`GT.Scene.tick.stages`) |
| `precede` · `succeed` | `runBefore` · `runAfter` |
| lane | queue |
| `retire*` · `retireImage` | `deferImageUnload` · `free*` |
| `*Recipe`(`D3D12RHIResourceRecipe` · `VulkanRHISamplerRecipe`) | `*Preset` · `ShaderCookRequest` |
| `poisonLiveReload` | `markGraphBroken` |
| Vulkan band | range(`SW_VK_SLOT_RANGE_SIZE`) |
| `HandleTable` · `ObjectHandle` | `SlotHandleTable` · `SlotHandle` |
| `isVisibleIntended` | `isVisibleRequested` |
| `RHI::applyPendingChange` | `RHI::recreateDevice` |
| `createParentDirectory` | `ensureParentDirectoryExists` |
| `transformNormal` | `transformVector`(방향 변환) |
| `-gv_editorOpenAllPanels=1` | `-gv_editorOpenPanel=all` |
| `RenderResourceXml` | `Serialization/Format/ReflectedXmlFile` |
| `CameraBlendCurve` · `CameraBlendKey` · `CameraBlendSpec`(GameFramework/Camera) | `BlendCurve` · `BlendCurveKey` · `BlendCurveSpec`(`Engine/Animation/BlendCurve.h`) |
| `Engine/Character/<평면 60 개>` | `Character/{Fit,Socket,Hit,Pose,AnimNotify}/`, 워핑 둘은 `Object/Animation/`(2026-10-05) |
| `Utility/Debug/*` · `Utility/Format/KeyValueFile` | `Utility/Profiling/*`(`DebugOverlayState` · `KeyValueFile` 은 `Utility/`) |
| `Graphics/Renderer/Debug/` | `Graphics/Debug/` |
| 엔진 루트 `LocalizationTools` · `EngineDevCommands.cpp` | `DevTools/` |
| `Input/Events/` · `Input/Utils/` · `Reflection/Rpc/` | 한 단계 위(`Input/` · `Reflection/`) |
| `Test/<실행 파일>/Test*.cpp`(평면) | 소스 폴더를 따르는 하위 폴더(`Test/README.md`) |

일부러 둔 용어: stamp · kit · cook · orphan · chord · pin.
