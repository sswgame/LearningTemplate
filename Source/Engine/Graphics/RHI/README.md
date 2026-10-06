# RHI — 그래픽 API 추상화

## 이것은 무엇이고 왜 있나

RHI(Rendering Hardware Interface)는 렌더러가 부르는 그래픽 API 공통 인터페이스입니다. DirectX 11, DirectX 12, Vulkan, OpenGL 네 백엔드가 같은 인터페이스를 구현합니다.
렌더러는 RHI만 부르므로 같은 렌더링 코드가 네 API 위에서 그대로 돕니다. 언리얼의 RHI와 같은 구조입니다.

이 폴더를 읽는 가장 큰 이유는 비교입니다. 같은 개념이 API마다 어떻게 다른지 같은 이름의 파일로 나란히 놓여 있습니다.
예를 들어 스왑체인 파일은 세 백엔드에 있고 OpenGL에는 없는데, 그 차이가 곧 API 세대의 차이입니다.

개발 빌드에서 백엔드는 각각 따로 로드되는 DLL(`RHI_*` 모듈)입니다. 그래서 실행 중에 백엔드를 바꿀 수 있습니다.
Shipping 빌드는 `SW_SHIPPING_RHI_BACKEND` 로 고른 백엔드 하나만 Engine에 정적으로 링크합니다.

## 머릿속 그림

```text
렌더러 (FrameRenderer, GpuScene, 머티리얼, CanvasRenderer)
        │  IRHIDevice / IRHIResourceFactory / IRHICommandList
        ▼
백엔드 DLL   D3D11RHI*   D3D12RHI*   VulkanRHI*   OpenGLRHI*
        ▼
그래픽 API
```

기억할 개념은 네 가지입니다.

**디바이스.** `IRHIDevice` 는 GPU 하나를 대표합니다. 프레임을 열고 닫고, 커맨드를 제출하고, 백엔드가 무엇을 할 수 있는지(`getCapabilities`) 알려 줍니다.
버퍼, 텍스처, PSO 같은 리소스는 디바이스가 가진 `IRHIResourceFactory` 로 만듭니다.

**두 가지 기록 대상.** 디바이스가 소유한 **프레임 스트림**과, 패스마다 만드는 **커맨드 리스트**가 있습니다. 둘 다 같은 기록 API(`IRHICommandList`)를 씁니다.
렌더 그래프의 패스는 커맨드 리스트에 기록하고, Present처럼 프레임 단위로 한 번 하는 일은 프레임 스트림에 기록합니다.

**핸들과 바인드리스 인덱스.** 리소스는 포인터가 아니라 정수 핸들로 다룹니다. 셰이더가 리소스를 읽을 때는 디바이스에 등록한 바인드리스 인덱스를 씁니다.
핸들 값은 디바이스 안에서만 뜻이 있습니다. 새 디바이스는 옛 디바이스와 같은 번호를 다시 줍니다.

**백엔드 모듈과 ABI.** 엔진과 백엔드 DLL은 가상 함수 테이블로 만납니다. 이 경계의 모양이 바뀌면 `Modules/RHIModuleAbi.h` 의 버전(`kRHIModuleAbiVersion`)과 스탬프를 함께 올립니다.
엔진은 버전이 다른 백엔드 DLL을 로드하지 않습니다.

## 따라 해 보기 — 한 기능을 네 백엔드에서 읽기

가위 사각형(scissor rect)으로 예를 듭니다. 가위는 그리기를 화면의 사각형 안으로 제한하는 기능이고, UI의 스크롤 영역이 이것으로 잘립니다.

1. `IRHICommandList.h` 에서 `setScissorRect` 의 선언과 주석을 읽습니다. 가위는 다음 `setViewport` 나 `beginRenderPass` 까지만 유지되고, 그 둘이 가위를 뷰포트 전체로 되돌립니다.
   언리얼의 `RHISetViewport` 와 같은 규칙입니다.
2. 네 백엔드의 `<백엔드>RHICommandContext.cpp` 에서 `setScissorRect` 를 엽니다. 같은 기능이 API마다 다르게 구현되어 있습니다.
   - DirectX 12와 Vulkan은 가위가 늘 켜져 있는 동적 상태라 값만 바꿉니다.
   - DirectX 11은 래스터라이저 상태의 `ScissorEnable` 을 늘 켜 두고, 뷰포트를 바인딩하는 곳에서 가위도 함께 바인딩합니다.
   - OpenGL은 `GL_SCISSOR_TEST` 를 켜고 끕니다. 클리어와 블릿도 가위를 따르므로 그동안은 가위를 끕니다.
3. 네 백엔드에서 실제로 같은 결과가 나오는지 테스트로 봅니다. 이 테스트는 GPU가 필요하므로 CI에서는 돌지 않습니다.

```powershell
cd build/Ninja-Debug/Bin
./EngineTest.exe --test_filter=RHIDeviceTest.ScissorRectClipsDrawsAndResetsWithViewport
```

테스트는 백엔드마다 디바이스를 만들고(`RHIBackendSweep`), 가위 안쪽만 칠해졌는지와 `setViewport` 뒤에 가위가 풀렸는지를 픽셀로 확인합니다.

다른 기능도 같은 순서로 읽으면 됩니다. 처음이라면 아래 순서를 권합니다.

1. `IRHIDevice.h` 로 디바이스가 무엇을 할 수 있는지 봅니다.
2. `IRHICommandList.h` 로 기록할 수 있는 명령을 모두 봅니다.
3. `RHITypes.h` 의 `constant` 블록에서 백엔드끼리 값이 같아야 하는 계약을 봅니다.
4. 백엔드 하나를 골라 `DeviceInit`, `Device`, `CommandContext` 순으로 읽습니다.
5. 같은 파일을 다른 백엔드에서 열어 비교합니다.

## 작동 원리

### 백엔드 폴더의 파일 구성

한 클래스를 여러 파일로 나눌 때는 소유 클래스 이름을 접두어로 남깁니다. `D3D12RHIResourceFactoryPipeline.cpp` 는 "`D3D12RHIResourceFactory` 의 파이프라인 부분"이라는 뜻입니다.
접두어가 없으면 어느 클래스의 일부인지 알 수 없습니다. 아래 표의 `<B>` 는 백엔드 접두어입니다.

