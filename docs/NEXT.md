# 다음 세션 인계

> 질문: 무엇이 남았고 무엇부터 하는가?
> 독자: 이어서 일하는 세션(에이전트와 사람).
> 결론: **남은 일**만 본다. 끝난 일은 git 기록에, 지킬 규칙은 CLAUDE.md · AGENTS.md · `docs/agents/` · 스킬에 있다 — 여기에 되풀이하지 않는다. 끝나면 줄을 지운다.

## 일하는 법

절차는 스킬이 정본이다: 묶음 끝 검증 · push `bundle-verify`, Linux `linux-verify`, 시나리오 실패 `scenario-debug`. 시나리오 체계 · 시간 기록은 `Test/README.md`. 사람이 보는 실행: `py -3 Scripts/dev/WatchScenarios.py [--preset] [--backend] [--stages a,b] [체인…]`.
병렬 작업자 워크트리는 main 에서 fast-forward 한 뒤 시작한다(옛 기준에서 시작한 갈래는 통합 때 수십 파일이 충돌했다).

## 상태 (2026-11-09)

- 작업자 W20 ~ W28 을 모두 main 에 합쳤다(App · 서버 전용 단위 테스트 통과, 빌드 경고 0). 전체 묶음 검증(VerifyBundle — 시나리오 · 골든 · Linux)은 아직 돌리지 않았다 — 아래 1 번이 첫 일이다.
- Android(Android-Shipping)는 대상 전체가 컴파일 · 링크되고 Vulkan 셰이더 쿠킹이 지난다. 웹(Web-Shipping)은 App.wasm 까지 지어진다.

## 남은 일

### 1. 묶음 검증 (먼저)

- `bash Scripts/dev/VerifyBundle.sh` 한 번. 작업자들이 알린 골든 변경을 기록한다: 외형 머티리얼 디퍼드 · 털 Marschner · 흙 색(materiallibrary · surfacelibrary · materiallineup · hairshading), 새 시나리오(ppmat_before/after/replace · decals_on · game.empty.decals · postprocess_material · saveroundtrip 셋), bodydynamics(Dancer 를 IK 가 들어 올림), charactereditor 몸 탭 캡처, 피부 층이 있는 캐릭터(구운 알베도).
- 통합 때 확인하지 못한 동작: 셋이 시나리오에서 본다 — Vulkan AMD 브레드크럼 · 128 타임스탬프 슬롯(실장치), DX12 placed 텍스처(`D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES` 가 RT+SRV 를 받는지), 배포 서버(shipping-check — 서버가 `Window/Linux` · `Input/Linux` 를 짓지 않는다).

### 2. 플랫폼 ([PlatformTargets](PlatformTargets.md) "남은 일")

- **Android:** `CookAssets`(호스트 빌드로 굽기) 통과 → `py -3 -m Scripts android-apk --game <게임>` → 실기기에서 기동 · 회전(pre-rotation 방향) · APK 팩 읽기 · 터치. JNI 클립보드(`AndroidClipboard.cpp` 는 지금 `isAvailable` false).
- **웹:** 브라우저에 띄우기(팩 패키징 · COOP/COEP 로컬 서버 · 브라우저 MCP 로 기동 · WebGPU · 입력 · IDBFS), WebSocket 전송(클라이언트)과 서버 WebSocket 수락.
- dzn SPIR-V→DXIL 충돌.

### 3. 통합 담당이 직접 (실행 · 측정이 필요)

- **에디터 첫 패널의 느린 프레임:** 새 패널이 처음 열리는 프레임이 약 1 초. `-gv_traceSlowTickMs` 트레이스로 그 프레임의 동기 일을 찾아 비동기로.
- **시나리오 대기:** boat · clothshowcase(골든 앞 구간이 물리 이력), waterplay(강체 속도 · 잠듦 감시 값으로 조건 대기).
- **측정:** 유니티 배치 크기 12 · 24 · 48(Release 깨끗한 빌드), [DesignBacklog](DesignBacklog.md) S8 측정 표 7 행, 작업자 W25 가 남긴 GPU 비교(`gpu.timings` — `GPU.<프레임 패스>` · `RT.GPUScene.upload` · `RT.Light.uploadBytes` · `GT.GPUScene.build.viewTransparentOrder`, c05a3ee251 대 지금 — 이득이 노이즈 안이면 되돌린다).

