# RHI 비동기 업로드 · 머티리얼 초기화 · PSO 준비 상태

> 상태: 설계 · 선행: 없음 · 영역: graphics · 옛번호: 없음 · 진행: 0/14

> 질문: 게임 스레드와 렌더 스레드가 GPU 완료를 기다리는 지점을 어떤 계약으로 없애고, 무엇부터 바꾸는가?
> 독자: RHI 백엔드와 렌더러를 고치는 작업자, 통합 담당.
> 결론: 업로드와 되읽기는 "제출 → 티켓 → 폴링" 계약 하나로 바꾸고(RHI ABI 버전을 한 번 높여 옛 동기 함수 세 개를 지운다), 새 머티리얼과 PSO 는 "준비 전 / 준비됨 / 실패" 상태를 데이터로 두어 준비 전에는 그리지 않는다. 프레임 경로에는 `waitIdle` 을 새로 만들지 않는다. ABI 변경이 없는 Vulkan 대기 좁히기(U2)는 먼저 갈 수 있다.

2026-10-25 코드 읽기로 정리했습니다. 작업자는 빌드와 실행을 하지 않으므로 실행으로 확인한 것은 없습니다. 근거는 **[코드]**(읽어서 확인), **[추정]**, **[확인 전]** 으로 표시합니다.

프로파일 스코프 `GT.Material.waitIdle`, `RT.Upload.waitIdle`, `RT.PSO.create`, `GT.Load.sync` 는 `wt/profscopes` 브랜치가 붙이는 중입니다. 이 문서를 쓸 때 그 브랜치에는 main 위의 커밋이 없어서 스코프가 어느 줄을 감싸는지는 확인하지 못했습니다. 이 문서는 스코프 이름만 인용하고, 모든 측정(M0, M1)은 그 스코프로 합니다.

관련 문서는 [AsyncResourceLoading](AsyncResourceLoading.md)(핸들 상태와 로드 중 규칙), [PreloadAndLod](PreloadAndLod.md)(PSO 미리 만들기와 텍스처 밉 스트리밍), [GraphicsFormatBundle](GraphicsFormatBundle.md)(머티리얼 PSO 변형이 실패하면 그리지 않는 규칙, G11)입니다. 이 문서는 그 위에서 GPU 쪽 대기만 다룹니다.

## 1. 지금 동기 대기 지점

| # | 위치 | 기다리는 것 | 스레드 | 호출 빈도 근거 | 스코프 |
|---|---|---|---|---|---|
| W1 | `Engine.cpp` 612-617 | RT 큐가 빌 때까지, 이어서 GPU 전체 | GT | 초기화를 기다리는 머티리얼이 있는 프레임만 | `GT.Material.waitIdle` |
| W2 | `VulkanRHIResourceFactory.cpp` 579-740, `VulkanOneShotCommands.h` 76-94 | 제출한 일이 아니라 큐 전체 | RT, GT | 텍스처 업로드가 있는 프레임, 업로드 수만큼 | `RT.Upload.waitIdle` |
| W3 | `FrameRendererPSO.cpp` 206, 322, 533, `FrameRendererDraw.cpp` 531 | 동기 `createPipelineState` | RT | 새 PSO 키가 처음 나온 프레임 | `RT.PSO.create` |
| W4 | `TextureCache.cpp` 319, `Material.cpp` 183, `MeshCache.cpp` 199, `PrefabAsset.cpp` 1180 | 디스크 읽기와 디코드, 텍스처는 GPU 업로드까지 | GT | 동기 API 를 부르는 시점 | `GT.Load.sync` |
| W5 | `GPUUploadQueue.cpp` `flush` | 워커의 메시 GPU 생성(CPU 대기) | GT | 새 메시가 있는 프레임 | `GT.GpuUpload.flush`(있음) |
| W6 | `CanvasRenderer.cpp` 475 → `TextureCache.cpp` 558 | GPU 전체 | RT | 캔버스 렌더 텍스처 크기가 바뀔 때 | 없음 |
| W7 | 아래 목록 | GPU 전체 | 여럿 | 수명 이벤트와 도구 | 필요 없음 |
| W8 | `RenderThread.cpp` 755, `FrameCapture.cpp` 110, 149, 181, `EditorUICapture.cpp` 233 | `readbackTexture2D` 의 GPU 대기 | RT, 도구 | 캡처 요청이 있을 때만 | 없음 |

W1 ~ W3 은 프레임 경로이고 이 계획의 대상입니다. W6 은 프레임 경로의 기존 `waitIdle` 이라 U13 이 지웁니다. W4 는 호출 시점이 호출처마다 달라 빈도를 코드로 말할 수 없으므로 `GT.Load.sync` 로 측정합니다. W5 는 GPU 가 아니라 워커를 기다리고 이미 스코프가 있어 이 계획의 대상이 아닙니다.

W7 은 지워지지 않고 남는 허용 목록입니다. 씬 활성화는 `SceneManager.cpp` 561입니다. 종료는 `Engine.cpp` 800입니다. 창 크기와 사용자 설정 변경은 `App.cpp` 599와 `UserSettingsHost.cpp` 102입니다. 렌더 스레드 모드 전환은 `RenderThread.cpp` 243과 `RenderThreadSubsystem.cpp` 42입니다. 셰이더 실시간 컴파일과 모듈 핫 리로드는 `LiveShaderSubsystem.cpp` 64와 `ModuleHost.cpp` 357, 365입니다. 백엔드 교체는 `RHISubsystem.cpp` 108과 `RHI.cpp` 335입니다. 에셋 핫 리로드는 `TextureCache::reload`, `MaterialCache::reload`, `MeshCache.cpp` 220입니다. 캡처 도구는 `PortraitRenderer.cpp` 174, `PortraitBatchUtil.cpp` 27, `ReflectionProbeBaker.cpp` 134, 223입니다.

호출 빈도의 코드 근거는 다음과 같습니다.

- W1 의 조건은 `MaterialCache::hasPendingInitialize` 하나입니다. 이 값은 원자 카운터이고, `requestInitialize`(`MeshComponent.cpp` 104, `EnvironmentUtil.cpp` 65, `SpriteInstanceBatch.cpp` 54)와 `pinAsset` 이 증가시킵니다. 그래서 안정 상태에서는 0 번이고, 씬 로드와 처음 쓰는 머티리얼의 스폰 프레임에 몰립니다 **[코드]**.
- W2 에서 RT 가 한 프레임에 부르는 업로드는 `completePendingLoads` 8개 이하(`kMaxUploadsPerFrame`), `updateStreaming` 2개 이하, 구간 갱신(`flushRegionUpdates`), 글리프 아틀라스 페이지(`CanvasRenderer.cpp` 418, 443)입니다. Vulkan 은 이 호출마다 큐를 비웁니다 **[코드]**.
- W3 의 키는 `RenderPSOCache::materialPSOKey` 가 (패스 PSO, 퍼뮤테이션 해시, 뷰 모드, 컬 반전)으로 만듭니다. 키가 캐시에 있으면 만들지 않으므로(`ensureMaterialPSOVariant`) 안정 상태에서는 0 번입니다 **[코드]**.