| 파일 | 하는 일 | DX11 | DX12 | GL | Vulkan |
|---|---|:--:|:--:|:--:|:--:|
| `<B>RHIDevice` | 디바이스 상태와 조회 | O | O | O | O |
| `<B>RHIDeviceInit` | 처음과 리사이즈 때 만드는 것 | O | O | O | O |
| `<B>RHIDeviceSubmission` | 프레임 제출, 펜스, 리스트 풀 | O | O | O | O |
| `<B>RHIDeviceDescriptor` | 셰이더 슬롯 배치 | – | O | – | O |
| `<B>RHIDeviceRenderPass` | 레이아웃 전이, 렌더 패스 키 | – | – | – | O |
| `<B>RHIRenderPassCache` | 렌더 패스와 프레임버퍼 소유 | – | – | – | O |
| `<B>RHIResource` | 버퍼와 텍스처 | O | O | O | O |
| `<B>RHIResourcePipeline` | PSO와 셰이더 스테이지 | O | O | O | O |
| `<B>RHIResourceBindless` | 바인드리스 인덱스 등록 | O | O | O | O |
| `<B>RHICommandContext` | 드로우, 디스패치, 바인딩 기록 | O | O | O | O |
| `<B>RHICommandList` | 독립 기록 단위 | O | O | O | O |
| `<B>RHISwapChain` | 창의 백버퍼와 Present | O | O | – | O |

빈 칸은 빠뜨린 것이 아니라 그 API에 개념이 없다는 뜻입니다.

- `Descriptor` 는 DirectX 12와 Vulkan에만 있습니다. DirectX 11과 OpenGL은 리소스를 슬롯 번호로 바인딩합니다.
  DirectX 12의 디스크립터 힙과 루트 시그니처, Vulkan의 디스크립터 세트와 파이프라인 레이아웃은 바인딩할 슬롯을 미리 선언해 두는 모델입니다. 이것이 두 세대의 가장 큰 차이입니다.
- `RenderPass` 는 Vulkan에만 있습니다. Vulkan만 렌더 패스와 프레임버퍼를 미리 만들어 캐시해야 하고, 다른 API는 렌더 타깃을 그때그때 바인딩합니다.
  캐시는 `VulkanRHIRenderPassCache` 가 소유하고, 디바이스는 텍스처 정보로 키와 설명만 만들어 넘깁니다.
- `SwapChain` 은 OpenGL에만 없습니다. 아래 "스왑체인" 절에 이유가 있습니다.
- DirectX 11은 명시적 제출이 없어서 `Submission` 파일이 짧습니다.

백엔드 사이에 같은 규칙이 필요하면 백엔드마다 따로 쓰지 않고 `Support/` 에 한 번 둡니다. 핸들 테이블, 인덱스 재사용, 지연 해제 큐, 타임스탬프 환산이 그런 예입니다.

### 프레임 스트림과 커맨드 리스트

| 무엇 | 얻는 곳 | 쓰는 곳 |
|---|---|---|
| 프레임 스트림 | `IRHIDevice::getFrameStreamContext()` | Present, 프레임 단위 작업 |
| 커맨드 리스트 | `IRHIDevice::createCommandList()` | 렌더 그래프의 패스 |
| 즉시 제출 | `executeCommandListImmediate` | 프레임 밖 일회성 작업 |

프레임 스트림은 `beginFrame` 이 열고 `endFrame` 이 제출합니다. 프레임 스트림의 `IRHICommandContext` 는 커맨드 리스트와 같은 API를 쓰지만 기록 범위가 없어서 `beginCommandList` 와 `endCommandList` 가 아무 일도 하지 않습니다.

`executeCommandList` 를 부르면 디바이스가 프레임 스트림을 그 지점에서 자르고 "앞 세그먼트, 이 리스트, 새 세그먼트" 순서로 연결합니다. 제출은 `endFrame` 에서 한 번에 합니다.
같은 큐에 제출한 순서가 곧 실행 순서이므로, 렌더 그래프가 정한 순서가 그대로 GPU 순서가 됩니다.

커맨드 리스트의 실체는 백엔드마다 다릅니다.

- DirectX 11은 리스트마다 Deferred Context를 하나 가지고, 끝날 때 `FinishCommandList` 로 닫습니다.
- DirectX 12는 리스트마다 `ID3D12GraphicsCommandList` 와 커맨드 얼로케이터를 하나씩 가집니다.
- Vulkan은 커맨드 풀과 커맨드 버퍼 한 쌍을 디바이스의 풀에서 빌리고, GPU 펜스를 지난 뒤 돌려줍니다. 커맨드 풀은 외부 동기화가 필요한 객체라 리스트마다 따로 둡니다.
- OpenGL은 커맨드 버퍼가 없고 컨텍스트가 스레드에 묶인 상태 머신입니다. 리스트는 컨텍스트를 감싸 GL을 바로 부를 뿐이고, 병렬 기록은 없습니다.

병렬 기록은 DirectX 12와 Vulkan이 지원하고, DirectX 11은 드라이버가 커맨드 리스트를 지원할 때만 켜집니다(`RHICapabilities::_bParallelCommandRecording`).
리스트는 기록을 시작한 스레드와 끝낸 스레드가 달라도 됩니다. 렌더 그래프의 병렬 레벨에서 첫 리스트를 렌더 스레드가 열고 워커가 닫기 때문입니다.

**기록 중의 상수 버퍼 갱신은 커맨드 리스트로 합니다**(`IRHICommandList::updateConstantBuffer`). 언리얼의 `RHIUpdateUniformBuffer` 에 해당합니다.
리스트가 자기 컨텍스트로 넘기므로 DirectX 11은 그 리스트의 Deferred Context에서 `Map` 하고, 나머지 백엔드는 버퍼의 이번 프레임 슬롯에 씁니다.
`IRHIResourceFactory::updateConstantBuffer` 는 기록 밖에서 에셋과 머티리얼 값을 쓸 때만 씁니다. DirectX 11에서는 즉시 컨텍스트를 잠그고 씁니다.
`RHIDeviceTest.CommandListHandedOffAcrossThreadsDoesNotLeakRecordingContext` 와 `CommandListConstantBufferUpdateReachesItsDraws` 가 이 두 규칙을 지킵니다.

