# Renderer — 한 프레임을 그리는 곳

RHI 가 "그래픽스 API 를 감싸는 층" 이라면, 여기는 그 위에서 **무엇을 어떤 순서로 그릴지**를
정하고 실행하는 층입니다.

## 폴더 = 한 프레임이 흘러가는 순서

```
Renderer/
  Pipeline/   파이프라인 "정의" — XML 로 기술되는 것
  Graph/      정의를 "실행 순서" 로 푸는 것
  Scene/      씬을 GPU 가 읽을 수 있는 데이터로 (GT 빌더 → 스냅샷 → RT 씬)
  Frame/      실제로 그리는 것
  Light/      씬 라이트를 한 구조버퍼로 (GpuLightBuffer) — 포워드·디퍼드가 같이 읽는다
  Canvas/     화면 2D(UI · 월드 글자) — CanvasRenderer(글리프 아틀라스 거울 · 구간 업로드, 사각형 구조버퍼 t15, 일괄마다 가위 + 루트 상수 +
              drawInstanced( 6, n ), canvas.hlsl). 게임 스레드가 칠한 Graphics/Canvas 의 그리기 목록만 읽는다
  Debug/      에디터가 읽는 통로 — RenderTargetRegistry(프레임 렌더타깃 목록) · DebugDrawQueue(선 · 구 · 상자 · 화살표 · 글자,
              지속 시간 · 카테고리. 넣은 것은 `endFrame` 에 확정돼 다음 에디터 프레임에 보이고, 씬이 멈춘 프레임은 시간이 흐르지 않는다)
  Capture/    격리 스튜디오 렌더 — PortraitRenderer(프리팹 초상화 · 썸네일 굽기)
  Cook/       오프라인 셰이더 쿠킹의 정책 — 무엇을 쿠킹할지(요청: 파이프라인 XML · 패스 종류 표 × 뷰 모드 · 머티리얼) · 전부 쿠킹(드라이버).
              Shader/ 는 한 장을 쿠킹하는 법만 안다
  RenderThread.cpp/h   위를 구동하는 스레드
```

### Pipeline/ — 무엇을 그릴지 기술한다

- `RenderPassAsset` — **바인드 템플릿**. 첨부(attachment)의 포맷·클리어 값.
  `Resource/engine/renderpass/*.xml`
- `RenderPipelineAsset` — **패스 그래프**. 어떤 패스가 무엇을 입력받아 무엇을 출력하는지.
  `Resource/engine/pipeline/*.xml`
- `RenderPipelineAssetCache` — 위 둘의 로드·캐시
- `RenderPassType` (RenderPassAsset.h) — 패스 타입 이름. XML 의 `_type` 이 이 열거형으로
  해석되고, 해석되지 않으면 `RenderPipelineAsset::validate` 가 잡습니다.
- `RenderPassTypeInfo` — 패스 종류 하나의 사실을 **enum 값마다 한 줄**로 모은 표입니다. 기본 셰이더(EngineDefaultAssets 칸) ·
  PSO 기본 상태 · 패스 define · 컬러 RT 수 · 그리는 대상(씬 메시 · 일반 풀스크린 · 컴퓨트) · 대신할 PSO · 입력 계약.
  엔진 PSO 등록(`ensurePassResources`) · 셰이더 쿠킹 요청 · `executePass` 디스패치 · 파이프라인 검증이 모두 이 표를
  enum 으로 읽습니다. 새 포스트 패스는 enum 한 줄 + 표의 case 하나이고, 전용 실행 코드가 필요한 패스만
  `executePass` 의 switch 에 case 를 더합니다.
  씬 메시 패스는 **그릴 머티리얼을 define 으로 거를 수 있다**(`_pRequiredMaterialDefine` — 메시 외곽선 `MeshOutline` 은 `MATERIAL_OUTLINE` 이 있는 배치만).
  드로우 · 머티리얼 PSO 변형 · 쿠킹이 같은 판정(`FrameRendererUtil::drawsMaterialInPass`)을 쓴다 — [Graphics/README.md](../README.md) "셀 셰이딩" 절.
- `RenderPassInputSignature` — 패스 입력의 **역할**(필수/선택). 타입마다의 목록은 위 표의 칸이고, 검증과 실행이 같은
  칸을 보므로 "선언은 했는데 안 걸리는 입력" 이 생길 자리가 없습니다.

- 첨부마다 해상도 나눗수(`RenderPassAttachment::_resolutionDivisor` — 1 · 2 · 4)가 있고, 패스는 출력 첨부의 크기로 렌더 패스를 엽니다.

XML 이 선언한 포맷과 코드가 만드는 것이 어긋나면 조용히 잘못 그리거나 GPU 가 죽습니다.
그래서 로드 시점에 `validate()` 가 자기모순을 검사합니다 — 모르는 패스 타입 · 역할, 렌더 타깃이 될 수 없는 첨부 포맷(Unknown · BC* ·
R32G32B32_FLOAT), 한 패스 안에서 나눗수가 다른 출력, 컬러 출력이 없는 지오메트리 패스, 컬러 출력이 표의 수(2)와 다른 GBuffer 패스.
GPU 타임스탬프 칸(`FrameRendererUtil::kGpuTimedPassCapacity`)보다 패스가 많은 파이프라인은 로드 때 경고하고 넘치는 패스는 재지 않습니다.

### Graph/ — 실행 순서를 푼다

`RenderGraph` 가 패스들의 입출력 의존성으로 위상 정렬하고, **레벨**(같이 돌 수 있는 패스 묶음)
단위로 나눕니다. 백엔드가 병렬 기록을 지원하면 한 레벨의 패스들을 여러 스레드가 동시에
기록합니다.

> 주의: 같은 레벨의 패스 콜백은 **동시에** 돕니다. 콜백이 만지는 FrameRenderer 공유 상태는
> 반드시 보호하거나(락보다 먼저 "나누지 않을 수 있나" 를 볼 것) 기록 전 셋업으로 옮겨야 합니다.
> 기록 중에는 bindless 레지스트리 · PSO 를 만들지 않습니다(`IRHIDevice::setParallelRecording`).

### Scene/ — 씬을 GPU 데이터로

세 타입이 한 줄로 이어집니다 — **두 스레드는 가운데 타입으로만 만납니다.**

- `GpuSceneBuilder` (게임 스레드) — MeshComponent 를 훑어 인스턴스 배열·배치(batch)·머티리얼 원소 표를 만든다.
  재구축 판단 캐시와 머티리얼 원소의 영속 ID 가 여기 산다. GPU 핸들은 하나도 없다.