W1 의 비용은 한 프레임이 아닙니다. `RenderThread::waitIdle` 은 RT 큐가 빌 때까지 기다린 뒤 장치 대기를 하므로 파이프라인에 들어 있는 프레임을 전부 끝내야 돌아옵니다(`RenderThread.cpp` 289-303, `IRHIDevice::waitIdle`의 `setRenderThreadDrain`) **[코드]**. 렌더 스레드를 쓰지 않는 구성에서는 `RenderThread::waitIdle` 이 바로 돌아옵니다 **[코드]**.

W2 의 Vulkan 업로드는 큐 락 `_queueMutex` 를 잡은 채 `vkQueueWaitIdle` 을 부릅니다. 같은 락을 RT 의 `endFrame` 제출도 잡으므로(`VulkanRHIDeviceSubmission.cpp` 481) 한 스레드의 업로드가 다른 스레드의 프레임 제출을 막고, 기다리는 대상도 자기 업로드가 아니라 큐에 있는 이전 프레임 전체입니다 **[코드]**. DX11, DX12, GL 의 같은 함수는 GPU 를 기다리지 않습니다. DX11 은 즉시 컨텍스트의 `UpdateSubresource`, DX12 는 프레임 리스트 앞에 끼는 복사 리스트(`flushPendingUploads`), GL 은 `glTexSubImage2D` 입니다 **[코드]**.

이 계획이 고치지 않는 것도 있습니다. `RT.BeginFrame` 이 기다리는 GPU 백프레셔 꼬리는 대기 가능 스왑체인으로 고칠 히치가 없다고 이미 기각했습니다(결정 기록 3절 "히치"). 비슷한 선례로 메시를 미리 읽어 보관하게 한 변경이 StarSkirmish LowEnd 의 최악 프레임을 25.8 ms 에서 4.1 ms 로 줄였습니다(결정 기록 5-2). 이 계획의 기대도 "새 머티리얼 · 업로드가 있는 프레임의 추가 히치 제거"로 한정합니다.

## 2. 설계

### 2.1 공통 원칙

- 기다리는 쪽에 기다릴 대상을 주지 않습니다. 대신 대상이 "준비 전 / 준비됨 / 실패" 중 어디인지를 데이터로 주고, 준비 전이면 그리지 않습니다. 폴백은 없고 실패는 오류로 남습니다.
- 상태는 만든 쪽이 쓰고(release), 읽는 쪽이 읽습니다(acquire). 읽는 쪽은 `Ready` 를 본 뒤에야 GPU 필드를 읽습니다.
- GPU 자원을 다른 스레드가 볼 수 있게 바꾸는 지점(bindless 등록, PSO 캐시 삽입)은 RT 프레임 셋업 구간 한 곳입니다. 이 구간은 `FrameRenderer::executePacket` 의 시작부터 `RenderGraph::executeParallel` 이 `setParallelRecording( true )` 를 부르기 전까지입니다(`FrameRenderer.cpp` 648-820, `RenderGraph.cpp` 501). RT 한 스레드가 직렬로 도는 구간이고 이전 프레임의 병렬 기록은 끝나 있습니다. 병렬 기록 중 bindless 테이블을 바꾸지 않는 규칙(`assertRegistryMutableNow`)을 지키는 가장 단순한 위치입니다 **[코드]**.
- 셋업 구간의 작업 순서는 (1) 끝난 전송을 `pollTransfer` 로 확인하고 bindless 에 등록해 발행, (2) 머티리얼 GPU 초기화, (3) 새 전송 제출, (4) PSO 상태 갱신입니다.

### 2.2 C. RHI 업로드와 되읽기: 제출 → 티켓 → 폴링

`IRHIResourceFactory` 의 동기 함수 세 개(`uploadTexture2D`, `uploadTexture2DRegion`, `readbackTexture2D`)를 지우고 아래 다섯 개로 바꿉니다. 시그니처는 U3 ~ U6 작업자가 U1 을 기다리지 않고 코딩할 수 있게 여기서 고정합니다.

| 함수 | 뜻 | 대체하는 것 |
|---|---|---|
| `submitTextureUpload( texture, desc )` | 새 텍스처의 밉 체인 전체를 제출한다 | `uploadTexture2D` |
| `submitTextureRegionUpload( texture, desc )` | 쓰고 있는 텍스처의 사각형 구간을 제출한다 | `uploadTexture2DRegion` |
| `submitTextureReadback( texture, mip, slice, outLayout )` | 읽기를 요청한다. 행 배치는 바로 채워 준다 | `readbackTexture2D` |
| `pollTransfer( ticket )` | `Pending`, `Complete`, `Failed` 를 값싸게 돌려준다 | 없음 |
| `takeTextureReadback( ticket, outBytes )` | 완료된 읽기 결과를 한 번 가져간다 | 없음 |

제출 함수는 `RHITransferSubmit` 을 돌려줍니다. 안에는 `RHISubmitResult`(`Accepted`, `Busy`, `Rejected`)와 `RHITransferTicket`(단조 증가하는 64비트 시리얼, 0 은 무효)이 있고 `[[nodiscard]]` 입니다.

**계약**

1. 제출 함수는 반환하기 전에 소스 메모리를 복사합니다. 호출자는 바로 버퍼를 재사용합니다.
2. 제출 함수는 GPU 완료를 기다리지 않습니다. 스테이징이 모자라면 기다리지 않고 `Busy` 를 돌려주며, 호출자가 다음 프레임에 다시 제출합니다. 검사에 걸리면 `Rejected` 이고 이유를 로그로 남깁니다. `Rejected` 는 다시 시도하지 않습니다.
3. 순서: 제출한 전송은 그 뒤에 제출되는 모든 프레임 커맨드보다 먼저 실행됩니다. 지금 DX12 가 복사 리스트를 프레임 리스트 앞에 끼우는 것과 같은 의미입니다 **[코드]**. 같은 프레임에서 이미 기록한 앞선 드로우가 어느 내용을 보는지는 백엔드마다 달라 정해져 있지 않으므로, 호출자는 업로드를 그리기 기록 전에 제출합니다. 지금 RT 가 셋업 구간에서 하는 것과 같습니다. 같은 텍스처에 대한 제출끼리는 제출 순서대로 실행됩니다.
4. 티켓의 `Complete` 는 "완료 워터마크 이상"으로 판정합니다. 워터마크는 연속으로 끝난 시리얼의 끝입니다. 그래서 `Complete` 를 본 뒤 같은 티켓을 다시 물어도 `Complete` 이고, 티켓을 해제하는 함수가 필요 없습니다. 큐 안에서 완료 순서가 제출 순서와 다를 수 있는 백엔드(Vulkan)는 연속 구간만 워터마크로 올립니다.
5. `Failed` 는 장치 손실처럼 끝나지 않을 일입니다. 오류를 로그로 남기고 `Failed` 를 돌려주며 대기하지 않습니다.
6. 스레드: `RHICapabilities::_bThreadSafeResourceCreation` 이 참인 백엔드(DX11, DX12, Vulkan)는 어느 스레드에서나 제출합니다. 거짓인 GL 은 컨텍스트를 가진 스레드(RT)에서만 제출합니다. `pollTransfer` 는 어느 스레드에서나 부를 수 있어야 합니다. GL 은 컨텍스트를 빌려서 묻습니다(`ScopedOpenGLContext`).
7. 소비자는 `Complete` 를 본 뒤에야 텍스처를 bindless 에 등록합니다. 등록이 없으면 셰이더가 읽을 SRV 인덱스가 없으므로 "완료 전 사용"은 규칙이 아니라 구조로 막힙니다. 로드 중에 보이는 회색 텍스처는 지금처럼 정해진 데이터입니다([AsyncResourceLoading](AsyncResourceLoading.md) 2절).

