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

  Support/           백엔드들이 공유하는 자료구조
                     RHIHandleTable(핸들→객체), RHIIndexFreeList(인덱스 재사용),
                     RHIReleaseQueue(GPU 가 다 쓴 뒤 해제), FrameResourceRing(프레임 슬롯)
  DX/                D3D 전용. RHIDxgiFormat (DX11·DX12 만 쓴다)
  Modules/           백엔드를 DLL 로 분리해 싣는 장치
                     RHIModuleAbi(호스트↔모듈 계약), RHIModuleEntry(모듈 측 매크로),
                     <백엔드>/ModuleEntry.cpp
  DX11/ DX12/ GL/ Vulkan/   백엔드 구현
                     Vulkan/VulkanRHIHandle.h 는 Vulkan C 핸들의 전방 선언 묶음이다 —
                     백엔드 **헤더**에 <vulkan/vulkan.h> 가 새어 들어가지 않게 한다
```

## 백엔드 폴더의 파일 구성

한 클래스를 여러 파일로 나눌 때는 **소유 클래스 이름을 접두어로 남깁니다.**
`D3D12RHIResourcePipeline.cpp` 는 "`D3D12RHIResource` 의 파이프라인 부분" 이라는 뜻입니다.
인터페이스는 한 덩어리인데 파일만 쪼갠 것이라, 접두어가 없으면 어느 클래스의 일부인지 사라집니다.

| 파일 | 무엇을 하는가 | DX11 | DX12 | GL | Vulkan |
|---|---|:--:|:--:|:--:|:--:|
| `<B>RHIDevice` | 디바이스 상태·조회 | O | O | O | O |
| `<B>RHIDeviceInit` | 한 번 만들고 리사이즈 때 다시 만드는 것 | – | O | O | O |
| `<B>RHIDeviceSubmission` | 프레임 제출·펜스·커맨드 리스트 풀 | – | O | O | O |
| `<B>RHIDeviceDescriptor` | 셰이더 슬롯 배치 (루트 시그니처 / 디스크립터 세트) | – | O | – | O |
| `<B>RHIDeviceRenderPass` | 렌더패스·프레임버퍼 캐시 | – | – | – | O |
| `<B>RHIResource` | 버퍼·텍스처 | O | O | O | O |
| `<B>RHIResourcePipeline` | PSO·셰이더 스테이지·렌더패스 객체 | O | O | O | O |
| `<B>RHIResourceBindless` | 리소스를 인덱스로 접근 가능하게 등록 | O | O | O | O |
| `<B>RHICommandContext` | 드로우·디스패치·바인딩 기록 | O | O | O | O |
| `<B>RHICommandList` | 독립 기록 단위 | O | O | O | O |
| `<B>RHISwapChain` | 창의 백버퍼 — 다음 것을 고르고, 크기를 바꾸고, 표시한다 | O | O | – | O |

**빈 칸은 빠뜨린 것이 아니라 그 API 에 개념이 없다는 뜻입니다.**

- DX11 에 `DeviceInit`/`Submission` 이 없는 이유: 디바이스가 작아(400여 줄) 나눌 만큼 크지 않습니다.
  D3D11 은 명시적 제출이 없어 `Submission` 에 담을 내용 자체가 거의 없습니다.
- `Descriptor` 가 DX12·Vulkan 에만 있는 이유: DX11/GL 은 리소스를 **슬롯 번호**로 바인딩합니다.
  DX12 의 디스크립터 힙 + 루트 시그니처, Vulkan 의 디스크립터 세트 + 파이프라인 레이아웃은
  "바인딩할 자리를 미리 선언해 두는" 모델이고, 이게 두 세대의 가장 큰 차이입니다.
- `RenderPass` 가 Vulkan 에만 있는 이유: Vulkan 만 렌더패스/프레임버퍼를 **미리 만들어 캐시**해야
  합니다. 다른 API 는 렌더타깃을 그때그때 바인딩합니다.
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
- `SW_RHI_AS_MODULES` (기본 ON) 이면 백엔드는 별도 DLL 입니다. 그래서 Engine 의 전역 변수를
  백엔드에서 그냥 `extern` 으로 참조할 수 없습니다 — 정책은 Engine 이 정하고 디바이스는
  메커니즘만 갖는 형태로 넘깁니다(`IRHIDevice::setImmediateSubmit` 참고).

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