- `GpuSceneSnapshot` — GT → RT 로 옮겨지는 **전부**. 여기 없는 값은 옮겨질 수 없다(소유 규칙은 Graphics/README "소유와 수명").
- `GpuScene` (렌더 스레드) — 스냅샷을 받아 인스턴스 구조버퍼·배치 표·간접 인자·머티리얼 버퍼로 올린다. 씬을 볼 수 없다.
- `GpuMeshVertexPool` · `GpuMeshMorphPool` — RT 소유 GPU 풀. 씬 메시 정점을 한 버퍼에 잇고(멀티 드로우), 모프 · 스키닝 결과를 담는다. 모프 풀의 결과는 두 구간
  [모프 메시][스킨 인스턴스]이다. 스킨 데이터는 **원본**(`Mesh::getSkinDataId` — `Mesh::createSkinInstance` 사본은 원본의 번호를 나눈다)마다 한 벌(레스트 ·
  가중치)만 올라가고, 그리는 메시마다 인스턴스 표 한 줄(결과 시작 · 원본 시작 · 정점 수 · 팔레트 시작)이 둘을 잇는다 — 컴퓨트(meshskin.hlsl)는 디스패치 하나에
  정점마다 이분 탐색으로 인스턴스를 찾는다. 집합이 바뀌면 다시 올리는 것은 바뀐 쪽뿐이다(모프 레스트 · 원본 · 인스턴스 표). 팔레트(본 하나 = float4 셋 —
  행벡터 4x4 의 0 · 1 · 2 열)는 프레임마다 올라간다(`uploadSkinPalettes`, 메시 → 항목 표로 한 번에). 팔레트는 GT 의 `AnimationSystem` 이 만들고
  `GpuSceneBuilder::collectSkinPalettes` 가 매 프레임(수집 건너뛰기와 무관하게) 스냅샷으로 옮긴다 — 군중 묶음과 나누는 유닛은 건너뛰고 묶음이 한 번 싣는다.
  **모프 타깃**(스킨드 메시): 원본의 타깃 차이(움직이는 정점만)는 레스트 버퍼 뒤(`g_SkinDeltaBase` 부터)에 원본마다 한 벌, 정점마다 차이 구간(가중치 버퍼의
  셋째 float4)을 둔다. 유닛의 가중치는 팔레트 행 뒤에 붙고 인스턴스 줄이 그 시작 · 타깃 수를 든다 — 컴퓨트가 **스키닝 앞에** 레스트에 Σ 가중치 × 차이를
  더한다. 스킨 없는 메시의 모프는 이 길을 타지 않는다(남은 일).
- `GpuVertexAnimationPool` — RT 소유. VAT 가 걸린 메시(`Mesh::setVertexAnimation`)의 표를 t14 버퍼 하나에 잇는다. 표는 굽고 나면 변하지 않아 집합이 바뀔 때만 올린다.

언리얼의 GPUScene 과 같은 발상으로, per-instance 월드 행렬을 구조버퍼에 올려 VS 가 직접 읽습니다.
`FrameRenderer::execute( pScene )`(에디터·테스트의 직접 경로)도 자기 빌더로 스냅샷을 만들어 **같은 길**로 올립니다.

### Frame/ — 실제로 그린다

- `FrameRenderer` — 프레임 실행의 중심. **파일 이름이 곧 주제**가 되도록 나뉩니다.
  - `FrameRenderer` — 수명(initialize·shutdown·loadPipeline)과 프레임 진입(execute·executePacket)
  - `FrameRendererResources` — 패스 자원의 수명. 기록 **전에** 만들어야 하는 것들(엔진 PSO 등록 · 상수버퍼 링 · 머티리얼 폴백 · 출력 패스(Present · Canvas)의 포맷별 변종)
  - `FrameRendererTransients` — 첨부(렌더타깃)의 수명과 조회. 창 크기·파이프라인이 바뀔 때만 다시 만든다
  - `FrameRendererReadback` — 첨부를 CPU 로 읽는 길(테스트 픽셀 비교 · `-gv_screenshot` PPM). **프레임 경로가 아니다** — GPU 를 기다린다
  - `FrameRendererCompute` — 컴퓨트 프리패스 다섯(인스턴스 애니메이션 · 메시 모프 · 메시 스킨(meshskin.hlsl, 디스패치 하나) · GPU 컬링 · 인스턴스 정렬).
    그래프 패스가 아니라 그리기 전에 직접 걸린다
  - `FrameRendererConstants` — 뷰/라이트 행렬 등 **프레임 상수 시드** (프레임당 1회)
  - `FrameRendererPassExecute` — 패스 타입별 실행 분기. 주 출력(백버퍼 · 게임 뷰 RT · 스크린샷 캡처)은 `resolvePresentTarget` 하나가 고르고 Present · Canvas 가 같이 쓴다.
    Canvas(화면 2D)는 Present 뒤 같은 출력에 **Load** 로 그리고, 스크린샷 캡처 → 백버퍼 복사는 Swapchain 을 쓰는 마지막 패스 끝에서 한다(그 전에 복사하면 UI 가 캡처에 없다).
    UI 는 톤맵 뒤라 색각 보정(`gv_colorVisionMode`)을 캔버스 셰이더가 직접 건다(루트 상수 `g_SwCanvasColorVision` — 주 출력만, 함수는 `colorvision.hlsli` 하나)
  - `FrameRendererDraw` — 드로우 루프
  - `FrameRendererViews` — 추가 뷰(카메라마다 하나)의 자원 준비 · 해제 · 그리기. 아래 "다중 뷰"
  - `FrameRendererPso` — 머티리얼 PSO 생성. 뷰 모드(`RenderViewMode` — Lit · Unlit · Wireframe)가 얹는 define 은
    `FrameRendererUtil::findViewModeDefine` 하나가 정하고 셰이더 쿠킹 요청도 같은 함수를 부릅니다 — 쿠커가 쿠킹하지 않은 define 은 Shipping 에서 PSO 를 못 만듭니다
- `FrameRenderer` 가 **소유하는 셋** — 각자 뮤텍스와 수명을 가진 상태라 클래스로 떼어 두었습니다:
  - `PassConstantRing` — 드로우마다 하나씩 나눠 주는 패스 상수버퍼 슬롯 링(원자 커서, 프레임마다 되감기)
  - `RenderPsoCache` — 엔진 패스 PSO · 출력 패스(Present · Canvas)의 대상 포맷별 PSO · 머티리얼 퍼뮤테이션 변형과 바인딩 레이아웃.
    만드는 일은 `FrameRendererPso` 가, 소유와 해제 순서(변형 → 패스 → 출력 포맷별)는 캐시가 안다
  - `TransientAttachmentPool` — 이름으로 찾는 프레임 첨부(렌더타깃) 풀과 "이번 프레임에 이미 클리어했는가"
- `RenderView` — 뷰 하나의 행렬·절두체·컬링 · 정렬 상수버퍼. 메인 카메라 · 그림자 라이트 · 추가 뷰가 각자 갖는다.
  `RenderViewSettings`(출력 · 화면 사각형 · 해상도 배율 · 그림자 · 후처리 · 컷)와 `RenderViewRequest`(게임 스레드가 만드는 추가 뷰 하나)도 여기 있다
- `RenderViewCollector` — 게임 스레드에서 씬의 카메라를 훑어 주 뷰 설정과 추가 뷰 요청 목록을 만든다(`collectExtraViews`)
- `RenderViewScheduler` — 추가 뷰 중 이번 프레임에 그릴 것을 고른다: 갱신 주기 · 보임 · 예산(`gv_renderViewBudget`, 늦은 것 먼저, 같으면 덜 그린 것)
- `ShaderParameterBinder` — 셰이더 리플렉션이 알려준 슬롯에 실제 값을 바인딩
- `PassConstantValues` — 이름으로 담아 두는 패스 상수 값 저장소
- `FrameResourceRegistry` — 패스 스코프 이름→리소스 매핑
- `RenderFramePacket` — 게임 스레드 → 렌더 스레드로 넘기는 프레임 데이터
- 컴퓨트 디스패치(애니메이션·컬링·정렬)는 `FrameRenderer::dispatchInstanceAnimation` / `dispatchCullAndSort` 가
  커맨드 리스트에 직접 건다 — 별도 래퍼 클래스는 없다

### 다중 뷰 — 카메라마다 출력 하나

`CameraComponent::setRenderOutput` 로 카메라가 출력을 고릅니다 — **화면 전체**(주 카메라), **화면 사각형**(분할 화면 · PiP),
**렌더 텍스처**(CCTV 모니터 · 백미러 · 미니맵, 언리얼 SceneCapture2D). 렌더 텍스처 이름은 `rendertarget/` 으로 시작하고(`TextureCache::isRenderTargetPath`),
머티리얼은 그 이름을 보통 텍스처처럼 적어 읽습니다 — 카메라가 등록될 때 크기를 알려(`declareRenderTarget`) 캐시가 렌더 타깃 텍스처로 만듭니다.