**동기 변형은 ABI 밖의 헬퍼입니다.** `RHITransferUtil::uploadTexture2DAndWait`, `uploadTexture2DRegionAndWait`, `readbackTexture2DAndWait` 를 Engine 에 둡니다(백엔드가 아니라 Engine 코드라 vtable 에 없습니다). 제출한 뒤 `pollTransfer` 를 짧게 양보하며 되풀이하고, 상한은 DX11 `waitIdle` 의 상한(10 초)과 같게 두며 넘으면 오류입니다. 사용처는 테스트, 도구, 스크린샷, 에디터 캡처, 동기 텍스처 `acquire` 입니다. RT 프레임 셋업 구간에서는 쓰지 않습니다. 백엔드에 두 번째 경로가 생기지 않는 것이 핵심입니다.

**콜백이 아니라 폴링인 이유.** `enqueueGPURelease` 콜백은 완료를 확인하는 스레드에서 불립니다. bindless 등록은 RT 셋업 구간에서만 할 수 있으므로 콜백으로 받아도 결국 RT 의 큐에 넣고 다시 읽게 됩니다. 폴링은 RT 가 셋업 구간에서 직접 읽으므로 스레드 경계가 한 번 줄어듭니다. 해제용 `enqueueGPURelease` 는 그대로 둡니다.

**ABI 결정.** `kRHIModuleAbiVersion` 을 33 에서 34 로 높이고 옛 가상 함수 세 개를 지웁니다. 백엔드 DLL 이 엔진과 같은 vtable 을 봐야 로드되므로(`RHIModuleAbi.h`) 이 변경은 한 번에 들어가야 하고 백엔드마다 쪼갤 수 없습니다. 옛 함수를 남기고 새 함수를 그 위에서 구현하는 기본 구현(shim)을 두는 안은 기각합니다. 같은 일을 하는 경로가 둘 남아 폴백 금지 원칙과 부딪히고, 호출자가 어느 쪽을 쓸지 매번 골라야 하기 때문입니다.

**스테이징 링.** 장치마다 고정 용량의 영속 매핑 업로드 링을 둡니다. 구간은 티켓 시리얼로 표시하고 워터마크가 지나면 회수합니다. 기존 `SpanAllocator::freeDeferred` 를 재사용할 수 있는지는 U5 와 U6 이 판단합니다 **[확인 전]**. 시작 용량은 64 MiB 로 두고 `RHIMemoryLedger` 의 Staging 항목으로 보이게 합니다. 이 값은 측정으로 정한 것이 아니라 4096² BC7 텍스처 한 장(밉 포함 약 21 MiB)을 몇 장 동시에 담는 크기로 잡은 추정이며 M1 이 조정합니다. 링보다 큰 요청은 단독 스테이징 버퍼를 만들고 완료 뒤 해제하며, 그 총량에 상한을 둡니다. 상한을 넘으면 `Busy` 입니다.

**전송 큐.** 1단계는 지금처럼 그래픽스 큐 하나입니다. 전용 복사 큐(Vulkan 의 전송 큐 패밀리, DX12 의 COPY 큐)는 큐 소유권 이전, 레이아웃 전이, 큐 사이 동기화가 필요하고 이득은 큰 텍스처를 스트리밍하는 중의 프레임 시간뿐이라 측정 없이 하지 않습니다. 같은 큐에서는 제출 순서가 곧 실행 순서이므로 `Complete` 이전에 사용해도 GPU 는 안전합니다. 그래도 소비자 규칙을 `Complete` 로 두는 것은 나중에 전용 큐를 도입할 때 규칙을 바꾸지 않기 위해서입니다.

**백엔드별 구현.**

| 백엔드 | 지금 | 새 계약 구현 | 난이도 |
|---|---|---|---|
| DX11 | `UpdateSubresource`(즉시 컨텍스트, 잠금) | 같은 호출 뒤 티켓마다 이벤트 쿼리 하나(풀). `pollTransfer` 가 `GetData` | 낮음. 호출 스레드의 드라이버 복사 비용은 그대로 |
| GL | `ScopedOpenGLContext` 로 컨텍스트를 빌려 `glTexSubImage2D` | 같은 호출 뒤 `glFenceSync`. `pollTransfer` 는 컨텍스트를 빌려 `glClientWaitSync` | 낮음에서 중간. 워커 제출 불가, 컨텍스트 점유 경합(`GPUUploadQueue.cpp` 주석). PBO 비동기 복사는 이 계획 밖 |
| DX12 | 프레임 슬롯의 업로드 힙과 복사 리스트가 프레임 리스트 앞에 끼는 구조, 얼로케이터 펜스 대기 | 전용 업로드 링, 복사 리스트와 얼로케이터 풀, 전용 `ID3D12Fence`. 제출은 즉시 `ExecuteCommandLists` 하고 `Signal` | 중간. `_uploadSlotMutex` 가 업로드와 프레임 제출에 같이 걸려 있어 범위를 줄여야 하고, 텍스처 상태 추적 갱신과 RT 기록이 겹치는지 점검해야 한다 |
| Vulkan | 일회성 커맨드 버퍼를 만들고 `vkQueueWaitIdle` | 업로드 링, 펜스 풀, 제출 뒤에도 살아 있는 전송 작업 객체(펜스 통과 후 회수). 시리얼과 워터마크 | 높음, 이득이 가장 큼. `VulkanOneShotCommands` 가 소멸자에서 커맨드 버퍼를 해제하는 구조를 바꾸고, 레코드의 `_layout` 갱신 시점을 정해야 한다 |

DX12 의 인덱스 반환 지연(`deferFreeBindlessIndex`)과 Vulkan 의 해제 큐는 이미 GPU 완료를 기준으로 합니다 **[코드]**. 타임라인 세마포어를 쓸 수 있는 Vulkan 버전 요구는 확인하지 않았으므로 펜스를 씁니다 **[확인 전]**.

