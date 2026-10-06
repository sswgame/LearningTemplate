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
                     RHIGpuTimestamp(타임스탬프 칸 → 마이크로초 + 기준점의 GPU 시계 나노초 — 기준점 · 미기록 칸 · 틱 환산 규칙 하나.
                     엔진 표 `GPU.<패스>` 와 Tracy GPU 타임라인이 같은 값을 쓴다. `readGpuClockNanos` 는 지금 GPU 시계 — Tracy 가 CPU 시계와 맞춘다),
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
                     Vulkan/VulkanRHIApiVersion.h 는 요구 API 판(1.3) 하나 — 셰이더 쿠킹 타깃과 물리 디바이스 선택이 같이 본다
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
- **가위(`IRHICommandList::setScissorRect`)는 다음 `setViewport` · `beginRenderPass` 까지만 삽니다** — 둘이 가위를 뷰포트 전체로 되돌립니다(언리얼 `RHISetViewport` 와 같다).
  같은 개념이 API 마다 다릅니다: DX12 · Vulkan 은 가위가 늘 켜진 동적 상태, DX11 은 래스터라이저 상태의 `ScissorEnable`(늘 켜 두고 뷰포트와 함께 건다),
  GL 은 `GL_SCISSOR_TEST` 를 켜고 끄며 클리어 · 블릿도 가위를 따르므로 그동안 끕니다(`RHIDeviceTest.ScissorRectClipsDrawsAndResetsWithViewport`).
- **텍스처 일부 고치기는 `uploadTexture2DRegion`** 입니다(밉 하나 · 면 하나의 사각형, 비압축만 — 글리프 아틀라스). 검사는 `validateTextureRegionUpload` 한 곳이고,
  동기 규약은 `uploadTexture2D` 와 같습니다(`RHIDeviceTest.RegionUploadReadsBackOnEveryBackend`).

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
- **엔진 자원 객체(`Material` · `MaterialInstance` · `Texture2D`)가 자기 bindless 인덱스 · 버퍼 · 텍스처를 내릴 때는 `IRHIDevice::releaseHandle( kind, handle )`**
  입니다. 마지막 소유는 게임 스레드(GpuScene 후보를 덮는 수집 잡 · 걷은 뷰)가 아무 때나 놓으므로, 렌더 스레드가 프레임을 들고 있으면(`RenderThread::submit` 이
  `notifyRenderFrameQueued`) 핸들만 줄에 두었다가 렌더 스레드가 그 프레임을 끝낸 자리(병렬 기록 밖) · `waitIdle` · 렌더 스레드를 풀 때 · `shutdown` 에서 내립니다.
  렌더 스레드 자신이거나 쉬고 있으면 바로 내립니다. `RHIDeferredHandleTest` 가 고정합니다.

## Vulkan 최소 판 — 1.3

`VulkanRHIApiVersion`(1.3) 하나가 셰이더 쿠킹 타깃(`-fspv-target-env=vulkan1.3` → SPIR-V 1.6)이자 디바이스 최소 판입니다. 물리 디바이스 선택은
이 판에 못 미치는 디바이스를 건너뜁니다 — 쿠킹된 SPIR-V 모듈을 하나도 받지 못하기 때문입니다. 1.3 에서는 HLSL `discard` 가 내는
`OpDemoteToHelperInvocation` 의 기능이 필수라 따로 확인하지 않습니다. 판을 바꾸면 이 헤더 하나만 고치고 셰이더를 다시 쿠킹합니다.

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

## 함정 · 계약

- **소프트웨어 어댑터**: `-gv_rhiSoftwareAdapter=1` 또는 환경 변수 `SW_RHI_SOFTWARE_ADAPTER=1`(ctest 가 인자 없이) — DX12 · DX11 WARP, Vulkan 은 CPU 디바이스
  (lavapipe · SwiftShader 가 설치돼 있어야 한다 — 없으면 경고와 함께 그 백엔드가 안 선다), GL 은 무시. 실제로 선 어댑터는 `IRHIDevice::isRunningOnSoftwareAdapter`.