### 4. Shooter3D 총 (사용자 지적)

- **소총 자세:** 권총 조준 클립으로 소총이 머리 옆에 들린다. 브랜치 `rifle-pose-wip`(5073ea9105)에 기반이 있다. 다음: Quaternius **Toon Shooter Game Kit**(묶음 페이지가 CC0 인지 먼저 확인)의 `Character_Soldier` 클립을 UBC 로 리타깃 → 몸 전체 사진 → 손 소켓 `Gun` 회전을 사진으로 맞춤 → 골든 다시.
- **걸을 때 총이 떨린다:** 원인 미확인. `-gv_shooterMotionTrace` 기록에 총 위치를 더해 손 · 카메라와 프레임별로 대조한다.

### 5. 작업자가 남긴 일

- **에디터 MVVM 남은 View 28 개:** Multiplayer 탭 7(탭 문맥을 세션 ViewModel 로), 인스펙터 코어(프로퍼티 그리드에 편집 모델 층 먼저), 큰 패널(Hierarchy · Profiler · MapCheck · PrefabEditor · ContentBrowser · Sequencer · AnimationRewind · RigDebug · BoneHierarchyPopup · StreamingCells · VolumetricFog), RenderTarget, 감시 값 4. 다 옮기면 `_migratedViews` 표와 린트를 지운다([EditorUI](agents/EditorUI.md)).
- **저장 왕복 시나리오:** Catalog · InputMap · BehaviorTree · Rig · Terrain · AudioEvent/Graph — 그 전에 패널이 `scratch` 사본을 가리킬 문서 경로 입구([StructureRefactor](StructureRefactor.md) §5).
- **렌더링:** Vulkan placed 텍스처(해제 큐에 메모리 참조 세기), TransientLeasePool 을 뷰 풀에 잇기, 비동기 컴퓨트 큐 패스 타임스탬프 · Tracy 프레임 패스 내보내기, 같은 자리 후처리 머티리얼이 앞 결과를 이어 읽기, 박막 · 털 · 스타킹 · 비단 외형의 디퍼드(G버퍼 칸 부족 — 포워드 전용으로 남김), 몸 머티리얼이 피부 층 표면 덮어쓰기(`_skinLayerSurfaceMap`)를 읽기, 텍스처 전이 · VFX · 프로브 디스패치의 버퍼별 전이를 묶음 API 로.
- **캐릭터:** 문신 집기를 바인드 자세가 아닌 지금 포즈로, 부품 오브젝트도 집기, 몸 탭 동작 시나리오.
- **결과 형식:** Core 밖 기준선 줄이기 → 0 이 되면 `Expected.h` 두 방향 변환 삭제([Errors](agents/Errors.md)). `ReflectedValueUtil::findBuiltinIndex` 를 None 이 있는 열거형으로, int32 본 · 버스 표와 Core 찾기 함수의 -1 감시값.
- **SOLID #14:** MemoryProfiler 누수 창을 `MemoryLeakWindow` 로, `judgeLeakCurve` · `printLeakWindowReport` 를 멤버로([StructureRefactor](StructureRefactor.md) §7).
- **표면 변형:** 지형 정점 셰이더가 `SurfaceDeform` 텍스처와 `g_SurfaceDeformParams` 를 읽기(네 백엔드 — [DesignBacklog](DesignBacklog.md)).

### 6. 리소스