### 2.3 A. 새 머티리얼 첫 사용을 프레임 경계로

지금 흐름은 이렇습니다. 컴포넌트가 머티리얼 경로를 잡으면 `requestInitialize` 가 카운터를 증가시킵니다. 게임 스레드의 `submitRenderPacket` 이 카운터를 보고 RT 와 GPU 를 모두 비운 뒤(W1) `initializePending` 으로 파일을 읽고, 상수 버퍼를 만들고, bindless 에 등록하고, 그 인덱스를 패킷에 싣습니다.

바꾸는 흐름은 이렇습니다.

1. `requestInitialize` 는 그대로입니다. 컴포넌트는 디바이스를 모릅니다.
2. 파일 읽기와 파싱(`Material::loadFromFile`)은 워커 단계로 옮깁니다. 텍스처 로드가 이미 쓰는 두 단계 구조([AsyncResourceLoading](AsyncResourceLoading.md) 3절)와 같은 구조입니다.
3. GPU 단계(상수 버퍼 생성과 첫 쓰기, `registerBindlessBufferSrv`, 텍스처 SRV 해석)는 RT 셋업 구간에서 `MaterialCache::initializePending` 이 합니다. 프레임당 개수 상한을 둡니다. 시작값은 측정 전에 정하지 않고 M1 이 정합니다.
4. `Material` 에 원자 상태 `GpuState`(`NotRequested`, `Pending`, `Ready`, `Failed`)를 둡니다. `Pending` 은 `requestInitialize` 가 쓰고, `Ready` 와 `Failed` 는 RT 가 쓰며, 게임 스레드가 읽습니다. 지금의 `isRHIValid()`(상수 버퍼 핸들이 0 이 아님)는 반쯤 초기화된 상태를 구분하지 못하므로 이 상태로 대체합니다.
5. 패킷 빌더(`GPUSceneBuilder.cpp` 1558 주변)는 `Ready` 가 아닌 머티리얼의 배치를 만들지 않습니다. 건너뛴 배치 수를 `GT.Material.notReadyBatches` 로 셉니다. `Failed` 는 `initializePending` 이 오류를 한 번 남기고, 그 메시는 그려지지 않습니다. 이 동작은 `MeshComponent.cpp` 97 의 주석이 이미 말하는 것과 같습니다 **[코드]**.
6. `Engine.cpp` 612-617 의 `waitIdle` 블록을 지웁니다.

보이는 변화는 새 머티리얼을 쓰는 오브젝트가 파일을 읽는 시간과 패킷 링 깊이만큼(수 프레임) 늦게 나타나는 것입니다. 이 지연을 숨기는 일, 곧 등장 디더([AsyncResourceLoading](AsyncResourceLoading.md) 3절 B2)를 머티리얼 준비 시점에 맞추는 일은 열린 문제 O1 입니다.

인덱스 재사용 위험은 지금의 `waitIdle` 이유와 다릅니다. 새 등록은 새 인덱스를 받는데, 게임 스레드가 놓은 핸들은 `releaseHandle` 이 "넘긴 프레임 + 1" 뒤까지 미루므로(`IRHIDevice.h` 195-205 주석) 이미 만든 패킷이 옛 인덱스를 값으로 보관하는 문제는 막혀 있습니다 **[코드]**. RT 가 직접 해제하는 DX11 과 GL 경로에서 같은 보장이 서는지는 확인하지 못했습니다 **[확인 전]**.

`MaterialInstance` 의 `updateRHI`(`MaterialInstance.cpp` 306)와 머티리얼 텍스처가 바뀔 때 상수 버퍼를 어느 스레드가 쓰는지(`refreshTextureBindings`)도 확인하지 못했습니다 **[확인 전]**. U10 작업자가 먼저 읽고 필요하면 이 절에 적습니다.

기각한 대안은 두 가지입니다. 게임 스레드가 레지스트리 락을 잡고 직접 등록하는 안은 "락을 잘 거는 대신 애초에 공유하지 않는다"는 지금 규칙(`setParallelRecording` 주석)과 반대입니다. 패킷이 인덱스 값 대신 머티리얼 핸들을 보관하고 RT 가 인덱스로 해석하는 안은 패킷과 GPU 씬 버퍼 형식을 바꾸는 큰 일에 비해 이득이 몇 프레임의 지연뿐이라 하지 않습니다.

### 2.4 D. PSO 준비 상태

`RenderPSOCache` 의 머티리얼 변형 항목은 지금 (핸들, 소유 여부) 둘입니다. 실패는 핸들 0 으로 기록되고, 아직 만들지 않은 것은 항목이 없는 것으로 표현됩니다. "만드는 중"이 없어서 만들기가 RT 에서 동기로 끝나야 합니다. 이 상태를 데이터로 드러냅니다.

- 상태 `Absent`(항목 없음), `Building`, `Ready`, `Failed` 를 항목에 둡니다. 키는 지금과 같습니다.
- `FrameRenderer::psoForBatch` 는 `Ready` 의 핸들만 돌려줍니다. `Building` 이면 그 배치는 이번 프레임에 그리지 않고 `RT.PSO.pendingBatches` 로 셉니다. `Failed` 는 만들 때 오류를 한 번 남깁니다. 지금도 핸들 0 이면 `drawBatchGroup` 이 그리지 않습니다 **[코드: `FrameRendererDraw.cpp` 447-450]**. 새로 생기는 것은 "아직 안 됨"과 "실패"를 가르는 구분입니다.
- `RenderPSOCache.h` 의 `findMaterialPSO` 주석은 "0 이면 부르는 쪽은 패스 PSO 로 그립니다"라고 적혀 있어 G11 이후의 동작(그리지 않음)과 다릅니다 **[코드]**. U11 이 이 주석도 고칩니다.
- 요청은 셋업 구간의 `ensureMaterialPSOs` 가 모아서 `requestMaterialPSO( key, desc )` 로 넣습니다. 이 함수는 `Absent` 를 `Building` 으로 바꾸고 빌더 큐에 넣습니다.

**빌더.** 전용 스레드 하나가 요청을 순서대로 만듭니다. 우선순위는 이번 프레임 배치가 필요한 키가 먼저이고 미리 만들기(`warmPinnedMaterialPSOs`)가 나중입니다. 결과는 완료 목록에 쌓고 RT 가 셋업 구간에서 캐시에 합칩니다. 캐시를 쓰는 스레드가 RT 하나로 유지되므로 "삽입은 기록 시작 전에만"이라는 지금 불변식이 그대로 서고, `bCreatedVariant` 일 때 하는 `ensureMaterialFallbackBuffers` 후처리도 합치는 자리에서 합니다.

**GL.** GL 은 `_bThreadSafeResourceCreation` 이 거짓이라 빌더 스레드를 쓸 수 없습니다. RT 가 프레임당 시간 예산 안에서 만들고 남은 키는 `Building` 으로 둡니다. 이것은 폴백이 아니라 같은 상태 규칙을 다른 방식으로 채우는 것입니다.

