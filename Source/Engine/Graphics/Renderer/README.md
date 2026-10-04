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
  - `FrameRendererResources` — 패스 자원의 수명. 기록 **전에** 만들어야 하는 것들(엔진 PSO 등록 · 상수버퍼 링 · 머티리얼 폴백 · Present 변종)
  - `FrameRendererTransients` — 첨부(렌더타깃)의 수명과 조회. 창 크기·파이프라인이 바뀔 때만 다시 만든다
  - `FrameRendererReadback` — 첨부를 CPU 로 읽는 길(테스트 픽셀 비교 · `-gv_screenshot` PPM). **프레임 경로가 아니다** — GPU 를 기다린다
  - `FrameRendererCompute` — 컴퓨트 프리패스 다섯(인스턴스 애니메이션 · 메시 모프 · 메시 스킨(meshskin.hlsl, 디스패치 하나) · GPU 컬링 · 인스턴스 정렬).
    그래프 패스가 아니라 그리기 전에 직접 걸린다
  - `FrameRendererConstants` — 뷰/라이트 행렬 등 **프레임 상수 시드** (프레임당 1회)
  - `FrameRendererPassExecute` — 패스 타입별 실행 분기
  - `FrameRendererDraw` — 드로우 루프
  - `FrameRendererViews` — 추가 뷰(카메라마다 하나)의 자원 준비 · 해제 · 그리기. 아래 "다중 뷰"
  - `FrameRendererPso` — 머티리얼 PSO 생성. 뷰 모드(`RenderViewMode` — Lit · Unlit · Wireframe)가 얹는 define 은
    `FrameRendererUtil::findViewModeDefine` 하나가 정하고 셰이더 쿠킹 요청도 같은 함수를 부릅니다 — 쿠커가 쿠킹하지 않은 define 은 Shipping 에서 PSO 를 못 만듭니다
- `FrameRenderer` 가 **소유하는 셋** — 각자 뮤텍스와 수명을 가진 상태라 클래스로 떼어 두었습니다:
  - `PassConstantRing` — 드로우마다 하나씩 나눠 주는 패스 상수버퍼 슬롯 링(원자 커서, 프레임마다 되감기)
  - `RenderPsoCache` — 엔진 패스 PSO · Present 포맷별 PSO · 머티리얼 퍼뮤테이션 변형과 바인딩 레이아웃.
    만드는 일은 `FrameRendererPso` 가, 소유와 해제 순서(변형 → 패스 → Present)는 캐시가 안다
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