- 뷰 하나(`FrameRenderer::ViewTarget`)가 **자기 것**으로 갖는 것: 트랜지언트 풀(해상도 배율이 곱해진 크기) · 컬링 입력(컬링 · 정렬 CB) · TAA 기록 · 커맨드 리스트 ·
  출력 텍스처. GpuScene 의 컬링 칸은 0 = 주 · 1 = 그림자 · 2.. = 추가 뷰(`kFirstExtraCullView`, 최대 `kMaxExtraRenderView`).
- 프레임 순서: 프리패스(애니메이션 · 모프 · 이번에 그릴 모든 뷰의 컬링) → 렌더 텍스처 뷰 → 주 뷰 → 화면 사각형 뷰(주 화면 위에 덮는다).
  패스 상수는 주 뷰의 시드에서 출발해 뷰-투영 · 풀 크기 · 플래그 · 컬링 칸만 덮어씁니다. 라이트 · 그림자 행렬은 프레임 공통입니다.
  그림자 볼륨은 주 시점 카메라에 맞춥니다(`DirectionalLightComponent::buildShadowProjectionForView`, `_shadowViewDistance > 0` 인 빛) — 추가 뷰가
  다른 곳을 보면 그 뷰의 그림자는 주 시점 볼륨 밖에서 빠집니다.
- 끌 기능: 그림자를 끈 뷰는 그림자 맵을 지우기만 하고, 후처리를 끈 뷰는 `SW_PASS_FLAG_SKIP_POST` 로 포스트 체인이 원본을 고릅니다.
- **컷 프레임**(`RenderViewSettings::_bCut`, `CameraComponent::markCut` → `consumeCut`): TAA 가 기록 자리에 이번 원본을 겁니다 — 지난 화면이 섞이지 않습니다.
  모션 벡터 · 자동 노출은 렌더러에 아직 없습니다.
- 직렬 경로의 패스는 `_frameCtx._pCmd` 에 기록합니다 — 뷰마다 그 리스트를 바꿔 둡니다.

### Capture/ — 초상화 굽기

`PortraitRenderer` 는 프리팹 하나를 **격리된 스튜디오**에서 그려 RGBA8 로 읽어 옵니다(`App --render-portraits=<prefab,..> [--portrait-size=N] [--portrait-dir=D]`,
런타임은 `EngineLoop::renderPortraits`). 격리는 둘입니다 — `SceneManager` 에 등록하지 않은 별도 `Scene`(게임 씬의 빛 · 안개 · 오브젝트가 끼어들지 않고,
게임 틱 · 저장 · 에디터가 보지 않는다)과 별도 `FrameRenderer` 인스턴스(주 렌더러의 TAA 기록 · 풀 · GpuScene 을 건드리지 않는다, 직접 경로
`execute( pScene )` 에 출력 크기 덮어쓰기). 같은 디바이스를 쓰므로 렌더 스레드를 멈추고(`RenderThread::waitIdle`) 그립니다. 프레이밍은 메시 로컬 경계 상자를
월드로 옮긴 것을 덮는 구로 정하고, 스튜디오의 키 라이트 하나 + 환경광만 비춥니다. 파일은 `Resource/ImageFileWriter`(PNG · DDS)로 씁니다.

### RenderThread

기본적으로 렌더링은 전용 스레드에서 돕니다(`gv_useRenderThread`, 기본 true).
게임 스레드는 `RenderFramePacket` 을 만들어 넘기고 계속 진행합니다.

## 읽는 순서 (처음이라면)

1. `Resource/engine/pipeline/forwardpipeline.xml` — 패스가 어떻게 기술되는지
2. `Pipeline/RenderPipelineAsset.h` — 그 XML 이 무엇으로 읽히는지
3. `Graph/RenderGraph.h` — 순서가 어떻게 정해지는지
4. `Frame/FrameRenderer.h` → `FrameRendererPassExecute.cpp` — 패스 하나가 어떻게 실행되는지
5. `Frame/FrameRendererDraw.cpp` — 드로우 한 번이 어떻게 나가는지

## 알아 둘 것

- **기본 씬에는 메시가 없습니다.** `SW_ACTIVE_GAME` 이 `Empty` 라서, 앱을 그냥 띄우면 드로우
  경로는 거의 실행되지 않습니다. 드로우 경로를 확인하려면 `EngineTest --test_filter=GpuSceneTest.*,RenderPassTest.*,RenderPassGpuTest.*`
  를 보세요 — 큐브를 넣고 실제로 그립니다.
- **`forwardpipeline` 은 완전한 체인**이라 레벨이 전부 1개입니다. 병렬 기록을 실제로 돌려
  보려면 `deferredpipeline` 을 써야 합니다(레벨 0 = Shadow + GBuffer).

## 함정 · 계약

- **한 `FrameRenderer` 로 두 씬을 번갈아 그리면 옛 배치가 나온다** — 씬 빌더의 수집 캐시(프리미티브 집합 세대)는 씬마다가 아니라서, 다른 매니저의 같은 세대
  번호를 "그대로" 로 본다. 픽셀 비교 시험은 씬마다 렌더러를 둔다(`RenderPassGpuTest.SkinnedMeshFollowsPaletteLikeCpuSkinning`).
- **UI 는 Present(톤맵) 뒤 Canvas 패스가 같은 출력에 Load 로 그린다** — 스크린샷 캡처 → 백버퍼 복사는 Swapchain 을 쓰는 마지막 패스 끝이다(그 전에 복사하면
  UI 가 캡처에 없다 — `RenderPassGpuTest.CanvasDrawsOnEveryBackend` 가 백버퍼 사본과 캡처를 견준다). Canvas 는 Swapchain 을 쓰는 마지막 패스여야 한다(검증).
- **스킨 팔레트는 `AnimationSystem::getUnits()` 에서 모은다, 레벨이 아니다** — `unregisterUnit` 은 레벨을 다음 평가까지 비운다. 레벨로 모으면 시체 하나를 걷는 프레임에
  모든 스킨드 메시가 팔레트 없이(바인드 포즈 = T 포즈) 한 번 그려진다(`GpuSceneTest.SkinPalettesSurviveAUnitLeavingTheFrame`).
- **스킨드 메시는 모프 풀의 뒤 구간이다** — 팔레트는 GT 의 `AnimationSystem` → `GpuSceneBuilder::collectSkinPalettes`(수집 건너뛰기와 무관하게 매 프레임) →
  스냅샷 → `GpuMeshMorphPool::uploadSkinPalettes`(풀 순서) → meshskin.hlsl. 팔레트 행은 행벡터 4x4 의 **열** 셋이다(행을 넣으면 전치된 회전).
  모프 타깃은 같은 컴퓨트에서 **스키닝 앞에** 더한다(가중치는 팔레트 행 뒤) — 스키닝 뒤에 더하면 민 방향이 본과 같이 돌지 않는다
  (`RenderPassGpuTest.MorphWeightsDeformBeforeSkinningLikeCpu`).