**기각된 결정과의 관계.** PSO 병렬 생성은 이득 9 %(드라이버가 직렬)로 기각됐습니다(결정 기록 3절). 이 설계는 생성을 빠르게 하려는 것이 아니라 RT 의 임계 경로에서 빼려는 것이라 스레드는 하나입니다. 레인 없이 공유 풀 위에서 돌린 GT 병렬화는 RT 를 170 에서 261 µs 로 늦췄고 "레인 있는 설계로 다시 판단"하라고 적혀 있습니다(결정 기록 3절 "태스크"). 빌더는 풀 작업이 아니라 전용 스레드로 둡니다. 드라이버 컴파일은 ms 단위라 풀 워커를 오래 붙잡기 때문입니다 **[추정]**.

**`warmPinnedMaterialPSOs` 와의 관계.** 이 함수의 루프(등장 디더 둘 × 패스 × 뷰 모드)는 그대로 "우선순위가 낮은 요청"을 넣는 일이 됩니다. 별도의 생성 루프가 사라지고, 같은 상태 데이터 위에서 "고정한 머티리얼의 PSO 가 모두 `Ready`" 를 물을 수 있게 됩니다. 이를 [AsyncResourceLoading](AsyncResourceLoading.md) 의 준비 집합이 로딩 화면의 종료 조건으로 쓸 수 있습니다.

**키 공간 전망.** 키는 (씬 메시 패스 수) × (머티리얼 퍼뮤테이션 수) × (마스크에 포함된 뷰 모드 수) × (컬 반전 2가지)의 곱입니다 **[코드: `materialPSOKey`, `collectFrameViewModeMask`]**. 곱이라서 불리언 스위치 하나가 키 수를 두 배로 만듭니다. 등장 디더가 불투명 머티리얼에서 이미 그랬습니다. 뷰 모드는 현재 보이는 모드만 키가 되지만, 에디터에서 모드를 바꾸는 순간 (머티리얼 수 × 패스 수)만큼 새 키가 한꺼번에 생기므로 가장 큰 히치 후보입니다 **[추정]**. 실제 키 수와 생성 시간 분포는 M0 이 `RT.PSO.create` 로 측정합니다. 쿠킹 산출물도 같은 곱을 따르며 게임 셰이더 하나가 백엔드당 180 파일입니다(결정 기록 5-5). Shipping 에서는 셰이더가 쿠킹돼 있어 드라이버 컴파일만 남을 것입니다 **[추정]**.

**빌더의 안전성은 확인하지 못했습니다 [확인 전].** 네 백엔드의 `createPipelineState` 를 워커에서 불러도 되는지(PSO 핸들 테이블 잠금, Vulkan 파이프라인 캐시의 외부 동기화), Dev 빌드의 런타임 셰이더 컴파일(`ShaderCache::getOrCompile`)이 워커에서 안전한지 점검해야 합니다. 이 점검은 U3 ~ U6 이 자기 백엔드에서 하고, 결과를 U12 가 읽고 시작합니다.

### 2.5 열린 문제

- **O1. 등장 디더와 준비 시점.** 등장 디더는 게임 스레드가 비율을 높여 갑니다. 머티리얼이나 PSO 때문에 RT 가 배치를 건너뛰면 디더가 이미 진행된 채로 갑자기 나타납니다. 권고안은 게임 스레드가 준비 여부(`Material::GpuState`, PSO 는 퍼뮤테이션 해시 단위의 원자 조회)를 읽고 `Ready` 가 된 프레임에 `beginAppearance` 를 시작하는 것입니다. PSO 쪽 조회 API 는 U12 가 정합니다.
- **O2. 링 용량과 `Busy` 의 빈도.** 64 MiB 와 프레임당 상한은 추정입니다. M1 이 `Busy` 횟수를 보고 정합니다.

## 3. 단위

| 단위 | 내용 | 선행 | GPU |
|---|---|---|---|
| U1 | 계약, ABI 34, 블로킹 헬퍼 | 없음 | 아니오 |
| U2 | Vulkan 일회성 제출의 대기 좁히기(ABI 불변) | 없음 | 예 |
| U3 | DX11 새 계약 구현 | U1 시그니처 | 예 |
| U4 | GL 새 계약 구현 | U1 시그니처 | 예 |
| U5 | DX12 새 계약 구현 | U1 시그니처 | 예 |
| U6 | Vulkan 새 계약 구현 | U1 시그니처, U2 | 예 |
| U7 | 계약 테스트와 옛 호출 이전 | U1 시그니처 | 예 |
| U8 | 텍스처 소비자 이전 | U1 시그니처 | 예 |
| U9 | 구간 갱신과 되읽기 소비자 이전 | U1 시그니처 | 예 |
| U10 | 머티리얼 GPU 상태와 프레임 경계 초기화 | U8 | 예 |
| U11 | PSO 상태 데이터와 드로우 규칙 | 없음 | 예 |
| U12 | PSO 백그라운드 빌더 | U11, U3 ~ U6 | 예 |
| U13 | 렌더 타깃 크기 변경의 `waitIdle` 제거 | U3, U4, U8 | 예 |
| U14 | `waitIdle` 이유 인자와 게이트 | U13 | 아니오 |

**병합 번들.** U1, U3 ~ U9 는 ABI 를 바꾸므로 한 번들로 병합하고 통합이 한 번에 빌드합니다. 워커는 빌드하지 않으므로 병렬로 작업하고, 번들에 들어가기 전의 개별 브랜치는 서로 없이는 컴파일되지 않는 것이 정상입니다. U1 은 새 함수를 더하면서 옛 가상 함수 선언을 지우고, 각 백엔드 단위는 자기 폴더에서 새 함수를 구현하면서 옛 오버라이드를 지웁니다. 그래서 main 에 두 경로가 함께 남는 구간이 없습니다. U2, U10, U11, U12, U13, U14 는 각각 따로 병합할 수 있습니다. 동시에 띄울 수 있는 작업자는 여섯 안팎입니다. 1차는 U1 을 먼저 끝낸 뒤 {Vulkan(U2 → U6), DX11(U3), GL(U4), DX12(U5), 테스트(U7), PSO 상태(U11)} 입니다.

**파일 소유(겹치지 않게).** `Source/Engine/Graphics/RHI/` 아래의 폴더는 하나의 작업자만 맡습니다.

