# RHI — 그래픽스 API 추상화

같은 렌더링 코드가 DirectX 11 / DirectX 12 / OpenGL / Vulkan 위에서 돌게 하는 계층입니다.
네 백엔드가 **같은 인터페이스를 구현**하므로, 같은 개념이 API 마다 어떻게 다른지 나란히 놓고
볼 수 있습니다 — 이 폴더를 읽는 가장 큰 이유입니다.

## 폴더

```
RHI/
  IRHI*.h            인터페이스. 여기가 RHI 의 얼굴이다
  RHITypes.h         포맷·토폴로지·PSO 서술 등 백엔드 공통 자료형
  RHICapabilities.h  백엔드가 무엇을 할 수 있는지 (bindless, 병렬 기록, indirect draw ...)
  RHIBackendRegistry 백엔드 팩토리 등록·조회. DLL 로딩은 그중 한 방식이다
  RHI.cpp/.h         디바이스 생성·핫스왑 등 상위 진입점
  RHIRenderResource  GPU 자원을 든 객체의 등록부 — 디바이스 수명 이벤트(release/forget/init)를 목록 전체에 통보
  RHIResidentBuffer  핸들 + 그것을 만든 디바이스. "값이 남아 있다" 와 "살아 있는 디바이스의 것" 을 가른다
  RHIStructuredBufferSlot  구조버퍼 + SRV/UAV 인덱스 한 벌 — 용량 확보·업로드·해제를 순서까지 맞춰 처리
  RHIConstantBufferSlot    상수버퍼 + 인덱스 한 벌 — 크기가 안 변한다(`create`). 기록 중 갱신은 커맨드 리스트로(`update( IRHICommandList&, … )`)
  RHICommandListForwarder  백엔드 CommandList 가 자기 Context 로 즉시 전달하는 공통 몸통(템플릿 — 매크로가 아니다)

  Support/           백엔드들이 공유하는 자료구조
                     RHIHandleTable(핸들→객체), RHIIndexFreeList(인덱스 재사용),
                     RHIReleaseQueue(GPU 가 다 쓴 뒤 해제), FrameResourceRing(프레임 슬롯),
                     RHIShaderRequest(파이프라인 서술체 → 컴파일 요청 해석 · 그래픽스 스테이지 쌍 컴파일 — 넷이 각자 갖던 규칙 하나),
                     RHIGpuTimestamp(타임스탬프 칸 → 마이크로초 — 기준점 · 미기록 칸 규칙 하나),
                     RHIBufferSize(32비트 API 의 버퍼 크기 — 넘치면 만들지 않는다),
                     RHIConstantBufferMirror(링 상수버퍼의 모든 칸이 마지막 값을 갖게 — DX12 · Vulkan),
                     RHILiveCommandListUtil(종료 때 살아 있는 커맨드 리스트를 디바이스에서 떼는 도우미)
  DX/                D3D 전용. RHIDxgiFormat(포맷 변환) · RHIDxgiTearing(VSync 끄기 — ALLOW_TEARING 과 Present 플래그는 짝)
  Modules/           백엔드를 DLL 로 분리해 싣는 장치
                     RHIModuleAbi(호스트↔모듈 계약), RHIModuleEntry(모듈 측 매크로),
                     <백엔드>/ModuleEntry.cpp
  DX11/ DX12/ GL/ Vulkan/   백엔드 구현
                     Vulkan/VulkanRHIHandle.h 는 Vulkan C 핸들의 전방 선언 묶음이다 —
                     백엔드 **헤더**에 <vulkan/vulkan.h> 가 새어 들어가지 않게 한다
                     Vulkan/VulkanRHIApiVersion.h 는 요구 API 판(1.3) 하나 — 셰이더 굽기 타깃과 물리 디바이스 선택이 같이 본다
```

## 백엔드 폴더의 파일 구성

한 클래스를 여러 파일로 나눌 때는 **소유 클래스 이름을 접두어로 남깁니다.**
`D3D12RHIResourceFactoryPipeline.cpp` 는 "`D3D12RHIResourceFactory` 의 파이프라인 부분" 이라는 뜻입니다.
인터페이스는 한 덩어리인데 파일만 쪼갠 것이라, 접두어가 없으면 어느 클래스의 일부인지 사라집니다.