- **다중 뷰(`FrameRendererViews.cpp`)의 함정 셋.** ① 디스패치마다 쓰는 상수버퍼(컬링 · 정렬)는 뷰마다 따로다 — 정렬 CB 하나를 주 뷰 · 추가 뷰가 나눠 쓰면 마지막
  기록만 남는다(`RenderView::_sortCb`). ② 직렬 경로의 패스는 `_frameCtx._pCmd` 리스트에 기록한다 — 프리패스 리스트가 이미 닫힌 뒤라 그 자리를 뷰의 리스트로 바꿔
  두지 않으면 Vulkan 이 죽고 나머지는 0 을 그린다. ③ D3D 의 `CopyResource` 는 같은 포맷 · 크기만 받는다 — 컷 프레임은 원본을 기록에 복사하지 않고 기록 자리에
  원본을 건다, 캡처를 백버퍼로 옮기는 것은 출력이 백버퍼 크기일 때만. GL 기본 프레임버퍼는 아래 원점이라 `setViewport` 가 y 를 뒤집는다(오프스크린 FBO 는 그대로).
  창에 나간 그림은 `blitTexture( 0, 텍스처 )`(src 0 = 백버퍼, Present 전 프레임 스트림)로 읽는다 — `RenderPassGpuTest.PresentedBackBufferMatchesTheCapture` · `ScreenRectViewLandsInItsCornerOfTheBackBuffer`.
  추가 뷰의 메모리는 뷰 픽셀 × 첨부 바이트다 — 포워드 12 B/px(512² 뷰 3 MB), 디퍼드 64 B/px(TAA 기록 포함, 512² 뷰 17 MB · 1080p 주 뷰 133 MB). 뷰 한도 8 개를 다 512² 디퍼드로
  써도 주 뷰 하나 수준이라 공유 풀은 하지 않았다 — 4 인 분할 화면(뷰마다 1/4 화면)도 합이 주 화면과 같다. 추가 뷰는 그래프 전체(그림자 패스 포함)를 자기 풀로 돌아
  그림자 맵도 뷰마다 하나다(크기는 뷰와 무관한 그림자 품질 — 2048² D24S8 = 16 MB/뷰, 품질 3 은 64 MB) — 공유는 비용으로는 이득이 작았다([결정 기록](../../../../docs/09_Decisions.md) 3절), 화질이 문제가 되면 주 뷰 그림자를 먼저 그려 나눠 읽게 한다.
  초상화 굽기(`PortraitRenderer`)는 동기다 — 부르는 곳이 `App --render-portraits`(일괄 CLI) 하나뿐이라 렌더 스레드를 멈추는 편이 맞다. 런타임 · 에디터 썸네일이 쓰게 되면 그때 큐로.
- **기본 포워드 파이프라인의 톤맵(Reinhard `c/(c+1)`)은 흰색을 0.5 로 누른다** — 2D 화면이 회색으로 죽는다. 2D 는 `forward2dpipeline.xml`(`-gv_renderPipeline`).
  씬의 `_localRotation` 은 라디안이다(`Units=rad`) — "0,0,-90" 은 조용히 엉뚱한 방향이다.
- **투명 순서의 정본은 CPU 의 `sortTransparent` 하나다**(정렬 레이어 키 → 깊이 → 후보 번호). GPU `instancesort.hlsl` 은 압축된 목록을 인스턴스 번호
  오름차순으로 되돌릴 뿐이다 — 거기서 깊이를 다시 재면 정렬 레이어 · 직교 시선 축을 모르고 같은 깊이를 불안정하게 갈라 CPU 와 다른 순서를 낸다.
  추가 뷰는 `buildViewTransparentOrders` 가 발행된 꼬리를 그 뷰의 눈으로 다시 정렬해 순번(정렬 디스패치 t2) · 배치 순서 · 뷰 슬롯 스트림(DX11)으로 싣는다 —
  배치끼리 깊이가 엇갈리는 것과 512 넘는 투명 배치(Preserve)는 그 뷰에서도 주 순서다. 머티리얼을 넘어 묶는 백엔드(DX12 · Vulkan)는 색만 다른 투명 머티리얼이
  한 배치라 순서가 GPU 정렬 하나로 정해지고, 나머지는 배치 순서가 정한다 — 둘 다 `RenderPassGpuTest.ExtraViewSortsTransparencyFromItsOwnEye` 가 본다.
- **트랜지언트 크기를 따르는 자원(TAA 히스토리 · Present 캡처)은 `releaseTransientResources` 만 놓는다** — 패스 자원만 다시 세우는 셰이더 리로드는 이것을 다시 만들지
  않는다(놓으면 리사이즈 전까지 히스토리 0). 컴퓨트 상수버퍼는 `collectComputeConstantBuffers` 표 하나로 만들고 놓는다.
- **첨부 `_resolutionDivisor` 를 쓰면 패스는 출력 첨부 크기로 열린다** — 한 패스의 출력은 같은 나눗수여야 한다(검증이 본다). Vulkan PSO 는 셰이더가 읽는 정점 속성만 건다.
  원본을 비켜 읽는 효과는 `g_SourceTexel`(원본 역할 입력의 실제 크기, `registerPassTexture`), 깊이를 비켜 읽는 효과는 `g_OutlineParams.yz`(프레임) — 깊이를 나누지 않은 파이프라인 기준이다.
- **거울 변환(월드 3x3 행렬식 < 0)은 컬을 뒤집은 PSO 변형으로 그린다** — 배치 키 · 정렬 키 · 투명 병합에 `_bReverseCulling` 이 들어 있고 PSO 변형 키의 한 축이다
  (언리얼 `bReverseCulling`). 트랜스폼만 바뀐 프레임도 부호를 다시 구한다.
- **깊이 첨부는 렌더 그래프의 쓰기다.** 그래프는 선언 순서상 앞선 쓰기를 생산자로 고르므로, 불투명 깊이를 읽을 패스(SSAO · 외곽선)는 투명 패스보다 **먼저 선언**한다.
  D3D11 은 첨부 전이(`prepareTextureForRenderTarget`)가 그 텍스처가 걸린 PS SRV 슬롯을 뗀다(`D3D11RecordingState::_arrPixelSrvTexture`).
- **패스 종류 하나 = `RenderPassType` 한 값 + `RenderPassTypeInfo.cpp` 의 case 하나**(기본 셰이더 · define · 포맷 · 클리어 · 입력 계약 · 플래그). 전용 실행이
  필요할 때만 `executePass` 의 switch 에 case. 런타임 PSO 와 쿠커가 같은 `selectRenderPassShader` 를 부른다. 마지막 열거자를 바꾸면
  `kRenderPassTypeCount` 를 직접 고친다(`RenderPassTest.TypeInfoTableCoversEveryEnumValue` 가 잡는다). 리플렉션 매니페스트는 키 순서로 쓴다(결정적).