| 단위 | 소유 파일 |
|---|---|
| U1 | `IRHIResourceFactory.h`, `RHITypes.h`, `Modules/RHIModuleAbi.h`, `IRHIDevice.h`(주석), `Support/` 의 새 파일 |
| U2, U6 | `RHI/Vulkan/` 전체 |
| U3 | `RHI/DX11/` 전체 |
| U4 | `RHI/GL/` 전체 |
| U5 | `RHI/DX12/` 전체 |
| U7 | `Test/EngineTest/Graphics/TestRHIDevice.cpp`, `Test/EngineTest/Renderer/TestRenderPassGPU.cpp`, `Test/EngineTest/Graphics/TestRenderTargetRegistry.cpp` |
| U8 | `Graphics/Texture/` |
| U9 | `Renderer/Canvas/CanvasRenderer.*`, `Renderer/Frame/Capture/FrameCapture.*`, `Renderer/RenderThread.cpp`, `Graphics/Debug/RenderTargetRegistry.*`, `Editor/Common/Backend/EditorUICapture.cpp`, `Editor/Common/Workspace/EditorRenderViews.cpp`, `Editor/Panels/RenderTarget/RenderTargetPanel.cpp` |
| U10 | `Graphics/Material/`, `Engine.cpp`, `Renderer/Scene/GPUSceneBuilder.cpp`, `Renderer/Scene/GPUScene.cpp`, `Renderer/Frame/FrameRenderer.cpp` |
| U11, U12 | `Renderer/Frame/Resource/RenderPSOCache.*`, `Renderer/Frame/FrameRendererPSO.cpp`, `Renderer/Frame/FrameRendererDraw.cpp`, `Renderer/Frame/FrameRenderer.h`, 새 파일 |
| U13 | `Graphics/Texture/TextureCache.cpp`, `RHI/DX11/` 와 `RHI/GL/` 의 bindless 파일(U3 와 U4 의 작업자가 이어서) |
| U14 | 호출처 전부(기계적 변경이라 혼자) |

`FrameRenderer.cpp` 는 U10 만 고칩니다(셋업 구간의 `initializePending` 호출). U8 은 `completePendingLoads` 와 `updateStreaming` 의 구현을 `TextureCache` 안에서 바꾸고 호출 줄은 그대로 둡니다. U11 과 U12 는 `warmPinnedMaterialPSOs` 의 호출 줄(`FrameRenderer.cpp` 817)을 건드리지 않습니다.

**단위별 내용과 검증.** GPU 검증은 통합이 `gpu-verifier`(Shipping, 네 백엔드)로 한 번에 돌립니다. 항목은 구현하는 단위가 `Test/QA/VerifyBatch.json` 에 더합니다.

- **U1.** 새 함수 다섯 개와 타입 세 개(`RHITransferTicket`, `RHITransferStatus`, `RHISubmitResult`)를 더하고, 옛 가상 함수 세 개를 지우고, ABI 정수와 스탬프를 함께 높입니다(`static_assert` 가 어긋남을 잡습니다). `IRHIDevice.h` 머리말의 완료 계약 표에 두 줄을 더하고 `RHITransferUtil` 을 만듭니다. 시그니처를 확정하기 전에 `design-reviewer` 로 한 번 검토합니다. 검증은 통합 빌드와 린트입니다.
- **U2.** `endSubmitAndWait` 가 `vkQueueWaitIdle` 대신 `VkFence` 를 `vkQueueSubmit` 에 달고, 큐 락은 제출하는 동안만 잡고, 대기는 락 밖의 `vkWaitForFences` 로 합니다. 일회성 풀 락(`_oneShotMutex`)은 커맨드 버퍼 기록에도 필요하다고 이해합니다(Vulkan 사양의 외부 동기화 규칙, 사양 확인 전). 그러므로 `vkEndCommandBuffer` 와 제출이 끝나면 풀 락을 놓고, 대기한 뒤 해제할 때 다시 잡습니다. 펜스는 장치가 풀로 재사용하고 U6 이 그대로 씁니다. 호출 스레드는 자기 업로드가 끝날 때까지 여전히 기다리므로 이것은 비동기가 아니라 대기 범위를 "큐 전체와 락 점유"에서 "내 제출"로 줄이는 변경입니다. 검증은 Vulkan GPU(공유)입니다. `RHIDeviceTest.TextureReadbackMatchesUpload` 와 업로드 테스트, 시나리오 `game/empty/automation/light2dcookie.scenario.xml`(구간 갱신), `ditherappear.scenario.xml` 입니다. M1 이 Release 독점으로 `RT.Upload.waitIdle` 합계와 RT 프레임 p99 를 전후로 비교합니다.
- **U3 ~ U6.** 위 백엔드 표의 구현과, 각 백엔드의 `createPipelineState` 가 워커에서 안전한지 점검한 결과를 이 문서 2.4 에 적는 일입니다. 각 단위는 자기 백엔드의 `pollTransfer` 가 `Pending` → `Complete` 로 가는 것과 순서 규칙을 U7 의 테스트가 통과하게 합니다.
- **U7.** `RHIDeviceTest` 에 계약 테스트를 더합니다: 제출 → `Pending` 또는 `Complete` → 폴링, 구간 갱신 뒤 업로드된 내용이 보임(순서), `Busy` 경로, 장치 손실 뒤 `Failed`(Vulkan 은 `VulkanLostDeviceStopsWaitingOnTheGPU` 와 같은 모양). 옛 함수를 부르는 테스트 40곳 남짓을 블로킹 헬퍼로 바꿉니다. GPU 네 백엔드입니다.
- **U8.** 텍스처 로드를 세 단계로 나눕니다: 워커 읽기 → RT 펌프의 `beginUpload`(텍스처 생성과 `submitTextureUpload`) → 이후 프레임의 `finishUpload`(`pollTransfer` 가 `Complete` 면 bindless 등록과 발행). `Busy` 는 다음 프레임에 다시 제출합니다. 프레임당 상한은 제출 수에 적용합니다. `updateStreaming` 의 위쪽 밉 교체도 같은 세 단계입니다. 동기 `acquire` 와 `wait()` 는 블로킹 헬퍼로 `beginUpload → 대기 → finishUpload` 를 합니다. 검증은 `RenderPassGPUTest.AsyncTextureStreamsTopMipsAfterBase`, `StreamingUploadsTheTextureNotedAsLargestOnScreenFirst`, `AssetStreamingTest.*` 와 텍스처가 있는 시나리오(`kaykitcharacters`) 네 백엔드입니다. 스크린샷으로 회색 텍스처가 진짜 텍스처로 바뀌는지 봅니다.
- **U9.** 글리프 아틀라스(`CanvasRenderer.cpp` 418, 443)는 새 함수로 바꾸고, `Busy` 면 더티 목록을 지우지 않고 다음 프레임에 다시 제출합니다. `RenderThread::readRequestedRenderTarget` 는 요청 프레임에 읽지 않고 티켓을 레지스트리에 두었다가 이후 프레임에 `takeTextureReadback` 합니다. 스크린샷과 에디터 캡처는 블로킹 헬퍼를 씁니다. 검증은 `engine/automation/uidemo.scenario.xml` 네 백엔드(글자), `-gv_screenshot` 이 쓰는 캡처 경로, `RenderTargetRegistry` 읽기 테스트, 에디터 `engine/automation/editor/uiaudit.scenario.xml` 입니다.
- **U10.** 2.3 의 여섯 항목입니다. 검증은 `material-cache-gpu-test`(매니페스트 id), 새 시나리오 `game/empty/automation/materialfirstuse.scenario.xml`(처음 보는 머티리얼의 오브젝트를 스폰하고 프레임마다 캡처해 몇 프레임 안에 나타나는지, Error 로그가 없는지 봄), 그리고 M1 의 `GT.Material.waitIdle` 호출 수 0 입니다.
- **U11.** 2.4 의 상태 데이터, `psoForBatch` 의 구분, `RT.PSO.pendingBatches` 카운터, `findMaterialPSO` 주석 수정입니다. 이 단위에서는 생성이 여전히 RT 에서 동기로 끝나므로 항목이 `Absent` → `Ready` 로 한 프레임 안에 가서 화면은 지금과 같아야 합니다. `RenderPSOCache` 상태 전이는 GPU 없이 테스트합니다(nogpu). GPU 는 기존 골든이 그대로인지만 봅니다.
- **U12.** 빌더 스레드, 우선순위, 완료 목록 합치기, GL 의 시간 예산, `-gv_psoAsync`(0 이면 U11 의 동기 생성, 상태 규칙은 같다). 이 스위치는 A/B 측정용이며 `gv_gpuUploadQueue` 와 같은 선례입니다. 검증은 새 시나리오 `game/empty/automation/psoasync.scenario.xml` 네 백엔드입니다. 뷰 모드를 번갈아 바꾸고 새 머티리얼을 스폰한 뒤, 몇 프레임 안에 동기 생성(`-gv_psoAsync=0`)과 같은 이미지에 이르는지, Error 로그가 없는지 봅니다.
- **U13.** `TextureCache::resizeRenderTarget` 의 `waitIdle`(W6)을 지웁니다. 옛 텍스처를 지연 해제로 넘기고 새 핸들과 인덱스를 받습니다. 전제는 DX11 과 GL 의 bindless 인덱스 반환이 GPU 완료 뒤로 미뤄지는 것입니다. DX12 는 이미 `deferFreeBindlessIndex` 로 그렇고, DX11 은 `releaseFreeListIndex` 로 즉시 돌려줍니다 **[코드]**. 핫 리로드의 `waitIdle` 은 도구 경로라 남깁니다. 검증은 에디터에서 캔버스 타깃 크기를 바꾸는 시나리오(`engine/automation/editor` 의 기존 시나리오 가운데 크기를 바꾸는 것이 있는지 확인해야 함 **[확인 전]**)와 `engine/automation/hotreload.scenario.xml` 입니다.
- **U14.** `IRHIDevice::waitIdle` 에 이유 열거형(`Shutdown`, `BackendSwap`, `Resize`, `HotReload`, `SceneActivation`, `Capture`, `Tool`)을 필수 인자로 더합니다. 호출처는 W7 의 20곳 안팎이고 U13 뒤에 남은 것입니다. 이유 열거형으로 이름을 가르면 프레임 경로에 새로 생기는 호출이 눈에 띄고, 린트 게이트가 프레임 경로 폴더(`Renderer/Frame/`, `Graphics/Material/`, `Graphics/Texture/` 의 비 reload 함수)에서 이유 `Tool` 이 아닌 호출을 막을 수 있습니다. 게이트는 열거형으로 막지 못하는 틈(함수 단위 허용)에만 둡니다. 기계적 변경이라 혼자 하고 커밋만 단계별로 나눕니다.