### 백엔드마다 다른 것

네 백엔드는 디바이스, 프레임 스트림, 커맨드 리스트, 리소스, 스왑체인 경로에서 같은 수준으로 구현되어 있습니다. 일부러 다르게 둔 곳은 다음과 같습니다.

| | Vulkan | DX11 | GL | DX12 |
|---|:--:|:--:|:--:|:--:|
| 스왑체인 객체 | 있음 | 있음 | 없음 | 있음 |
| 텍스처 바인드리스 | 텍스처 배열 | 슬롯 에뮬레이션 | 슬롯 에뮬레이션 | 텍스처 배열 |
| 컨텍스트 스레드 고정 | 아니요 | 예 | 예 | 아니요 |

- **텍스처 바인드리스.** DirectX 12와 Vulkan은 크기 제한 없는 텍스처 배열에 인덱스로 접근합니다. DirectX 11과 OpenGL은 드로우 직전에 텍스처를 슬롯에 바인딩해 같은 기능을 냅니다.
  인스턴스와 머티리얼 데이터 같은 구조체 버퍼는 네 백엔드가 같은 슬롯을 씁니다. 셰이더 쪽 계약은 [Shader 문서](../Shader/README.md)에 있습니다.
- **배리어.** `prepareTextureForShaderRead` 와 `transitionBuffer` 는 DirectX 12와 Vulkan에서 배리어와 레이아웃 전이입니다. DirectX 11은 상태 전이가 없는 대신 슬롯 해저드를 풉니다.
  읽기 전에 픽셀 셰이더 SRV 슬롯을 비우고, 읽기 상태로 돌리는 버퍼를 컴퓨트 셰이더 UAV 슬롯에서 해제합니다. OpenGL은 `glMemoryBarrier` 를 냅니다.
- **컨텍스트 스레드.** DirectX 11의 즉시 컨텍스트와 OpenGL 컨텍스트는 소유 스레드 하나에서만 씁니다(`IRHIDevice::requiresExclusiveContextThread`).
- **렌더 패스 객체.** Vulkan만 렌더 패스 객체를 미리 만들어 캐시합니다. `createRenderPass` 는 첨부가 없는 설명을 거절합니다.
- **OpenGL 클립 규약.** OpenGL은 `glClipControl` 로 DirectX와 같은 규약을 씁니다. 오프스크린은 `GL_UPPER_LEFT`, 기본 프레임버퍼는 `GL_LOWER_LEFT`, 깊이는 `GL_ZERO_TO_ONE` 입니다.
  이것을 빼면 화면이 상하로 뒤집히고 깊이 정밀도가 절반이 됩니다.

### 스왑체인 — 같은 개념, 다른 무게

스왑체인은 창에 표시할 백버퍼 여러 장의 집합입니다. `<B>RHISwapChain` 세 개를 나란히 열면 API 세대 차이가 그대로 보입니다.

| | 누가 만드나 | 다음 백버퍼를 고르는 법 | 함께 가지는 것 |
|---|---|---|---|
| DX11 | 디바이스와 함께 | 매 프레임 RTV를 다시 잡는다 | 백버퍼 RTV |
| DX12 | 따로 | `GetCurrentBackBufferIndex()` | 백버퍼, RTV, 리소스 상태 |
| Vulkan | 따로, 서피스부터 | `vkAcquireNextImageKHR` | 이미지, 프레임버퍼, 세마포어 |

- **DirectX 11은 스왑체인을 넘겨받기만 합니다.** `D3D11CreateDeviceAndSwapChain` 한 호출에서 디바이스와 스왑체인이 함께 만들어지기 때문입니다.
- **DirectX 12는 백버퍼의 리소스 상태를 함께 가집니다.** 백버퍼 상태(`PRESENT`, `RENDER_TARGET`, `COPY_DEST`)는 커맨드 리스트가 아니라 리소스에 속한 전역 상태입니다.
  상태와 그 상태를 바꾸는 배리어(`transitionTo`)를 한 객체가 가지고 있어야 어긋나지 않습니다.
- **Vulkan만 동기화 객체를 가집니다.** acquire 세마포어는 진행 중인 프레임 슬롯 수만큼, renderFinished 세마포어는 이미지 수만큼 둡니다.
  드라이버가 진행 중 프레임 수보다 적은 이미지를 줄 수 있어 두 수가 같다는 보장이 없습니다. 프레임 슬롯 자체는 디바이스의 것이라 `acquireNextImage( device, frameSlot )` 처럼 인자로 받습니다.
- **OpenGL에는 스왑체인 객체가 없습니다.** 드라이버가 창의 백버퍼를 숨기고 `SwapBuffers(HDC)` 한 줄이 Present의 전부입니다.
  그 `HDC` 는 스레드에 컨텍스트를 붙이는 `MakeCurrent` 에도 쓰이므로 스왑체인이 아니라 컨텍스트의 일부입니다. 이름만 스왑체인인 빈 클래스를 만들지 않습니다.

스왑체인은 가상 인터페이스가 아니라 백엔드 안의 구체 클래스입니다. 백엔드 밖에서 스왑체인을 다형적으로 다룰 일이 없고, 가상 인터페이스로 만들면 상태 없는 빈 껍데기가 됩니다.
DirectX 12의 RTV 힙은 디바이스가 소유합니다. 오프스크린 렌더 타깃과 같은 힙을 나눠 쓰기 때문에, 앞쪽 `getBufferCount()` 개가 백버퍼이고 그 뒤가 오프스크린입니다.

### VSync

VSync는 설정 파일(`EngineConfig::_window._bVSync`, 기본 꺼짐), 플레이어 사용자 설정(`display.vsync`), 명령줄(`-vsync`) 순서로 정해집니다.
사용자 설정은 기본값과 다르게 저장했을 때만 영향을 줍니다. Present 경로는 `IRHIDevice::isVSyncEnabled()` 를 읽습니다.

