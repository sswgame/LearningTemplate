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
  한 자리 — `-gv_rhiBackend=N` 도 같은 판정을 지난다). 실제로 뜬 백엔드는 로그 `Initializing RHI with backend:` 로 확인한다.
- 에디터는 `-EnableEditor` 를 줘야 뜬다. 없이 돌린 검증은 에디터 OFF 검증이다 — 로그로 실제로 로드됐는지 본다.
- Shipping 테스트 바이너리는 `build/Ninja-Shipping/TestBin/` 에 있고 작업 디렉터리는 `Bin/` 이다. `Bin/` 에 남은 낡은 테스트 exe 사본을
  실행하지 말 것.
- 셰이더 소스(.hlsl/.hlsli)를 고쳤으면 다시 굽는다. 빌드는 HLSL 을 다시 굽지 않는다 — `App.exe --bake-shaders` 뒤에 재고 비교한다.
  Shipping 쿠킹은 `bake.stamp` 의 내용 해시로 검증하고 어긋나면 빌드를 세운다(`Scripts/generate/CookAssets.py --verify-shaders`).
- 비결정적 실패는 한 번 재현으로 "확정" 이라 부르지 않는다. 3~5 회 다시 돌려 재현율을 본다. 간헐 실패를 보면
  `--output-on-failure` 출력 전체를 파일로 남긴다(요약만 남으면 어느 케이스인지 모른다).
- 직접 실행으로 통과해도 CTest 에서 질 수 있다(병렬 부하 · 시한). 끝내기 전 검증은 CTest 로 한다. 멈추면 `LastTest.log` 의 마지막 `[ RUN ]` 을 본다.

---

## 1. 남은 일

영역별로 묶었다. 영역 안에서는 위에 있을수록 먼저 볼 것이다. 줄 번호는 2026-10-03 기준이라 어긋날 수 있다 — 함수 이름으로 찾는다.
"확인 필요" 가 붙은 항목은 열려 있는지부터 확인하고 시작한다.

### 1-0. 다음 묶음 — 트리 전체를 건드리는 것들(차례로, 병렬 금지) (사용자 지시 2026-10-03)

- **클래스 이름 일관화 — 감사 목록 전부**(사용자: "일관되게 바꿔봐"). **별칭은 두지 않는다**(사용자: 아직 실제 게임이 없다) — 씬 · 데이터 XML 의 타입 · 루트 이름,
  스크립트 · CI 의 CLI 플래그까지 새 이름으로 다시 쓰고 옛 이름은 어디에도 남기지 않는다.
  묶음: (1) `IRHIResource`→`IRHIResourceFactory`(`getResourceFactory`), `ActionMap`→`InputMap`, `InputDeviceType`→`InputGlyphStyle`,
  `ShaderResourceBind`→`ShaderResourceBindOp` · `ShaderResourceBinding`→`ShaderReflectedBinding`, `PrefabManager`→`PrefabCache`, `HPBarBaseComponent`→`HealthBarComponent`,
  `AttackBaseComponent`→`MeleeHitboxComponent`, `EffectBaseComponent`→`FadeOutComponent`, `DamageUIComponent`→`DamageNumberComponent`, `ObjectSnapshotCommand`→`ObjectUndoUtil`,
  `Cpu*` 시간 → `MonotonicClock` · `Stopwatch` · `Deadline` · `GameTimer` · `ScopedTimer`, `RenderPassManager`/`RenderPassResource`/`RenderPipelineResource`→
  `RenderPipelineAssetCache`/`RenderPassAsset`/`RenderPipelineAsset`, `EditorAssetKind`→`EditorAssetType`, `IModuleCodeHolder`→`IModuleUnloadListener`(`onModuleUnloading`);
  (2) 런타임 정보 행 `*Traits`→`*Info`, `ShaderBindingBinder`→`ShaderParameterBinder`, `GpuMaterialGpu`→`GpuMaterialGroupBuffer`, `RHIConstantBufferShadow`→`RHIConstantBufferMirror`,
  `ReloadFileManager`→`FileWatchDispatcher`, `LiveShaderManager`→`ShaderRecompiler`, `FrameTimeline`→`FixedTimestep`, `FadeService`→`ScreenFade`,
  `BackendSwapController`→`RHIBackendSwitcher`, `KeyCodes`/`MouseButtons`/`GamepadButtons`→`*Util`, `GamepadXInput`/`GamepadJoystick`→`XInputGamepadDevice`/`LinuxJoystickGamepadDevice`,
  `CCD`→`ContinuousCollision`, `ColliderTileComponent`→`TileColliderComponent`, `OverridesOnTick`→`HasOnTickOverride`, `ShaderBindingContract`→`ShaderBindingValidator`,
  `RenderPassInputContract`→`RenderPassInputSignature`, `EngineOwnedServices`→`EngineServiceCollection`, `EngineStartup*`→`EngineInit*`, `MonsterDataCatalog`→`MonsterCatalog`,
  `ZoneRuntime`→`ZoneTracker`, `SelectionManager`→`EditorSelection`; (3) `AnimationGraph*`→`AnimGraph*`, `LevelLoad*Event`→`SceneLoad*Event`, `Load/SaveCompletedEvent`→
  `SaveGameLoaded/SavedEvent`, `SceneDocument::EntityNode`→`SceneObjectNode`, `EditorBackgroundJob`→`EditorBackgroundTask`, `SpatialElement`→`SpatialElement2D`,
  `InputMapEditorPanel`/`PrefabEditorPanel`→`InputMapPanel`/`PrefabPanel`, ReflectionParser 소문자 구조체 → 소문자 네임스페이스(`tpl`→`template`), `framres`/`commandmod`→
  `frameresource`/`commandmodifier`; (4) Bake · Cook 용어(사용자 지시 — 상용 엔진 기준): **Cook = 배포 · 실행용 플랫폼 데이터로 바꾸기**(UE Cook), **Import = 원본 →
  엔진 형식**(UE Factory · Unity Importer), **Bake = 미리 계산한 결과**(라이트맵 · 내비 · 애니메이션 굽기 — 지금은 없음, 그때만 쓴다), **Generate = 빌드가 만드는 코드 ·
  헤더**. 그래서 `ShaderBaker` · `ShaderBakeDriver` · `ShaderBakeRequest` · `ShaderBakeStamp`→`ShaderCooker` · `ShaderCookDriver` · `ShaderCookRequest` · `ShaderCookStamp`,
  `Renderer/Bake/`→`Renderer/Cook/`, `--bake-shaders`→`--cook-shaders`(옛 철자 없음), `bake.stamp`→`cook.stamp`, `TextureBaker`→`TextureImporter`(이미 `TextureImportConfig` ·
  `TextureImportRule` 과 짝), `BakeShippingHostDefaults.py`→`GenerateShippingHostDefaults.py`, 주석 · 문서의 "굽다"도 같은 구분으로. `ResourceManager`→`AssetManager`
  ("Resource" 는 디스크 트리 · 팩, "Asset" 은 읽은 객체), `EngineData`/`GameData`/`EditorData`→`EngineDefaultAssets`/`GameSettings`/`EditorToolDefaults`.
  CLI 철자도 하나로: `ArgumentList.xxx` 가 `"bake-shaders", "bakeshaders"` · `"cook-scenes", "cookscenes"` · `"cooked-dir", "cookeddir"` 처럼 하이픈 없는 철자를 같이
  받는다 — 별칭 금지 결정대로 하이픈 철자 하나만 남긴다(스크립트 · CI · 문서의 사용처도). 리플렉션 주석 키도 같다: `AnnotationMeta.txt` 가
  `Alias` 를 `PreviousName` · `PreviousNames` 로도 받는다(사용처 0) — `Alias` 하나로.
- **case 중괄호 일관성 규칙**(한 switch 안에서 한 case 라도 중괄호면 모두) — 픽서 패치 준비됨(스크래치), 39 파일 다시 쓰기 + AGENTS.md · 04 규칙 문장.
- **한 파일에 클래스가 여럿이면 클래스마다 `namespace sw { }` 블록을 나눈다** — 규칙 + 가능하면 게이트, 트리 전체 적용.
- **시험 코드의 `std::chrono` 직접 읽기 70 여 곳 → 엔진 시계(`CpuClock` 계열, 이름 변경 뒤 `MonotonicClock`)** + 직접 읽기를 막는 게이트.

### 1-1. 직렬화 · 리플렉션

- **씬 · 프리팹 파일을 넘는 오브젝트 참조가 없다.** 파일 안에서는 엔티티 `id` 로 가리킨다. 파일을 넘는 참조가 필요해지면 오브젝트마다 영속 GUID 를 싣는다.

### 1-2. 오브젝트 · 씬 · 틱 · 물리

- **프리팹 오버라이드의 남은 모서리 셋** — (1) 인스턴스에 더한 컴포넌트는 로드 때 목록 끝에 붙어, 가운데 있던 것은 저장 · 로드 뒤 순서가 바뀐다 (2) 물려받은
  컴포넌트의 이름표를 바꾸면 제거 + 추가로 기록돼 그 컴포넌트에 프리팹 수정이 더는 닿지 않는다(UE 도 물려받은 컴포넌트 이름 변경을 막는다 — 에디터에서 막을지)
  (3) 프리팹에서 사라진 컴포넌트의 오버라이드는 경고와 함께 버려지고 다음 저장에서 사라진다.

- **`getAllGameObjects()` 값 반환이 6 곳에 있다(모두 일회성).** 프레임 경로에 들어오면 `getAllGameObjects( out )` 또는 `forEachGameObject` 로 바꾼다(조건부).
- **`MeshInstanceBatch` 의 한계.** 항목 수가 만들 때 정해지고(resize 없음, `setEntryVisible` 로 숨기기만), 배치 하나 = 메시 · 머티리얼 하나라 항목별
  머티리얼 · 투명 정렬이 없다.

### 1-3. 그래픽스 · RHI · 셰이더

- **`.hdr` 원본 굽기가 없다** — 지금 굽기는 `.hdr` 을 만나면 8 비트로 자르지 않고 실패로 알린다. HDR 원본이 필요해지면 DirectXTex `LoadFromHDRFile` → BC6H.

- **GPU 메모리 측정(사용자 요청 2026-10-03, gfxfix 워크트리 병합 뒤)** — (1) 드라이버 총량 · 예산: DXGI `QueryVideoMemoryInfo`(DX12 · DX11), `VK_EXT_memory_budget`(Vulkan),
  GL 은 벤더 확장(`GL_NVX_gpu_memory_info` · `GL_ATI_meminfo`)이 있을 때만(없으면 "모름"). (2) 엔진 집계: RHI 자원 생성 · 해제 때 종류별(텍스처 · 렌더 타깃 · 버퍼 ·
  트랜지언트 풀 · 디스크립터)로 실제 할당 크기를 더한다(DX12 `GetResourceAllocationInfo`). 총량 − 집계 = "엔진 밖(드라이버 · 스왑체인)". 보여 주기는 CPU 태그와 같게
  `-gv_profileFrames` 보고 · ProfilerPanel. 시험: 텍스처 하나를 만들고 지우면 집계가 그 크기만큼 오르내린다(hostgpu, 4 백엔드).

- **2D 정렬 레이어가 없다.** 깊이가 같으면 거리로 정렬해, 같은 Z 의 월드 UI 와 월드 스프라이트 순서가 뒤집힐 수 있다.
- **점광 · 스폿 그림자** — RHI 텍스처 차원(배열 · 큐브, 면 단위 타깃 · 올리기 · 읽기)은 있다. 남은 것: 그림자 패스 다중 뷰(면 여섯) → 셰이더 쪽(DX12 · Vulkan
  큐브 · 배열 bindless 테이블, DX11 · GL TextureCube 슬롯) + `swSampleShadowAtWorld`. 3 단계 전에 큐브 대신 2D 아틀라스(Unity URP · Godot — RHI 변경 없음)로 갈지 먼저 정한다.