- **GPU · 드라이버** — 반복 TDR 은 어댑터를 망가뜨린다(재부팅 필요). DX12 는 실패 지점에서 InfoQueue · DRED 를 강제로 뽑는다. 이름 없는 객체("Unnamed")가 보이면 `SetName` 부터 붙인다.
  비동기 로거는 크래시 직전 메시지를 잃는다 — 직접 진단은 `fopen` + `fflush` + `fclose`.
- **GPU 메모리는 `RHIMemoryLedger` 가 센다** — 생성은 핸들 표에 넣는 자리, 해제는 지연 해제 콜백에서만 적는다(destroy 요청 시점이 아니다). 새 자원 경로를
  더하면 거기서 `recordAllocation` / `recordFree` 를 부른다. Vulkan `heapUsage`(이 AMD 드라이버)는 `vkAllocateMemory` 합뿐이라 "엔진 밖" ≈ 0 — 스왑체인 몫은
  DX12 · DX11 수치로 본다. GL 은 벤더 확장이 없으면 사용량이 "모름" 이다. DX11 · GL 은 할당 크기 API 가 없어 논리 크기다.
- **D3D11 `UpdateSubresource` 에 상자가 없으면 버퍼 전체 길이를 원본에서 읽는다** — 용량을 남겨 둔 버퍼에 짧게 올릴 때는 상자를 준다(원본 뒤를 넘어 읽어
  드라이버 안에서 죽는다 — `RenderPassGpuTest.PartialStructuredBufferUploadReadsOnlyTheSourceRange` 가 가드 페이지로 지킨다).
- **텍스처 영역 업로드는 `uploadTexture2DRegion`(가끔 · 작게 — Vulkan 은 제출하고 기다린다)** — 매 프레임 큰 구간이 필요해지면 프레임 커맨드 리스트에 복사를 기록하는
  길을 먼저 만든다. 검사는 `validateTextureRegionUpload` 한 곳(DX12 는 전체 업로드와 같은 스테이징 슬롯 · 배리어 — 새 슬롯 규칙을 만들지 말 것).
- **블렌드는 곧은(SrcAlpha) · 프리멀티플라이(`_bPremultipliedAlpha`, One) 둘이고 알파 채널은 네 백엔드 모두 One/InvSrcAlpha 다** — GL 도 `glBlendFuncSeparate`
  (`RHIDeviceTest.PremultipliedBlendAddsColorWithoutAlphaMultiply` 가 알파 255 로 지킨다).
- **가위(`setScissorRect`)는 `setViewport` · `beginRenderPass` 가 뷰포트 전체로 되돌린다** — DX11 은 래스터라이저 상태에 늘 켜 두고 뷰포트를 거는 세 자리가 가위도 건다
  (뷰포트를 거는 새 자리를 만들면 가위도 건다 — 안 그러면 Deferred Context 의 빈 가위로 아무것도 안 그려진다). GL 은 가위 시험을 켜고 끄며, 클리어 · 블릿 동안은 끈다.
- **Vulkan 의 텍스처 하나짜리 프레임버퍼 렌더 패스는 CLEAR 고정이다** — 깊이 없는 컬러 하나를 Load · DontCare 로 여는 패스는 load op 을 키로 드는 합성 경로로 간다
  (`RHIDeviceTest.LoadOpKeepsSingleOffscreenTarget`). 새 렌더 패스 경로를 만들면 Load 가 앞 그림을 지우지 않는지 그 시험으로 본다.
- **주의: DX12 `enqueueGpuRelease`(`_fenceValue`)** — 다른 스레드의 `waitForQueueDrain` 이 같은 값을 먼저 Signal 하면 기록 중인 프레임이 제출되기 전에 해제가 돌 수 있다.
  기존 DX12 해제 경로 전부에 해당한다(열린 일).