실행 중에 바꿀 때는 렌더 스레드를 멈춘 뒤 `IRHIDevice::setVSync` 를 부릅니다(`UserSettingsHost`). 그래서 DXGI 스왑체인은 티어링을 지원하면 늘 `ALLOW_TEARING` 으로 만듭니다.
DirectX 11과 12는 `Present( 0, 0 )` 만으로 VSync가 꺼지지 않습니다. 스왑체인의 `ALLOW_TEARING` 플래그와 Present 플래그가 짝이어야 하고, `ResizeBuffers` 에도 같은 플래그를 다시 넘겨야 합니다(`DX/RHIDxgiTearing.h`).
Vulkan은 Present 호출에 동기화 인자가 없고, 스왑체인의 present 모드(FIFO, MAILBOX, IMMEDIATE)가 같은 역할을 합니다.

### 런타임 UI가 쓰는 기능

화면 2D 그리기(`CanvasRenderer`)를 위해 RHI에는 네 기능이 있습니다. 모두 네 백엔드가 같은 결과를 내는지 `RHIDeviceTest` 가 확인합니다.

- **R8 텍스처**(`RHIFormat::R8_UNORM`). 글리프 SDF 아틀라스처럼 한 채널만 필요한 텍스처에 씁니다. 셰이더는 `.r` 로 읽습니다.
- **영역 업로드**(`IRHIResourceFactory::uploadTexture2DRegion`). 텍스처의 밉 하나, 면 하나에서 사각형 구간만 고칩니다. 비압축 포맷만 받고, 검사는 `validateTextureRegionUpload` 한 곳에서 합니다.
  동기 규칙은 전체 업로드(`uploadTexture2D`)와 같습니다(`RegionUploadReadsBackOnEveryBackend`).
- **프리멀티플라이 블렌드**(PSO의 `_bPremultipliedAlpha`). 색에 알파를 미리 곱한 이미지를 섞을 때 씁니다. 원본 색 계수가 SrcAlpha 대신 One이 됩니다.
  알파 채널은 두 블렌드 모두 네 백엔드에서 One/InvSrcAlpha입니다(`PremultipliedBlendAddsColorWithoutAlphaMultiply`).
- **가위 사각형**(`IRHICommandList::setScissorRect`). "따라 해 보기"에서 읽은 기능입니다. 사각형 클리핑을 가위로 하면 배치가 끊기지 않습니다.

### 디바이스 종료 순서

`IRHIDevice::shutdown` 은 가상 함수가 아닌 템플릿 메서드이고, 백엔드는 단계마다의 훅만 채웁니다. 순서는 네 백엔드가 같습니다.

1. GPU 리소스를 보관하는 객체에 알립니다(`RHIRenderResource::releaseAllFor( this )`). 디바이스가 아직 살아 있으므로 리소스를 돌려줄 수 있습니다.
2. GPU를 기다리고 해제 큐를 비웁니다(`waitIdleInternal`). 1단계가 넘긴 리소스와 남은 `enqueueGpuRelease` 콜백이 여기서 실제로 해제됩니다.
3. 프레임 스트림 컨텍스트를 해제하고, 살아 있는 커맨드 리스트를 디바이스에서 분리합니다(`detachCommandRecordingInternal`).
   렌더 그래프가 리스트를 보관하므로 리스트는 디바이스보다 오래 살 수 있습니다. 분리하지 않으면 리스트의 소멸자가 이미 종료된 디바이스에 반납하려 합니다.
4. 백엔드 리소스, 스왑체인, 네이티브 디바이스를 종료합니다(`shutdownInternal`).

`shutdown` 을 부르기 전에 렌더 스레드는 이미 멈춰 있어야 합니다. `shutdown` 은 `waitIdle` 과 달리 렌더 스레드를 비우지 않습니다.
초기화에 실패해 `shutdown` 을 거치지 않고 사라지는 디바이스는 소멸자가 `RHIRenderResource::forgetAllFor` 로 핸들만 잊게 합니다. `RHIDeviceShutdownTest.StepsRunInContractOrder` 가 순서를 지킵니다.

### GPU 리소스를 놓는 세 가지 방법

- **엔진 리소스 객체**(`Mesh`, `Material`, `MaterialInstance`, `Texture2D`)는 `IRHIDevice::releaseHandle( kind, handle )` 로 핸들을 반환합니다.
  마지막 소유자는 게임 스레드가 아무 때나 놓습니다. 렌더 스레드가 프레임을 기록 중이면(`RenderThread::submit` 이 `notifyRenderFrameQueued`) 핸들을 줄에 두었다가 그 프레임이 끝난 뒤 해제합니다.
  해제 시점은 렌더 스레드의 `flushDeferredHandleReleases`, `waitIdle`, 렌더 스레드 종료, `shutdown` 중 먼저 오는 곳입니다. 렌더 스레드 자신이 부르거나 렌더 스레드가 쉬고 있으면 바로 해제합니다.
  언리얼의 `FDeferredCleanupInterface` 에 해당하고, `RHIDeferredHandleTest` 가 지킵니다.
- **엔진 밖 네이티브 리소스**는 `IRHIDevice::enqueueGpuRelease( delegate )` 로 백엔드 해제 큐(`RHIReleaseQueue`)에 넣습니다. DirectX 12와 Vulkan은 GPU 펜스를, DirectX 11과 OpenGL은 프레임 지연을 기준으로 해제합니다.
  펜스 값을 올리는 스레드가 읽어야 "이 프레임"이 맞으므로 렌더 스레드에서 부릅니다. 에디터의 `EditorDrawReleaseQueue` 가 ImGui 디스크립터와 렌더 타깃을 이것으로 넘깁니다.
