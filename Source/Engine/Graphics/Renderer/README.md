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
  Debug/      에디터가 읽는 통로 — RenderTargetRegistry(프레임 렌더타깃 목록) · DebugDrawQueue(라인/스피어 큐)
  RenderThread.cpp/h   위를 구동하는 스레드
```

### Pipeline/ — 무엇을 그릴지 기술한다

- `RenderPassResource` — **바인드 템플릿**. 첨부(attachment)의 포맷·클리어 값.
  `Resource/engine/renderpass/*.xml`
- `RenderPipelineResource` — **패스 그래프**. 어떤 패스가 무엇을 입력받아 무엇을 출력하는지.
  `Resource/engine/pipeline/*.xml`
- `RenderPassManager` — 위 둘의 로드·캐시
- `RenderPassType` (RenderPassResource.h) — 패스 타입 이름. XML 의 `_type` 이 이 열거형으로
  해석되고, 해석되지 않으면 `RenderPipelineResource::validate` 가 잡습니다.
- `RenderPassInputContract` — 패스 입력의 **역할** 표(필수/선택). 검증과 실행이 같은 표를 보므로
  "선언은 했는데 안 걸리는 입력" 이 생길 자리가 없습니다.

XML 이 선언한 포맷과 코드가 만드는 것이 어긋나면 조용히 잘못 그리거나 GPU 가 죽습니다.
그래서 로드 시점에 `validate()` 가 자기모순을 검사합니다.

### Graph/ — 실행 순서를 푼다

`RenderGraph` 가 패스들의 입출력 의존성으로 위상 정렬하고, **웨이브**(같이 돌 수 있는 패스 묶음)
단위로 나눕니다. 백엔드가 병렬 기록을 지원하면 한 웨이브의 패스들을 여러 스레드가 동시에
기록합니다.

> 주의: 같은 웨이브의 패스 콜백은 **동시에** 돕니다. 콜백이 만지는 FrameRenderer 공유 상태는
> 반드시 보호해야 합니다. 실제로 여기서 데이터 레이스가 있었습니다.

### Scene/ — 씬을 GPU 데이터로

세 타입이 한 줄로 이어집니다 — **두 스레드는 가운데 타입으로만 만납니다.**

- `GpuSceneBuilder` (게임 스레드) — MeshComponent 를 훑어 인스턴스 배열·배치(batch)·머티리얼 원소 표를 만든다.
  재구축 판단 캐시와 머티리얼 원소의 영속 ID 가 여기 산다. GPU 핸들은 하나도 없다.
- `GpuSceneSnapshot` — GT → RT 로 옮겨지는 **전부**. 여기 없는 값은 옮겨질 수 없다(소유 규칙은 Graphics/README "소유와 수명").
- `GpuScene` (렌더 스레드) — 스냅샷을 받아 인스턴스 구조버퍼·배치 표·간접 인자·머티리얼 버퍼로 올린다. 씬을 볼 수 없다.
- `GpuMeshVertexPool` · `GpuMeshMorphPool` — RT 소유 GPU 풀. 씬 메시 정점을 한 버퍼에 잇고(멀티 드로우), 모프 결과를 담는다.

언리얼의 GPUScene 과 같은 발상으로, per-instance 월드 행렬을 구조버퍼에 올려 VS 가 직접 읽습니다.
`FrameRenderer::execute( pScene )`(에디터·테스트의 직접 경로)도 자기 빌더로 스냅샷을 만들어 **같은 길**로 올립니다.

### Frame/ — 실제로 그린다

- `FrameRenderer` — 프레임 실행의 중심. **파일 이름이 곧 주제**가 되도록 나뉩니다.
  - `FrameRenderer` — 수명(initialize·shutdown·loadPipeline)과 프레임 진입(execute·executePacket)
  - `FrameRendererResources` — 패스 자원의 수명. 기록 **전에** 만들어야 하는 것들(엔진 PSO 등록 · 상수버퍼 링 · 머티리얼 폴백 · Present 변종)
  - `FrameRendererTransients` — 첨부(렌더타깃)의 수명과 조회. 창 크기·파이프라인이 바뀔 때만 다시 만든다
  - `FrameRendererReadback` — 첨부를 CPU 로 읽는 길(테스트 픽셀 비교 · `-gv_screenshot` PPM). **프레임 경로가 아니다** — GPU 를 기다린다
  - `FrameRendererCompute` — 컴퓨트 프리패스 넷(인스턴스 애니메이션 · 메시 모프 · GPU 컬링 · 인스턴스 정렬). 그래프 패스가 아니라 그리기 전에 직접 걸린다
  - `FrameRendererConstants` — 뷰/라이트 행렬 등 **프레임 상수 시드** (프레임당 1회)
  - `FrameRendererPassExecute` — 패스 타입별 실행 분기
  - `FrameRendererDraw` — 드로우 루프
  - `FrameRendererPso` — 머티리얼 PSO 생성
- `FrameRenderer` 가 **소유하는 셋** — 각자 뮤텍스와 수명을 가진 상태라 클래스로 떼어 두었습니다:
  - `PassConstantRing` — 드로우마다 하나씩 나눠 주는 패스 상수버퍼 슬롯 링(원자 커서, 프레임마다 되감기)
  - `RenderPsoCache` — 엔진 패스 PSO · Present 포맷별 PSO · 머티리얼 퍼뮤테이션 변형과 바인딩 레이아웃.
    만드는 일은 `FrameRendererPso` 가, 소유와 해제 순서(변형 → 패스 → Present)는 캐시가 안다
  - `TransientAttachmentPool` — 이름으로 찾는 프레임 첨부(렌더타깃) 풀과 "이번 프레임에 이미 클리어했는가"
- `RenderView` — 뷰 하나의 행렬·절두체·컬링 상수버퍼. 메인 카메라와 그림자 라이트가 각자 갖는다
- `ShaderBindingBinder` — 셰이더 리플렉션이 알려준 슬롯에 실제 값을 바인딩
- `PassConstantValues` — 이름으로 담아 두는 패스 상수 값 저장소
- `FrameResourceRegistry` — 패스 스코프 이름→리소스 매핑
- `RenderFramePacket` — 게임 스레드 → 렌더 스레드로 넘기는 프레임 데이터
- 컴퓨트 디스패치(애니메이션·컬링·정렬)는 `FrameRenderer::dispatchInstanceAnimation` / `dispatchCullAndSort` 가
  커맨드 리스트에 직접 건다 — 별도 래퍼 클래스는 없다(`ComputePass` 는 쓰이지 않은 채 남아 있어 지웠다)

### RenderThread

기본적으로 렌더링은 전용 스레드에서 돕니다(`gv_useRenderThread`, 기본 true).
게임 스레드는 `RenderFramePacket` 을 만들어 넘기고 계속 진행합니다.

## 읽는 순서 (처음이라면)

1. `Resource/engine/pipeline/forwardpipeline.xml` — 패스가 어떻게 기술되는지
2. `Pipeline/RenderPipelineResource.h` — 그 XML 이 무엇으로 읽히는지
3. `Graph/RenderGraph.h` — 순서가 어떻게 정해지는지
4. `Frame/FrameRenderer.h` → `FrameRendererPassExecute.cpp` — 패스 하나가 어떻게 실행되는지
5. `Frame/FrameRendererDraw.cpp` — 드로우 한 번이 어떻게 나가는지

## 알아 둘 것

- **기본 씬에는 메시가 없습니다.** `SW_ACTIVE_GAME` 이 `Empty` 라서, 앱을 그냥 띄우면 드로우
  경로는 거의 실행되지 않습니다. 드로우 경로를 확인하려면 `EngineTest --test_filter=GpuSceneTest.*,RenderPassTest.*,RenderPassGpuTest.*`
  를 보세요 — 큐브를 넣고 실제로 그립니다.
- **`forwardpipeline` 은 완전한 체인**이라 웨이브가 전부 1개입니다. 병렬 기록을 실제로 돌려
  보려면 `deferredpipeline` 을 써야 합니다(웨이브0 = Shadow + GBuffer).