- **배열 · 큐브 텍스처는 bindless 등록이 거부된다**(셰이더 테이블이 Texture2D 뿐). 면은 `_arrColorTargetSlice` · `_depthTargetSlice` 로 고른다.
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
  bindless 0 번은 null Texture2D SRV(`kNullTextureBindlessIndex`) — 힙이 CBV · 버퍼 · 텍스처 한 공간이라 0 으로 남은 텍스처 인덱스(머티리얼 폴백 원소)가 상수버퍼를 텍스처로 읽어
  채널마다 NaN 이 섞였다(군중 시험이 무관한 커밋 뒤에 DX12 만 실행마다 다른 픽셀 수로 졌다 — 0 번에 무엇이 앉는지가 등록 순서에 달렸다).
- **Vulkan** — acquire 한 이미지는 present 로만 돌려준다(present 없는 프레임마다 acquire 하면 `UINT64_MAX` acquire 로 교착). 리소스 해제는 실제 GPU 펜스(단조 세대)와 이어야 한다.
  일회성 업로드는 `VulkanOneShotCommands` · 전용 풀 · `_queueMutex`. `vulkan1.3` DXC 는 `discard` 를 demote 로 내므로 기능을 켠다 — 쿠킹된 셰이더가 바뀌면 검증 레이어 로그를 다시 읽는다.
  와이어프레임은 `fillModeNonSolid`. 백버퍼 블릿의 이전 레이아웃은 `UNDEFINED`.
- **GL** — 컨텍스트는 렌더 워커가 프레임마다 쥐었다 놓는다. 잡기는 누구든 기다려서(250 ms) 한다 — 한 번만 시도하면 자원 생성 가드가 쥔 순간 렌더 스레드가
  프레임을 잃는다. 못 잡으면 로그(`GL context not acquired`)에 쥔 스레드(렌더 스레드인지) · 쥔 시간이 남는다. `ARB_gl_spirv` 가 없으면 초기화에서 끊는다(다른 백엔드로 넘어가지
  않는다). `glClipControl` 은 `#ifdef GL_CLIP_CONTROL`(없는 토큰) 같은 가드 뒤에 두지 말 것(상하 반전이 오래 숨었다). MRT 클리어는 `glClearBufferfv`, `R16G16B16A16_FLOAT` 는
  `GL_HALF_FLOAT`, `drawInstanced` 는 startInstance 를 버린다. 로그 문구에 `[Error]` 같은 레벨 토큰을 쓰지 말 것(스모크가 센다).
- **스왑체인은 진짜 객체다**(`5aea5ef1`) — 가상 인터페이스로 되돌리지 말 것, GL 은 의도적으로 없다. Present PSO 는 대상 포맷(`getBackBufferFormat`)으로, BGRA 는 `-gv_rhiBackBufferFormat=1`
  로 검증. `IRHIDevice` 에 백엔드 전용 API 를 두지 않는다 — 에디터 같은 외부 모듈은 네이티브 핸들을 판 번호 든 `RHINativeHandles` 로 받는다
  (`IRHIDevice::queryNativeHandles` 가 판 · 크기를 대조). 구체 디바이스로 캐스팅하지 말 것. 에디터 능력을 `RHICapabilities` 에
  넣지 말 것(`createRendererBackend` 가 모르는 백엔드에 `nullptr`). 백엔드 능력은 이름이 아니라 `getCapabilities()` 런타임 값으로.
- **백엔드 하나만 고쳐진 모양이 계속 나온다**(`createStructuredBuffer` 는 DX12 만 64 비트로 곱한다). 시험은 백엔드별 계약으로 쓴다. 안 쓰이는 경로는 조용히 썩는다 — 폴백은 지우고
  "아직 안 쓰는 기능" 은 시험과 함께 남긴다(인덱스 드로우의 유일한 검증은 `RHIDeviceTest.IndexedIndirectDrawReadsInstanceSlotStream`, `createIndexBuffer` 는 순수 가상).
- **RHI 백엔드의 .cpp 는 `Graphics/RHI/<백엔드 폴더>/` 에 두면 끝이다**(모듈 · Shipping 이 폴더로 가져간다). 파일은 `<Backend>RHIDevice` · `…DeviceInit` · `…DeviceSubmission` 축으로. 백엔드는 별도 MODULE DLL 이라 Engine
  전역 변수를 extern 으로 못 쓴다 — 정책은 Engine, 메커니즘은 디바이스.