- **디바이스 수명 이벤트**는 `RHIRenderResource` 레지스트리가 전합니다. GPU 리소스를 보관하는 객체는 이 클래스를 상속하고, 언리얼의 `FRenderResource` 에 해당합니다.
  디바이스가 살아 있으면 `releaseRhi`, 이미 없으면 `forgetRhi` 가 불립니다. 새 디바이스가 만들어지면 `EngineLoop` 이 `initAllFor( device )` 를 부르고, 각 객체의 `initRhi` 는 여러 번 불러도 결과가 같습니다.
  통보 도중에 다른 객체가 해제될 수 있으므로(머티리얼이 텍스처 참조를 놓으면 그 자리에서 `Texture2D` 가 사라진다) 레지스트리는 부르기 직전에 객체가 아직 있는지 잠금 아래에서 다시 확인합니다.

### 엔진 밖 모듈이 디바이스를 쓰는 법

에디터처럼 엔진 밖 모듈이 네이티브 핸들(DirectX 디바이스, Vulkan 인스턴스)을 쓸 때는 `IRHIDevice::queryNativeHandles( RHINativeHandles& )` 로만 받습니다.
모듈 경계를 넘는 것은 버전(`_version`)과 크기(`_byteSize`)를 가진 POD 하나이고, 엔진의 버전(`kRHINativeHandlesVersion`)과 다르면 채우지 않고 거절합니다.
백엔드는 `queryNativeHandlesInternal` 만 채웁니다. `RHINativeHandlesTest.QueryRejectsAnotherLayoutAndFillsTheMatchingOne` 가 버전 비교를 지킵니다.

백엔드는 별도 DLL이라 Engine의 전역 변수를 `extern` 으로 참조할 수 없습니다. 그래서 정책은 Engine이 정하고 디바이스는 메커니즘만 가집니다(`IRHIDevice::setImmediateSubmit` 이 그 예).

### Vulkan 최소 버전 — 1.3

`Vulkan/VulkanRHIApiVersion.h` 의 버전(1.3) 하나가 셰이더 쿠킹 타깃(`-fspv-target-env=vulkan1.3`, SPIR-V 1.6)이자 디바이스 최소 버전입니다.
물리 디바이스를 고를 때 이 버전에 못 미치는 디바이스는 건너뜁니다. 쿠킹된 SPIR-V 모듈을 하나도 받지 못하기 때문입니다.
1.3에서는 HLSL `discard` 가 내는 `OpDemoteToHelperInvocation` 기능이 필수라 따로 확인하지 않습니다. 버전을 바꾸면 이 헤더 하나만 고치고 셰이더를 다시 쿠킹합니다.

## 확장하는 법

### RHI에 함수를 더하기

1. `IRHIDevice.h`, `IRHIResourceFactory.h`, `IRHICommandList.h` 중 맞는 인터페이스에 순수 가상 함수를 더합니다. 백엔드 전용 API는 `IRHIDevice` 에 두지 않습니다.
2. 네 백엔드를 모두 구현합니다. 그 API에 개념이 없으면 빈 함수 대신 같은 결과를 내는 방법을 찾습니다. 앞의 "배리어" 항목처럼 DirectX 11과 OpenGL에도 할 일이 있는 경우가 많습니다.
3. 백엔드끼리 값이 같아야 하는 상수는 `RHITypes.h` 의 `constant` 블록에 한 번만 둡니다. 지금 값이 같더라도 바뀔 수 있는 값은 공유하고, 별칭을 만들지 않습니다.
4. `Modules/RHIModuleAbi.h` 의 `kRHIModuleAbiVersion` 과 `kRHIModuleAbiStamp` 를 함께 올립니다. 둘이 어긋나면 `static_assert` 가 빌드를 멈춥니다.
5. `RHIDeviceTest` 에 `RHIBackendSweep` 으로 네 백엔드를 도는 테스트를 더합니다. 결과는 픽셀이나 리드백 값으로 확인합니다.

새 백엔드 소스 파일은 `Graphics/RHI/<백엔드 폴더>/` 에 두면 됩니다. 모듈 타깃과 Shipping 빌드가 폴더 단위로 소스를 가져갑니다.

### 바꾼 뒤 확인하는 법

바인딩이나 백엔드 코드를 바꿨다면 아래를 모두 돌립니다. 셰이더 계약 테스트는 쿠킹된 바이너리를 읽으므로 쿠킹이 먼저입니다.

```powershell
cmake --build --preset Ninja-Debug
cd build/Ninja-Debug/Bin
./App.exe --cook-shaders
./EngineTest.exe --test_filter=ShaderBindingValidatorTest.*
./EngineTest.exe --test_filter=RHIDeviceTest.*
./EngineTest.exe --test_filter=GpuSceneTest.*,RenderPassTest.*,RenderPassGpuTest.*
cd ../../..
py -3 Scripts/dev/RunBackendSmoke.py
```

`RunBackendSmoke.py` 는 실제 앱을 네 백엔드로 띄워 스크린샷의 평균 색과 큐브 픽셀 수를 비교합니다.

- 디버그 레이어 메시지는 프레임 끝에 `[Error]` 로 로그에 나옵니다(`flushDebugMessages`). OpenGL도 Shipping이 아닌 빌드에서는 디버그 컨텍스트(`WGL_CONTEXT_DEBUG_BIT_ARB`)와 KHR_debug 콜백을 씁니다.
  GL 오류는 `[Error]`, 중간 심각도는 `[Warning]` 으로 나오고 알림은 끕니다. 스모크 로그의 `[Error]` 가 0이 아니면 읽어 봅니다.
- `-gv_rhiBackBufferFormat=1` 은 B8G8R8A8 백버퍼를 요청합니다. 백버퍼에 그리는 PSO가 `getBackBufferFormat()` 을 따르는지 이것으로 확인합니다.
  로그의 `백버퍼 포맷: 요청 → 채택` 줄이 실제로 채택된 포맷입니다.
- 스왑체인이나 Present를 고쳤다면 창 크기를 실제로 바꿔 봅니다. `ResizeBuffers` 의 플래그가 생성 때와 다르면 그 뒤의 Present가 `INVALID_CALL` 이 되는데, 리사이즈를 하지 않으면 드러나지 않습니다.
  창을 700×520으로 바꾸고 `-gv_screenshot` 을 찍으면 PPM 헤더가 클라이언트 영역 크기인 `684 481` 로 따라와야 하고, 로그의 `[Error]` 는 0이어야 합니다.

