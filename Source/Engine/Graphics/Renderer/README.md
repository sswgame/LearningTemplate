# Renderer — 한 프레임을 그리는 곳

RHI 가 "그래픽스 API 를 감싸는 층" 이라면, 여기는 그 위에서 **무엇을 어떤 순서로 그릴지**를
정하고 실행하는 층입니다.

## 폴더 = 한 프레임이 흘러가는 순서

```
Renderer/
  Pipeline/   파이프라인 "정의" — XML 로 기술되는 것
  Graph/      정의를 "실행 순서" 로 푸는 것
  Scene/      씬을 GPU 가 읽을 수 있는 데이터로
  Frame/      실제로 그리는 것
  RenderThread.cpp/h   위 넷을 구동하는 스레드
```

### Pipeline/ — 무엇을 그릴지 기술한다

- `RenderPassResource` — **바인드 템플릿**. 첨부(attachment)의 포맷·클리어 값.
  `Resource/engine/renderpass/*.xml`
- `RenderPipelineResource` — **패스 그래프**. 어떤 패스가 무엇을 입력받아 무엇을 출력하는지.
  `Resource/engine/pipeline/*.xml`
- `RenderPassManager` — 위 둘의 로드·캐시
- `RenderPassType` (RenderPassResource.h) — 패스 타입 이름. XML 의 `_type` 이 이 열거형으로
  해석되고, 해석되지 않으면 `RenderPipelineResource::validate` 가 잡습니다.

XML 이 선언한 포맷과 코드가 만드는 것이 어긋나면 조용히 잘못 그리거나 GPU 가 죽습니다.
그래서 로드 시점에 `validate()` 가 자기모순을 검사합니다.

### Graph/ — 실행 순서를 푼다

`RenderGraph` 가 패스들의 입출력 의존성으로 위상 정렬하고, **웨이브**(같이 돌 수 있는 패스 묶음)
단위로 나눕니다. 백엔드가 병렬 기록을 지원하면 한 웨이브의 패스들을 여러 스레드가 동시에
기록합니다.

> 주의: 같은 웨이브의 패스 콜백은 **동시에** 돕니다. 콜백이 만지는 FrameRenderer 공유 상태는
> 반드시 보호해야 합니다. 실제로 여기서 데이터 레이스가 있었습니다.

### Scene/ — 씬을 GPU 데이터로

`GpuScene` 이 씬의 MeshComponent 를 훑어 인스턴스 배열과 배치(batch)로 만듭니다.
언리얼의 GPUScene 과 같은 발상으로, per-instance 월드 행렬을 구조버퍼에 올려 VS 가 직접 읽습니다.

### Frame/ — 실제로 그린다

- `FrameRenderer` — 프레임 실행의 중심. 파일이 여럿으로 나뉩니다.
  - `FrameRendererTransients` — 프레임 임시 자원(렌더타깃·상수버퍼 슬롯)과 엔진 PSO
  - `FrameRendererConstants` — 뷰/라이트 행렬 등 **프레임 상수 시드** (프레임당 1회)
  - `FrameRendererPassExecute` — 패스 타입별 실행 분기
  - `FrameRendererDraw` — 드로우 루프
  - `FrameRendererPso` — 머티리얼 PSO 생성·캐시
- `ShaderBindingBinder` — 셰이더 리플렉션이 알려준 슬롯에 실제 값을 바인딩
- `PassConstantValues` — 이름으로 담아 두는 패스 상수 값 저장소
- `FrameResourceRegistry` — 패스 스코프 이름→리소스 매핑
- `RenderFramePacket` — 게임 스레드 → 렌더 스레드로 넘기는 프레임 데이터
- `ComputePass` — 비동기 컴퓨트 디스패치

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
  경로는 거의 실행되지 않습니다. 드로우 경로를 확인하려면 `EngineTest --test_filter=RenderPassTest.*`
  를 보세요 — 큐브를 넣고 실제로 그립니다.
- **`forwardpipeline` 은 완전한 체인**이라 웨이브가 전부 1개입니다. 병렬 기록을 실제로 돌려
  보려면 `deferredpipeline` 을 써야 합니다(웨이브0 = Shadow + GBuffer).