| 파일 | 무엇을 하는가 | DX11 | DX12 | GL | Vulkan |
|---|---|:--:|:--:|:--:|:--:|
| `<B>RHIDevice` | 디바이스 상태·조회 | O | O | O | O |
| `<B>RHIDeviceInit` | 한 번 만들고 리사이즈 때 다시 만드는 것 | O | O | O | O |
| `<B>RHIDeviceSubmission` | 프레임 제출·펜스·커맨드 리스트 풀 | O | O | O | O |
| `<B>RHIDeviceDescriptor` | 셰이더 슬롯 배치 (루트 시그니처 / 디스크립터 세트) | – | O | – | O |
| `<B>RHIDeviceRenderPass` | 이미지 레이아웃 전이 · 캐시에 넘길 키/서술 조립 | – | – | – | O |
| `<B>RHIRenderPassCache` | PSO 호환 렌더패스 · 합성 프레임버퍼 · desc 렌더패스의 **소유자** (별도 클래스) | – | – | – | O |
| `<B>RHIResource` | 버퍼·텍스처 | O | O | O | O |
| `<B>RHIResourcePipeline` | PSO·셰이더 스테이지·렌더패스 객체 | O | O | O | O |
| `<B>RHIResourceBindless` | 리소스를 인덱스로 접근 가능하게 등록 | O | O | O | O |
| `<B>RHICommandContext` | 드로우·디스패치·바인딩 기록 | O | O | O | O |
| `<B>RHICommandList` | 독립 기록 단위 | O | O | O | O |
| `<B>RHISwapChain` | 창의 백버퍼 — 다음 것을 고르고, 크기를 바꾸고, 표시한다 | O | O | – | O |

**빈 칸은 빠뜨린 것이 아니라 그 API 에 개념이 없다는 뜻입니다.**

- D3D11 은 명시적 제출이 없어 `Submission` 이 짧을 뿐, 자리는 다른 백엔드와 같습니다.
- `Descriptor` 가 DX12·Vulkan 에만 있는 이유: DX11/GL 은 리소스를 **슬롯 번호**로 바인딩합니다.
  DX12 의 디스크립터 힙 + 루트 시그니처, Vulkan 의 디스크립터 세트 + 파이프라인 레이아웃은
  "바인딩할 자리를 미리 선언해 두는" 모델이고, 이게 두 세대의 가장 큰 차이입니다.
- `RenderPass` 가 Vulkan 에만 있는 이유: Vulkan 만 렌더패스/프레임버퍼를 **미리 만들어 캐시**해야
  합니다. 다른 API 는 렌더타깃을 그때그때 바인딩합니다. 캐시 자체(맵·뮤텍스·파괴 순서)는
  `VulkanRHIRenderPassCache` 가 소유하고, 디바이스는 텍스처 레코드를 풀어 키와 서술만 만들어 넘깁니다.
- `SwapChain` 이 GL 에만 없는 이유: OpenGL 에는 **스왑체인 객체가 없습니다.** 드라이버가 창의
  백버퍼를 숨기고 `SwapBuffers(HDC)` 한 줄이 present 의 전부입니다. 게다가 그 `HDC` 는 스레드에
  컨텍스트를 붙이는 `MakeCurrent` 에도 쓰이므로 스왑체인이 아니라 **컨텍스트**입니다 — 이름만
  스왑체인인 껍데기를 만들면 그게 거짓말이 됩니다.

## 읽는 순서 (처음이라면)

1. `IRHIDevice.h` — 디바이스가 무엇을 할 수 있는지
2. `IRHICommandList.h` — 기록할 수 있는 명령의 전부
3. `RHITypes.h` 의 `constant` 블록 — 백엔드끼리 **값이 같아야 하는** 계약들
4. 한 백엔드를 골라 `DeviceInit → Device → CommandContext` 순으로
5. 같은 파일을 다른 백엔드에서 열어 비교

## 알아 둘 것