## 함정과 주의

### 공통

**`RHICapabilities` 의 바인드리스 플래그를 실제 지원으로 읽지 마세요.** 백엔드 기본값은 후보일 뿐입니다. DirectX 12는 `supportsNativeBindlessSampling()` 이 루트 시그니처(`_bBindlessRootSignature`)를 확인한 결과가 실제 값입니다.
백엔드의 능력은 백엔드 이름이 아니라 `getCapabilities()` 의 런타임 값으로 판단합니다. 에디터 렌더러 같은 외부 모듈의 능력은 `RHICapabilities` 에 넣지 않습니다. 에디터는 `IImGuiRendererBackend::createRendererBackend` 가 모르는 백엔드에 nullptr를 돌려주는 것으로 판단합니다.

**DirectX 11과 OpenGL의 `prepareTextureForShaderRead` 와 `transitionBuffer` 를 빈 함수로 두지 마세요.** DirectX 11은 여기서 SRV와 RTV, UAV 사이의 슬롯 해저드를 풀고 OpenGL은 메모리 배리어를 냅니다.
비워 두면 그 백엔드만 0을 읽습니다.

**`transitionBuffer` 는 상태가 그대로면 아무것도 하지 않습니다.** 같은 버퍼를 컴퓨트 디스패치 두 개가 이어서 쓰고 읽을 때는 `IRHICommandList::uavBarrier` 를 넣습니다. DirectX 11에서는 의도적으로 아무 일도 하지 않습니다.

**바인드리스 인덱스는 종류별 함수(`unregisterBindlessTexture`, `unregisterBindlessResource`)로 반납합니다.** DirectX 11, OpenGL, Vulkan은 텍스처와 버퍼의 인덱스 공간이 따로이고 DirectX 12만 힙 하나를 씁니다.
텍스처 인덱스를 버퍼 쪽에 반납하면 살아 있는 버퍼 슬롯이 비게 됩니다(`RHIDeviceTest.BindlessTextureReleaseKeepsBufferIndices`). 인덱스 충돌이 의심되면 등록과 반납 양쪽에서 인덱스와 소유자를 기록하는 추적부터 넣습니다.

**백엔드 구조를 가장 약한 백엔드에 맞추지 마세요.** 파일 이름은 엔진 인터페이스의 어휘(`SwapChain`, `CommandContext`)를 쓰고 네 백엔드가 같은 이름을 가집니다.
그 개념이 없는 백엔드는 빈 파일을 만들지 않고 이 문서에 이유를 적습니다. 백엔드 공용 자료구조는 `Support/` 에 둡니다. 백버퍼 상태는 `transitionTo` 로만 바꿉니다.

**백엔드 하나만 고쳐진 코드가 자주 나옵니다.** 예를 들어 `createStructuredBuffer` 의 크기 계산을 DirectX 12만 64비트로 곱하고 있었습니다. 테스트는 백엔드별 계약으로 씁니다.

**안 쓰이는 경로는 조용히 망가집니다.** 폴백 경로는 지우고, 아직 안 쓰는 기능은 테스트와 함께 남깁니다.
인덱스 드로우의 유일한 검증은 `RHIDeviceTest.IndexedIndirectDrawReadsInstanceSlotStream` 이고, `createIndexBuffer` 는 순수 가상입니다.

**구체 디바이스 클래스로 캐스팅하지 마세요.** `VulkanRHIDevice` 같은 클래스의 레이아웃과 vtable은 모듈 사이의 계약이 아닙니다. 네이티브 핸들은 `queryNativeHandles` 로 받습니다.

**`IRHIDevice::waitIdle` 을 렌더 스레드 밖에서 부르면 렌더 스레드의 패킷을 먼저 비웁니다.** 그래서 렌더 스레드에서 텍스처나 머티리얼 캐시를 잠근 채 기다리면 교착이 됩니다.
`RenderThread` 는 패킷을 실행한 뒤에 `_tail` 을 올리므로 "큐가 비었다"는 "프레임이 끝났다"와 같습니다. 창 리사이즈는 `RenderThread::waitIdle()` 뒤에 합니다.
게임 스레드의 버퍼 생성과 렌더 스레드의 드로우별 맵 읽기는 `_bindlessMutex` 의 읽기 잠금과 배타 잠금으로 나눕니다. 배타 잠금 안에서 읽기 잠금을 잡으면 스스로 멈춥니다.

**커맨드 리스트는 디바이스보다 오래 살 수 있습니다.** 디바이스를 종료하기 전에 `cmdList.reset()` 을 부릅니다. 기록 상태 캐시는 기록 스트림마다 하나입니다. OpenGL은 하나여야 맞습니다.
웨이브 배리어는 렌더 스레드가 첫 패스 리스트를 열어 앞부분에 기록하고 워커가 닫습니다. 연 스레드와 닫는 스레드가 다르므로 스레드 로컬에 묶인 상태가 함정입니다.

**GPU 메모리는 `RHIMemoryLedger` 가 셉니다.** 생성은 핸들 테이블에 넣는 곳에서, 해제는 지연 해제 콜백에서만 기록합니다. destroy를 요청한 시점에 기록하지 않습니다.
새 리소스 경로를 더하면 그곳에서 `recordAllocation` 과 `recordFree` 를 부릅니다.
Vulkan의 `heapUsage` 는 일부 드라이버에서 `vkAllocateMemory` 합계뿐이라 "엔진 밖" 사용량이 0에 가깝게 나옵니다. 스왑체인 사용량은 DirectX 12와 11 수치로 봅니다.
OpenGL은 벤더 확장이 없으면 사용량을 모릅니다. DirectX 11과 OpenGL은 할당 크기 API가 없어 논리 크기를 셉니다.

**배열 텍스처와 큐브 텍스처는 바인드리스 등록이 거절됩니다.** 셰이더 테이블이 Texture2D뿐이기 때문입니다. 그릴 면은 `_arrColorTargetSlice` 와 `_depthTargetSlice` 로 고릅니다.