- **패스가 머티리얼로 배치를 거르면(메시 외곽선의 `_pRequiredMaterialDefine`) 드로우 · 머티리얼 PSO 변형 · 쿠커가 같은 `drawsMaterialInPass` 를 본다** — 하나라도 빠지면 쿠킹 안 된 변형을 런타임이 찾거나(Shipping 매니페스트 미스) 외곽선을 모르는 셰이더가 앞면 컬링으로 그려진다. 툰 구 시험은 정점 색을 흰색으로 둔다(생성기의 검증 색이 계단 위에 그라데이션을 얹는다).
- **렌더 스레드는 씬을 못 본다** — 런타임 경로의 `_pScene` 은 늘 null, `GpuSceneSnapshot` 이 유일한 통로다. CPU 폴백을 다시 만들지 말 것. 스레드 경계 타입은 `GpuSceneBuilder`(GT, GPU
  핸들 0) / `GpuSceneSnapshot` / `GpuScene`(RT)이다. 스냅샷은 GT 가 만든 것만 옮긴다 — RT 가 파생하는 값(`_indirectCommandCount`)을 실으면 GT 의 0 이 덮는다(증상: "카메라를 움직일
  때만 메시가 보인다"). 패킷은 자기완결이어야 하고 소유(`shared_ptr`)를 싣는다 — 생포인터는 `CheckRenderOwnership` 이 막고 예외는 `// SW_OWNERSHIP_RAW_OK: <이유>`.
- **상수버퍼를 드로우 · 디스패치가 나눠 쓰면 안 된다**(같은 함정이 두 번: 패스 CB, 컬링 CB). 값이 바뀔 때만 쓰는 CB 는 `RHIConstantBufferMirror` 로 링의 모든 칸에 채운다(안 그러면
  DX12 · Vulkan 이 세 프레임 중 둘을 0 으로 그린다). `updateConstantBuffer` 크기는 만든 크기를 넘으면 안 된다(GL 만 막는다) — 셰이더를 다시 구우면 머티리얼 CB 가 커질 수 있어
  `MaterialInstance` 가 `_constantByteSize` 로 다시 만든다. CB 칸 크기는 리플렉션, 쓰는 크기는 XML `shaderType` — 어긋나면 옆 프로퍼티 색이 오염된다(`writeBoundedValue`).
- **GPU 모프는 구조버퍼 풀 + 정점 셰이더 인덱스 읽기다**(Vertex 버퍼의 UAV · DX12 VB 의 UPLOAD 힙 · DX11 겸용 불가 때문). `Mesh::setVertices` 는 매번 GPU 버퍼를 다시 만들므로 매 프레임
  CPU 정점 갱신은 금지.
- **GPU 가 드로우 커맨드를 만든다** — 컬링이 가시 인스턴스 ID 를 압축해 커맨드를 생성한다(개수만 세면 보이는 쪽이 사라진다). 투명은 GPU 바이토닉 정렬, 블렌드 모드는 머티리얼이다.
  투명 배치는 정렬된 순서에서 연속한 같은 키를 묶는다(배치 순서 = 깊이 순서). 병합 키의 `_materialCb` 는 레이아웃이 MaterialCB 슬롯을 가질 때만 넣는다. `RHIDispatchIndirectCommand` ·
  `RHIDrawIndexedIndirectCommand` 는 C++ 참조가 0 이어도 지우지 않는다(인자 버퍼 레이아웃 정본).
- **GpuSceneBuilder 계약** — 전체 · 부분 수집은 같은 `fillCandidateFromPrimitive`. 집합 · 퍼뮤테이션 세대가 바뀌거나 더티가 1/4 을 넘으면 전체 수집. 부분 프레임에는 회수 시계를
  멈춘다(머티리얼 원소 회수는 돈다). 퍼뮤테이션 해시는 `SortKey` 에 직접, 정지한 씬의 재수집 트리거는 `MaterialUtil::getPermutationGeneration()`. 발행한 인스턴스 배열은 다시 고치지
  않는다(`GpuInstanceRing` 은 `use_count()==1` 슬롯에만 짓는다). `rebuildTransparentTail` 은 접두부 갱신과 같은 `runParallel` 의 블록 0 이다. `DrawCandidate` 의 `shared_ptr` 을
  날 포인터로 바꾸지 말 것(같은 주소에 새 메시가 태어나면 ABA). `PrimitiveRegistry` 의 더티는 렌더 상태 · 월드 행렬만 둘이다.
- **렌더 패스** — 패스 입력은 선언이 곧 바인딩이다(`RenderPassInputSignature` 역할 표, 첨부 역할은 `_role` 선언 → 정본 이름 → 포맷). `findTransient` 의 핸들 0 은 백버퍼다 — 없는 첨부를
  열면 씬이 백버퍼로 간다. `beginColorPass` 가 false 면 그리지도 닫지도 않는다. WAR 간선은 "생산자 다음 쓰기", 같은 이름 패스는 거절, `executeParallel` 은 모든 레벨의 리스트를 먼저
  마련하고 못 하면 false(직렬 폴백은 앞 레벨을 두 번 그린다). 풀스크린 패스의 컬은 `None` 고정. 후처리 효과는 패스가 아니라 함수(`postchain.hlsl` + 퍼뮤테이션, 기준은
  `forwardpipelinestaged.xml` · `FusedPostChainMatchesStaged`). 깊이 프리패스는 `SW_PASS_DEPTH_PREPASS` · LessEqual, DX11 은 VS 만.
- **그림자** — 직교 투영 깊이 범위는 눈 기준 `[거리-반경, 거리+반경]`(아니면 그림자 항이 늘 1), 샘플은 `swSampleShadowAtWorld`, 회귀는 행렬로(`ShadowMatrixDepthRangeContainsScene`) ·
  픽셀로 보려면 바닥(`-gv_benchGround=1`). 그림자 시험은 그림자 깊이 쓰기를 끈 판과 **달라야** 한다. 렌더 차이가 같은 프로세스 안에서는 결정적이고 프로세스마다 갈리면 배치 순서를 의심한다.
  바이어스는 텍셀 단위로 환산한다(`DirectionalShadowProjection::computeShaderParams` — 깊이 1 · 노멀 오프셋 2 텍셀). NDC 상수로 두면 볼륨에 비례해 커진다
  (360 m 볼륨에서 0.02 = 7.2 m: 작은 물체 그림자 소실 · 발치 분리). 그림자 맵은 화면 크기와 무관(`gv_shadowQuality` → 1024~4096), 맞춘 볼륨은 절두체 ∩ 받는 높이 띠 + 텍셀 스냅.
  필터는 3x3 PCF, 에뮬 백엔드(DX11 · GL)는 GatherRed 쌍선형 비교라 네 백엔드 그림이 같다.
- **타임스탬프 계약** — 칸은 패스 인덱스로 고정(흐르는 카운터는 병렬 기록에서 경쟁), 기다리지 않고 링 슬롯이 펜스를 지난 뒤에만 읽는다, 안 적은 칸은 음수. 계측 게이트는
  `SW_PROFILE_COMPILED` — Shipping 에서는 통째로 빠진다(로그에만 쓰는 값은 `[[maybe_unused]]`).
- **`GpuUploadQueue`** 는 GT 가 `buildFromScene` 뒤 · 스냅샷 전에 동기로 flush 한다. 워커 생성은 `RHICapabilities::_bThreadSafeResourceCreation`(GL 은 큐가 받지 않는다 — 렌더 스레드가 그 프레임에 만든다. 게임 스레드가 GL 자원을 만들면 렌더 스레드가 쥔 컨텍스트를 기다리다 시간을 넘긴다). 비상 스위치 `-gv_gpuUploadQueue=0`.
- **디퍼드의 고정 비용은 채움률이다**(1280×720 2503 us · 640×360 864 us, 라이트 256 개 몫 ~600 us) — 타일/클러스터 컬링은 측정이 가리키는 자리가 아니다. GBuffer 는 같은 머티리얼
  셰이더에 `SW_PASS_GBUFFER` 를 얹는다(출력은 양쪽 다 구조체).
- **인스턴스 배치를 든 컴포넌트는 `setOwnerComponent( this )` 를 부르고, 활성 변화(`onOwnerActiveInHierarchyChanged` · `_bActive` 의 `onPropertyChanged`)에 `markAllEntriesDirty` 를 부른다.**
  빌더는 `MeshComponent` 와 같은 규칙(`Component::isActive`)으로 소유 컴포넌트가 꺼진 배치를 뺀다 — 더티를 찍지 않으면 부분 수집이 지난 프레임 후보를 그대로 쓴다.

## 옮겨 온 내용 — 다시 쓰기 전

아래는 Graphics README 에서 원문 그대로 옮겨 온 절이다. 이 문서를 다시 쓰는 단위가 본문에 녹여 없앤다.

### 패스 입력 역할 계약 — 파이프라인 XML 의 선언이 곧 바인딩이다

풀스크린 패스(Lighting · SSAO · Bloom · Outline · TAA · Tonemap · Present)는 XML 의 `_listInput` 을 **역할 이름으로
전부 건다**(`FrameRenderer::registerDeclaredInputs`). 첨부 이름·포맷 → 역할은 `resolveRenderPassInputRole` 하나가
정한다: 고정 역할 이름(GBufferAlbedo · GBufferNormal · ShadowMap · AOColor)은 그 역할, 그 밖의 깊이는 SceneDepth,
나머지 컬러는 SourceColor. 셰이더는 역할 이름으로 읽는다(`g_SourceColorIndex` · `g_AmbientOcclusionIndex` …).
타깃은 선언한 출력 중 첫 번째로 존재하는 것이다.

`RenderPassInputSignature`(Pipeline/, 타입마다의 목록은 `RenderPassTypeInfo` 표의 칸)가 타입마다 읽는 역할의 필수/선택 목록이고, `RenderPipelineAsset::validate` 4번
검사가 로드 시점에 대조한다 — 계약에 없는 역할을 선언하면 "선언만 있고 바인딩되지 않는 입력", 필수 역할이 빠지면
"셰이더가 kInvalidIndex 를 읽습니다", SourceColor 가 둘이면 오류. 새 역할이 필요하면 (1) enum 과
이름표, (2) 패스 종류 표(`RenderPassTypeInfo`)의 계약 칸, (3) PassCB 의 `g_<Role>Index`, (4) 에뮬 슬롯 표(`swSampleIndex` · `commitBindlessTextureBindings`)
네 곳이다. `FrameRenderer::setInputRoleEnabled( role, false )` 는 그 역할을 걸지 않는 쇼 플래그다(테스트가 켬/끔을 비교한다).

### GPUScene 인스턴스드 드로우 (언리얼 방식)

메시 드로우는 per-instance world/material 을 **영속 구조버퍼**(`SwInstanceData` — 정의는 `instancedata.hlsli` 하나로 그래픽스와 컴퓨트
셋이 함께 쓴다, C++ `GpuInstance` 와 128 바이트 레이아웃 일치)에서 읽고, 배치당 간접 드로우 하나로 그린다(같은 PSO 의 배치들은 멀티 드로우 하나). VS 는 입력 어셈블러가 주는
인스턴스 슬롯(`SW_INSTANCESLOT` — 간접 인자의 startInstance(배치 시작) + 서수)으로 `swLoadInstance( input.instanceSlot )`
를 불러 월드 행렬과 `materialIndex` 를 얻어 PS 에 넘기고, PS 는 `SW_MATERIAL( materialIndex )` 로 셰이더 타입별 머티리얼
버퍼 `g_SwMaterials`(t9) 의 원소를 읽는다.
`g_SwInstancesIndex` 가 `kInvalidIndex` 면 `g_World`/`g_MaterialIndex` 폴백(풀스크린 · 픽스처 드로우). 인스턴스·머티리얼 버퍼는 **4백엔드가
같은 슬롯(t4/t9)** 을 쓰고 백엔드는 그 슬롯을 어떻게 거는지만 다르다:

| 백엔드 | t4/t9 구조버퍼를 거는 방법 |
|--------|--------------------|
| DX12   | t/u 슬롯 **디스크립터 테이블** — 등록 때 오프라인(CPU) 힙에 만든 뷰를 드로우/디스패치 직전 `flushSlotTables` 가 온라인 힙 블록에 `CopyDescriptors` 해 루트 테이블로 건다(언리얼 `FD3D12DescriptorCache`). CB 만 루트 CBV. 텍스처 배열은 힙 시작 테이블(t0 space1). 루트 예산 25/64 dword (`shaderslot::dx12`). SM6.6 힙 인덱싱은 쓰지 않는다 |
| DX11   | `StructuredBuffer` SRV — `createStructuredBuffer` 가 SRV 생성, `VS/PSSetShaderResources` |
| OpenGL | SSBO `glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 슬롯)` |
| Vulkan | 슬롯 세트(set 0, binding 16+슬롯 STORAGE_BUFFER). 바인딩이 바뀐 드로우 직전 **커맨드 버퍼 자신의 풀 묶음**(`VulkanDescriptorPoolSet`, 언리얼 `FVulkanDescriptorPoolSetContainer`)에서 세트를 할당해 쓴다(`flushSlotSet`) — 락 없음, 버퍼가 펜스를 지나 재사용될 때 통째로 리셋. 텍스처 배열·immutable sampler 는 set 1 |

- 드로우가 인스턴스를 찾는 길은 아래 "씬 드로우 경로" 절(인스턴스 슬롯 스트림 → 가시 목록 → 인스턴스 버퍼)이다.

## 소유와 수명 — 누가 만들고, 누가 놓고, 누가 빌리는가

"누가 소유하고 누가 빌리는지" 를 타입에 둔다. 그렇지 않으면 RT 가 만든 값을 GT 가 덮어쓰고, 스냅샷이 해제된
머티리얼을 읽고, 게임 모듈이 만든 객체를 엔진이 모듈 사후에 놓는다. 아래가 규칙이고 타입과 린트가 지킨다 — 문서만 믿지 말 것.

| 객체 | 만드는 곳 · 소유 | 렌더 스레드가 보는 방식 |
|---|---|---|
| `Mesh` | `Mesh::create*()` (Engine) · `MeshComponent` 의 `shared_ptr` | 스냅샷 배치가 `shared_ptr` 로 **함께 소유**, `upload()` 에서 역참조 |
| `Material` | `Material::create()` (Engine) · `MaterialCache` 의 `shared_ptr` + 참조 수 | 스냅샷 배치·원소가 `shared_ptr` 로 함께 소유 |
| `MaterialInstance` | `MaterialInstance::create()` (Engine) · `MeshComponent` 의 `shared_ptr` | 위와 같음. `updateRhi` 를 RT 가 부른다 |
| `Texture2D` | `TextureCache` 의 `unique_ptr` + 참조 수 | 보지 않는다 — 머티리얼이 **SRV 인덱스(값)** 로 실어 준다 |
| GPU 핸들(버퍼·텍스처) | `IRHIResourceFactory` | 파괴는 디바이스 해제 큐가 **펜스 뒤로 미룬다**(DX12·Vulkan). CPU 객체가 먼저 죽어도 된다 |
| `GpuSceneSnapshot` | GT `GpuSceneBuilder::exportCpuSnapshot` 이 프레임마다 만들어 `RenderFramePacket` 에 싣는다 | RT `GpuScene::adoptCpuSnapshot` 이 통째로 받는다 |
| GPU 슬롯·컬 뷰·간접 개수 | RT `GpuScene` 이 `upload()` 에서 만든다 | 스냅샷 타입에 없으므로 **옮겨질 수 없다** |

규칙 일곱:

1. **스레드를 넘어 역참조하는 것은 소유를 함께 싣는다.** 스냅샷·패킷의 멤버는 `shared_ptr` 이거나 값이다.
   생포인터는 키(`GpuMaterialElementKey`) 같은 **정체성**에만 쓴다 — 키는 비교만 하고 역참조하지 않는다.
2. **한쪽만 만드는 값은 그쪽 타입에만 있다.** 옮겨지는 것은 `GpuSceneSnapshot` 하나로 묶고, `export`/`adopt` 는 그
   타입을 통째로 옮긴다. 필드를 손으로 골라 복사하는 함수를 다시 만들지 말 것.
   만드는 쪽(`GpuSceneBuilder`)과 받는 쪽(`GpuScene`)은 **다른 클래스**다 — RT 가 씬을 읽거나 GT 가 GPU 핸들을
   만지는 코드는 컴파일되지 않는다. 씬 직접 경로(`FrameRenderer::execute( pScene )`)도 자기 빌더로 같은 길을 탄다.
3. **모듈 경계를 넘어 소유될 수 있는 객체는 Engine 의 `create()` 로만 태어난다 — 컴파일러가 지킨다.**
   `shared_ptr` 의 제어 블록은 `make_shared` 를 부른 DLL 에 산다. 그래서 Material · MaterialInstance · Mesh 의
   생성자는 `create()` 만 만들 수 있는 열쇠(`CreateKey`)를 요구한다: 모듈에서 `make_shared` 해도, 스택에 값으로
   두어도 **컴파일되지 않는다.** 덤으로 "shared 로 소유되지 않은 머티리얼" 이 존재할 수 없어 스냅샷이 언제나
   `shared_from_this` 로 소유를 빌릴 수 있다. (`Material*` 인자는 ADL 로 `std::make_shared` 를 끌어오므로
   Engine 안에서도 `sw::make_shared` 로 한정한다.)
4. **놓는 순서는 디바이스보다 먼저.** 기동 표에서 FrameRenderer 단계가 RHI 단계 뒤에 서므로, 역순 종료에서 스냅샷 소유를
   먼저 놓고 디바이스를 내린다. 소멸자에 맡기면 디바이스 사후에 GPU 자원을 돌려주려 한다.

5. **핸들 값은 디바이스 안에서만 정체성이다.** 새 디바이스의 첫 PSO·버퍼·디스크립터는 옛 디바이스와 **같은 번호**를
   받는다(할당 순서가 결정적이다). 핸들 값으로 "그대로인가" 를 판단하는 캐시 — `FramePassContext` 의 마지막 바인딩,
   `RenderGraphExecutionContext` 의 리소스 상태 — 는 디바이스를 내릴 때 함께 잊는다(`resetBindingCache` · `reset`).
   GPU 버퍼를 드는 객체는 핸들을 맨몸으로 들지 말고 **`RHIResidentBuffer`**(핸들 · 디바이스)로 든다 — `isResident()` 가
   "올라가 있다" 를, `getLiveDevice()` 가 해제해도 되는 디바이스만 돌려준다. 값이 남아 있다는 것만으로 "살아 있는
   디바이스의 것" 임이 보장되는 이유는 아래 5-1 이다.

5-1. **GPU 자원을 드는 객체는 `RHIRenderResource` 를 상속한다 — 예외 없이.** 언리얼 `FRenderResource` 와 같은 자리다.
   태어날 때 전역 등록부에 자기를 넣고, 디바이스 수명 이벤트가 목록 전체에 밀어 넣는다:
   `IRHIDevice::shutdown()` 이 **자원을 내리기 전에**(종료 1 단계) `releaseAllFor( this )`, `~IRHIDevice` 가 안전망으로
   `forgetAllFor( this )`, 새 디바이스가 선 직후 `EngineLoop` 이 `initAllFor( device )`.
   지금 상속하는 것은 `Mesh` · `Material` · `MaterialInstance` · `Texture2D` 넷이다.
   **바깥에서 캐시를 훑어 일괄 해제·일괄 재생성하는 함수를 다시 만들지 말 것** — 목록에서 빠진 것이 조용히 틀린다.
   통보 중에 남이 파괴될 수 있으므로(머티리얼이 텍스처 참조를 놓으면 그 자리에서 `Texture2D` 가 죽는다) 등록부는
   부르기 직전에 "아직 있나" 를 잠금 아래에서 다시 묻는다.
6. **게임 모듈이 씬 오브젝트를 들 때는 핸들이다.** 상태 복원(모듈 리로드 · RHI 교체)은 씬을 통째로 지우고 다시
   만든다. 생포인터는 죽은 주소가 되고 `ComponentHandle` 은 nullptr 로 끝난다. 절차 생성물은 스냅샷에 싣지 말고
   `onBeforeStateSerialize` 에서 걷고 `onAfterStateDeserialize` 에서 다시 만든다(`BenchScene`).

7. **GPU 리소스는 그리기 전에 만든다.** 렌더 스레드는 그리기만 한다. 게임 스레드가 "이번 프레임에 그릴 것" 을
   알고 있으므로 스냅샷을 내보내기 전에 `GpuUploadQueue` 로 넘겨 워커가 병렬로 만든다(`-gv_gpuUploadQueue=0` 으로
   끌 수 있다). 워커 생성 가능 여부는 백엔드가 답한다(`_bThreadSafeResourceCreation`) — OpenGL 은 컨텍스트가
   스레드에 묶여 큐가 받지 않고 렌더 스레드가 그 프레임에 만든다. 큐는 **앞당기는 장치**이지 유일한 통로가 아니다: 큐가 못 다룬 것은 렌더
   스레드가 그 자리에서 만든다(`Mesh::initRhi` 는 멱등이다).

무엇이 무엇을 지키는가: 옮겨지는 값의 집합은 `GpuSceneSnapshot` **타입**이, 생성·소유 방식은 **패스키 생성자**가
컴파일 시점에 지킨다. C++ 가 못 막는 것은 "옮겨지는 구조체에 원시 포인터 필드를 추가하는 것" 하나이고, 그것만
`Scripts/lint/gate/CheckRenderOwnership.py` 가 본다(CTest `lint` 라벨 · pre-commit).
재현·회귀 테스트: `RenderPassGpuTest.MaterialLifetimeFollowsPacket` (ASAN 프리셋에서 해제 후 사용을 잡는다),
`RenderPassGpuTest.RendererSurvivesDeviceRecreate` (디바이스 재생성 뒤 유리 큐브).
헤드리스 재현: `-gv_rhiSwapAtFrame=30 -gv_rhiSwapTo=<0..3>` (DX11=0 · DX12=1 · Vulkan=2 · GL=3) 과 `-gv_screenshotFrame=100`.

## 설계 메모 — 언리얼과 같은 것, 다른 것

남은 일은 [docs/06_Backlog.md](../../../../docs/06_Backlog.md) 의 그래픽스 절이 정본입니다. 여기에는 구조와 주의만 둡니다.

**언리얼과 같은 자리:**

- **Present PSO 는 대상 포맷마다 하나.** PSO 의 렌더 타깃 포맷은 바인딩된 타깃의 실제 포맷(`IRHIResourceFactory::getTextureFormat( handle )` ·
  `IRHIDevice::getBackBufferFormat()`)에서 뽑는다. `buildPresentPsoVariants` 가 셋업에서 백버퍼 · 오프스크린 포맷을 미리 만들고,
  기록 중의 `ensurePresentPso` 는 조회만 한다 — PSO 생성은 락 없는 핸들 표 · Vulkan 렌더패스 캐시를 건드리므로 태스크 워커에서 만들면 안 된다.
- **Vulkan 슬롯 세트 풀은 커맨드 버퍼 쌍이 들고 다닌다**(`VulkanDescriptorPoolSet`, 언리얼 `FVulkanDescriptorPoolSetContainer`). 쌍이 GPU 펜스를
  지나 돌아온 뒤 `beginCommandList` 가 통째로 리셋한다 — 할당 경로에 락이 없다.
- **DX12 루트 시그니처 25/64 dword**: CB 는 루트 CBV, t/u 슬롯은 디스크립터 테이블(`flushSlotTables` 가 바뀐 테이블만 온라인 힙 블록에 복사).
  `ShaderBindingValidatorTest.Dx12RootSignatureFitsBudget` 가 계약에서 예산을 계산한다.
- **패스 상수버퍼는 드로우마다 슬롯을 받는다**(`PassConstantRing`, 기록 전에 `PassConstantRing::ensureCapacity` 로 배치 수만큼). 한 버퍼를 드로우들이
  나눠 쓰면 GPU 는 제출 뒤에 읽으므로 모두 마지막 값을 본다 — `RenderPassGpuTest.MultiBatchPassKeepsPerBatchConstants` 가 메시 둘로 고정한다.
- **머티리얼 원소는 영속 ID**(GPUScene 식): 처음 본 쌍에만 자리를 주고, 안 쓰이면 지연 회수하되 **자리를 옮기지 않는다**(인스턴스에 적힌
  materialIndex 가 엉뚱한 원소를 가리키게 된다). `GpuSceneTest.MaterialElementIdsPersistAcrossBuildsAndAreFreed`.
- **머티리얼 폴백 버퍼는 stride 마다 하나**(`ensureMaterialFallbackBuffers`, stride 는 `ShaderBindingSlot::_elementStride`) — SRV 의 구조 stride 는
  셰이더 선언과 같아야 한다(RDG 더미 버퍼와 같은 규칙).
- **인스턴스 원소 레이아웃은 시험이 대조한다.** `ShaderBindingValidatorTest.InstanceElementLayoutMatchesCpuStruct`(nogpu)가 쿠킹된 바이너리의
  stride · 필드 오프셋을 `GpuInstance` 와, 컴퓨트 쪽 이름(`g_Instances` · `g_InstancesRW`)까지 같은 표로 본다.
- **투명 순서의 정본은 CPU 한 곳**(`GpuSceneBuilder::sortTransparent` — 정렬 레이어 키 → 깊이 → 후보 번호). GPU 컬링이 투명 배치를 압축한 뒤
  `instancesort.hlsl` 은 깊이를 다시 재지 않고 **인스턴스 번호 오름차순**으로 되돌린다(배치 안의 인스턴스가 CPU 순서로 놓이므로). 깊이는 직교 카메라에서
  시선 축, 원근에서 거리다(`Render2DSettings::computeTransparentSortAxis`) — [2D/README.md](../2D/README.md).
- **2D 빛은 3D 와 같은 빛 목록(t12)이다**(`SW_LIGHT_TYPE_POINT2D` · `GLOBAL2D` · 그 뒤의 `SHADOW2D` 가림막 토막). 빛 받는 스프라이트(`sprite2dlit.hlsl` + `lighting2d.hlsli`)만
  읽고 3D 조명 식(`swShadeLights`)은 건너뛴다 — 3D 빛이 하나도 없으면 키라이트로 폴백한다. 2D 게임은 `forward2dpipeline.xml`(그림자 맵 · 톤맵 없음, `-gv_renderPipeline`).
- **스프라이트 프레임 · 색은 인스턴스 칸**(`GpuInstance::_sprite` = `GpuSpriteInstanceData` 16 바이트 — 프레임 · 색 · 픽셀 스냅, Custom Primitive Data 자리). 배치 키를
  건드리지 않아 같은 텍스처의 스프라이트는 한 드로우다. 스프라이트 메시는 양면 사각형(`MeshUtil::createSpriteQuad`)이고 UV 는 메시의 것이다
  (`RenderPassGpuTest.SpriteFramesAndTintsArePerInstance`).
- **값이 실제로 바뀔 때만 일한다.** 상수버퍼 · 바인딩 상태(DX12 슬롯 테이블 · Vulkan 슬롯 세트)는 내용이 달라질 때만 버전을 올리고 다시 만든다 —
  같은 값을 다시 넣는 호출이 흔하다.

**의도적으로 다른 것:**

- **트랜지언트 메모리 앨리어싱 없음.** 각 트랜지언트를 개별 텍스처로 프레임 내내 든다(정확성이 아니라 메모리 차이).
- **DX12 에 PSO 디스크 캐시가 없다**(`ID3D12PipelineLibrary`). Vulkan 은 종료 때 파이프라인 캐시를 저장한다.
- **배리어는 레벨 프롤로그가 한꺼번에 발행한다**(`RenderGraph::setLevelPrologue`). 그래프에서 스플릿 배리어를 뽑지 않는다 — 단순하고 병렬 기록에
  안전한 대신 세밀한 겹침을 포기했다.
- **텍스처 배열 용량은 고정** + 펜스 뒤 인덱스 재사용. 스트리밍 · 축출은 디스크립터가 아니라 텍스처 스트리밍의 일이다.
- **GL 은 결합 샘플러뿐**(ARB_gl_spirv 는 분리 샘플러 불가)이라 슬롯의 샘플러를 엔진이 정한다. DX11/GL 은 텍스처를 슬롯에 걸므로 머티리얼 경계가 곧 배치 경계다.

## 씬 드로우 경로 — 정점 풀 · 배치 표 · 인스턴스 슬롯 스트림

같은 PSO·머티리얼(버퍼·CB·텍스처·원소 수)의 연속 배치는 `drawIndirect( args, offset, count )` 한 번(멀티 드로우)이다.
배치마다 다른 값은 드로우 호출이 아니라 **데이터**가 준다:
- `GpuMeshVertexPool` — 씬 메시 정점을 한 정점 버퍼에 이어 붙인다. 간접 인자의 `startVertex` 가 풀 오프셋. 메시 집합이 같으면
  다시 만들지 않는다. 못 든 메시는 자기 버퍼(멀티 드로우엔 못 묶인다).
- `g_SwBatches`(t13, `GpuBatchInfo` 32바이트) — 배치의 인스턴스 시작·모프 풀 시작·정점 풀 시작·VAT 표 시작. 패스당 한 번 건다. 컬링 t1 과 같은 버퍼.
- `g_SwVertexAnimation`(t14, `GpuVertexAnimationPool`) — 정점 애니메이션(VAT) 표. 메시마다 머리 원소(프레임 수 · 프레임율 · 정점 수 · 반복) + 프레임 × 정점
  float4(위치, 팔면체 노멀을 담은 정수). 정점 셰이더(`swLoadAnimatedVertex`)가 VAT 시계(PassCB `g_SwVertexAnimationTime` = 게임 스레드 군중 시계) +
  인스턴스의 `vertexAnimationPhase` 로 두 프레임을 골라 보간한다 — 먼 군중이 CPU 포즈 · GPU 스키닝 없이 인스턴스마다 다른 위상으로 한 드로우.
- 인스턴스 슬롯 스트림(정점 슬롯 1, `SW_INSTANCESLOT`, uint, 인스턴스 스텝) — `0,1,2,…`. 간접 인자의 `startInstance` 가 배치 시작이라
  입력 어셈블러가 네 API 모두 `startInstance + i` 를 준다. 정점 셰이더는 `swLoadInstance( input.instanceSlot )` 로 자기 인스턴스를,
  `inst.meshBatchIndex` 로 배치 표를 읽는다. **SV_InstanceID 는 쓰지 않는다**(startInstance 포함 여부가 API 마다 달라서).
- 루트 상수는 그룹당 하나(`g_SwMaterialCount`, `setGraphicsRootConstants` — DX12 루트 상수 / Vulkan 푸시 상수 / DX11·GL 은 b2 에뮬).

**API 차이 하나는 남는다 — SV_VertexID.** Vulkan·GL 은 startVertex 를 포함하고 D3D 는 드로우 안의 0 기반 번호다.
`binding.hlsli` 의 `swComputeMorphElement` 가 흡수하고 `RHIDeviceTest.SceneDrawVertexIdStartsAtZeroOnlyOnD3D` 가 네 백엔드의 기대를 고정한다.
**버린 설계**: DX12 커맨드 시그니처의 루트 상수 주입 + Vulkan/GL DrawIndex — 그림은 맞지만 DX12 ExecuteIndirect 가 호출당 두 배
느려진다(런타임 패치). 비용은 상태 변경이 아니라 호출 수라 정렬 순서로는 줄지 않는다.
진단: `-gv_drawMerge=0`(배치마다 호출) · `-gv_vertexPool=0`(메시마다 정점 버퍼).

- **새 디바이스는 첫 PSO · 버퍼에 옛 것과 같은 번호를 준다** — 디바이스를 넘어 사는 캐시는 `releasePassResources` · `shutdown` 에서 잊는다. 머티리얼 등록부 비우기는 그룹 목록과
  셰이더 경로 → 인덱스 맵을 같이(한쪽만 비우면 투명이 알파 0 으로 사라진다). 재생성 판정은 bindless 인덱스가 아니라 세대가 든 핸들로(DX11 · GL 은 인덱스를 즉시 회수한다).