- **`RHIModuleAbi.h` 를 바꾸면** 엔진과 `RHI_*` DLL 을 **모두 함께** 다시 빌드해야 합니다.
  낡은 백엔드 DLL 은 함수 포인터가 어긋나 즉시 크래시합니다.
- Dev 에서 백엔드는 별도 DLL(`RHI_*` MODULE)입니다(Shipping 은 하나를 Engine 에 정적 링크). 그래서 Engine 의 전역 변수를
  백엔드에서 그냥 `extern` 으로 참조할 수 없습니다 — 정책은 Engine 이 정하고 디바이스는
  메커니즘만 갖는 형태로 넘깁니다(`IRHIDevice::setImmediateSubmit` 참고).
- **기록 중의 상수버퍼 갱신은 커맨드 리스트로 갑니다**(`IRHICommandList::updateConstantBuffer`, 언리얼 `RHIUpdateUniformBuffer` 와 같은 자리).
  리스트가 자기 컨텍스트로 넘기므로 DX11 은 그 리스트의 Deferred Context 에 `Map` 하고, DX12 · Vulkan · GL 은 버퍼의 이번 프레임 칸에 씁니다.
  `IRHIResourceFactory::updateConstantBuffer` 는 기록 **밖**(에셋 · 머티리얼 파라미터) 전용입니다 — DX11 은 즉시 컨텍스트 + 잠금.
  리스트는 begin 과 end 가 다른 스레드여도 됩니다(RenderGraph 병렬 레벨의 첫 리스트) —
  `RHIDeviceTest.CommandListHandedOffAcrossThreadsDoesNotLeakRecordingContext` · `CommandListConstantBufferUpdateReachesItsDraws` 가 못박습니다.

## 디바이스 종료 순서 — `IRHIDevice::shutdown` 하나가 정한다

`shutdown` 은 비가상 템플릿 메서드이고 백엔드는 단계 훅만 채웁니다. 순서는 네 백엔드가 같습니다.

1. 자원을 든 쪽에 알린다 — `RHIRenderResource::releaseAllFor( this )`. 디바이스가 아직 살아 있어 돌려줄 수 있다.
2. GPU 를 기다리고 해제 큐를 비운다 — `waitIdleInternal`. 1 이 넘긴 자원 · 남은 `enqueueGpuRelease` 콜백이 여기서 실제로 사라진다.
3. 프레임 스트림 컨텍스트를 놓고 살아 있는 커맨드 리스트를 디바이스에서 뗀다 — `detachCommandRecordingInternal`
   (`RHILiveCommandListUtil::detachAll`). 리스트는 디바이스보다 오래 살 수 있고(렌더 그래프가 든다), 떼지 않으면 그 소멸자가 내려간 디바이스에 반납하려 든다.
4. 백엔드 자원 · 스왑체인 · 네이티브 디바이스를 내린다 — `shutdownInternal`.

주의: 렌더 스레드는 이미 멈춰 있어야 합니다 — `shutdown` 은 `waitIdle` 과 달리 렌더 스레드를 비우지 않습니다.
`shutdown` 을 거치지 않고 사라지는 디바이스(초기화 실패)는 소멸자가 `RHIRenderResource::forgetAllFor` 로 핸들만 잊게 합니다.
`RHIDeviceShutdownTest.StepsRunInContractOrder` 가 단계 순서를 고정합니다.

## 엔진 밖 모듈이 디바이스를 만지는 창구

- **네이티브 핸들은 `IRHIDevice::queryNativeHandles( RHINativeHandles& )` 로만 받습니다.** 모듈 경계를 넘는 것은 판 번호(`_version`) ·
  크기(`_byteSize`)를 든 POD 하나이고, Engine 이 자기 판(`kRHINativeHandlesVersion`)과 다르면 채우지 않고 거절합니다. 백엔드는
  `queryNativeHandlesInternal` 만 채웁니다. 에디터의 ImGui 렌더러 백엔드(Vulkan 은 인스턴스 · 물리 디바이스 · 큐 패밀리 · 렌더 패스까지)가 이것을 씁니다.
  구체 디바이스 클래스(`VulkanRHIDevice` 등)로 캐스팅해 부르지 말 것 — 그 레이아웃 · vtable 은 모듈 사이의 계약이 아닙니다.
  `RHINativeHandlesTest.QueryRejectsAnotherLayoutAndFillsTheMatchingOne` 가 판 대조를 고정합니다.