- **반해상도 후처리** — 첨부별 `_resolutionDivisor`(1 · 2 · 4)는 있다. 남은 것: 반해상도 패스가 읽는 입력의 텍셀 크기(`g_OutlineParams.yz` 는 프레임 텍셀),
  `deferredpipeline.xml` 블룸을 반해상도로 나누기, Release 로 p50 · p99 측정.

### 1-4. 에디터

- **imgui Vulkan 백엔드가 UI 스레드에서 `vkQueueSubmit` 을 부른다** — 글꼴 아틀라스 업로드(`ImGui_ImplVulkan_UpdateTexture`) · 보조 뷰포트(`RenderPlatformWindowsDefault`)가
  렌더 스레드와 같은 큐에 엔진 `_queueMutex` 밖에서 제출한다(큐 외부 동기화 위반). ImGui 의 아틀라스 파괴(`ImGui_ImplVulkan_DestroyTexture`)도 즉시 해제다.

- **에디터 자체 시험(`SW_EDITOR_SELF_TEST`)이 입력을 흉내 내지 못한다** — 그래프 패널 ↔ 저장 커맨드 배선, 인스펙터 콤보 직접 편집, 툴팁 호버 · 드래그 드롭은
  ImGui 입력 이벤트를 넣는 창구(`ImGuiIO::AddMousePosEvent` 류를 프레임 단계에서 주입)가 있어야 덮인다.

- **DPI 150 % 모니터와 모니터 사이 이동을 실물로 보지 않았다.** 96 DPI 기계에서 `-gv_editorUiScale=1.5` 로만 봤다. 글자 선명도 · 창 · 스왑체인 크기 ·
  `io.ConfigDpiScaleFonts` · `ConfigDpiScaleViewports` 를 본다.

### 1-5. 핫 리로드 · 모듈

- **RuntimeAPI 가 Engine 헤더를 include 한다** — `RuntimeAPI/Service/ModuleService.h` 가 `Engine/Common/EngineServiceList.xxx` 를 세 번 include 해 서비스 id 를 만든다.
  "RuntimeAPI 는 순수 계약" 과 어긋난다. 서비스 목록을 RuntimeAPI 쪽으로 옮기거나(Engine 이 그것을 include) 계약 문장을 사실대로 고칠지 정한다.

- **RHI 백엔드 교체 경로(`reinitializeAfterRhiSwap`)는 아직 에디터 → 게임 순으로 인스턴스를 다시 세운다** — 기동은 게임 → 에디터로 바뀌었다. 같은 순서로 맞출 것.

- **바깥 빌드(터미널 · IDE)의 리로드 트리거는 여전히 mtime 디바운스뿐이다** — 에디터가 시킨 빌드는 성공 뒤에만 올린다(`LiveReloadManager::notifyBuildStarted/Finished`).
  바깥 빌드도 "빌드 성공" 신호(ninja 종료 · 스탬프 파일)를 받으려면 빌드 쪽 협조가 필요하다.
- **모듈이 렌더 패스를 등록하는 창구가 없다.** `FramePassContext` · 커맨드 리스트 · 트랜지언트 풀을 모듈 경계 밖으로 내야 하고, 그것은 RT 안전 계약까지
  내보내는 일이다. 쓰는 모듈이 생기면 그때.

### 1-6. 게임프레임워크 · 킷 · 게임

- **TurnBattle 세이브가 옛 형식(pp0/pp1 두 칸)을 아직 읽는다** — `SaveGame.cpp` 의 `ppCount` 없을 때 갈래. 별칭 · 옛 형식 제거 결정대로 지우고 시험 픽스처를 지금 형식으로.


- **강체 · 고정 스텝 누적기가 없다.** `PhysicsWorld::step` 은 겹침 이벤트만 낸다. 강체가 생기면 적분과 누적기를 넣는다.
- **리눅스에서 yad 만 깔린 기계에는 "All files" 필터가 없다**(`LinuxFileDialog.cpp`). `yad --file --file-filter='A | *.txt' --file-filter='All files | *'`
  가 뜨는지 확인한 뒤에만 `buildGtkStyleCommand( ..., true )` 로 바꾼다 — yad 가 인자를 거부하면 다이얼로그가 아예 안 뜬다.

### 1-7. Core · 태스크

- **sw 할당자 밖 누적 할당의 85 % 는 `FileUtil` 의 `std::filesystem` 이다**(기동 ~670 KB / 1 만 회 — collectFiles · fileExists · 디렉터리 순회). 할당자 인자가 없는
  표준 API 라 줄이려면 Win32 · POSIX 순회로 바꾼다. 상주량은 1 KB 미만이라 전역 operator new 교체는 하지 않는다(사용자 결정).

- **`runParallel` 합류 대기가 남의 태스크(IO 등)를 도와 실행할 수 있다.** 프로파일에 보이면 IO 레인을 따로 둔다(조건부).
- **TaskManager 스테이지 디버그 이름** — 프로파일러에 연결할 때 넣는다(지금은 연결돼 있지 않다).
- **`fixed_string` 의 해시가 FNV(`computeHash64`)다.** 느리지만 프로파일에 안 보여 두었다(낮음).

### 1-8. 성능 (재고 나서 정할 것)

- **DX12 · Vulkan Present 히치.** 큐브 100 · 600 프레임 중 40 프레임이 1~18 ms 다(DX11 은 없다). 다음 후보는 DXGI 대기 가능 스왑체인
  (`FRAME_LATENCY_WAITABLE_OBJECT` + `SetMaximumFrameLatency` + 대기). 함정: 플래그는 `ResizeBuffers` 에도 같게. 재기 전에 VSync 가 정말 꺼졌는지 보고 p99 로 본다.
- **DX12 `releaseOnlineBlocksDeferred` 의 `_onlineBlockMutex` 경합.** 병렬 기록 중 RT `mutex::lock` 의 79 % 였다. 후보는 워커별 대기 목록. 고치기 전에 다시 잴 것.
- **에디터 모드 `GT.Editor.updateUi` ~5 ms(큐브 8000)를 쪼개 보지 않았다.** 창을 전면에 두고 잰다(가려지면 RT.BeginFrame 이 67 ms 를 기다려 5.8↔75 ms 로 흔들린다).
- **8000 무버의 `components`(onTick) ~325 us.** 남은 비용은 오브젝트 → 틱 항목 → 컴포넌트 포인터 추적이다. 더 줄이려면 오브젝트 모델 밖 배치 경로
  (언리얼 Mass · 유니티 DOTS 자리)나 트랜스폼 SoA 2 단계가 필요하다 — 큰 구조 변경이라 할지부터 정한다(1-11 의 구조 후보).
- **직렬화기(이름 대조 · 텍스트 파싱)와 리소스 로드의 리플렉션 비용, 비동기 씬 로드 중 최악 프레임을 재지 않았다.** 큰 씬 · 쿠킹본으로 잰다(Dev 는 `Cooked/`
  를 마운트하지 않는다).
- **`GpuInstance` 96 → 112 B 의 비용을 재지 않았다.**
- **조건부 후보 묶음.** 병렬 틱 문턱의 교차점 · GameObject 레이아웃 · 적응형 틱 문턱 · 스폰 비용(~1.1 us, 잠금 여섯) · 시퀀서 성능 수치. TickItem 인라인
  재시도는 오브젝트의 틱 부기 49 B 를 먼저 줄여야 한다. 측정해서 이기면 한다.
- **Core 에서 미룬 결정.** 전역 소형 블록 할당자(프레임당 할당이 0 근처가 된 뒤 로드 시간으로 판단 — 지금 ~10 회/프레임), 비동기 파일 IO(오버랩드 · io_uring).
- **필드 재배치로 8 B 이상 줄일 수 있는 타입이 남아 있다**(`RunPaddingReport.py` 로 보고만 함). 많이 만들어지는 것: GameObject 200→192, Mesh 112→104,
  MaterialInstance · Material · ActionMap::ActionEntry 16, MaterialProperty · ShaderBindingSlot · InlineSuccessorList · GlobalVariableInfo/Registrar 8. 싱글턴(InputManager ·
  Logger 64 등)은 이득이 작다. PROPERTY 필드는 직렬화 순서라 옮기지 않는다. 위치 초기화 표(EditorAssetKindInfo · AssetMatchRow · CommandRow)는 모든 행을 같이 바꿔야 한다.
  FrameRenderer 진단 세터(`setMeshMorphDiag` · `setDrawMergeEnabled` · `setVertexPoolEnabled`)는 Shipping 제외 후보.
- **ReflectionParser 강제 include PCH**(`CoreMinimal.h` 를 PCH 로 — 타깃당 ~0.4 s). 캐시 위치 · 무효화가 필요하다. 값이 작아 보류.

### 1-9. 빌드 · 린트 · CI · 테스트

- **`Test/README.md` 의 "구성마다 도는 케이스 수" 표(2026-10-01 실측)가 낡았다** — 다시 잰다(Debug CoreTest 만 해도 348 개).


- **코드 · 문서 29 곳이 옛 백로그의 날짜 항목 · 옛 절 번호를 가리킨다**(`ci.yml:156` · `TargetRules.cmake:51` · `GameEvents.h:12` "1-0c" · `docs/07` "1-0e" ·
  `FrameRendererCompute.cpp` "백로그 1-4" 등). 주석 정리(현재형 핵심만)와 함께 고친다 — 날짜 사연은 지우고, 남길 지식은 이 문서 3절 위치나
  `git show 7ce95fc8:docs/06_Backlog.md` 로. 목록은 grep `06_Backlog\|백로그` 로 다시 뽑는다. 코드 주석의 "예전에는 …" 경위 서술도 같은 정리에서 현재형 주의로
  바꾼다(예: `Core/Memory/Memory.h:58` · `Core/Task/TaskManager.h` · `Core/Task/TaskFuture.h:268` · `App/App.cpp` · `TurnBattle/SpeciesData.h:34` — README 들은 10-04 에 정리됨).

- **시험 공백 목록** — `StringBuilder` 할당 실패(주입 창구 없음), 팩과 낱개 파일의 우선순위, 컴포넌트 풀 키, `syncAfterSceneGenerationChange`,
  `RenderGraph::executeParallel` 의 제출 실패 경로, `_materialCb` 병합 키(그래픽스).
- **`AppSmokeTest` 의 "이 기계에서 못 도는 백엔드" 판정이 로그 문자열 둘에 기댄다** — 표식을 내는 곳(`RHI.cpp` · `OpenGLRHIDeviceInit.cpp`)을 하나의 구조화된
  결과(열거값)로 바꾸는 그래픽스 쪽 수정.
- **`RunForwardDeclarationCandidates.py --show-unused` 의 거짓 "쓰임 없음" 208 건** — 보고 전용이라 손으로 걸러야 한다.
- **imgui-node-editor vcpkg 오버레이**(`ThirdParty/imgui-node-editor/vcpkg-port/`, `<exception>` 패치)는 업스트림이 같은 고침을 받으면 지운다.
- **`TestRenderPassGpu.cpp` 의 남은 같은 줄**("큐브 하나 든 씬" · "한 프레임 돌리기")은 같은 모양의 케이스가 늘면 도우미로.
- **옛 시험 산출물 정리**(사용자 폴더라 두었다): `%TEMP%` 의 `sw_*`, `build/*/Bin` · `TestBin` 의 `prefab_test/` · `TestTemp/` · `temp_gen_*` · `temp_collide/`.
  옛 규칙으로 지은 바이너리가 남은 프리셋은 다시 지어야 새 규칙을 따른다.

### 1-10. 관찰 중 — 다시 보이면 원인을 판다

- **Shipping `EngineTest_NoGPU` · HostOnly 간헐 세그폴트**(09-20 · 21 · 22 에 한 번씩). 09-23 에 고친 DX11 기록 컨텍스트 결함과 모양은 같지만 단정하지 않았다.
  이제 시험 실행 파일에 크래시 핸들러가 있어 다음에는 스택이 남는다 — 직접 실행해 전체 출력을 파일로 받는다.