- **Sprite Fusion 타일셋**(상세 페이지마다 `License: CC0`): Castle · Tiny Swords(Pixel Frog), Dungeon(0x72), Four Seasons Platformer(Analog Studios), Retro Gameboy(Ben Leggett) — `https://www.spritefusion.com/downloads/tilesets/<이름>.zip`. 1Bit Platformer(Kenney)는 Kenney 원본에서.
- **Sprite Fusion VFX 25 종**(MIT — 저작권 고지를 credits 에): 캔버스에 그리는 TypeScript 코드다 → 브라우저 MCP 로 재생해 스프라이트 시트로 굽는다.
- **PixelFX Piper**(무료 브라우저 효과 생성기): 이용 조건을 먼저 확인해 credits 에, 기본 효과를 시트로 내보내 층 쌓기 기법으로 효과를 만든다.

### 7. 그 뒤

- Empty 게임 이름을 TestBed 로 전수 개명(옛 이름 린트로 막기).
- 스캐폴드 `new-online-kit` 로 키트 하나를 실제로 지어 보기. 게임을 `-cooked-content` 로 띄워 경로를 조립해 읽는 에셋이 빠지지 않는지 보기.
- 남은 단위 테스트: `py -3 Scripts/dev/TestCoverage.py` 의 "없음" 폴더, 온라인 `AccountTicketConnect` 의 만료 · 재시도 길(성공 길은 `net.ticketconnect` 시나리오가 본다).
- 흔들리는 게임 골든: 다시 지면 그 로그의 단계 · 백엔드 · 직전 `Assets.Pending` 을 적는다.
- **Linux**(큰 일이 모두 끝난 뒤 — 사용자 결정): `LinuxScenarios.py all`, `.linux` 골든, 드라이버를 고르는 칸(dzn · Mesa d3d12 GPU), 에디터 체인 단계 약 18(WSLg · Xvfb 입력 초점 의심). lavapipe `blendEnable` + `R16G16_SNORM` 은 `VulkanRHIResourceFactory` 가 이제 막는다 — 재현해 확인.

## 사용자에게 받을 것

영수증 · 푸시 · 외부 로그인의 실제 계정과 키(지금은 로컬 가짜 끝점까지 — `Config/Server/Online/Providers/`, `Scripts/net/OnlineLocalRun.py`). 새 의존성(인코더 라이브러리)의 라이선스.

## 꼭 알아야 할 결정

- **리소스 규칙(2026-11-09):** 받아 오는 리소스는 무료 · 공개 라이선스(CC0 우선, MIT 처럼 재배포가 되는 것은 고지와 함께)만 — 묶음 페이지의 라이선스를 확인해 credits 에 URL · 날짜. 출처 하나에 고집하지 않고 뼈대가 달라도 리타깃한다. 에이전트가 리소스를 직접 그리지 않는다. PixelFX Piper(무료)는 쓴다. Agent Sprite Forge 는 도입하지 않는다(유료 생성 API 필요). Quaternius 는 2026-08 QAL(원본 재배포 금지)로 바뀌었다 — 새 묶음은 그 묶음 페이지가 CC0 일 때만. 원본 바이너리는 Git LFS, 받아 둔 팩은 `/AssetLibrary/`(무시)에 두고 쓰는 것만 `common/models_raw · textures_raw/<출처>/<묶음>/` 로.
- 범용 스크립팅 언어는 도입하지 않는다(실제 요구가 확인될 때만 — [판단](DesignBacklog.md#핫-리로드와-스크립팅-언어-판단)). 런타임 아틀라스는 하지 않는다. 메모리 풀은 측정 뒤, GC 는 권하지 않는다.
- 웹은 wasm32 다(유니티 · Godot 웹과 같다 — memory64 는 Safari 미지원 · 경계 검사 비용). 교차 빌드는 호스트 빌드(`SW_HOST_BUILD_DIR`, 기본 `build/Ninja-Release`)의 ReflectionParser · App 을 쓰고 스스로 다시 짓는다. 교차 프리셋은 vcpkg 설치 폴더를 따로 쓴다.
- 최상위 `Mods/` 는 `Test/Mods/`(시연 모드는 테스트 자료). App 의 `Dev/` 는 남긴다(배포에 쓰지 않는 인터페이스를 두지 않는다).
- 줄끝: `.gitattributes` 가 `* text=auto eol=lf`.