**소프트웨어 어댑터로 띄우려면 `-gv_rhiSoftwareAdapter=1` 이나 환경 변수 `SW_RHI_SOFTWARE_ADAPTER=1` 을 씁니다.** CTest는 인자 없이 환경 변수를 씁니다.
DirectX 12와 11은 WARP, Vulkan은 CPU 디바이스를 씁니다. Vulkan은 lavapipe나 SwiftShader가 설치되어 있어야 하고, 없으면 경고와 함께 그 백엔드가 뜨지 않습니다. OpenGL은 이 설정을 무시합니다.
실제로 소프트웨어 어댑터로 떴는지는 `IRHIDevice::isRunningOnSoftwareAdapter` 로 확인합니다.

**GPU와 드라이버 문제를 조사할 때.** TDR이 반복되면 어댑터가 망가져 재부팅이 필요합니다. DirectX 12는 실패 지점에서 InfoQueue와 DRED 정보를 직접 꺼내 봅니다.
디버그 메시지에 이름 없는 객체("Unnamed")가 보이면 `SetName` 부터 붙입니다. 비동기 로거는 크래시 직전 메시지를 잃으므로, 직접 진단할 때는 `fopen`, `fflush`, `fclose` 로 파일에 씁니다.

**영역 업로드는 가끔, 작게만 씁니다.** Vulkan은 `uploadTexture2DRegion` 에서 제출하고 기다립니다. 매 프레임 큰 구간을 올려야 하면 프레임 커맨드 리스트에 복사를 기록하는 경로를 먼저 만듭니다.
DirectX 12는 전체 업로드와 같은 스테이징 슬롯과 배리어를 씁니다. 영역 업로드용 슬롯 규칙을 새로 만들지 않습니다.

**가위를 바인딩하는 곳을 새로 만들지 마세요.** `setViewport` 와 `beginRenderPass` 가 가위를 뷰포트 전체로 되돌립니다.
DirectX 11은 뷰포트를 바인딩하는 세 곳이 가위도 함께 바인딩합니다. 뷰포트를 바인딩하는 곳을 새로 만들면 가위도 바인딩해야 합니다. 그러지 않으면 Deferred Context의 빈 가위 때문에 아무것도 그려지지 않습니다.

### DirectX 11

**`ID3D11DeviceContext` 는 스레드 안전하지 않습니다.** 드로우 경로의 상수 버퍼 갱신은 워커가 바인딩한 자기 Deferred Context에 `Map(WRITE_DISCARD)` 합니다. 즉시 컨텍스트는 `_immediateContextMutex` 를 잡고 씁니다.
기록 컨텍스트의 스레드 로컬에는 디바이스 일련번호, 슬롯, 세대로 된 토큰만 둡니다. 포인터를 두면 해제된 컨텍스트에 `Map` 합니다. 모든 드로우 진입점은 `bindGraphicsPipelineForDraw()` 를 거칩니다.

**인스턴스 버퍼를 컴퓨트 셰이더 UAV 슬롯에서 해제해야 SRV로 읽힙니다.** 해제하지 않으면 D3D11이 SRV를 NULL로 강제합니다. 이 해저드는 WARNING이라 로그에 나오지 않으므로 해저드 ID만 ERROR로 올려 둡니다.

**`UpdateSubresource` 에 상자를 주지 않으면 버퍼 전체 길이를 원본에서 읽습니다.** 용량을 남겨 둔 버퍼에 짧게 올릴 때 상자가 없으면 원본 뒤를 넘어 읽어 드라이버 안에서 크래시가 납니다.
`RenderPassGpuTest.PartialStructuredBufferUploadReadsOnlyTheSourceRange` 가 가드 페이지로 지킵니다.

**기록이 끝난 `ID3D11CommandList` 는 백버퍼를 붙잡고 있습니다.** 리사이즈 전에 놓아야 합니다.
버퍼의 SRV는 버퍼 레코드(`BufferRecord`)에 둡니다. 기록 경로가 읽는 데이터를 별도 해시 맵에 두면 생성과 삭제가 일으키는 재해시를 기록이 읽습니다.

### DirectX 12

**펜스 대기의 시간 초과는 성공이 아닙니다.** 커맨드 얼로케이터는 리스트마다 하나씩 둡니다. 얼로케이터를 나눠 쓰면 DEVICE_HUNG이 납니다. 업로드 얼로케이터를 Reset하기 전에는 그 슬롯의 펜스를 직접 기다립니다.
업로드 복사 리스트는 열어 둔 채 기록만 하고, `flushPendingUploads` 가 프레임에 한 번 제출합니다(`_uploadSlotMutex`).

**공유해도 된다는 전제는 다시 확인하세요.** "X는 하나뿐이라 공유해도 된다"는 주석은 X를 여럿으로 만드는 변경(병렬 기록 같은)이 들어오면 틀린 말이 됩니다. 커맨드 얼로케이터 공유가 바로 그런 경우였습니다.

**DRED의 `PageFault VA=0` 은 하드웨어 고장이 아닙니다.** 엔진이 null 디스크립터나 리소스를 넘겼다는 신호입니다. 엔진 결함인지 드라이버 문제인지는 같은 재현을 다른 백엔드로 돌려 가립니다.

**디바이스가 멈춘(hung) 상태에서는 `Reset()` 과 `GetBuffer()` 가 실패합니다.** 반환값을 보고 그 프레임을 건너뜁니다. 백버퍼 목록은 하나라도 만들지 못하면 통째로 비웁니다. 크기만 확인하는 가드는 null 원소를 통과시키기 때문입니다.

**바인드리스 인덱스를 고르는 일과 뷰 생성은 한 임계 구역에서 합니다**(`acquireBindlessIndex(lock)`). 상수 버퍼 뷰는 256바이트로 정렬합니다.

**오프스크린 레코드(`_mapOffscreenTexture`)는 기록 경로에서도 `findOffscreenTargetView` 로만 읽습니다.** 이 함수는 잠근 채 값을 복사합니다.
이터레이터를 든 채 `transitionTexture` 를 부르면 같은 뮤텍스를 다시 잡아 교착이 됩니다.