- **Shipping `CoreTest` 의 `Failed to deserialize config from: shipping_host_baked`**(한 번, 3 회 재실행 통과). `ConfigManager::loadConfigFromJson`.
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
- **macOS — 2026-09-24 부터 지원 대상이 아니고 어디서도 컴파일되지 않는다.** 되살릴 때 순서: `ci.yml` macOS 잡 복원 → configure 실패 → `CocoaWindow` 가 `_onResize` 를
  부르지 않음(스왑체인이 안 따라감) → `CocoaSplashWindow` 가 창을 만들지 않음 → `applyWindowVisibility` 이름 변경 컴파일 → `PosixCallStackCapture` 의 폴트 PC
  (Linux 만) · `Posix*` 의 macOS 가드 · `Mac` 파일워처.
- **`SetupVcpkg.py --install` 이 `vcpkg.cmake` 만 보고 "찾았다" 고 끝낸다.** 윈도우에서 클론한 트리를 리눅스에서 쓰면 `vcpkg` 바이너리 없이 성공을 보고한다
  (툴체인이 부트스트랩하므로 치명적이진 않다).

---

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
- **이 문서는 같은 커밋에서 고친다.** 끝낸 항목은 지운다. 남길 교훈이 있으면 3절에 한두 줄로 옮긴다. 여러 PC 에서 고치는 문서라
  작업 중에 pull · 충돌이 날 수 있다 — 커밋 전에 받아서 합친다.

### 안 하기로 한 것 (다시 제안하지 말 것)

- **지금 하지 않는 구조 후보 — 다시 볼 조건과 함께**(2026-10-03 상용 엔진 비교로 결정): 트랜스폼 SoA 2 단계(UE 액터도 AoS, 측정 근거가 생기면) ·
  선행 조건 스케줄러(시스템이 서로의 결과에 기대기 시작하면 — UE `AddTickPrerequisite` 모양) · 에셋 로더 등록제(종류가 대여섯이 되면 — UE `UFactory`) ·
  참조 카운트 RHI 핸들(한 리소스를 여럿이 나눠 들기 시작하면 — UE `TRefCountPtr`) · Mesh/Material `SlotHandle`(하지 않는다 — `shared_ptr` 이 수명과 RT 안전을 한 번에
  준다) · `ResourceUtil` 소유 객체화(하지 않는다 — UE `FPaths` 도 정적) · 링크 단위 분할(증분 링크 시간이 문제가 되면 별도 PR) · API 통합 남은 판단(다음 훑기).

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

---

## 3. 참고 — 다음 작업에 필요한 것

끝낸 일에서 남긴 교훈만 모았다. 사연은 `git log` 에 있다 — 여기에는 다시 물릴 함정, 지켜야 할 계약, 되돌리면 안 되는 결정, 재는 법만 적는다.
같은 내용이 `AGENTS.md` · 폴더 `README.md` · 코드 주석에 정본으로 있으면 그쪽을 가리킨다.

### 3-1. 측정 · 프로파일

- **물리 벤치: `PhysicsBenchTest`**(Release) — 먼 이동 바디가 있는 step p50 1124~2468 → 319~330 us(없는 step 은 319~328 us 그대로).

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
- **GPU 비용은 `GPU.<패스>` 타임스탬프로 나눈다.** 패스를 지워서 나누면 타깃 사슬이 바뀌어 답이 뒤집힌다(추정 38 us, 실측 123 us). `RT.BeginFrame` 은 GPU 시간의 대리값이 아니다.
- **재기 전에 VSync 가 꺼졌는지 본다** — 1/RT.Frame 이 주사율과 같으면 VSync 다. DXGI 는 스왑체인 생성과 `ResizeBuffers` **둘 다**에 `ALLOW_TEARING` +
  `Present( 0, DXGI_PRESENT_ALLOW_TEARING )`(짝이 안 맞으면 `INVALID_CALL`, `RHI/DX/RHIDxgiTearing.h`). Vulkan 은 present 모드. CLI 는 `-vsync`.
- **셰이더를 고쳤으면 재기 전에 `App.exe --bake-shaders`.** 빌드는 HLSL 을 다시 굽지 않는다.
- **벤치 스위치**(`Source/Games/Empty/BenchScene.cpp`): `-gv_benchMeshes=N` · `-gv_benchLights=N` · `-gv_benchGround=1` · `-gv_benchMovePercent=%` · `-gv_benchInstanced=1` ·
  `-gv_benchTickMovers=N`(틱 **안** 세터 — 실제 게임플레이 경로) · `-gv_benchSpawnChurn=N` · `-gv_benchMeshVariants` · `-gv_benchMeshShapes=N` ·
  `-gv_benchMaterialChurn*` · `-gv_benchAnimate=0`(컴퓨트 회전과 `update` 사인파를 **둘 다** 멈춘다), `-gv_deferred=1`, `-gv_useRenderThread=0`.
- **프레임당 힙 할당**은 `-gv_profileFrames` 보고의 `alloc/frame`, 콜스택은 `-gv_profileAllocSites=N`(Debug App — 횟수는 최적화와 무관, 시간은 같이 재지 말 것).
- **씬 로드 측정**: `Scripts/dev/GenerateStressScene.py` 로 큰 씬(도형 섞기) → `GameConfig.json` `_startupScene` → `[SceneLoad]` 줄. Dev 는 `Cooked/` 를 마운트하지
  않으므로 쿠킹 효과는 `--cooked-dir=<repo>/Resource` 로 굽고 재고 지운다. `[SceneLoad]` 가 `.xml` 을 가리키면 쿠킹본을 안 읽은 것이다.
- **벤치가 상태를 공유하면 단계 순서를 잰다.** 손대지 않은 대조군이 움직이면 하니스를 의심한다. 벤치 메시가 공유라 배치 결함을 가린 적이 있다 — 씬에서 온 메시로도 본다.
- **GPU 업로드 비용은 호출당이다**(DX12 ~3.3 us) — 쪼개면 느려진다. 구간을 배열로 묶어 한 번에.
- **워커가 쓴 데이터를 다른 코어가 읽으면** 캐시 이동이 항목당 일(~40 ns)보다 비싸다. 쓰는 스레드와 읽는 스레드를 같게 둔다.
- **워커는 공유 카운터 · 비트필드에 쓰지 않는다.** `fetch_add` 한 줄이 병렬 플러시를 직렬보다 느리게 했다. "하나라도" 플래그는 프레임에 한 번 쓰고 읽기만 한다.
- **도구**: `RunDuplicateCode.py --filter <dir> --no-headers`(머리말에 "합칠 대상 아님" 목록), `RunEngineLayerGraph.py`, `Scripts/dev/BackendSmoke.py`(4 백엔드 평균 RGB),
  Release 로 읽는 `TaskManagerBenchTest` · `GameObjectBenchTest` · `ContainerBenchTest`, `Test/TestFramework/TestBench.h`.

### 3-2. 검증 · 시험 쓰기

- **CoreTest 는 엔진을 쓰지 않는다** — include 경로로는 막을 수 없다(`TestFramework` 가 Engine 을 PUBLIC 링크, `TestFramework.h` → `EngineMinimal.h`).
  `CheckTestSuites` 규칙 6 이 CoreTest 파일의 직접 Engine · GameFramework · Editor include 와 `engine::` 호출을 막는다. 엔진 타입이 필요하면 지역 대역을 쓰거나 EngineTest 에.
- **레이어 때문에 지금 자리가 가장 낮은 합법 자리인 파일 넷**(`SpriteClipCache` · `PackCompressionUtil` · `ObjectSnapshotCommand` · `GpuLight.h`)은 README 에
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
  `test::FakeRHIDevice`(병렬 기록 nogpu), `LitCubeScene` · `renderPresentCaptureOf` · `compareCaptures`(TestRenderPassGpu.cpp), 에디터 지역 서비스 `Test/EditorTest/EditorTestServices.h`.
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
- **sccache 서버 포트(4226)를 Windows 와 WSL 이 나눠 쓴다.** 동시에 빌드하면 모든 컴파일이 `failed to fill whole buffer` 로 깨진다(코드 오류처럼 보인다). WSL 은
  `export SCCACHE_SERVER_PORT=4227`, 아니면 상대 서버를 `sccache --stop-server`.
- **WSL 에는 GPU 가 없다**(Vulkan 은 `llvmpipe` 하나, GL 은 `ARB_gl_spirv` 가 없어 빠진다) — API 오용은 잡지만 드라이버 거동은 못 본다. `libwayland-dev` 가 필요하다.
  **gdb 가 없다** — `/proc/<pid>/task/*/stat` 의 utime · stime 을 두 번 떠 사용자/커널을 가르고, SIGUSR1 처리기(`backtrace_symbols_fd`)를 임시로 넣어 `tgkill` 로 스레드마다
  스택을 뜬다. 리눅스 파일 하나는 `clang++ -fsyntax-only -DSW_PLATFORM_LINUX … -include Source/Core/pch.h <file>` 로 검사할 수 있다.
- **Ubuntu 26.04 는 `libxml2.so.2` 가 없어 번들 `ld.lld` 가 뜨지 못한다** — 시스템 lld 를 `--ld-path=/usr/bin/ld.lld` 로 EXE · SHARED · MODULE 세 링커 플래그 모두에
  (`SetupLinuxDevEnvironment.py` 가 안내한다). 리눅스 LLVM 은 `/usr/lib/llvm-*` glob 자연순 내림차순으로 찾는다(손목록은 새 배포판을 비켜간다).
- **리눅스 CI 는 ubuntu-22.04 의 `libclang-dev`(16 미만)다.** 파서에 새 libclang API 를 쓰면 리눅스 잡만 선다 — `CINDEX_VERSION` 으로 가른다. CI 러너 파이썬은 3.10 이라
  f-string 식 안의 백슬래시 · 여러 줄 식이 configure 를 죽인다(`CheckPythonMinimumVersion.py`). GH Windows 러너는 cp1252 라 한글을 print 하는 스크립트가 빌드째 죽는다
  (증상: `sccache stats: 0 hits, 0 misses`) — 스크립트는 `Scripts/common` 을 import 한다(UTF-8 stdout). 재현은 `PYTHONIOENCODING=cp1252`.
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
- **유니티 빌드는 `CI-*` 프리셋에만 켜져 있다.** `Ninja-*` 가 초록이어도 익명 네임스페이스 충돌이 없다는 뜻이 아니다 — 헬퍼 · 상수는 `XxxInternal` 구조체로 감싼다.
  `sw_skipUnitySources` 가 다시 길어지면 규칙이 깨지고 있다는 신호다.
- **LLVM 을 다시 깔면 PCH 가 전부 낡는다**(`… has been modified since the precompiled header was built`). `.pch` 와 짝 `cmake_pch.cxx.obj` 를 같이 지운다(`SetupLlvm.py` 가 한다).
- **LTO 함정** — clang `-flto` obj 는 MSVC `lib.exe` 가 못 읽는다(LNK1107). 아카이버는 "지금 컴파일러 옆" 을 먼저 본다(리눅스 `/usr/bin` 에는 llvm-ar 이 없어 LTO 가 조용히 꺼진다).
  CMake 는 IPO 아카이브 명령을 `project()` 때 굽고, `check_ipo_supported` 는 거짓 NO 를 내서 직접 판정한다(`cmake/Environment/ToolchainBinaries.cmake`). `SW_ENABLE_LTO` 하나가 Release · Shipping.
- **GPU · 드라이버** — 반복 TDR 은 어댑터를 망가뜨린다(재부팅 필요). DX12 는 실패 지점에서 InfoQueue · DRED 를 강제로 뽑는다. 이름 없는 객체("Unnamed")가 보이면 `SetName` 부터 붙인다.
  비동기 로거는 크래시 직전 메시지를 잃는다 — 직접 진단은 `fopen` + `fflush` + `fclose`.

### 3-4. 빌드 · CMake · 린트 · 스크립트

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
- **X-매크로 목록 `.xxx` 의 정본은 `Core/Predefined/`** 이고 죽은 사본은 `CheckDataFileReferences` 가 막는다. `PredefinedNameType.xxx` 의 줄 순서가 곧 intern 인덱스다(중간 삽입
  금지, 대소문자만 다른 이름 금지).