**측정.** M0 은 착수 전입니다. `wt/profscopes` 병합 뒤 Release 에서 혼자(GPU 독점) 네 스코프의 호출 수와 ms 분포, RT 와 GT 프레임의 p99 와 최악 프레임을 측정합니다. 시나리오는 게임 시작, 씬 로드, 새 머티리얼 스폰, 뷰 모드 전환, 글자가 많은 UI 변경이고 설정은 LowEnd 를 포함합니다. M0 에서 W2 의 합이 작거나 Vulkan 이 아닌 백엔드에서 W1 과 W3 이 작으면 U10 과 U12 의 우선순위를 낮춥니다. M1 은 U2 병합 뒤와 마지막 단위 뒤에 같은 시나리오로 다시 측정합니다. 결과는 [검증과 측정](../08_Verification.md) 에 적습니다.

## 4. 규칙

아래 여섯은 이 계획이 끝난 뒤에도 남는 규칙입니다. 단위가 끝나면 정착 위치로 옮기고 이 절에서 지웁니다.

1. 게임 스레드가 부르는 새 API 는 GPU, 디스크, 네트워크 완료를 기다리지 않습니다. 형태는 `request` → 핸들 → 폴링입니다. 동기 변형은 이름에 드러냅니다(`AndWait` 접미사). 기존 `acquire` 류는 이름을 그대로 두고 손댈 때 고칩니다. 정착 위치는 `docs/agents/CppStyle.md` 의 스레드 항목입니다.
2. RHI 의 업로드와 되읽기는 "제출 → 티켓 또는 완료 콜백"만 새로 만듭니다. 새 동기 가상 함수는 만들지 않습니다. 옛 동기 함수를 지우는 것(U1)이 이 규칙의 구조적 강제이고, `IRHIResourceFactory.h` 머리말에 한 줄을 적습니다.
3. 프레임 경로에서 `waitIdle` 을 새로 만들지 않습니다. 허용은 수명 이벤트와 도구뿐입니다. U14 의 이유 열거형과 게이트가 강제합니다.
4. 늦게 도착한 자원은 "준비 전엔 그리지 않음"으로 둡니다. 폴백은 두지 않고 실패는 오류로 남깁니다. 상태 열거형(`GpuState`, PSO 상태)과 건너뛴 수 카운터가 이 규칙의 형식입니다.
5. 쓰기 I/O 는 `ThreadedLocalStore`(`Source/GameFramework/Base/Online/Local/ThreadedLocalStore.h`)를 거칩니다. 게임 스레드에서 동기로 파일에 쓰지 않습니다. 형식으로 막지 못하므로 리뷰가 맡고, 기존 동기 쓰기 호출처의 감사는 이 문서 밖의 후보입니다.
6. RT 에서 동기 생성이 생기면 `RT.*` 스코프로 감쌉니다. 스코프가 없는 동기 생성은 프로파일에 보이지 않아 다음 히치의 원인이 됩니다. 정착 위치는 `docs/agents/CppStyle.md` 입니다.

## 5. 위험과 되돌리기