**바인드리스 0번은 null Texture2D SRV입니다**(`kNullTextureBindlessIndex`). 힙 하나에 상수 버퍼, 버퍼, 텍스처 뷰가 같이 있습니다.
0번에 다른 뷰가 앉으면 0으로 남은 텍스처 인덱스(머티리얼 폴백 원소)가 상수 버퍼를 텍스처로 읽어 채널마다 NaN이 섞입니다. 0번에 무엇이 앉는지가 등록 순서에 달려 있어 실행마다 결과가 달랐습니다.

**`drawIndirect` 가 메시 정점 버퍼를 덮어쓴 적이 있습니다.** 로그와 테스트가 모두 통과해도 화면이 깨질 수 있으니, 렌더 변경은 스크린샷까지 봅니다.

**`enqueueGpuRelease` 는 해제를 너무 일찍 할 수 있습니다**(`_fenceValue`). 다른 스레드의 `waitForQueueDrain` 이 같은 값을 먼저 Signal하면, 기록 중인 프레임이 제출되기 전에 해제가 돌 수 있습니다.
기존 DirectX 12 해제 경로 전체에 해당하는 열린 문제입니다.

### Vulkan

**acquire한 이미지는 Present로만 돌려줍니다.** Present 없는 프레임마다 acquire하면 `UINT64_MAX` 대기로 교착이 됩니다. 리소스 해제는 실제 GPU 펜스(단조 증가하는 세대 번호)와 연결해야 합니다.

**일회성 업로드는 `VulkanOneShotCommands` 와 전용 풀, `_queueMutex` 로 합니다.**
`vulkan1.3` 타깃의 DXC는 `discard` 를 demote로 내므로 그 기능을 켭니다. 쿠킹된 셰이더가 바뀌면 검증 레이어 로그를 다시 읽습니다.
와이어프레임은 `fillModeNonSolid` 기능이 필요합니다. 백버퍼로 블릿할 때 이전 레이아웃은 `UNDEFINED` 입니다.

**렌더 패스는 `VulkanRHIRenderPassCache::RenderPassSpec` 과 `createRenderPassFromSpec` 한 곳에서만 만듭니다.** 스왑체인 CLEAR와 LOAD, 오프스크린, PSO 호환, 합성, 설명 기반 경로가 모두 이 설명을 채웁니다.
첨부와 의존성을 손으로 적는 곳을 다시 만들지 않습니다.

**텍스처 하나짜리 프레임버퍼 렌더 패스는 CLEAR로 고정입니다.** 깊이 없는 컬러 하나를 Load나 DontCare로 여는 패스는 load op을 키로 쓰는 합성 경로로 갑니다(`RHIDeviceTest.LoadOpKeepsSingleOffscreenTarget`).
새 렌더 패스 경로를 만들면 Load가 앞의 이미지를 지우지 않는지 이 테스트로 봅니다.

### OpenGL

**컨텍스트는 렌더 워커가 프레임마다 잡았다가 놓습니다.** 잡을 때는 누구든 250ms까지 기다립니다. 한 번만 시도하면 리소스 생성 가드가 컨텍스트를 잡은 순간 렌더 스레드가 프레임을 잃습니다.
잡지 못하면 로그(`GL context not acquired`)에 컨텍스트를 잡은 스레드와 잡은 시간이 남습니다.

**GL 리소스 생성과 상수 버퍼 갱신은 `ScopedOpenGLContext` 로 감쌉니다.** 이 함수들은 렌더 스레드 밖(게임 스레드)에서도 불립니다. 감싸지 않으면 `glGen*` 이 오류도 로그도 없이 쓸 수 없는 이름을 남깁니다.

**`ARB_gl_spirv` 가 없으면 초기화를 멈춥니다.** 다른 백엔드로 넘어가지 않습니다.

**`glClipControl` 을 `#ifdef GL_CLIP_CONTROL` 같은 가드 뒤에 두지 마세요.** 그런 토큰은 없어서 블록 전체가 컴파일에서 빠집니다. 있는지는 함수 포인터로 판단합니다.
빠지면 화면이 상하로 뒤집히는데, 픽셀 평균과 개수는 반전과 무관해서 오래 숨어 있었습니다. 패리티 테스트는 비대칭 장면에서 그려진 픽셀의 무게중심이 위쪽인지를 확인합니다.

**OpenGL의 작은 차이들.** MRT 클리어는 `glClearBufferfv` 를 씁니다. `R16G16B16A16_FLOAT` 는 `GL_HALF_FLOAT` 로 올립니다. `drawInstanced` 는 startInstance를 버립니다.
알파 채널 블렌드를 따로 지정하려면 `glBlendFuncSeparate` 를 씁니다. 로그 문구에 `[Error]` 같은 레벨 토큰을 쓰지 않습니다. 스모크 테스트가 그 토큰을 셉니다.

## 더 볼 곳

- [Graphics](../README.md): 렌더링 입문과 한 프레임의 흐름
- [Renderer](../Renderer/README.md): RHI를 부르는 쪽. 프레임 실행과 소유 규칙
- [Shader](../Shader/README.md): 바인딩 슬롯 계약과 백엔드별 바인딩 방식
- [RHI 프레임 계약](../../../../docs/05_RHI_FrameContract.md): 프레임과 렌더 타깃의 순서 규칙
- [핫 리로드와 C-ABI](../../../../docs/03_LiveReload_and_ABI.md): 백엔드 DLL을 로드하고 교체하는 방법

| 파일 | 내용 |
|---|---|
| `IRHIDevice.h` | 디바이스 인터페이스와 종료 순서 |
| `IRHICommandList.h` | 기록할 수 있는 명령 |
| `RHITypes.h` | 포맷, PSO 설명, 백엔드 공통 상수 |
| `RHICapabilities.h` | 백엔드가 할 수 있는 일 |
| `Modules/RHIModuleAbi.h` | 엔진과 백엔드 DLL의 계약 버전 |
| `RHIRenderResource.h` | 디바이스 수명 이벤트 레지스트리 |