- **`CheckCodeConventions` 알아 둘 것** — 명명 판정은 `kMapContainerVocabulary` × `kMapNamingSubject` 표 하나. `Style/BitfieldBoolean` · `Naming/DuplicateInternalHelper` ·
  `Style/HeaderMemberInitializer` 는 전체 스캔에서만 돈다. `Naming/OutParameter` 는 `out` 이 든 지역 변수(`arrOutput`)를 오탐한다. 게이트는 파일을 동시에 훑으니 규칙
  객체에 상태를 들지 말 것. 자기 시험 조각은 그 검사가 **통과하는** 바탕(`_kCleanFixture`) 위에 위반 하나만 얹는다.
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
- **머티리얼 enum · 플래그 글은 `EnumInfo::tryParseText`** — 모르는 이름 · 표식 값(`Count`)은 경고하고 값을 쓰지 않는다. enum 타입 이름은 `findInterned` 로만.

- **저장되는 상태는 PROPERTY 이고, 모든 PROPERTY 타입은 직렬화기가 실어 나를 수 있어야 한다**(`SerializerUtil::canCarryProperty`,
  `ReflectionSerializationTest.EveryPropertyHasATypeTheSerializersCanCarry`, 모듈판은 SmokeTest 의 `ModuleApiTest`). enum 에 `ENUM()` 이 없으면 `"null"` 로 저장된다.
  런타임 핸들(`void*`)은 `Transient`. `PROPERTY()` 를 빼먹은 필드는 매 실행 "모르는 필드" 경고를 내고 값은 기본값으로 돈다.
- **모르는 칸 · 모르는 열거자는 그 칸만 실패한다**(컨테이너면 그 원소, 맵이면 그 항목) — 세 형식이 같은 규칙이다. 기록 타입 해시를 모르는 칸(지운 enum · 타입)은 크기로
  짐작해 읽지 않는다. 모르는 타입의 컴포넌트는 `MissingComponent` 가 원문을 맡아 같은 형식으로 다시 쓴다. 프리팹을 못 찾은 엔티티는 `SceneDocument::EntityNode` 로 보존한다.
- **바이너리는 enum 을 열거자 이름 해시로 싣는다**(플래그는 켜진 이름 수 + 해시, 이름 없는 값만 `0 + int64`). 열거자 이름을 바꾸면 데이터를 다시 쓴다(옛 바이너리는
  읽히지 않는다 — 실제 게임 데이터가 생긴 뒤라면 `ValueAlias`). 한 enum 안의 `Red` · `RED` 는 해시가 같다 — `registerEnum` 이 알린다. 판 `BinaryWireVersion` 은 스트림 머리마다 있다. `kObjectReflectedSchemaVersion` 은 일부러
  올리지 않았다(올리면 옛 상태가 모두 거절된다).
- **버전 절차는 `runVersionedDeserialize` 한 벌**이고 형식 사이 차이는 `SchemaVersionSource` · `SchemaOrphanPolicy` 두 enum 뿐이다. 버린 orphan 은 로드마다 한 줄 알린다 —
  새 이관도 `findOrphan` · `findOrphanHash` · `applyOrphanTo…` 로 찾아야 경고에서 빠진다(`_bClaimed`). 이관은 기록 타입이 같을 때만 제자리, 스칼라 → 스칼라는 텍스트를 거쳐
  (`tryCoerceBinaryPayload`), 비트 재해석은 금지다. 판단은 전선이 싣고 온 타입 해시로(payload 크기로 짐작하면 `1.5f` 가 `1069547520` 이 된다).