| 위험 | 영향 | 완화 |
|---|---|---|
| ABI 34 를 한 번에 맞춰야 한다 | 한 백엔드가 늦으면 번들 전체가 대기 | U2 를 먼저 병합해 Vulkan 이득을 먼저 얻는다. DX11 과 GL 구현은 작다. 번들은 U6 없이 병합하지 않는다 |
| 시그니처를 구현 뒤에 바꾸면 ABI 를 다시 높여 모든 백엔드를 재빌드 | 되돌리기 어려운 결정 | U1 병합 전에 `design-reviewer` 로 검토하고, DX12 와 Vulkan 작업자가 시그니처로 구현 가능한지 먼저 읽는다 |
| GL 의 `pollTransfer` 와 블로킹 헬퍼가 컨텍스트 소유 스레드(RT)의 진행에 기대는 점 | 비 RT 스레드의 대기가 RT 가 오래 컨텍스트를 점유하면 시간 초과 | 컨텍스트를 빌려 묻는다. 블로킹 헬퍼의 비 RT 사용은 RT 가 도는 동안에만 허용 **[추정]** |
| Vulkan 의 완료 순서가 제출 순서와 다를 수 있다 | 워터마크가 앞서 올라가면 미완 자원을 발행 | 연속 구간만 워터마크로 올린다. 테스트(U7)가 지연된 앞 제출 위에서 확인 |
| 스테이징 메모리 | 큰 텍스처가 몰리면 `Busy` 가 길어지거나 메모리가 튄다 | 링 용량 상수와 단독 버퍼 총량 상한, 장부 표시, M1 이 조정 |
| DX12 `_uploadSlotMutex` 범위 | 새 제출이 RT 프레임 제출을 막을 수 있음 | U5 가 락 범위를 줄이고 텍스처 상태 추적과의 겹침을 점검 |
| `Material` GPU 필드의 스레드 간 읽기 | 게임 스레드가 쓰는 중인 인덱스를 읽는 경쟁 | `GpuState` 를 acquire 로 읽은 뒤에만 읽는 규칙, Debug 에서 단언 |
| PSO 빌더의 스레드 안전성 | 백엔드나 셰이더 컴파일러의 잠금 누락 | U3 ~ U6 의 점검이 끝나기 전 U12 를 시작하지 않는다. `-gv_psoAsync=0` 으로 동기 생성과 비교 |
| 보이는 변화(수 프레임 늦은 등장, 뷰 모드 전환 직후 빈 배치) | 사용자가 결함으로 느낄 수 있음 | 건너뛴 수 카운터로 드러내고 O1 로 디더를 맞춘다 |
| Vulkan 이 아닌 백엔드에서는 W2 가 이미 대기가 없다 | 이득이 Vulkan 에 몰림 | M0 이 먼저 측정한다. 이득이 작으면 U10 과 U12 를 미룬다 |

**되돌리기.** U2, U10, U11, U12, U13, U14 는 각각 커밋 단위로 revert 합니다. ABI 번들은 병합 커밋 하나를 revert 합니다(`git revert -m 1`). 옛 함수와 새 함수가 한동안 공존하는 단계는 만들지 않으므로 중간 상태로 되돌리는 방법은 없습니다.

**한 백엔드씩 옮기는 단계.** ABI 는 한 번에 높이지만, 새 계약이 약속하는 성질(제출이 GPU 를 기다리지 않음)은 백엔드마다 따로 채워집니다. 순서는 다음과 같습니다.

1. U2: Vulkan 단독, ABI 불변. 대기 범위가 큐 전체에서 내 제출로 줄고 큐 락을 놓습니다.
2. 번들: DX11, GL, DX12 는 원래 GPU 를 기다리지 않아 새 계약의 구현이 "같은 호출 + 완료 확인"입니다. Vulkan 은 U6 로 비동기가 됩니다.
3. U10 → U11 → U12: 소비자 쪽 대기를 하나씩 지웁니다.

## 6. 상용 엔진 비교

언리얼의 RHI 는 업로드를 `FRHICommandList` 에 기록하는 명령으로 두고, 텍스처 스트리밍은 워커에서 텍스처를 만들어(`RHIAsyncCreateTexture2D`) `FRHIGPUFence` 를 폴링하며, 되읽기는 `FRHIGPUTextureReadback` 이 준비 여부(`IsReady`)를 폴링하는 것으로 알고 있습니다. 유니티는 `AsyncGPUReadback.Request` 가 요청 객체를 돌려주고 `done` 을 폴링하며, 텍스처 업로드는 품질 설정의 Async Upload Buffer Size 와 Time Slice 로 링 버퍼를 시간 예산 안에서 쓰는 파이프라인으로 알고 있습니다. Godot 의 `RenderingDevice` 는 `texture_update` 가 스테이징 버퍼 블록에 복사해 프레임 안의 전송 커맨드로 보내고, 되읽기는 비동기 변형(`texture_get_data_async`)이 콜백으로 돌려주는 것으로 알고 있습니다. 모두 "제출 → 펜스나 폴링 → 사용"이라 이 설계와 같은 방향입니다.

다른 점은 둘입니다. 우리는 콜백 대신 티켓 폴링을 쓰는데, 소비자(bindless 등록)가 RT 셋업 구간에서만 일해야 하기 때문입니다. 그리고 1단계에서 전용 전송 큐를 쓰지 않습니다. 이 단락은 모두 기억에 기댄 것이며 API 이름의 현재 가용 범위(플랫폼, 버전)와 전용 복사 큐의 사용 여부, 스테이징 블록의 크기는 **[확인 전]** 입니다. 결정 근거로는 쓰지 않았고, 근거는 1절의 코드 읽기입니다.

## 7. 정한 것과 확인하지 못한 것

정한 것(사용자 결정이 필요한 항목은 없습니다):

- ABI 34 에서 옛 동기 함수 세 개를 지우고 shim 을 두지 않는다. 동기 변형은 ABI 밖에 있는, 이름에 `AndWait` 가 드러나는 헬퍼다.
- 완료는 콜백이 아니라 티켓 폴링이다. 해제용 `enqueueGPURelease` 는 그대로 둔다.
- 1단계는 그래픽스 큐 하나다. 전용 전송 큐는 측정 뒤에 다시 판단한다.
- 새 머티리얼의 GPU 초기화는 RT 셋업 구간에서 하고, 준비 전에는 그리지 않는다.
- PSO 는 네 가지 상태를 데이터로 두고 생성 스레드는 하나이며 병렬 생성은 하지 않는다.

확인하지 못한 것:

- `wt/profscopes` 스코프가 감싸는 줄. 이 문서의 W1 ~ W4 와 스코프의 대응은 내 읽기입니다.
- 네 백엔드와 셰이더 컴파일러의 워커 안전성(2.4).
- DX11 과 GL 에서 RT 가 직접 인덱스를 해제하는 경로의 지연 보장(2.3).
- `MaterialInstance::updateRHI` 와 텍스처 교체 때 상수 버퍼를 쓰는 스레드(2.3).
- 타임라인 세마포어의 Vulkan 버전 요구(2.2).
- 링 용량 64 MiB 와 프레임당 상한의 적정값. 모두 추정이며 M1 이 정합니다.
- 상용 엔진의 API 세부(6절).