- **엔진 밖 네이티브 자원을 놓을 때는 `IRHIDevice::enqueueGpuRelease( delegate )`** 로 백엔드 해제 큐(`RHIReleaseQueue`)에 넣습니다 —
  DX12 · Vulkan 은 GPU 펜스, DX11 · GL 은 프레임 지연 기준이라 엔진 자원과 같은 시점에 사라집니다. **프레임을 기록하는 스레드(렌더 스레드)에서**
  부릅니다(펜스 값을 올리는 스레드가 읽어야 "이 프레임" 이 맞다). 에디터는 `EditorDrawReleaseQueue` 가 놓은 ImGui 디스크립터 · 렌더 타깃을
  그것을 그린 마지막 프레임 뒤에 이 창구로 넘깁니다.

## Vulkan 최소 판 — 1.3

`VulkanRHIApiVersion`(1.3) 하나가 셰이더 굽기 타깃(`-fspv-target-env=vulkan1.3` → SPIR-V 1.6)이자 디바이스 최소 판입니다. 물리 디바이스 선택은
이 판에 못 미치는 디바이스를 건너뜁니다 — 구운 SPIR-V 모듈을 하나도 받지 못하기 때문입니다. 1.3 에서는 HLSL `discard` 가 내는
`OpDemoteToHelperInvocation` 의 기능이 필수라 따로 확인하지 않습니다. 판을 바꾸면 이 헤더 하나만 고치고 셰이더를 다시 굽습니다.

## 스왑체인 — 같은 개념, 다른 무게

`<B>RHISwapChain` 세 개를 나란히 열면 세대 차이가 그대로 보입니다.

| | 누가 만드나 | 다음 백버퍼를 고르는 법 | 딸려 오는 것 |
|---|---|---|---|
| DX11 | **디바이스와 함께** (`D3D11CreateDeviceAndSwapChain`) | 매 프레임 RTV 를 다시 잡는다 | 백버퍼 RTV |
| DX12 | 따로 (`CreateSwapChainForHwnd`) | `GetCurrentBackBufferIndex()` — DXGI 가 정해 준다 | 백버퍼들, RTV, 리소스 상태 |
| Vulkan | 따로 (서피스부터 직접) | `vkAcquireNextImageKHR` — **세마포어를 받아** 신호한다 | 이미지, 뷰, 프레임버퍼, 세마포어 2종 |

- **DX11 은 `attach` 만 합니다.** 디바이스와 스왑체인이 한 호출에서 함께 태어나기 때문에, 만들지
  않고 넘겨받아 소유합니다.
- **DX12 는 리소스 상태를 함께 갖습니다.** 백버퍼 상태(`PRESENT` ↔ `RENDER_TARGET` ↔ `COPY_DEST`)는
  커맨드 리스트가 아니라 리소스에 속한 전역 상태라, 상태와 그걸 바꾸는 배리어(`transitionTo`)를
  한 객체가 함께 들고 있어야 어긋나지 않습니다.
- **Vulkan 만 동기화 객체를 갖습니다.** acquire 세마포어는 **인플라이트 프레임 슬롯**으로 세고,
  renderFinished 세마포어는 **이미지 인덱스**로 셉니다 — 드라이버가 인플라이트 프레임 수보다 적은
  이미지를 줄 수 있어 두 개수가 같다는 보장이 없습니다. 프레임 슬롯 자체(펜스·커맨드버퍼·디스크립터
  링)는 스왑체인이 아니라 디바이스의 것이라, `acquireNextImage( device, frameSlot )` 처럼 슬롯을
  인자로 받습니다.

RTV 힙은 **디바이스가 소유합니다**(DX12). 오프스크린 렌더타깃과 같은 힙을 나눠 쓰기 때문입니다 —
앞쪽 `getBufferCount()` 칸이 백버퍼, 그 뒤가 오프스크린입니다.