- **실패는 버릴 수 없다.** 실패할 수 있는 동사(load · save · read · write · parse · (de)serialize · apply · restore · import · export · cook · compile · bake · revert · convert ·
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
- **문자열 해시 식을 바꾸지 말 것.** `StringUtil::computeHash64`(FNV-1a, 문자를 `make_unsigned_t<CharT>` 로 넓힌다)에 쿠킹 산출물 · 셰이더 베이크 스탬프 · 파이썬 쿠커가 걸려 있다.
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
- **`AnnotationMeta.txt` 의 `flag.X` 한 줄이 단독 토큰과 `X = true` 를 함께 등록한다.** `ArgumentList.xxx` 의 `bUseDefaultValue` 를 켜면 주지 않은 인자에도 `getArgument` 가 true 다.
- **XML** — 쓰기는 `XmlNode::toString` 하나, float 는 `std::to_chars` 최단 왕복(그래서 되돌리기 스냅샷을 바이너리로 바꾸지 않는다), 긴 줄 접기는 시작 태그 속성에만(pugixml 은 텍스트
  안 `"` 를 이스케이프하지 않는다), 태그는 `sanitizeTag` 가 `::` → `__`. 정수 속성은 `tryGetAttributeIntInRange`, 불리언 글은 `StringUtil::tryParseBool`(관대한 `parseBool` 은 실패를
  알아야 하는 자리에 쓰지 말 것). Windows 헤더가 `small` 을 매크로로 정의한다.
- **JSON** — `JsonValue` 는 빌린 포인터다: 같은 부모에 `set( 새 키 )` · `pushBack()` 을 하면 앞서 꺼낸 형제 핸들이 죽는다("하나 받아 다 채우고 다음"). nlohmann
  `is_number_integer()` 는 부호 없는 수에도 참 — unsigned 를 먼저 본다. `JsonSerializer::loadFile` 은 실패해도 그 앞까지 읽힌 값이 남는다. `SerializeContext` 는 `deriveFromDefault()`.
- **컨테이너를 어떻게 채울지는 컨테이너가 정한다** — 역직렬화는 `appendElement`, 인스펙터는 `allowsInPlaceElementWrite()`. 왕복 시험은 세 형식 모두, 값은 정렬되지 않은 순서로.
- **경로** — 리소스 id 하나로 든다(`ResourceUtil::toResourceId` — 루트 밖 · `..` 는 빈 글). 쓰기 경로는 `ResourceUtil::getWritePath` 하나 + `ensureParentDirectoryExists`(bool, 실패하면
  그 자리에서 경로 · OS 이유를 알린다). 맵 키는 `normalizePath`(소문자), 여는 경로는 `normalizeSeparators`(`collectFiles` 는 대소문자를 보존한다). 배포 빌드는 `.meta` 를 쓰지도
  GUID 를 지어내지도 않는다 — 배포본 GUID 표는 쿠커가 도메인마다 넣는 `assetregistry.txt` 다. `ensureMeta` 는 루트 밖 절대 경로면 null GUID.
- **쿠킹 이름은 `AssetCookPath`(`Engine/Resource/AssetFormat.h`) 표 하나** — `.scene.xml` → `.scene.bin`, `.prefab.xml` · `.prefab.json` → `.prefab.bin`, `toSourcePath` ·
  `isCookableSource`. 로더 · 쿠커 · 에디터 판정 · `SceneManager::saveActiveScene` 이 모두 지난다. 쿠킹은 엔진이 한다(`PrefabManager::cookAllPrefabs` · `App --cook-scenes` ·
  `AssetDatabase::writeRegistryFiles`), 파이썬 `CookAssets.py` 는 스테이징만. 쿠킹은 왕복 검증한 엔티티만 바이너리로 바꾸고 나머지는 XML 로 남기며 WARNING 을 낸다.
- **압축** — 팩 enum `PackCompressionType` 과 스트림 enum `CompressionCodecType` 은 독립된 디스크 포맷이다(`static_cast` 로 잇지 말 것). `CompressionCodecRegistry` 는 `EngineLoop` 가 소유하고
  Core 에는 슬롯만 있다. 모듈이 등록한 코덱은 그 모듈 shutdown 에서 `unregisterCodec`. zlib 은 Windows 에서 4 GB, LZ4 는 2 GB 가 한계다.
- **씬 · 프리팹 손 XML 을 쓰지 말 것** — 임베디드 오브젝트 XML 은 리플렉션 산출물이다. 머티리얼 XML 에서 `_permutations` 를 빼먹으면 네 백엔드가 제각각 무너져 렌더러 버그로 오인한다
  — 실제 에셋 + `setPropertyValue` 로 간다.

### 3-6. 오브젝트 · 씬 · 틱

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
- **빌리기는 포인터(이번 호출 · 프레임), 보관은 `GameObjectHandle` · `ComponentHandle` + `resolve*`.** 이름 기반 `GameObjectPtr` · `ComponentPtr` 를 되살리지 말 것. 게임 모듈은
  컴포넌트를 생포인터로 들지 않는다(상태 복원이 씬을 갈아엎는다). 되살릴 때 id 보존: `createGameObjectWithId`, `ComponentIdRestoreScope` 는 `onRegister` **전에**, 프로세스 토큰이
  다르면 id 를 버린다. 오브젝트 id 는 프로세스 전역이다 — 시험에서 "다음 = +1" 을 가정하지 말 것.
- **틱 중 구조 변경은 큐 하나(`deferStructuralChange`)에 부른 순서대로**, 게임 쪽 `deferPostTick` 은 그 뒤다. 이름 · 틱 설정 · 서브틱 · 태그 쓰기는 `Component::deferIfStructureFrozen` 을
  지난다. 틱 중 `addComponent` 는 `nullptr` — `executeOrDeferPostTick`. 시험은 틱 **안에서** 본 값을 목 훅으로 기록한다(`getComponentCount` 는 미룸과 해제를 가르지 못한다).
- **틱 중 트랜스폼 쓰기** — 자기 오브젝트를 틱하는 스레드면 칸의 대기 자리에, 남의 오브젝트면 쓰기 큐로(`SceneComponent::queueTickWrite` 하나). 남의 값은 늘 틱 전 값을 읽는다.
  큐는 (대상, 쓴 오브젝트 id, 순번)으로 정렬해 적용하므로 결과가 스레드 배정에 달리지 않는다. 워커가 쓰는 더티 플래그는 바이트 · `atomic<uint8>` relaxed(비트필드는 이웃
  비트를 덮고, 같은 값을 겹쳐 쓰는 것도 데이터 경쟁이다).
- **월드 합성은 `updateWorldTransformFromParent` 한 곳이고, 렌더 더티를 찍는 곳은 `onWorldTransformUpdated` 하나다.** 합성 경로를 하나 더 만들면 메시가 화면에서 얼어붙는다.
  트랜스폼 값은 전역 `SceneTransformStorage`(256 칸 페이지, 옮기지 않는다)에 있고 컴포넌트는 칸 번호만 든다. 쓰기 알고리즘은 `SceneTransformHierarchy` 가 갖는다.
- **부착** — 오브젝트의 루트 씬 컴포넌트는 하나(둘째는 primary 아래로), `AttachRule { KeepRelative, KeepWorld }`, `canAttachTo`(다른 매니저 · 소켓을 거친 순환 · 파괴 대기 부모)는
  미루기 **전에** 묻는다. 자식의 정의는 `getParent` 하나(소켓 자식 포함). 월드 값은 `setWorldPosition` · `setWorldTransform`, 크기는 월드 상자 하나(`getWorldBox` · `AABB::transformedBy`),
  경계는 컴포넌트가 선언한다(`getWorldBounds`). `transformVector` 는 방향(w=0) 변환이지 법선 변환이 아니다 — 법선은 `invert().transpose()`, 셰이더는 `swComputeWorldNormal`.
- **`forEachGameObject` 콜백에서 생성 · 파괴 · 이름 변경 · 풀 생성을 하지 말 것** — `shared_mutex` 가 재진입하지 않아 교착한다(`WalkScope` 가 Debug 에서 단언). 그런 순회는
  `getAllGameObjects( out )`. 컴포넌트 목록을 범위 for 로 도는 중에 붙이면 반복자가 풀린다 — 인덱스로. 콜백이 형제를 지울 수 있는 걷기는 핸들로 모으고 매번 다시 푼다.
  소멸자에서 미루는 경로를 타지 말 것(`~SceneComponent` 는 `detachFromParentImmediate()`).
- **`tick()` 에서 `TaskManager::waitAll()` 을 부르지 말 것** — 렌더 기록 · 스트리밍 · 오디오까지 기다린다. 자기 스테이지를 `waitStage` 로.
- **`finishTick` 순서**: 병렬 읽기 해제 → `_bTicking=false` → 지연 트랜스폼 → 지연 포스트틱 → `mergePendingAdds` → dirty 면 재 flush → 지연 파괴.
- **컴포넌트 해체는 `destroyComponentInstance` 한 곳이고 `removeComponent` 는 순서를 지킨다**(swap-remove 금지 — 첫 일치 · primary · 안정 키가 순서에 기댄다). 컴포넌트는 `_pPool`
  (나온 풀)과 `_pTypeInfo` 를 든다 — 이름표 `_componentName` 은 런타임 라벨일 뿐이라 조회 키로 쓰면 안 된다(Shipping 에서만 힙이 깨졌다). 풀 키는 FQN 이다. 컴포넌트는 풀에서
  제자리에 생기므로 이동 연산을 되살리지 말 것. 멤버 없는 파생 컴포넌트도 `REFLECT_BODY` 가 필요하다.
- **`markPendingDestroy()` 는 묘비만 세운다** — 파괴 목록에 넣는 것은 `destroyObject` 다. 동시 파괴는 `tryMarkPendingDestroy()`(exchange)가 true 인 스레드 하나만 진행한다.
  이름은 살아 있는 오브젝트만 차지한다(`isNameTakenUnlocked`).
- **활성** — 계층 활성 재계산은 값이 그대로면 멈추므로 부모가 바뀌는 모든 자리가 그 자리에서 다시 맞춘다. `Component::isActive` 는 소유자 계층 활성을 포함하므로 오브젝트 `setActive`
  가 컴포넌트 비트를 복사하면 안 된다. 변화는 `onOwnerActiveInHierarchyChanged` 로 알린다. `TickRegistry` 의 목록에 들어 있는 것이 곧 활성이다.
- **플레이 수명주기** — 컴포넌트의 "시작됨" 비트가 시작 · 끝을 한 번씩 짝짓는다. 플레이 중 붙은 컴포넌트는 다음 틱 단계(`GT.Scene.tick.beginPlay`)에서 시작한다. 월드 플레이 상태는
  `SceneManager::setWorldPlaying`. 다시 만든 인스턴스(되돌리기 · 핫 리로드 · DontDestroyOnLoad)는 onBeginPlay 를 다시 받는다 — `markPersistent` 는 같은 id 로 **다시 만든다**.
- **틱 선언** — 기본은 "`onTick` 을 오버라이드했는가"(`OverridesOnTick_v<T>`), 우선순위는 `kMaxTickPriority`(63). 다른 그룹의 선행 조건이 있으면 뒤따르는 쪽을 그 그룹으로 옮긴다.
- **기본값은 만들 때 한 번이다**(CDO 자리, `DefaultPatch`). 모듈 등록 · 리로드에서 살아 있는 값에 다시 찍지 말 것. `ComponentDefaults` 의 기본값 파일은 없어도 되는 파일이다
  ("시도했는가" 로 한 번만 연다). 프리팹 형식은 읽을 때 정해 든다(`PrefabStateFormat`), 쓰기는 `PrefabAsset::saveToFile` 하나, 오버라이드는 `타입#n` 키.
- **찾지 말고 등록받는다.** 매 프레임 씬을 훑던 주광 조회가 GT 의 38 % 였다. 빛은 `LightRegistry`(종류별 칸), 카메라는 `CameraRegistry::selectCamera`(동률은 컴포넌트 id — 등록
  순서로 가르면 되돌리기마다 뒤집힌다), 그림자 빛은 `Scene::findShadowCastingDirectionalLight`. 프리미티브 등록부에 섞지 않는다(그릴 수 있는 것만 담는 계약).
- **씬 로드** — `SceneManager` 대기열은 한 자리다. 밀려난 요청 · `shutdown` · 취소도 약속에 `nullptr` 을 채워야 `future.get()` 이 영원히 멈추지 않는다. 로드 중 모듈 팩토리가 바뀌면
  (`getFactoryHeadSerial`) 다시 짓는다. `SceneManager::shutdown` 은 씬을 내리기 **전에** 활성을 비운다.
- **물리** — `stepPhysics` 는 틱과 트랜스폼 적용 뒤에 한 번 돈다. 콜라이더는 틱하지 않고 틱 안의 질의는 지난 step 을 본다. `onOverlapBegin( const OverlapInfo& )` 안에서는 스폰 ·
  파괴해도 된다. 순간이동은 `teleportTo` · `BodyMoveType::Teleport`(아니면 연속 바디가 그 길을 쓴다). 셀 범위는 `CellRange` 하나, 셀 순회 변수는 int64(`MaxInt32` 로 접히면 안 끝난다),
  `toCellCoord` 는 float64 로 나눈 뒤 접는다. 공간 색인 규약은 `Source/Engine/Spatial/README.md`.
- **"바뀌었나" 검사는 제곱 거리를 `MathUtil::EpsilonSquared` 와 비교한다**(`Epsilon` 이면 프레임당 1e-3 아래 움직임이 영원히 삼켜진다). `GpuSceneBuilder::bCamSame` 의 이력은 의도다.
  `float4x4::invert` 는 행렬식이 정확히 0 · NaN 일 때만 항등을 돌려준다(절대 임계값은 작은 부모 · 큰 직교 카메라를 깨뜨렸다).
- **시퀀서** — "지나갔는가" 는 `previousFrame < start <= frame`, 이전 프레임 없음은 `kNoPreviousFrame`(INT32_MIN — -1 은 frameMin 0 과 겹친다). 프레임은 배 정밀도로 곱하고 천분의 일을
  얹어 자른다. 반복 재생 시간은 한 바퀴 안으로 감는다(float32 는 10^6 초 근처에서 0.016 을 더해도 안 움직인다).
- **`getTags()` 와 `getOrCreateTags()` 는 다르다** — 공용 빈 상수를 `const_cast` 로 돌려주면 한 번 쓰는 순간 모든 오브젝트가 그 태그를 갖는다. `TagID::computeId` 하나가 리터럴 · 런타임
  태그 ID 를 만들고, 계층 비교(`Faction` → `Faction.Player`)에는 문자열이 같이 필요하다.

### 3-7. 그래픽스 · RHI · 셰이더

- **주의: DX12 `enqueueGpuRelease`(`_fenceValue`)** — 다른 스레드의 `waitForQueueDrain` 이 같은 값을 먼저 Signal 하면 기록 중인 프레임이 제출되기 전에 해제가 돌 수 있다.
  기존 DX12 해제 경로 전부에 해당한다(열린 일).

- **셰이더 굽기는 패스 종류 표 전체 × (머티리얼 없음 + 머티리얼) × `RenderViewMode` 를 굽는다** — 파이프라인 XML 에 나오는 패스만 곱하면 런타임
  (`ensurePassResources`)이 만드는 변형이 빠진다. 뷰 모드 define 의 정본은 `FrameRendererUtil::findViewModeDefine`, `ShaderBakeRequestTest.BakedManifestHoldsEveryRequest` 가
  커밋된 매니페스트를 대조한다. Vulkan 최소 판과 굽기 타깃(`-fspv-target-env`)은 `VulkanRHIApiVersion.h` 하나 — 1.3 미만 디바이스는 고르지 않는다(SPIR-V 1.6).

- **텍스처는 들일 때 굽는다(사용자 결정 2026-10-03 — UE 임포트 방식).** 런타임은 DDS 만 읽고, 원본은 `<domain>/textures_raw/` 에만 둔다(`CheckTextureFolders`).
  원본 ↔ DDS 대조는 원본 폴더마다 `bake.stamp`(원본 바이트 + 해석한 규칙 + 베이커 버전의 해시, DDS 해시) — `App --bake-textures` · `--check-textures`(헤드리스로
  에디터 모듈을 올린다, Shipping 은 이유를 남기고 실패), CI 대조는 `TextureBakeStampTest`. 함정: 굽기 동작을 바꾸면 `TextureBakerInternal::kBakerVersion` 을 올려야
  모든 스탬프가 어긋남이 된다. Debug 의 DirectXTex BC7 은 블록당 수백 ms 라 큰 원본은 Release App 으로 굽는다. 밉 · 변환은 `TEX_FILTER_FORCE_NON_WIC`(결정적).

- **디바이스 종료 순서는 `IRHIDevice::shutdown`(비가상 템플릿 메서드) 하나가 정한다** — releaseAllFor → `waitIdleInternal` → `detachCommandRecordingInternal` →
  `shutdownInternal`. 백엔드는 훅만 채우고 앞부분을 다시 적지 않는다(네 벌일 때 DX12 · DX11 이 이미 어긋나 있었다). 리스트 떼기는 `RHILiveCommandListUtil::detachAll`.
- **트랜지언트 크기를 따르는 자원(TAA 히스토리 · Present 캡처)은 `releaseTransientResources` 만 놓는다** — 패스 자원만 다시 세우는 셰이더 리로드는 이것을 다시 만들지
  않는다(놓으면 리사이즈 전까지 히스토리 0). 컴퓨트 상수버퍼는 `collectComputeConstantBuffers` 표 하나로 만들고 놓는다.

- **텍스처 슬롯 샘플러의 정본은 `bindingslots.hlsli` 의 `SW_ENGINE_TEXTURE_SAMPLER`(t0..t3, 선형 · 클램프)와 `SW_MATERIAL_TEXTURE_SAMPLER`(t5..t8, 선형 · 랩)** —
  GL 은 유닛마다 샘플러 객체, DX11 은 정적 세트. 이로써 네 백엔드 벤치 프레임이 바이트까지 같다(골든 이미지 백엔드마다 같은 그림).
- **기록 중의 CB 갱신은 `IRHICommandList::updateConstantBuffer`, 기록 밖(에셋)은 `IRHIResource::updateConstantBuffer`** — 한 버퍼는 프레임에 한 번만 쓴다.
- **배열 · 큐브 텍스처는 bindless 등록이 거부된다**(셰이더 테이블이 Texture2D 뿐). 면은 `_arrColorTargetSlice` · `_depthTargetSlice` 로 고른다.
- **첨부 `_resolutionDivisor` 를 쓰면 패스는 출력 첨부 크기로 열린다** — 한 패스의 출력은 같은 나눗수여야 한다(검증이 본다). Vulkan PSO 는 셰이더가 읽는 정점 속성만 건다.
- **머티리얼 · 인스턴스 형식 판정은 `AssetFormatRegistry::upgradeXmlWithActiveRegistry`**(씬 · 프리팹과 같다) — `ResourceManager` 없이 돈다. 본문의 enum 글은 `TypeRegistry` 가 필요하다.

- **거울 변환(월드 3x3 행렬식 < 0)은 컬을 뒤집은 PSO 변형으로 그린다** — 배치 키 · 정렬 키 · 투명 병합에 `_bReverseCulling` 이 들어 있고 PSO 변형 키의 한 축이다
  (언리얼 `bReverseCulling`). 트랜스폼만 바뀐 프레임도 부호를 다시 구한다.
- **깊이 첨부는 렌더 그래프의 쓰기다.** 그래프는 선언 순서상 앞선 쓰기를 생산자로 고르므로, 불투명 깊이를 읽을 패스(SSAO · 외곽선)는 투명 패스보다 **먼저 선언**한다.
  D3D11 은 첨부 전이(`prepareTextureForRenderTarget`)가 그 텍스처가 걸린 PS SRV 슬롯을 뗀다(`D3D11RecordingState::_arrPixelSrvTexture`).
- **머티리얼 텍스처 슬롯(t5..t8)의 샘플러는 `shaderslot::kMaterialTextureSampler`(LINEAR_WRAP) 하나** — GL 은 샘플러 객체를 유닛에 `glBindSampler`, DX11 은 정적 세트.

- **패스 종류 하나 = `RenderPassType` 한 값 + `RenderPassTypeTraits.cpp` 의 case 하나**(기본 셰이더 · define · 포맷 · 클리어 · 입력 계약 · 플래그). 전용 실행이
  필요할 때만 `executePass` 의 switch 에 case. 런타임 PSO 와 베이커가 같은 `selectRenderPassShader` 를 부른다. 마지막 열거자를 바꾸면
  `kRenderPassTypeCount` 를 직접 고친다(`RenderPassTest.TypeTraitsTableCoversEveryEnumValue` 가 잡는다). 리플렉션 매니페스트는 키 순서로 쓴다(결정적).
- **인스펙터 위젯 · CallInEditor 인자는 `ReflectBuiltins.xxx` 를 펼친 표 하나**(`InspectorBuiltinValue.h`) — 내장 타입을 더하면 `InspectorWidgetFor<T>` 특수화가
  없으면 컴파일이 선다. .xxx 의 문자열 줄은 `std::string`, 프로퍼티는 `sw::string`(`InspectorBuiltinCppType` 이 메운다).

- **셰이더 이름 규칙은 C++ 와 같다**(AGENTS.md "### HLSL", `CheckShaderConventions` 게이트): 함수 camelCase(공유 헤더는 `sw…`), 타입 PascalCase
  (공유 헤더는 `Sw…`, `_t` 없음), 필드는 C++ 멤버 이름에서 `_` 를 뺀 것. `g_*` · cbuffer · 시맨틱 · 진입점(`VSMain` · `PSMain` · `CSMain`)은 C++ 가
  문자열로 묶으므로 바꾸면 같은 커밋에서 C++ 도 바꾼다. **셰이더 쪽 개명은 계약 검사를 조용히 끌 수 있다**(`validate` 는 표에 없는 이름을 건너뛴다) —
  `ShaderBindingContractTest.EveryBoundNameIsInBakedReflection` 이 C++ 가 아는 이름이 구운 매니페스트에 있는지 본다.

- **렌더 스레드는 씬을 못 본다** — 런타임 경로의 `_pScene` 은 늘 null, `GpuSceneSnapshot` 이 유일한 통로다. CPU 폴백을 다시 만들지 말 것. 스레드 경계 타입은 `GpuSceneBuilder`(GT, GPU
  핸들 0) / `GpuSceneSnapshot` / `GpuScene`(RT)이다. 스냅샷은 GT 가 만든 것만 옮긴다 — RT 가 파생하는 값(`_indirectCommandCount`)을 실으면 GT 의 0 이 덮는다(증상: "카메라를 움직일
  때만 메시가 보인다"). 패킷은 자기완결이어야 하고 소유(`shared_ptr`)를 싣는다 — 생포인터는 `CheckRenderOwnership` 이 막고 예외는 `// SW_OWNERSHIP_RAW_OK: <이유>`.
- **셰이더 바인딩 계약의 정본은 `bindingslots.hlsli` 하나**(HLSL · C++ 같은 파일 include). 백엔드는 shaderslot 상수만 쓰고 바인딩 숫자 리터럴은 금지, `ShaderBindingContract` 가 구운
  바이너리를 대조한다(nogpu). `draw()` 에 CB 인자는 없다. 백엔드 간 공유 상수는 `RHITypes.h` `constant` 블록 하나 — 지금 값이 같아도 바뀔 수 있으면 공유하고 별칭도 금지.
- **상수버퍼를 드로우 · 디스패치가 나눠 쓰면 안 된다**(같은 함정이 두 번: 패스 CB, 컬링 CB). 값이 바뀔 때만 쓰는 CB 는 `RHIConstantBufferShadow` 로 링의 모든 칸에 채운다(안 그러면
  DX12 · Vulkan 이 세 프레임 중 둘을 0 으로 그린다). `updateConstantBuffer` 크기는 만든 크기를 넘으면 안 된다(GL 만 막는다) — 셰이더를 다시 구우면 머티리얼 CB 가 커질 수 있어
  `MaterialInstance` 가 `_constantByteSize` 로 다시 만든다. CB 칸 크기는 리플렉션, 쓰는 크기는 XML `shaderType` — 어긋나면 옆 프로퍼티 색이 오염된다(`writeBoundedValue`).
- **셰이더 산출물 스테일 판정은 내용 해시 하나다**(`ShaderBaker::computeEffectiveSourceHash`, mtime 금지 — 구운 바이너리를 커밋하므로 git 이 mtime 을 섞는다). 스탬프 헤더 `SWBAKE 3`,
  `bake.stamp` 는 CR 을 뗀 바이트로. 매니페스트가 바이너리보다 낡으면 바인더가 빈 레이아웃으로 그려 DX12 DEVICE_HUNG 이다 — `CookAssets.py --verify-shaders`. 백엔드 하나만 다른
  그림을 내면 ① 셰이더 산출물 ② 엔진 바깥을 다 되읽었는데 맞으면 셰이더 **코드 모양**(GL 드라이버가 DXC early-return 을 잘못 컴파일했다)을 의심한다. 프로브는 `nointerpolation`
  슬롯에 정수를 싣고, 모프 진단은 `-gv_morphDiag=1|2|3`.
- **베이크 바이너리 이름은 `ShaderBaker::computeBinaryFileName` 하나**(퍼뮤테이션 해시 포함 — 빠지면 define 이 GPU 에 안 닿는데 리플렉션은 맞아 보인다). 패스 define 의 정본은
  `FrameRendererUtil::getPassDefine`(런타임 PSO 와 베이커가 같이 부른다 — 갈리면 Shipping 에서만 그 드로우가 사라진다). "컬러 출력 없는 패스에는 픽셀 스테이지가 없다" 는
  `hasPixelStage` 하나. 실시간 컴파일 요청은 `ShaderCache::makeLiveCompileDesc`(경로 `Saved/ShaderCache/<rhi>/<해시>-<opt|dbg>/`), 베이커는 늘 최적화다. 셰이더 리로드 대상은
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
  기록 끝난 `ID3D11CommandList` 가 백버퍼를 붙든다(리사이즈).
- **DX12** — 펜스 대기의 시간 초과는 성공이 아니다. 커맨드 얼로케이터는 리스트마다(공유가 DEVICE_HUNG 의 진짜 원인이었다), 업로드 얼로케이터 Reset 은 그 슬롯의 펜스를 직접 기다린다.
  업로드 복사 리스트는 열어 두고 기록만 하며 `flushPendingUploads` 가 프레임에 한 번 내보낸다(`_uploadSlotMutex`). bindless 인덱스는 `acquireBindlessIndex(lock)` 로 집는 일과 뷰 생성을
  한 임계 구역에. CBV 는 256 B 정렬. drawIndirect 가 메시 VB 를 덮어쓴 적이 있다 — 렌더 변경은 스크린샷까지 본다.
- **Vulkan** — acquire 한 이미지는 present 로만 돌려준다(present 없는 프레임마다 acquire 하면 `UINT64_MAX` acquire 로 교착). 리소스 해제는 실제 GPU 펜스(단조 세대)와 이어야 한다.
  일회성 업로드는 `VulkanOneShotCommands` · 전용 풀 · `_queueMutex`. `vulkan1.3` DXC 는 `discard` 를 demote 로 내므로 기능을 켠다 — 구운 셰이더가 바뀌면 검증 레이어 로그를 다시 읽는다.
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
- **렌더 패스** — 패스 입력은 선언이 곧 바인딩이다(`RenderPassInputContract` 역할 표, 첨부 역할은 `_role` 선언 → 정본 이름 → 포맷). `findTransient` 의 핸들 0 은 백버퍼다 — 없는 첨부를
  열면 씬이 백버퍼로 간다. `beginColorPass` 가 false 면 그리지도 닫지도 않는다. WAR 간선은 "생산자 다음 쓰기", 같은 이름 패스는 거절, `executeParallel` 은 모든 레벨의 리스트를 먼저
  마련하고 못 하면 false(직렬 폴백은 앞 레벨을 두 번 그린다). 풀스크린 패스의 컬은 `None` 고정. 후처리 효과는 패스가 아니라 함수(`postchain.hlsl` + 퍼뮤테이션, 기준은
  `forwardpipelinestaged.xml` · `FusedPostChainMatchesStaged`). 깊이 프리패스는 `SW_PASS_DEPTH_PREPASS` · LessEqual, DX11 은 VS 만.
- **그림자** — 직교 투영 깊이 범위는 눈 기준 `[거리-반경, 거리+반경]`(아니면 그림자 항이 늘 1), 샘플은 `swSampleShadowAtWorld`, 회귀는 행렬로(`ShadowMatrixDepthRangeContainsScene`) ·
  픽셀로 보려면 바닥(`-gv_benchGround=1`). 그림자 시험은 그림자 깊이 쓰기를 끈 판과 **달라야** 한다. 렌더 차이가 같은 프로세스 안에서는 결정적이고 프로세스마다 갈리면 배치 순서를 의심한다.
- **타임스탬프 계약** — 칸은 패스 인덱스로 고정(흐르는 카운터는 병렬 기록에서 경쟁), 기다리지 않고 링 슬롯이 펜스를 지난 뒤에만 읽는다, 안 적은 칸은 음수. 계측 게이트는
  `SW_PROFILE_COMPILED` — Shipping 에서는 통째로 빠진다(로그에만 쓰는 값은 `[[maybe_unused]]`).
- **`GpuUploadQueue`** 는 GT 가 `buildFromScene` 뒤 · 스냅샷 전에 동기로 flush 한다. 워커 생성은 `RHICapabilities::_bThreadSafeResourceCreation`(GL 은 인라인). 비상 스위치 `-gv_gpuUploadQueue=0`.
- **디퍼드의 고정 비용은 채움률이다**(1280×720 2503 us · 640×360 864 us, 라이트 256 개 몫 ~600 us) — 타일/클러스터 컬링은 측정이 가리키는 자리가 아니다. GBuffer 는 같은 머티리얼
  셰이더에 `SW_PASS_GBUFFER` 를 얹는다(출력은 양쪽 다 구조체).
- **RHI 백엔드에 .cpp 를 더하면** `cmake/Engine/RhiBackendSources.cmake` 에도. 파일은 `<Backend>RHIDevice` · `…DeviceInit` · `…DeviceSubmission` 축으로. 백엔드는 별도 MODULE DLL 이라 Engine
  전역 변수를 extern 으로 못 쓴다 — 정책은 Engine, 메커니즘은 디바이스.
- **DDS 의 `dwFourCC` 는 D3DFMT 정수일 수 있다**(레거시 부동소수점). 스플래시는 32bpp 비압축만 받는다 — `splash.dds` 를 BC 로 저장하지 말 것. `.hdr` 은 굽지 않는다(8 비트 경로).

### 3-8. 에디터

- **에디터가 UI 스레드에서 놓는 GPU 자원(ImGui 텍스처 · 게임 뷰 렌더 타깃)은 `EditorDrawReleaseQueue` 에 맡긴다** — 렌더 스레드는 같은 draw 스냅샷을 여러 패킷에
  다시 그리므로 UI 스레드에서 읽은 펜스 값으로는 모자란다. 그 스냅샷 번호 이상을 그리는 프레임에서 `IRHIDevice::enqueueGpuRelease`(렌더 스레드에서만)로 넘긴다.
  새 렌더 타깃은 그리기 전 패킷이 샘플링할 수 있어 만들 때 클리어 색으로 채운다(Vulkan UNDEFINED 레이아웃).

- **Undo 의 오브젝트 편집은 엔진 데이터 명령(`ObjectSnapshotCommand` — 오브젝트 id · 이름 · 스냅샷)으로 기록한다** — 리로드를 넘어야 할 기록은 Engine 코드로 만든다.
  모듈 람다 명령은 에디터 리로드 때 `CommandStack::releaseCodeWithin` 이 뗀다(묶음은 안쪽 하나라도 걸리면 통째로). 대상 조회는 id, 같은 프레임에 지우고 되살린
  오브젝트는 지연 파괴 때문에 새 id 를 받으므로 이름으로 다시 찾는다. 선택 · dirty 는 `ObjectEditListener` 로.
- **에디터 동작 검증은 에디터 안 자체 시험** — `SW_EDITOR_SELF_TEST` 로 등록하고 `AppSmokeTest.EditorSelfTestsPassInsideTheEditor` 의 기대 목록에 한 줄 더한다
  (`-gv_editorSelfTest=<패턴>`, 실행 중에는 사용자 `imgui.ini` 를 읽지도 쓰지도 않는다). 워크스페이스는 오브젝트 GUID · 프리팹 경로 사본을 들지 않는다(id · 씬이 정본).

- **DebugDrawQueue 는 `endFrame` 에 비워진다** — 에디터 UI 보다 먼저 채운 것(게임 업데이트)만 보인다(`debug_draw` 시각화). 틱에서 채우는 생산자가 생기면
  이중 버퍼로. `ActionRoom::drawDebug` 를 부르는 곳은 아직 없다. 메뉴 경로는 `EditorCommandRegistry::validate` 가 "그려지지 않는 경로" 를 잡는다.

- **패널 · 팝업 · 인스펙터 · 시각화는 자기 .cpp 의 `SW_EDITOR_PANEL` · `SW_EDITOR_POPUP` · `SW_EDITOR_INSPECTOR` · `SW_EDITOR_VISUALIZER` 한 줄로 등록한다**
  (`EditorRegistry<T>`, (order, id) 정렬, 같은 id 거절). 메뉴 배치는 커맨드 표 줄의 `_menuPath` · `_menuOrder`(백의 자리가 바뀌면 구분선). 매니저 · 메뉴바에
  손 목록을 다시 만들지 말 것. 시각화 마스크 비트는 등록 순서의 index 라 순서 키를 바꾸면 비트 자리도 바뀐다(지금은 저장하지 않아 무해).

- **에셋 종류 하나 = `EditorAssetKind` 한 값 + `EditorAssetType.cpp` 의 `kArrAssetMatch`(판정 · 핫 리로드 칸) · `kArrKindInfo`(이름 · 라벨 · 패널 · 아이콘 · 색 ·
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
  0..1 비율은 `Units=ratio`, `PropertyMetaHintTest.UnitsMatchHowValuesAreStored` 가 본다. 검색은 `EditorListFilter`, 0 건 안내는 `drawNoSearchResultHint`(손으로 쓴 `stristr` 술어는 빈 필터에서
  목록을 지운다).
- **ImGui 수명 짝** — 플랫폼 백엔드 `shutdown()` 은 `BackendPlatformUserData` 를 확인한 뒤에만, 초기화 실패 경로도 전역을 걷는다, 팝업에 `p_open=&_bOpen` 을 넘기지 말 것(X 버튼이 `onClose`
  를 건너뛴다). 모달이 떠 있으면 키가 `InputManager` 까지 오지 않는다. 에디터 draw 스냅샷은 획득 → present **또는 포기**(`abandonPendingDraw`)로 끝난다. 입력 위젯은 `drawTextField` 하나.
- **에디터 상태 · 설정** — 설정 파일 경계는 "앱이 다시 쓰는가": `EditorConfig.json`(전체 재생성 — 테마만), 손으로 정하는 경로는 읽기 전용 `editordata.json`. Game View 클리어 색은
  `_clearColor`. 상태를 소유자에게 옮길 때는 그 소유자가 언제 서는지부터 본다(테마가 `EditorContext::initialize()` 전에 읽혀 조용히 버려졌다). DPI: 96 DPI 기준값 × 배율, 테마에서 곱하고
  되읽을 때 나눈다(짝이 깨지면 이중 배율). 에셋 핫 리로드는 에디터 소유(`ReloadFileManager`), 감시 접두어는 절대 경로.
- **기계 훑기의 알려진 오탐** — 델리게이트로 묶인 `&Class::method` 는 "죽은 함수" 로 잡힌다. `EditorThemeUtil` 팔레트 · 킷의 소비자 없는 세터 · 게터는 정상이다. 쓰이는지는 `= delete` 로
  바꾸고 빌드해 센다.
- **패널 시각 검증 사각** — 피킹 클릭 · 기즈모 우선순위는 사람이 눌러야 보인다. 그리기 회귀는 `Game View` 정점 수로 전후를 비교한다.

### 3-9. 핫 리로드 · 모듈 · 엔진 서비스

- **씬은 기동 단계 `ModuleTypes` 뒤에만 읽는다** — `TypeRegistry::areAllModuleTypesRegistered()` 가 거짓이면 `SceneManager::requestLoadFuture` · `SceneCooker::cookAllScenes` 가
  거절한다. 모듈 이미지 · 타입 등록은 그 단계에서 App 로더(`ModuleHost::loadModuleImages`)가 하고, 인스턴스는 RHI 뒤에 **게임 → 에디터** 순(그래야
  `-gv_editorStartupScene` 이 마지막 요청이 된다). Shipping 통째 링크(/WHOLEARCHIVE) 목록은 `sw_configureAppDependencies`(모듈 등록 뒤)에서만 읽는다 — App 이 GF · 게임보다
  먼저 add_subdirectory 되어 GF · 킷 · 게임의 등록기가 배포본에서 빠져 있었다. 쿠킹은 `ContentSource::SourceTree`(팩은 산출물이라 입력이 아니다)이고, MissingComponent 가
  든 씬은 굽지 않고 실패(종료 코드 → CookAssets)로 센다.

- **엔진 기동 · 종료 순서는 `EngineStartupStepList.xxx` 의 의존 칸이 정하고, 표는 그 순서대로 적는다**(UE `USubsystem` 의존 선언). 의존은 식별자 목록
  `{ A, B }` 라 오타 · 아래 줄 의존은 컴파일 오류(static_assert), 정렬은 의존만 보고 동점은 이름 순, 그 결과가 줄 순서와 같은지
  `EngineStartupSequenceTest.TableIsWrittenInStartupOrder` 가 본다(의존을 빼먹으면 진다). 종료는 초기화한 단계만 역순.
  새 단계는 호스트(EngineLoop · 시험 하네스 `Test/TestFramework/main.cpp`)마다 `<단계>StartupStep` 구조체 하나(initialize · shutdown · destroy, 기본은 no-op)를 더한다 —
  빠지면 `EngineStartupStepTable` 이 컴파일 오류. 해제(destroy)는 **표의 모든 단계**를 역순으로 돈다(실패 · 건너뜀 · 닿지 못한 단계 포함)라 본문은 null 안전이어야 하고,
  백엔드 교체는 `shutdownDependentsOf( RHI )` · `restartStoppedSteps()` 로 같은 initialize 본문을 다시 돌리므로 본문은 다시 설 수 있어야 한다(객체가 있으면 다시 쓰고
  디바이스 설정은 매번 건다 — 교체 뒤 `setMergeBatchesAcrossMaterials` 가 옛 디바이스 값으로 남던 결함이 이것). 로거 · 명령줄 · 크래시 핸들러는 두 호스트가 `EngineBootstrap`
  하나를 쓴다(로거 스레드는 메모리 프로파일러보다 먼저, 로거 객체는 맨 마지막 — 해제 중의 진단이 남는다). 서비스 표 칸은 낱말(`Required/Optional` ·
  `GameVisible/HostOnly` · `EngineCreated/HostCreated`, RuntimeAPI `ServiceListColumns.h`), `destroyAll` 순서는 `makeDestroyOrder()` 로 시험한다.

- **리로드 거절 사유는 옛 이미지를 내리기 전에 본다** — `LiveReloadManager::setOnValidateImage`(ABI · API 표)와 배치 콜백(`OnBeforeCommitBatchDelegate` 가 false
  면 아무것도 내리지 않음, 게임 상태 찍기 실패 포함)이 적용 전 실패를 막아 옛 모듈이 계속 돈다. 적용 뒤 결함만 `markGraphBroken`(UE Live Coding 과 같다).

- **모듈 코드를 쥘 수 있는 등록부는 `IModuleCodeHolder` 를 상속해 스스로 등록한다**(`releaseModuleCode` 에 손 목록을 다시 만들지 말 것). 보유자 객체는
  엔진(또는 App) 코드가 만들고 생성자를 .cpp 에 둔다 — 모듈 안에서 만든 보유자가 모듈보다 오래 살면 훑기가 내려간 vtable 로 뛴다.

- **에셋 핫 리로드의 경계: 임포트 · 감시 · 씬 알림은 에디터, 런타임 파일의 제자리 다시 읽기는 엔진 캐시.** `AssetHotReload` 에 종류별 코드를 넣지
  말 것 — 새 종류는 엔진에 `IAssetCache` 등록 + `EditorAssetTypeRegistry` 줄의 `_pCacheKindName`(· 굽는 종류는 `_pfnImportSource`).
  컴포넌트 알림은 `AssetHotReload::notifyAssetUsers` 가 `PROPERTY( AssetPath )` 값으로 찾아 `onPropertyChanged` 를 부른다 — 에셋에서 계산한 상태는
  `onPropertyChanged` 가 **값이 같아도** 다시 맞춰야 한다. 리로드 전용 컴포넌트 훅 · `#if !SW_SHIPPING` 가드는 두지 않는다.

- **모듈 리로드는 App 의 것이다.** Engine 에는 `IModuleHandleProvider` 창구만 두고 공개 헤더에 리로드 콜백을 두지 않는다. `LiveReloadManager` 는 `Source/App/Module/`(Shipping 에서 빠진다).
  Shipping 은 모듈을 내리지 않는다. 검증은 SmokeTest.
- **핫 리로드는 섀도 복사본을 올린다.** Windows 는 지연 로드 훅(`DelayLoadNotifyHook.cpp`)이 `GameFramework.dll` import 를 지금 복사본으로 돌린다(빼면 원본이 한 벌 더 올라와 정적 상태가
  둘). 리눅스는 SONAME 을 같은 길이로 제자리에서 고친다(`ModuleImagePatch`). 결속은 `verifyModuleBindings` 가 본다.
- **옛 이미지는 바로 내리지 않는다**(`deferImageUnload`, 배치 4 개, 배치 안에서는 의존하는 쪽부터). 다른 코드가 구독 중인 채널을 만든 이미지는 프로세스 끝까지 올려 둔다(언리얼도 같다 —
  되돌리지 말 것).
- **`engine::releaseModuleCode` 는 델리게이트 스텁 주소로** 그 이미지가 단 등록을 뗀다. 뗀 것이 있다는 경고는 모듈의 손 정리가 빠졌다는 뜻이고 늘 0 이어야 한다. 시험 함정: 몸통이 같은
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
- **엔진 서비스는 `EngineServiceList.xxx` 의 `owned` 열에서** `EngineOwnedServices::createAll()` / `bindInto()` 로 생성된다(호스트가 먼저 만든 것은 덮지 않는다, 정의는 `.cpp`, 자리는
  `Source/Engine/` — `Common` 이면 `CheckEngineLayers` 가 막는다). 호스트 대조는 `CheckEngineServiceBinding`. 시험의 서비스 흔들기 창구는 `test::rebindEngineServices` 하나.
  `EngineServiceTest` 의 기대값도 같은 X-매크로라 `gameAllowed` 값 자체가 틀린 것은 못 잡는다.
- **Engine 폴더 include 그래프는 DAG 다**(`RunEngineLayerGraph.py`, 다시 제안하지 말 목록은 `docs/07_EngineStructureVsCommercial.md` 4절). 일부러 그 층에 둔 것: 핸들 · `TagID` 는 Core,
  `CommandStack` 은 `EngineLoop` 소유(핫 리로드를 넘어 산다), `TileMapXml.h` 는 Engine. 엔진 창은 `WindowResizeEvent` 를 발행하지 않는다(델리게이트). 상태가 살아남아야 하면 Engine · App 에 둔다.
- **기동 순서**: 로거 · 크래시 핸들러 → `ResourceUtil::initialize()`(로거 뒤라야 진단이 남는다) → 설정 → `ResourceManager::initialize()` + `mountContent`. 종료는 `_rhi->shutdown()` 이
  `ResourceManager::shutdown` 보다 먼저, 오디오는 TaskManager 보다 먼저(`_voiceMutex` 로 `_bInitialized` 를 먼저 내린다), 로거를 세운 뒤 `MemoryProfiler`. 시험 호스트도 앱과 같은 순서
  (리플렉션 등록 → 설정 → ResourceManager).

### 3-10. Core · 태스크 · 메모리

- **보고의 "(sw 할당자 밖)" 은 CRT 합 − 태그 합**이라 프로파일러보다 먼저 잡힌 sw 블록도 들어간다 — MemoryProfiler 는 부트스트랩 맨 앞에서 선다. 새 스레드는 Unknown
  에서 시작하므로 띄운 쪽의 태그를 인자로 넘겨 첫 줄에서 건다. 배열은 `sw_new_array` · `make_unique<T[]>`(맨 `new` 는 `Style/RawNew` 가 막는다). 외부 라이브러리는
  공개 설정 지점으로만 sw 할당자에 잇는다(pugixml `set_memory_management_functions`, nlohmann 할당자 인자, zlib zalloc · zstd advanced · LZ4 extState, stb STBI_*).

- **메모리 태그(UE LLM 식)는 Debug 전용이다.** 거는 자리는 셋 — 기동 단계 표(`EngineStartupStepList.xxx`)의 태그 칸, 서비스 생성의 `kServiceMemoryTag<Type>`,
  하위 시스템 진입점의 `SW_MEMORY_SCOPE`. 태스크 · 병렬 청크는 **만든 쪽의 태그를 상속**한다(`TaskNode` · `ParallelGroup` 의 패딩 자리, 크기 그대로). 분포와 sw 할당자 밖
  몫은 `-gv_profileFrames` 보고의 "memory by tag" 와 ProfilerPanel 에서 본다 — Unknown 이 커지면 진입점이 빠진 것이다. ImGui 는 `SetAllocatorFunctions` 로 sw 할당자를
  지나므로 에디터 실행의 alloc/frame 에 ImGui 할당이 들어간다. GPU 메모리는 대상이 아니다(CPU 힙만).

- **프로세스 정적 캐시(`ShaderReflectionLibrary` 매니페스트 같은 것)는 엔진 종료 단계가 비운다** — 안 비우면 기동 뒤에 채운 몫이 종료 누수 검사(기준선 대비 바이트 ·
  블록 수)에 남는다(백엔드 교체 뒤 ~1.1 MB). 진단은 MemoryProfiler 세부 추적을 켜고 `destroyAll` 뒤 `getTopCallStacks( LiveBytes )`. 교체 전 백엔드의 매니페스트는
  종료까지 상주한다(상한 4 개라 둔다). 모듈 인스턴스 내리기는 에디터 · 게임 모두 타입을 걷은 **뒤** 서비스를 뗀다(`ModuleHostInternal::destroyInstance`).

- **STL 구성(`SW_ENABLE_STL_CONTAINER=ON`, CI `CI-Debug-STL`)은 C++17 이라 std 해시 컨테이너에 이종 조회 · `contains` 가 없다** — sw 쪽 얇은 클래스가 메운다.
  커스텀 컨테이너 전용 시험은 그 구성에서 건너뛴다.

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
- **명령줄** — 미등록 `gv_` 키는 `_mapPendingGlobal` 에 남았다가 모듈이 변수를 올릴 때 적용된다. gv_ 선언은 읽는 파일에, `extern` 재선언 금지(`SW_EXTERN_GLOBAL_VARIABLE_*` 은 정의 파일 밖에서
  읽을 때만 — `CheckGlobalVariableKinds`). 시험용 전역 변수는 `SW_TEST_GLOBAL_VARIABLE_*`(Shipping 에서 빠진다), 배포본을 스크립트가 조종할 것만 `SW_KEEP_IN_SHIPPING`. bool 이 아닌 `-gv_*` 에
  값을 빠뜨리면 경고 + 기본값.
- **싱글턴을 옮기지 않는 자리**: `CrashContextStore`(시그널 핸들러가 읽는다), 등록자 헤드 · `TestRegistry`(main 이전 자기 등록), `TagID` · `hashed_string` 인터닝(프로세스 전역이어야 뜻이 있다).
  Core 에 인스턴스가 필요하면 Logger 모양 — 인스턴스는 `EngineLoop`, Core 에는 포인터 슬롯.

### 3-11. 입력 · 오디오 · 게임프레임워크

- **통합 `ActionMap` 은 `InputManager::beginFrame` 이 갱신한다** — 게임 코드가 `update()` 를 다시 부르면 한 프레임에 두 번 흐른다(Input README 예제가 그랬다).

- **마우스 `getSmoothDelta` 는 프레임당 한 번 `IInputDevice::onEventsDispatched( dt )` 에서 정해진다** — `setSmoothing(f)` 는 1/60 초 동안 남기는 비율
  (τ = -(1/60)/ln f, 60 Hz 에서 옛 계수와 같다). 이벤트 처리기 안에서 스무딩을 다시 돌리면 폴링 레이트마다 감각이 달라진다. 프레임 이동은 `getMovementDelta()` 하나.
- **2D 콜라이더 판정은 `overlapsBounds`(순수 기하)와 `isTouching`(레이어 반영) 둘** — 둘 다 바디 등록 여부와 무관하게 같은 답(Unity `Bounds.Intersects` · `IsTouching`).
- **엔진 · 킷 컴포넌트는 태그를 붙이지 않는다** — 종류는 `GameObjectManager::forEachComponentOfType<T>` 로 찾는다(UE `GetAllActorsOfClass`). 프레임마다 쓰는
  소비자가 생기면 타입별 등록부(O(해당 타입))를 Release 로 재고 정한다. 글자 입력은 `InputManager::setTextInputCallback` 하나(UE `OnKeyChar`).

- **도구 에셋 종류는 표 하나** — 대화 노드는 `kArrDialogueNodeTraits` 한 줄 + 러너 switch 의 case 하나(`-Wswitch-enum` 이 짚음), 다음 노드는 러너 · 에디터 미리보기가
  같이 쓰는 `DialogueCursor::step`. 핀 번호 `nodeId*100+offset` 은 디스크 포맷. 타일맵 레이어 표(`kArrTileFlagLayerInfo`)의 XML 속성 이름과 줄 순서는 파일 형식이다
  (바꾸면 옛 맵의 그 레이어가 기본값으로 읽힌다 — `TileMapXmlTest.SavedBytesMatchTheExistingFormat`). `SequenceItemKind` 값은 JSON 정수라 번호를 바꾸지 말 것;
  시퀀서 이벤트는 `SequencePlayerComponent::registerSequenceEvent` 로 받는다.

- **`GameEvents.h` 의 이벤트는 프레임워크가 그 자리에서 낸다**(세이브 · 로드 완료 = `GameInstanceBase::save/loadStateToFile`, 레벨 로드 요청 · 완료 =
  `requestFirstScene` · `requestEntranceScene`, 일시정지 = `GameModeStateMachine`). `SceneManager` 를 직접 부른 로드는 LevelLoad 이벤트를 내지 않는다.
  낼 자리가 없는 이벤트는 두지 않는다.
- **스프라이트 클립 키(`transformKeys`)는 클립 타임라인의 초이고 루트(primary) 스프라이트에는 적용하지 않는다**(경고) — 움직일 스프라이트는 루트 아래에.

- **입력** — 창 메시지는 큐에만 넣고 장치 상태를 바꾸는 길은 `beginFrame` 의 재생 하나다(포커스 · 포인터 진입도 큐 순서 안). `RawInputEventType` 은 뒤에만 덧붙인다(리플레이 파일이 번호를
  담는다). 입력 시험은 메시지 → `beginFrame` → 조회 → `endFrame`. XInput 트리거도 `setAxis( 4 · 5 )` 로 넣어야 데드존이 먹는다. 리바인딩은 바인딩 종류를 지킨다(`getRebindSlotIndex`),
  바인딩 종류는 `kArrBindingKindTraits` 표 하나(+ `static_assert`, 저장소는 `-Wswitch-default`). 통합 ActionMap 은 `InputManager::beginFrame` 이 갱신한다.
- **오디오** — 볼륨 · 음소거 상태는 `IAudioSystem` 이 들고 백엔드는 `applyVolume()` 통보만, 음소거는 마스터 보이스 한 곳, BGM 은 `_musicGeneration`. WAV 는 파서가 먼저(팩 안은 파서만).
  실제 보이스 동작은 장치 없이는 확인할 수 없다.
- **피해 · 월드 UI(킷)** — 피해는 `UnitStatsComponent::applyTakeDamage` 한 자리에서만 깎인다. `DamageAppliedEvent` 는 큐로, 같은 프레임이 필요하면 `registerDamageApplied`. 월드 UI(HP 바 ·
  데미지 숫자)는 저장되지 않는 `SpriteInstanceBatch` 로 그린다 — 자식 컴포넌트로 만들면 씬 · 프리팹 · 스냅샷에 저장돼 다음 시작에 겹친다. 스프라이트 UV · 색은 인스턴스에 싣는다(같은 텍스처는
  한 배치). 확인용 씬 `Resource/game/empty/maps/spriteui.scene.xml`, 글리프 · 클립은 `Scripts/generate/GenerateSpriteTextures.py`.
- **GameData** 는 `GameInstanceBase::initialize` 가 서비스로 묶는다. 언어 코드는 `LocalizationManager::normalizeLanguageCode` 의 철자 하나. 로컬라이제이션 조회의 `const utf8*` 는 추가 전용
  `LocalizedTextArena` 에 있어 영구 유효하다. 대화 핀 번호(`nodeId * 100 + offset`)는 디스크 포맷이고 주인은 `DialogueGraphAsset` 하나다.
- **키트 소속은 의존 관계로 판별되지 않는다**(전부 Engine 만 include). 다른 장르도 쓰는 것(HP 바 · 데미지 숫자 · 중력)은 `UI/` · `Base/`. 리플렉션 대상 헤더는 소스와 같은 재귀 규칙으로
  모은다(다르면 새 폴더의 `REFLECT` 타입이 컴파일되고 등록만 안 된다).
- **설정 표의 열쇠는 타입이다**(`ensureConfig<T>( path, baked )`). Shipping 은 디스크의 `Config/` 를 보지 않는다. 고정 스텝 상한은 `EngineConfig::_fixedDeltaTime` · `_maxFixedStepPerFrame`
  (넘친 잔액은 버린다). `ModuleFrameState` 래치 지점이 둘인 것은 의도다(옮기면 에디터 Step 한 칸이 틱 없이 소비된다).
- **리눅스 스플래시** — `XPutImage` 는 1:1 이라 우리가 줄인다, `Expose` 마다 지워지므로 배경 픽스맵, `override_redirect` 창은 XWayland 에서 안 뜬다(EWMH `_NET_WM_WINDOW_TYPE_SPLASH`).
  서버가 "정상" 이어도 화면에 없을 수 있다 — 최종 확인은 사람 눈이다. 창의 `isVisible()`(지금 화면에 있나)과 `isVisibleRequested()`(의도)는 다른 질문이다.

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
  `Cb` · `Fbo` 풀어 쓰기, 되돌리기 스냅샷 바이너리 통일, `StringBuilder::appendFormat` 잘림(버퍼를 늘려 다시 포맷한다), `EditorViewportClient` 쪼개기(공통 빼기로 간다).
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
| `*Recipe`(`D3D12RHIResourceRecipe` · `VulkanRHISamplerRecipe`) | `*Preset` · `ShaderBakeRequest` |
| `poisonLiveReload` | `markGraphBroken` |
| Vulkan band | range(`SW_VK_SLOT_RANGE_SIZE`) |
| `HandleTable` · `ObjectHandle` | `SlotHandleTable` · `SlotHandle` |
| `isVisibleIntended` | `isVisibleRequested` |
| `RHI::applyPendingChange` | `RHI::recreateDevice` |
| `createParentDirectory` | `ensureParentDirectoryExists` |
| `transformNormal` | `transformVector`(방향 변환) |
| `-gv_editorOpenAllPanels=1` | `-gv_editorOpenPanel=all` |
| `RenderResourceXml` | `Serialization/Format/ReflectedXmlFile` |

일부러 둔 용어: stamp · kit · bake · cook · orphan · chord · pin.
