# RHI 비동기 업로드 · 머티리얼 초기화 · PSO 준비 상태

> 상태: 구현중 · 선행: 없음 · 영역: graphics · 옛번호: 없음 · 진행: 14/15

2026-10-28: 기능별 테스트 · 시나리오(`Test/` · `Resource/**/automation/`)를 새 프레임워크를 만들려고 전부 지웠다. 이 문서의 테스트 · 시나리오 · `ctest` · `AppTestUtil` 언급은 옛 구조를 가리키며, 새 틀이 정해지면 다시 쓴다.

> 질문: 게임 스레드와 렌더 스레드가 GPU 완료를 기다리는 지점을 어떤 계약으로 없애고, 무엇부터 바꾸는가?
> 독자: RHI 백엔드와 렌더러를 고치는 작업자, 통합 담당.
> 결론: 업로드와 되읽기는 "제출 → 티켓 → 폴링" 계약 하나로 바꾸고(RHI ABI 버전을 한 번 높여 옛 동기 함수 세 개를 지운다), 새 머티리얼과 PSO 는 "준비 전 / 준비됨 / 실패" 상태를 데이터로 두어 준비 전에는 그리지 않는다. 프레임 경로에는 `waitIdle` 을 새로 만들지 않는다. ABI 변경이 없는 Vulkan 대기 좁히기(U2)는 B0 에 들어갔다.

2026-10-25 코드 읽기로 정리했습니다. 근거는 **[코드]**(읽어서 확인), **[추정]**, **[확인 전]** 으로 표시합니다. 프로파일 스코프 `GT.Material.waitIdle`, `RT.Upload.waitIdle`, `RT.PSO.create`, `GT.Load.sync` 는 `wt/profscopes` 브랜치가 붙이는 중이라 모든 측정(M0, M1)은 그 스코프로 합니다(스코프가 어느 줄을 감싸는지는 확인하지 못했습니다).
관련 문서: [PreloadAndLod](PreloadAndLod.md)(PSO 미리 만들기와 텍스처 밉 스트리밍). 이 문서는 그 위에서 GPU 쪽 대기만 다룹니다.

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

W1 ~ W3 은 프레임 경로이고 이 계획의 대상입니다. W6 은 기존 프레임 경로 `waitIdle` 이라 U13 이 지웁니다. W4 는 호출 시점이 호출처마다 달라 `GT.Load.sync` 로 측정합니다. W5 는 GPU 가 아니라 워커를 기다리고 이미 스코프가 있어 대상이 아닙니다.
W7 은 남는 허용 목록입니다 — 씬 활성화, 종료, 창 크기 · 사용자 설정 변경, 렌더 스레드 모드 전환, 셰이더 실시간 컴파일 · 모듈 핫 리로드, 백엔드 교체, 에셋 핫 리로드(`TextureCache::reload` · `MaterialCache::reload` · `MeshCache.cpp` 220), 캡처 도구(`PortraitRenderer` · `PortraitBatchUtil` · `ReflectionProbeBaker`). 정확한 위치는 `grep waitIdle` 로 센다.

호출 빈도의 코드 근거:
- W1 의 조건은 원자 카운터 `MaterialCache::hasPendingInitialize` 하나이고 `requestInitialize`(`MeshComponent.cpp` 104, `EnvironmentUtil.cpp` 65, `SpriteInstanceBatch.cpp` 54)와 `pinAsset` 이 올립니다. 안정 상태에서는 0 번이고 씬 로드와 처음 쓰는 머티리얼의 스폰 프레임에 몰립니다 **[코드]**. 비용은 한 프레임이 아닙니다 — `RenderThread::waitIdle` 은 RT 큐가 빌 때까지 기다린 뒤 장치 대기를 해 파이프라인의 프레임을 전부 끝내야 돌아옵니다(`RenderThread.cpp` 289-303). 렌더 스레드를 쓰지 않으면 바로 돌아옵니다 **[코드]**.
- W2 에서 RT 가 한 프레임에 부르는 업로드는 `completePendingLoads` 8개 이하(`kMaxUploadsPerFrame`), `updateStreaming` 2개 이하, 구간 갱신(`flushRegionUpdates`), 글리프 아틀라스 페이지(`CanvasRenderer::uploadAtlasPage`)입니다. Vulkan 은 호출마다 큐를 비웠고, 큐 락 `_queueMutex` 를 잡은 채 `vkQueueWaitIdle` 을 불러 RT 의 `endFrame` 제출(`VulkanRHIDeviceSubmission.cpp` 481)을 막았으며 기다리는 대상도 자기 업로드가 아니라 큐의 이전 프레임 전체였습니다 **[코드]**. DX11 · DX12 · GL 의 같은 함수는 GPU 를 기다리지 않습니다(DX11 `UpdateSubresource`, DX12 프레임 리스트 앞 복사 리스트, GL `glTexSubImage2D`).
- W3 의 키는 `RenderPSOCache::materialPSOKey` 가 (패스 PSO, 퍼뮤테이션 해시, 뷰 모드, 컬 반전)으로 만들고 캐시에 있으면 만들지 않아(`ensureMaterialPSOVariant`) 안정 상태에서는 0 번입니다 **[코드]**.

이 계획이 고치지 않는 것: `RT.BeginFrame` 의 GPU 백프레셔 꼬리는 대기 가능 스왑체인으로 고칠 히치가 없다고 이미 기각했습니다. 비슷한 선례로 메시를 미리 읽어 보관하게 한 변경이 StarSkirmish LowEnd 최악 프레임을 25.8 → 4.1 ms 로 줄였습니다. 이 계획의 기대도 "새 머티리얼 · 업로드가 있는 프레임의 추가 히치 제거"로 한정합니다.

## 2. 설계

### 2.1 공통 원칙

- 기다리는 쪽에 기다릴 대상을 주지 않고 대상이 "준비 전 / 준비됨 / 실패" 중 어디인지를 데이터로 주며, 준비 전이면 그리지 않습니다. 폴백은 없고 실패는 오류로 남습니다. 상태는 만든 쪽이 release 로 쓰고 읽는 쪽이 acquire 로 읽으며 `Ready` 를 본 뒤에야 GPU 필드를 읽습니다.
- GPU 자원을 다른 스레드가 볼 수 있게 바꾸는 지점(bindless 등록, PSO 캐시 삽입)은 RT 프레임 셋업 구간 한 곳입니다: `FrameRenderer::executePacket` 시작부터 `RenderGraph::executeParallel` 이 `setParallelRecording( true )` 를 부르기 전까지(`FrameRenderer.cpp` 648-820, `RenderGraph.cpp` 501). RT 한 스레드가 직렬로 도는 구간이고 이전 프레임의 병렬 기록은 끝나 있어 병렬 기록 중 bindless 테이블을 바꾸지 않는 규칙(`assertRegistryMutableNow`)을 지키는 가장 단순한 위치입니다 **[코드]**.
- 셋업 구간 작업 순서: (1) 끝난 전송을 `pollTransfer` 로 확인하고 bindless 에 등록해 발행, (2) 머티리얼 GPU 초기화, (3) 새 전송 제출, (4) PSO 상태 갱신.

### 2.2 C. RHI 업로드와 되읽기: 제출 → 티켓 → 폴링

`IRHIResourceFactory` 의 동기 함수 세 개(`uploadTexture2D`, `uploadTexture2DRegion`, `readbackTexture2D`)를 지우고 아래 여섯 개로 바꿉니다(ABI 34 유지, B0 안에서 design-reviewer 검토 반영).

| 함수 | 뜻 | 대체하는 것 |
|---|---|---|
| `submitTextureUpload( texture, desc )` | 새 텍스처의 밉 체인 전체를 제출한다 | `uploadTexture2D` |
| `submitTextureRegionUpload( texture, desc )` | 쓰고 있는 텍스처의 사각형 구간을 제출한다 | `uploadTexture2DRegion` |
| `submitTextureReadback( texture, mip, slice, outLayout )` | 읽기를 요청한다. 행 배치는 바로 채워 준다 | `readbackTexture2D` |
| `pollTransfer( ticket )` | `Pending`, `Complete`, `Failed` 를 값싸게 돌려준다 | 없음 |
| `takeTextureReadback( ticket, outBytes )` | 완료된 읽기 결과를 한 번 가져간다 | 없음 |
| `discardTextureReadback( ticket )` | 가져가지 않을 읽기 결과를 버린다(`Pending` 이어도 됨) | 없음 |

제출 함수는 `[[nodiscard]] RHITransferSubmit` 을 돌려줍니다: `RHISubmitResult`(`Accepted`, `Busy`, `Rejected`)와 `RHITransferTicket`(단조 증가 64비트 시리얼 + 장치 세대, 시리얼 0 은 무효). 값을 직접 짓는 코드는 형식으로 못 막아 백엔드가 막습니다 — 시리얼 0 · 다른 장치 세대 · 발급한 적 없는 시리얼은 `pollTransfer` 가 오류를 남기고 `Failed`(`RHITransferSerials::classify`). 장치 세대(`allocateRHIDeviceGeneration`)는 백엔드 인스턴스마다 하나라 백엔드 교체 뒤의 옛 티켓이 새 장치에서 `Complete` 로 보이지 않습니다.

**계약**(10항)의 정본은 `IRHIResourceFactory.h` 전송 절 머리 주석(1 ~ 7항)과 `pollTransfer` · `registerBindless*` 의 doc 주석(8 ~ 10항)입니다. 설계 판단만 남깁니다.

- 소비자는 `Complete` 를 본 뒤에야 텍스처를 bindless 에 등록합니다. 주석이 아니라 코드가 막습니다(백엔드 레코드의 마지막 전송 시리얼이 워터마크 아래가 아니면 오류와 `kInvalidDescriptorIndex`).
- 렌더 타깃처럼 프레임 커맨드가 상태를 바꾸는 텍스처의 전송은 RT 기록 밖에서만 제출합니다(계약 3). GL 의 `pollTransfer` 는 컨텍스트를 기다리지 않습니다(계약 4).

**동기 변형은 ABI 밖의 헬퍼입니다.** `RHITransferUtil::uploadTexture2DAndWait` · `uploadTexture2DRegionAndWait` · `readbackTexture2DAndWait`(Engine, vtable 에 없음)가 제출 뒤 `pollTransfer` 를 되풀이하며 10 초 상한을 넘으면 `TimedOut` 오류입니다. 읽기 결과는 `PendingReadback` 이 소멸 때 버립니다. 사용처는 테스트 · 도구 · 캡처 · 동기 `acquire` 이고 RT 프레임 셋업 구간은 `ScopedForbidBlockingTransfer` 로 표시해 Debug 에서 단언합니다(표시는 U10 의 몫). 백엔드에 두 번째 경로가 생기지 않는 것이 핵심입니다.

**콜백이 아니라 폴링인 이유.** bindless 등록은 RT 셋업 구간에서만 가능해 콜백으로 받아도 RT 큐에 넣고 다시 읽게 됩니다. 버퍼 되읽기는 반대로 프레임 완료 콜백 안에서 한 번 복사하면 끝나 콜백입니다(`createReadbackBuffer` + `readReadbackBuffer`). 해제용 `enqueueGPURelease` 는 그대로입니다.

**ABI 결정.** `kRHIModuleAbiVersion` 33 → 34, 옛 가상 함수 세 개를 지웁니다(백엔드 DLL 이 같은 vtable 을 봐야 하므로 한 번에). 옛 함수 위에 새 함수를 얹는 shim 은 경로가 둘 남아 폴백 금지와 부딪혀 기각합니다.

**스테이징 링.** 장치마다 고정 용량 영속 매핑 링(`gv_rhiStagingRingMegabytes`, 시작 64 MiB 는 추정이며 M1 이 조정, 0 이하면 초기화 거절)을 두고 구간은 티켓 시리얼로 표시해 워터마크가 지나면 회수합니다. 장부는 `RHIStagingAllocator`(`RHI/Support`)이고 규칙은 `RHIStagingRingTest`(GPU 없음)가 봅니다. 링보다 큰 요청은 링이 비었을 때 일회성 단독 버퍼로 혼자 받고(`gv_rhiStagingSingleMaxMegabytes` 초과는 `Rejected`), 지금 차서 못 받는 요청은 `Busy` 입니다.

**전송 큐.** 1단계는 그래픽스 큐 하나입니다. 전용 복사 큐는 소유권 이전 · 레이아웃 전이가 필요하고 이득이 큰 텍스처 스트리밍 중 프레임 시간뿐이라 측정 없이 하지 않습니다. 소비자 규칙을 `Complete` 로 두는 것은 전용 큐를 도입할 때 규칙을 바꾸지 않으려는 것입니다.

**백엔드별 구현.**

| 백엔드 | 지금 | 새 계약 구현 | 난이도 |
|---|---|---|---|
| DX11 | `UpdateSubresource`(즉시 컨텍스트, 잠금) | `gv_rhiDirectUploadMaxKilobytes`(256 KiB) 이하는 같은 호출, 큰 것은 `STAGING` 텍스처(잠금 밖 소스 복사)를 잠금 안에서 `CopySubresourceRegion`. 티켓마다 이벤트 쿼리(풀)를 `GetData` 로 폴링. 링은 없다(D3D11 에는 버퍼 → 텍스처 복사가 없음) | 낮음(측정 전, 확인 `rhi-transfer-dx11-staged-upload`) |
| GL | 영속 매핑 PBO 링(GL 4.4)에 컨텍스트 밖에서 복사 후 PBO 오프셋으로 `glTexSubImage*` | `glFenceSync` + `glFlush`, `pollTransfer` 는 컨텍스트를 기다리지 않고 원자 워터마크를 읽고(컨텍스트를 쥐었거나 비어 있을 때만 `glClientWaitSync( 0 )`). 4.4 없으면 업로드 `Rejected` | 끝남(GL-1). 되읽기는 PBO 되읽기 링(`gv_rhiReadbackRingMegabytes`, 확인 `rhi-transfer-gl-readback-ring`). 비 RT 제출은 프레임 경로 밖에서만 |
| DX12 | 프레임 슬롯 복사 리스트가 프레임 리스트 앞에 끼는 구조 | 전용 업로드 링 + 전송 작업(`D3D12TransferJob`) + 전용 `ID3D12Fence`, 제출은 즉시 `ExecuteCommandLists` + `Signal` | 끝남(U5). 되읽기는 링 없이 읽기마다 읽기 힙 버퍼 |
| Vulkan | 업로드 · 되읽기 링 둘 + 전송 작업(`VulkanTransferJob`: 커맨드 버퍼 + 펜스) | 제출은 기다리지 않고 `vkGetFenceStatus` 로 앞에서부터 걷어 워터마크를 올린다 | 끝남(U6). 프레임 밖 구조 버퍼 갱신과 GPU 시계 읽기는 아직 `VulkanOneShotCommands` 로 기다린다 |

DX12 의 인덱스 반환 지연(`deferFreeBindlessIndex`)과 Vulkan 의 해제 큐는 이미 GPU 완료를 기준으로 합니다 **[코드]**. Vulkan 타임라인 세마포어 버전 요구는 확인하지 않아 펜스를 씁니다 **[확인 전]**.

### 2.3 A. 새 머티리얼 첫 사용을 프레임 경계로

지금 흐름: 컴포넌트가 머티리얼 경로를 잡으면 `requestInitialize` 가 카운터를 올리고, 게임 스레드의 `submitRenderPacket` 이 카운터를 보고 RT 와 GPU 를 비운 뒤(W1) `initializePending` 으로 파일을 읽고 상수 버퍼를 만들고 bindless 에 등록하고 인덱스를 패킷에 싣습니다.

바꾸는 흐름:
1. `requestInitialize` 는 그대로(컴포넌트는 디바이스를 모름).
2. 파일 읽기와 파싱(`Material::loadFromFile`)은 워커 단계로(텍스처 로드의 두 단계 구조와 같음).
3. GPU 단계(상수 버퍼 생성과 첫 쓰기, `registerBindlessBufferSrv`, 텍스처 SRV 해석)는 RT 셋업 구간에서 `MaterialCache::initializePending` 이 하며 프레임당 개수 상한은 M1 이 정합니다.
4. `Material` 에 원자 상태 `GpuState`(`NotRequested`, `Pending`, `Ready`, `Failed`)를 둡니다(`Pending` 은 `requestInitialize`, `Ready` · `Failed` 는 RT 가 쓰고 게임 스레드가 읽음). 반쯤 초기화된 상태를 못 구분하는 `isRHIValid()` 를 대체합니다.
5. 패킷 빌더(`GPUSceneBuilder.cpp` 1558 주변)는 `Ready` 가 아닌 머티리얼의 배치를 만들지 않고 건너뛴 수를 `GT.Material.notReadyBatches` 로 셉니다. `Failed` 는 `initializePending` 이 오류를 한 번 남기고 그 메시는 그려지지 않습니다(`MeshComponent.cpp` 97 주석과 같은 동작 **[코드]**).
6. `Engine.cpp` 612-617 의 `waitIdle` 블록을 지웁니다.

보이는 변화는 새 머티리얼을 쓰는 오브젝트가 파일을 읽는 시간과 패킷 링 깊이만큼(수 프레임) 늦게 나타나는 것이며, 이를 숨기는 등장 디더를 준비 시점에 맞추는 일은 O1 입니다.


인덱스 재사용: 새 등록은 새 인덱스를 받고 게임 스레드가 놓은 핸들은 `releaseHandle` 이 "넘긴 프레임 + 1" 뒤까지 미루므로(`IRHIDevice.h` 195-205 주석) 이미 만든 패킷이 옛 인덱스를 값으로 보관하는 문제는 막혀 있습니다 **[코드]**. RT 가 직접 해제하는 DX11 · GL 경로에서 같은 보장이 서는지는 확인하지 못했습니다 **[확인 전]**. `MaterialInstance::updateRHI`(`MaterialInstance.cpp` 306)와 `refreshTextureBindings` 가 상수 버퍼를 어느 스레드에서 쓰는지도 확인하지 못했습니다 **[확인 전]**(U10 작업자가 먼저 읽고 이 절에 적는다).

기각: 게임 스레드가 레지스트리 락을 잡고 직접 등록(공유하지 않는다는 규칙과 반대), 패킷이 인덱스 대신 머티리얼 핸들을 보관하고 RT 가 해석(형식 변경이 큰 데 비해 이득은 몇 프레임 지연뿐).

### 2.4 D. PSO 준비 상태

`RenderPSOCache` 의 머티리얼 변형 항목은 지금 (핸들, 소유 여부)이고 실패는 핸들 0, 미생성은 항목 없음으로 표현해 "만드는 중"이 없습니다. 이 상태를 데이터로 드러냅니다.

- 상태 `Absent`, `Building`, `Ready`, `Failed` 를 항목에 둡니다(키는 그대로). `FrameRenderer::psoForBatch` 는 `Ready` 의 핸들만 돌려주고 `Building` 이면 그 배치를 이번 프레임에 그리지 않으며 `RT.PSO.pendingBatches` 로 셉니다. `Failed` 는 만들 때 오류를 한 번 남깁니다(지금도 핸들 0 이면 `drawBatchGroup` 이 그리지 않음 **[코드: `FrameRendererDraw.cpp` 447-450]** — 새로 생기는 것은 "아직 안 됨"과 "실패"의 구분).
- `RenderPSOCache.h` 의 `findMaterialPSO` 주석("0 이면 패스 PSO 로 그립니다")은 G11 이후 동작과 다르므로 U11 이 고칩니다.
- 요청은 셋업 구간의 `ensureMaterialPSOs` 가 모아 `requestMaterialPSO( key, desc )` 로 넣고, 이 함수가 `Absent` 를 `Building` 으로 바꿔 빌더 큐에 넣습니다.

**빌더.** 전용 스레드 하나가 요청을 순서대로 만듭니다(이번 프레임 배치가 필요한 키가 먼저, 미리 만들기 `warmPinnedMaterialPSOs` 가 나중). 결과는 완료 목록에 쌓고 RT 가 셋업 구간에서 캐시에 합치므로 캐시를 쓰는 스레드가 RT 하나로 유지되어 "삽입은 기록 시작 전에만"이라는 불변식이 서고 `bCreatedVariant` 후처리(`ensureMaterialFallbackBuffers`)도 합치는 자리에서 합니다.
**GL.** `_bThreadSafeResourceCreation` 이 거짓이라 빌더 스레드를 쓸 수 없어 RT 가 프레임당 시간 예산 안에서 만들고 남은 키는 `Building` 으로 둡니다(폴백이 아니라 같은 상태 규칙을 다른 방식으로 채우는 것).
**기각된 결정과의 관계.** PSO 병렬 생성은 이득 9 %(드라이버가 직렬)로 기각됐습니다. 이 설계는 빠르게 만들려는 것이 아니라 RT 임계 경로에서 빼려는 것이라 스레드는 하나입니다. 레인 없이 공유 풀에서 돌린 GT 병렬화가 RT 를 170 → 261 µs 로 늦춘 선례가 있어 빌더는 풀 작업이 아니라 전용 스레드입니다(드라이버 컴파일이 ms 단위라 풀 워커를 오래 붙잡음 **[추정]**).
**`warmPinnedMaterialPSOs`.** 루프(등장 디더 둘 × 패스 × 뷰 모드)는 "우선순위가 낮은 요청"을 넣는 일이 되어 별도 생성 루프가 사라지고, "고정한 머티리얼의 PSO 가 모두 `Ready`" 를 물을 수 있습니다.
**키 공간 전망.** 키 = (씬 메시 패스 수) × (머티리얼 퍼뮤테이션 수) × (마스크에 포함된 뷰 모드 수) × (컬 반전 2) **[코드: `materialPSOKey`, `collectFrameViewModeMask`]** 이라 불리언 스위치 하나가 키 수를 두 배로 만듭니다. 에디터에서 뷰 모드를 바꾸면 (머티리얼 수 × 패스 수)만큼 새 키가 한꺼번에 생겨 가장 큰 히치 후보입니다 **[추정]**. 실제 키 수와 생성 시간 분포는 M0 이 `RT.PSO.create` 로 잽니다. 쿠킹 산출물도 같은 곱을 따르며 게임 셰이더 하나가 백엔드당 180 파일입니다. Shipping 에서는 드라이버 컴파일만 남을 것입니다 **[추정]**.

**빌더 안전성 점검.** Vulkan · DX12 의 `createPipelineState` 는 워커에서 부를 수 있습니다(렌더패스 캐시 락 vkrpcache, `RHIRenderPassLedger` 로 DX11 · DX12 · GL 의 렌더패스 벡터 락). **DX11 과 GL 은 아직 점검하지 않았고 Dev 의 `ShaderCache::getOrCompile` 도 보지 않아** U12 시작 전에 해야 합니다 **[확인 전]**.

**락 점검 결과(rhilocks, 코드 읽기).** 락 없이 `push_back` 한 벡터의 포인터를 밖으로 내주는 모양을 RHI 전체에서 찾아 고쳤습니다(GL bindless 등록 · 해제를 `ScopedOpenGLContext` 안으로, `IRHIDevice` 의 렌더 스레드 상태 변수를 atomic 으로, DX11 의 `destroyBuffer` · `destroyPipelineState` 병렬 기록 중 호출 감시, `MaterialCache::acquire` · `reload` 의 초기화를 `_impl->_mutex` 밖으로 + `DeviceInitScope` 가 동시 초기화를 오류로 막음). **락 순서 규칙**: GL 가드가 렌더 스레드의 프레임 끝을 기다리므로 렌더 스레드가 아닌 스레드가 락을 쥔 채 등록 · 해제를 부르고 렌더 스레드가 프레임 안에서 같은 락을 잡으면 교착입니다(`MaterialCache` 둘이 그랬다). `SlotHandleTable::get` 의 주소는 테이블이 사는 동안 안 옮겨지고 값의 수명(읽는 동안 다른 스레드가 destroy 하지 않음)은 호출처 전제이며 위반 가능성은 못 찾았습니다(백엔드 `resolve*` 래퍼 안쪽은 읽지 않음 **[확인 전]**).


### 2.5 열린 문제

- **O1. 등장 디더와 준비 시점.** 머티리얼이나 PSO 때문에 RT 가 배치를 건너뛰면 디더가 진행된 채 갑자기 나타납니다. 권고안: 게임 스레드가 준비 여부(`Material::GpuState`, PSO 는 퍼뮤테이션 해시 단위 원자 조회)를 읽고 `Ready` 가 된 프레임에 `beginAppearance` 를 시작합니다. PSO 쪽 조회 API 는 U12 가 정합니다.
- **O2. 링 용량과 `Busy` 의 빈도.** 64 MiB 와 프레임당 상한은 추정이며 M1 이 `Busy` 횟수를 보고 정합니다.

## 3. 단위

| 단위 | 내용 | 선행 | GPU |
|---|---|---|---|
| U7 | 계약 GPU 테스트(코드 끝남, 실행 전 — 옛 호출 이전은 B0 가 했다) | B0 | 예 |
| U8 | 새 텍스처 업로드의 세 단계(코드 끝남, GPU 확인 전 — 아래 "끝난 단위: U8") | B0 | 예 |
| U9 | 렌더 타깃 읽기 티켓(코드 끝남, GPU 확인 전 — 글리프 아틀라스 몫은 이미 끝남) | B0 | 예 |
| U10 | 머티리얼 GPU 상태와 프레임 경계 초기화 | U8 | 예 |
| U11 | PSO 상태 데이터와 드로우 규칙(코드 끝남, GPU 확인 전) | 없음 | 예 |
| U12 | PSO 백그라운드 빌더 | U11, U5, U6, DX11 · GL 의 워커 안전성 점검 | 예 |
| U13 | 렌더 타깃 크기 변경의 `waitIdle` 제거(코드 끝남, GPU 확인 전) | U8 | 예 |
| U14 | `waitIdle` 이유 인자(코드 끝남, 게이트는 남음) | U13 | 아니오 |

**끝난 단위: B0 골격 번들.** U1(계약, ABI 34, 블로킹 헬퍼), U2(Vulkan 일회성 제출의 펜스 대기), U3(DX11), U4(GL)가 끝났고 U5 ~ U9 는 **골격**까지 들어갔습니다: 네 백엔드가 새 계약을 구현하고 옛 호출처와 테스트가 `...AndWait` 위에서 컴파일됩니다. GPU 네 백엔드 확인(`RHIDeviceTest`, `RenderPassGPUTest` 캡처)은 통합 L 에서 Release 로 통과했고(2026-10-25), 남은 확인은(측정 질문 넷)입니다. 통합 L 이 GL 폴링을 고쳤습니다(컨텍스트 없는 스레드의 `pollTransfer` 가 컨텍스트가 비어 있으면 한 번 집어 펜스를 봄 — 게임 스레드가 동기 로드로 막혀 RT 가 프레임을 돌리지 않으면 10 초 시간 초과까지 멈추던 것). nogpu `RHITransferTest.*` 는 `ctest-nogpu` 가 돕니다.

**병합 전략: B0 골격 번들 하나, 그 뒤는 단위별.** 옛 동기 가상 함수 셋을 지우면 네 백엔드와 모든 호출처가 한 번에 새 계약 위에 서야 컴파일되므로 쪼갤 수 없습니다. B0 는 계약(문구 수정, `discardTextureReadback`, 장치 세대, 대기 결과 분리)과 구현(DX11 → GL → Vulkan → DX12 → 호출처 · 테스트)을 커밋만 나눠 한 번에 병합하고 통합이 한 번에 빌드합니다. shim 이 없으므로 main 에 두 경로가 함께 남는 구간이 없습니다. 그 뒤 U5 ~ U9, U10 ~ U14 는 각각 따로 병합합니다. 동시에 띄울 수 있는 작업자는 여섯 안팎입니다.

**B0 의 알려진 비용.** 이전에 GPU 를 기다리지 않던 DX11 · DX12 · GL 의 프레임 경로 호출처가 `...AndWait` 로 바뀌어 **GPU 왕복이 새로 생겨** 런타임 UI 예제가 눈에 띄게 느려졌습니다(사용자 확인). 가장 잦은 둘은 비동기로 바꿨습니다.
- 글리프 아틀라스(`CanvasRenderer::uploadAtlasPage`): 페이지 전체와 구간을 제출만 하고 새 페이지는 첫 업로드 티켓이 `Complete` 가 된 이후 프레임에 등록합니다(그 전에는 그 페이지 글리프를 그리지 않음). `Busy` 는 더티를 남겨 다음 프레임에 다시 내고 `Rejected` · `Failed` 는 오류를 남기고 그 페이지를 멈춥니다(`release` 가 풂).
- 텍스처 구간 갱신(`TextureCache::flushRegionUpdates`, `Texture2D::writeCPUBytes`): 제출만 합니다. `Busy` 면 그 차례의 나머지를 다음 프레임으로 미루고(순서 유지) `Rejected` 는 오류를 남기고 버립니다. 두 함수의 제출 구간은 `ScopedForbidBlockingTransfer` 로 표시합니다.
- 남은 대기: 새 텍스처 업로드(`completePendingLoads` · `updateStreaming` · 동기 `acquire`, U8)와 `RenderThread` 렌더 타깃 읽기(요청이 있을 때만, U9).
- 측정(통합 M, Release, 새 한글 음절 400 개 판 `-lang=ko -gv_benchMeshes=100 -gv_benchUiHangulGlyphs=400 -gv_profileFrames=600`): 두 비동기화 전후 `RT.Canvas.Upload` 최악 DX12 62.9 → 4.2 ms 로 줄었다(DX11 · GL 도 같은 규모, Vulkan 은 U6 뒤).

**끝난 단위: GL-1(GL 스테이징 링).** 업로드(밉 전체, BC 포함)와 구간 갱신은 영속 매핑 PBO 링에 컨텍스트 밖에서 복사하고 컨텍스트는 PBO 바인딩 · `glTexSubImage*` · 펜스에만 빌립니다. 4.4 가 없으면 업로드는 모두 `Rejected`(클라이언트 메모리 경로 없음), 장치 손실은 제출 진입의 `isLost` 가 `Rejected` 로 돌려줍니다(`RHIStagingRingTest.LostDeviceRejectsInsteadOfBusyWhenTheRingIsFull`). 확인 `rhi-transfer-gl1-staging-ring`.

**끝난 단위: U6(Vulkan 비동기 전송).** 제출 안에서 기다리지 않고, 링 둘(업로드 · 되읽기)과 전송 작업 위에서 시리얼은 큐 락 안에서 매기며 완료는 펜스를 앞에서부터 걷어 올립니다(계약 5 · 7 · 8). 링보다 큰 전송은 단독 버퍼(`RHIStagingRingGPUTest.VulkanTransferLargerThanTheRingIsAcceptedAlone`). 결과 옮기기는 DX12 · Vulkan · GL 공용 `RHIStagingRingUtil::settleAllocation` · `RHITransferSubmit::fromUnaccepted` 한 곳입니다. 복사 오프셋은 텍셀 블록 바이트와 4 의 최소공배수로 맞춥니다(R8 작은 밉 · 12 바이트 텍셀 때문). 전송 중 파괴한 텍스처의 해제는 `_transferReleaseQueue` 가 미룹니다. GPU 확인 `rhi-transfer-vk-u6` 은 통합 N 에서 통과했고(Debug 검증 레이어 오류 0, UI 예제 프레임 벽시계 5.4 → 2.8 ms), `RT.Upload.waitIdle` 은 이제 프레임 밖 버퍼 갱신 · 시계 읽기만 잽니다.

**끝난 단위: U8(텍스처 업로드의 세 단계, 코드만 — 빌드 · GPU 확인은 통합).** 워커 읽기 → RT 펌프(`TextureCache::completePendingLoads`)의 `Texture2D::beginUploadFromImage`(텍스처 생성 · `submitTextureUpload`, 기다리지 않음) → 이후 프레임의 펌프가 `finishUpload`(`pollTransfer` 가 `Complete` 면 `registerBindlessTexture` 와 공개). `AssetLoadBlock` 에 "업로드 중" 단계(`kPhaseUploading`)와 완료 한 걸음의 결과 `AssetCompletionStep`(`Done` · `Failed` · `Submitted` · `Busy`)를 더했다 — 제출 뒤에는 펌프의 `runUploadPollIfPending` 과 `wait()` 가 CAS 로 집은 **한 스레드만** 확인하고, 펌프가 제출한 블록을 다른 스레드의 `wait()` 가 집으면 그 자리에서 `finishUpload` 를 블로킹으로 합니다. `Busy` 는 블록이 완료 대기로 돌아가 다음 펌프가 다시 내며(텍스처는 지우고 다시 만든다), 프레임당 상한은 **제출 수**입니다. 동기 `acquire` · `loadFromImage` · `loadFromCPUBytes` 는 같은 `begin` + `finish` 를 블로킹으로 합니다(`AndWait` 를 부르지 않는다). 위쪽 밉 교체도 같은 세 단계입니다(`beginUpgradeResidency` → `updateStreaming` 이 폴링하는 `_listUpgrading` → `finishUpgradeResidency`, 올리는 중 표시 `_bUpgrading` 는 끝날 때 내림 — `updateStreaming` 의 반환값은 이제 **끝낸** 수이고 상한 0 은 폴링만 한다). 올리는 중인 CPU 텍스처의 구간 갱신(`writeCPUBytes`)은 처음 업로드 뒤에 덮이도록 그대로 제출합니다. 테스트: `RenderPassGPUTest.AsyncTextureBecomesReadyThroughThePumpWithoutWait`(새), `StreamingUploadsTheTextureNotedAsLargestOnScreenFirst`(한 호출이 제출 · 폴링은 상한 0 으로). 남은 위험: 핫 리로드의 `reload` 는 `_bUpgrading` 이 내려가길 기다리므로 폴링할 RT 가 없으면 멈춘다(도구 경로). 같은 이유로 에디터 썸네일은 `runCompletionIfPending` 뒤 `Loading` 으로 남고 이후 RT 펌프가 끝낸다.

**끝난 단위: U11(PSO 상태 데이터, 코드만 — 빌드 · GPU 확인은 통합).** `RenderPSOCache::MaterialPSOState`(`Absent` · `Building` · `Ready` · `Failed`)를 변형 항목에 두고 항목은 `building()` · `ready( pso, bOwned )` · `failed()` 로만 만듭니다(핸들 0 의 `ready` 는 `failed`). `beginMaterialPSO( key )` 가 "처음 올린 쪽만 만든다" 를 한 번에 정하고(`hasMaterialPSO` 후 `setMaterialPSO` 의 두 걸음은 만드는 스레드가 둘이면 같은 키를 두 번 만든다), `findMaterialPSO` 는 `Ready` 의 핸들만, `findMaterialPSOState` 가 0 의 이유를 답합니다. `FrameRenderer::psoForBatch` 는 `pOutState` 로 상태를 내주고 `drawBatchGroup` 이 `Building` 인 배치를 `RT.PSO.pendingBatches` 로 셉니다(생성이 아직 RT 에서 동기라 값은 늘 0 이어야 한다 — 이 0 이 U12 의 비교 기준). `findMaterialPSO` 주석도 G11 이후 동작으로 고쳤습니다. 상태 전이는 nogpu `RenderPSOCacheTest.MaterialVariantStateGoesFromBuildingToReadyOrFailed`. 남은 U12: `requestMaterialPSO( key, desc )` · 빌더 스레드 · 완료 목록 합치기.

**끝난 단위: U13(렌더 타깃 크기 변경, 코드만 — 빌드 · GPU 확인은 통합).** `TextureCache::resizeRenderTarget` 의 `waitIdle`(W6)을 지웠다. 옛 GPU 텍스처 · SRV 는 `Texture2D::detachRHI` 로 해제하지 않고 떼어 스트리밍 교체와 같은 지연 해제 줄(`_retired`, 게임 스레드의 `releaseRetiredTextures` → `releaseHandle` 이 이미 만든 패킷이 끝난 뒤 돌려줌)에 넘기고 새 텍스처는 새 핸들 · 인덱스를 받습니다 — 그래서 DX11 · GL 이 `unregisterBindlessTexture` 에서 인덱스를 곧바로 돌려줘도 새 텍스처가 옛 인덱스를 물려받아 이전 프레임을 오염시키지 않습니다(설계가 요구한 "DX11 · GL 인덱스 반환의 GPU 완료 뒤 미룸" 을 백엔드가 아니라 이 호출처가 지연 해제로 채웠다). 새 인덱스는 세대를 올려 빌려 간 머티리얼이 다시 받습니다(기존). 핫 리로드의 `waitIdle` 은 도구 경로라 남깁니다. 확인은 기존 `CanvasTargetRecreatesTextureWhenWidgetResizes`(옛 텍스처 해제 줄을 비우는 한 줄을 더함)와 에디터 캔버스 시나리오. **[확인 전]** DX11 · GL 에서 `releaseHandle` 의 "넘긴 프레임 + 1" 이 GPU 가 옛 SRV 를 다 읽은 뒤인지(스트리밍 교체가 이미 같은 가정을 쓴다).

**끝난 단위: U9(렌더 타깃 읽기 티켓, 코드만 — 빌드 · GPU 확인은 통합).** `RenderThread::readRequestedRenderTarget` 가 요청 프레임에 기다리지 않는다. 요청을 받으면 `submitTextureReadback` 만 하고(`PendingRenderTargetRead` — 티켓을 직접 쥐고 소멸할 때 `discardTextureReadback`; `PendingReadback` 은 쥔 동안 같은 스레드의 `...AndWait` 를 단언으로 막아 스크린샷과 겹치므로 쓰지 않았다) 이후 프레임의 같은 함수가 `pollTransfer` 로 보고 `Complete` 면 `takeTextureReadback` 해 레지스트리에 게시한다. 읽는 중에는 새 요청을 레지스트리에 둔 채 받지 않고, `Busy` 면 같은 요청을 다음 프레임에 다시 내며, `Rejected` · `Failed` · 죽은 핸들은 조용한 실패로 게시합니다. `stop()` 이 팩토리보다 오래 살지 않게 남은 티켓을 버립니다(인라인 제출 경로 포함). 스크린샷과 에디터 캡처는 블로킹 헬퍼를 그대로 씁니다. 새 테스트는 없습니다 — 렌더 스레드 읽기는 장치가 있는 에디터 시나리오(`uiaudit.scenario.xml`)와 `uidemo.scenario.xml`, `-gv_screenshot` 캡처 경로로 봅니다. 같은 결함 후보: 에디터 `RenderTargetPanel` 의 `...AndWait`(`RenderTargetViewUtil.h`)는 UI 스레드의 도구 경로라 남겼다.

**끝난 단위: U14(`waitIdle` 이유 인자, 코드만 — 빌드는 통합).** `RHIWaitIdleReason`(`Shutdown` · `BackendSwap` · `Resize` · `HotReload` · `SceneActivation` · `Capture` · `Tool`, 그리고 U10 이 지울 옛 경로 표시 `MaterialInit`)을 `IRHIDevice::waitIdle` · `RenderThread::waitIdle` 의 필수 인자로 더하고 `Source/` 와 `Test/` 의 호출처 전부(약 70곳)에 이유를 적었다(테스트 · 에디터 UI 는 `Tool`, 텍스처 · 머티리얼 · 메시 핫 리로드는 `HotReload`, `ModuleHost::drainRenderWorkers` 는 `DrainReason` 을 그대로 옮김). 호출은 `SW_LOG_TRACE` 로 이유를 남긴다. 남은 것: (1) `Engine.cpp` 의 `MaterialInit` 은 U10 이 블록과 함께 지운다, (2) 린트 게이트(프레임 경로 폴더 `Renderer/Frame/` · `Graphics/Material/` · `Graphics/Texture/` 의 `Tool` 이 아닌 호출 금지)는 아직 없다 — 게이트 일부가 지워지는 중이라 보류.

**끝난 단위: U7(계약 테스트, 코드만 — 실행은 통합).** `RHIDeviceTest` 에 다섯 케이스(모두 네 백엔드): `SubmittedTransferCompletesByPollingWithoutAFrame`(프레임 없이 폴링만으로 `Complete`, 다시 물어도 `Complete`, 되읽기가 올린 바이트와 같음), `RegionUploadSubmittedAfterAnUploadIsVisibleInSubmitOrder`(구간 갱신이 업로드 뒤에 보이고 뒤 티켓의 `Complete` 가 앞 티켓을 덮음), `DestroyingTheTextureRightAfterSubmitIsSafe`(전송 중 파괴 뒤 티켓이 멈추지 않고 새 텍스처 업로드가 정상), `DiscardedReadbackReleasesItsStaging`(가져가지 않고 버리기를 16 번 되풀이해도 뒤의 읽기가 성공), `BindlessRegistrationIsRefusedWhileATransferIsPending`(등록이 성공했다면 그 전송은 `Complete`, `Complete` 뒤에는 성공). 이미 있던 것: 장치 손실 뒤 `Rejected`(`LostDeviceRejectsTransferSubmitsAtEntry` · Vulkan 은 `VulkanLostDeviceStopsWaitingOnTheGPU`)와 링이 찬 `Busy`(`RHIStagingRingGPUTest`). 검증 항목은 검증 묶음의 `rhi-async-u7-u13-gpu`(`pending: true`).

**끝난 단위: U5(DX12 비동기 전송).** 텍스처 전송이 프레임 슬롯 복사 리스트와 프레임 제출 락(`_uploadSlotMutex`)을 거치지 않고, 전송 작업 · 전송 펜스 · 업로드 링을 씁니다(링 구간 정렬은 `D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT` 512). 제출은 `_transferSubmitMutex` 안에서 `ExecuteCommandLists` · `Signal` · 시리얼 발급을 합니다(락 순서 `_transferSubmitMutex` → 펜스 줄 잎 락). 텍스처의 마지막 전송 시리얼은 `max` 로 기록합니다. GPU 확인은 `rhi-transfer-dx12-u5`. 남은 것: DX12 되읽기 링(잦아지면, M1 이 정함), GL 의 `allocateReadbackSpan` 이 `RHIStagingRingUtil` 을 쓰지 않는 것(GL 되읽기 링을 만질 때 합침).

**파일 소유.** U7 `Test/EngineTest/Graphics`, U8 `Graphics/Texture/`, U9 캔버스 · 캡처 · `RenderThread` · 에디터 캡처, U10 `Graphics/Material/` · `Engine.cpp` · `GPUSceneBuilder.cpp` · `FrameRenderer.cpp`, U11 · U12 `RenderPSOCache.*` · `FrameRendererPSO.cpp` · `FrameRendererDraw.cpp`, U13 `TextureCache.cpp` + DX11 · GL bindless, U14 호출처 전부(혼자). `FrameRenderer.cpp` 는 U10 만 고치고 U11 · U12 는 `warmPinnedMaterialPSOs` 호출 줄을 건드리지 않습니다.


**단위별 내용과 검증.** GPU 검증은 통합이 `gpu-verifier`(Shipping, 네 백엔드)로 한 번에 돌리고 항목은 구현하는 단위가 검증 묶음 에 더합니다.

- **U7.** `RHIDeviceTest` 계약 테스트: 제출 → `Pending` 또는 `Complete` → 폴링, 프레임 없이 폴링만 도는 경로(진행 보장), 구간 갱신 뒤 업로드된 내용이 보임(순서), 업로드 직후 텍스처 파괴, `Busy` 경로, 장치 손실 뒤 `Failed`(Vulkan 은 `VulkanLostDeviceStopsWaitingOnTheGPU` 와 같은 모양), 읽기를 가져가지 않고 버리는 경로, 끝나지 않은 전송이 있는 텍스처의 bindless 등록 거절. GPU 네 백엔드.
- **U8.** 텍스처 로드를 세 단계로 나눕니다: 워커 읽기 → RT 펌프의 `beginUpload`(텍스처 생성과 `submitTextureUpload`) → 이후 프레임의 `finishUpload`(`pollTransfer` 가 `Complete` 면 bindless 등록과 발행). `Busy` 는 다음 프레임에 다시 제출하고 프레임당 상한은 제출 수에 적용합니다. `updateStreaming` 의 위쪽 밉 교체도 같은 세 단계. 동기 `acquire` 와 `wait()` 는 블로킹 헬퍼로 `beginUpload → 대기 → finishUpload` 를 합니다(B0 의 `Texture2D::createTextureWithMips` · `loadFromCPUBytes` 의 `uploadTexture2DAndWait` 를 이 흐름으로). 걸림돌: `AssetLoadBlock` 의 완료 단계가 한 번에 끝나는 모양이라 펌프(RT)가 제출한 뒤 다른 스레드의 `wait()` 가 같은 블록의 완료를 집지 않게 하는 "업로드 중" 단계를 `AssetHandle` 에 더해야 합니다(`Resource/` 도 이 단위 소유에 더함). 검증: `RenderPassGPUTest.AsyncTextureStreamsTopMipsAfterBase`, `StreamingUploadsTheTextureNotedAsLargestOnScreenFirst`, `AssetStreamingTest.*`, 텍스처가 있는 시나리오(`kaykitcharacters`) 네 백엔드, 스크린샷으로 회색 → 진짜 텍스처.
- **U9.** 글리프 아틀라스는 끝났습니다(`rhi-transfer-u9-canvas-gpu`). 남은 것: `RenderThread::readRequestedRenderTarget` 가 요청 프레임에 읽지 않고 읽기 티켓(`PendingReadback`)을 레지스트리에 두었다가 이후 프레임에 `take`. 스크린샷과 에디터 캡처는 블로킹 헬퍼를 그대로 씁니다. 검증: `engine/automation/uidemo.scenario.xml` 네 백엔드, `-gv_screenshot` 캡처 경로, `RenderTargetRegistry` 읽기 테스트, 에디터 `uiaudit.scenario.xml`.
- **U10.** 2.3 의 여섯 항목. 검증: `material-cache-gpu-test`(매니페스트 id), 새 시나리오 `game/empty/automation/materialfirstuse.scenario.xml`(처음 보는 머티리얼의 오브젝트를 스폰하고 프레임마다 캡처해 몇 프레임 안에 나타나는지, Error 로그 없음), M1 의 `GT.Material.waitIdle` 호출 수 0.
- **U11.** 2.4 의 상태 데이터, `psoForBatch` 구분, `RT.PSO.pendingBatches` 카운터, `findMaterialPSO` 주석 수정. 생성이 여전히 RT 에서 동기라 항목이 한 프레임 안에 `Absent` → `Ready` 로 가 화면은 지금과 같아야 합니다. 상태 전이는 nogpu 테스트, GPU 는 기존 골든만 봅니다.
- **U12.** 빌더 스레드, 우선순위, 완료 목록 합치기, GL 시간 예산, `-gv_psoAsync`(0 이면 U11 의 동기 생성, A/B 측정용으로 `gv_gpuUploadQueue` 와 같은 선례). 검증: 새 시나리오 `game/empty/automation/psoasync.scenario.xml` 네 백엔드 — 뷰 모드를 번갈아 바꾸고 새 머티리얼을 스폰한 뒤 몇 프레임 안에 동기 생성과 같은 이미지에 이르는지, Error 로그 없음.
- **U13.** `TextureCache::resizeRenderTarget` 의 `waitIdle`(W6)을 지웁니다(옛 텍스처를 지연 해제로 넘기고 새 핸들 · 인덱스를 받음). 전제: DX11 · GL 의 bindless 인덱스 반환이 GPU 완료 뒤로 미뤄지는 것(DX12 는 이미 `deferFreeBindlessIndex`, DX11 은 `releaseFreeListIndex` 로 즉시 돌려줌 **[코드]**). 핫 리로드의 `waitIdle` 은 도구 경로라 남깁니다. 검증: 에디터에서 캔버스 타깃 크기를 바꾸는 시나리오(기존 `engine/automation/editor` 중 있는지 확인 **[확인 전]**)와 `hotreload.scenario.xml`.
- **U14.** `IRHIDevice::waitIdle` 에 이유 열거형(`Shutdown`, `BackendSwap`, `Resize`, `HotReload`, `SceneActivation`, `Capture`, `Tool`)을 필수 인자로 더합니다(호출처는 W7 의 20곳 안팎, U13 뒤에 남은 것). 이유로 이름을 가르면 프레임 경로의 새 호출이 눈에 띄고 린트 게이트가 프레임 경로 폴더(`Renderer/Frame/`, `Graphics/Material/`, `Graphics/Texture/` 의 비 reload 함수)에서 `Tool` 이 아닌 호출을 막을 수 있습니다(열거형으로 못 막는 함수 단위 허용 틈에만 게이트). 기계적 변경이라 혼자 하고 커밋만 단계별로 나눕니다.

**측정.** M0 은 착수 전입니다. `wt/profscopes` 병합 뒤 Release 에서 혼자(GPU 독점) 네 스코프의 호출 수와 ms 분포, RT · GT 프레임의 p99 와 최악 프레임을 잽니다. 시나리오는 게임 시작, 씬 로드, 새 머티리얼 스폰, 뷰 모드 전환, 글자 많은 UI 변경이고 설정은 LowEnd 포함. M0 에서 W2 의 합이 작거나 Vulkan 이 아닌 백엔드에서 W1 · W3 이 작으면 U10 · U12 의 우선순위를 낮춥니다. M1 은 B0 병합 뒤와 마지막 단위 뒤에 같은 시나리오로 다시 잽니다.

**B0 측정 질문 넷**(세는 스코프 `RT.Transfer.*` 가 생긴 뒤 perf-compare 케이스와 함께 검증 묶음 항목으로 올립니다).
1. 제출에서 `Complete` 까지 몇 프레임인가(같은 프레임: DX11 · GL · Vulkan B0, 한 프레임 이상: DX12 · 이후 Vulkan). U8 의 `finishUpload` 가 생기면 제출 프레임과 처음 `Complete` 를 본 프레임의 차로 셉니다.
2. `pollTransfer` 한 번의 비용(DX11 즉시 컨텍스트 락 + `GetData`, DX12 `_uploadSlotMutex` 공유 + 전송 펜스 읽기, GL 원자 읽기 + 필요할 때 `glClientWaitSync`). 프레임당 폴링 수가 늘 때 락 경합이 생기는가.
3. U2(Vulkan 대기 범위 축소)와 U6(비동기화)의 이득 — `RT.Upload.waitIdle` 합계와 RT 프레임 p99 전후.
4. `...AndWait` 대기의 CPU 사용(yield 32 번 뒤 200 µs 잠), 10 초 상한에 걸린 적이 있는가.

## 4. 규칙

아래 여섯은 이 계획이 끝난 뒤에도 남는 규칙이며 단위가 끝나면 정착 위치로 옮기고 이 절에서 지웁니다.

1. 게임 스레드가 부르는 새 API 는 GPU · 디스크 · 네트워크 완료를 기다리지 않습니다(`request` → 핸들 → 폴링). 동기 변형은 이름에 드러냅니다(`AndWait` 접미사). 정착 위치는 `docs/agents/CppStyle.md` 스레드 항목.
2. RHI 업로드와 되읽기는 "제출 → 티켓 또는 완료 콜백"만 새로 만들고 새 동기 가상 함수는 만들지 않습니다(옛 함수를 지우는 U1 이 구조적 강제, `IRHIResourceFactory.h` 머리말에 한 줄).
3. 프레임 경로에서 `waitIdle` 을 새로 만들지 않습니다. 허용은 수명 이벤트와 도구뿐이며 U14 의 이유 열거형과 게이트가 강제합니다.
4. 늦게 도착한 자원은 "준비 전엔 그리지 않음". 폴백 없이 실패는 오류로 남깁니다(`GpuState` · PSO 상태 열거형과 건너뛴 수 카운터).
5. 쓰기 I/O 는 `ThreadedLocalStore`(`Source/GameFramework/Base/Online/Local/ThreadedLocalStore.h`)를 거칩니다. 형식으로 막지 못하므로 리뷰가 맡고 기존 동기 쓰기 호출처 감사는 이 문서 밖의 후보입니다.
6. RT 에서 동기 생성이 생기면 `RT.*` 스코프로 감쌉니다(스코프 없는 동기 생성은 프로파일에 안 보여 다음 히치의 원인). 정착 위치는 `docs/agents/CppStyle.md`.

## 5. 위험과 되돌리기

| 위험 | 영향 | 완화 |
|---|---|---|
| 시그니처를 구현 뒤에 바꾸면 ABI 를 다시 높여 모든 백엔드를 재빌드 | 되돌리기 어려운 결정 | B0 병합 뒤의 시그니처 변경은 ABI 를 다시 높인다(`design-reviewer` 검토 결과는 B0 안에서 반영함) |
| B0 의 프레임 경로 호출처가 `...AndWait` 로 GPU 를 기다린다 | DX11, DX12, GL 에서 로드가 많은 프레임이 느려진다 | 글리프 아틀라스와 구간 갱신은 비동기로 바꿨다(`rhi-transfer-u9-canvas-gpu` 통과, 최악 60 ~ 90 ms → 1 ~ 4 ms). 새 텍스처 업로드(U8)와 렌더 타깃 읽기(U9)가 남았고 측정 질문 2, 4 가 크기를 본다 |
| Vulkan · DX12 전송이 RT 기록과 텍스처 상태를 두고 겹친다 | 레이아웃 · 리소스 상태 불일치 | 계약 3(렌더 타깃 전송은 RT 기록 밖에서만). 확인 `rhi-transfer-vk-u6` · `rhi-transfer-dx12-u5` 의 디버그 레이어 오류 0 |
| PSO 빌더의 스레드 안전성 | 백엔드나 셰이더 컴파일러의 잠금 누락 | 네 백엔드 점검이 끝나기 전 U12 를 시작하지 않는다. `-gv_psoAsync=0` 으로 동기 생성과 비교 |
| Vulkan 이 아닌 백엔드는 W2 가 이미 대기가 없다 | 이득이 Vulkan 에 몰림 | M0 이 먼저 측정한다. 이득이 작으면 U10 · U12 를 미룬다 |


**되돌리기.** U5 ~ U14 는 B0 가 선 뒤 각각 커밋 단위로 revert 합니다. 번들 B0 는 병합 커밋 하나를 revert 합니다(`git revert -m 1`). 옛 함수와 새 함수가 공존하는 단계가 없어 중간 상태로 되돌리는 방법은 없습니다.

## 6. 상용 엔진 비교

언리얼(`FRHIGPUFence` · `FRHIGPUTextureReadback`), 유니티(`AsyncGPUReadback`), Godot(`texture_get_data_async`)도 "제출 → 펜스나 폴링 → 사용"이라 방향이 같고, 다른 점은 콜백 대신 티켓 폴링(소비자가 RT 셋업 구간에서만 일해야 해서)과 1단계에서 전용 전송 큐를 쓰지 않는 것입니다. 기억에 기댄 비교이며 결정 근거는 1절의 코드 읽기입니다 **[확인 전]**.

## 7. 정한 것과 확인하지 못한 것

정한 것(사용자 결정이 필요한 항목은 없습니다):
- ABI 34 에서 옛 동기 함수 세 개를 지우고 shim 을 두지 않는다. 완료는 콜백이 아니라 티켓 폴링이다(2.2). B0 는 쪼개지 않는 골격 번들 하나로 병합하고 U5 ~ U14 는 그 뒤 단위별로 병합한다(3절).
- 이 결정들은 이 문서와 B0 커밋 메시지에 둔다(통합이 옮길 곳을 정한다). 1단계는 그래픽스 큐 하나이고 전용 전송 큐는 측정 뒤에 판단한다.
- GL 업로드는 PBO 링 하나로만 올린다(GL-1). 글리프 아틀라스는 기다리지 않는다(새 페이지는 첫 업로드 `Complete` 프레임에 등록). 새 머티리얼의 GPU 초기화는 RT 셋업 구간에서 하고 준비 전에는 그리지 않으며, PSO 는 네 가지 상태를 데이터로 두고 생성 스레드는 하나다(병렬 생성 없음).

확인하지 못한 것: `wt/profscopes` 스코프가 감싸는 줄, DX11 · GL 의 PSO 생성 워커 안전성(2.4), DX11 · GL 에서 RT 가 직접 인덱스를 해제하는 경로의 지연 보장과 `MaterialInstance::updateRHI` 의 상수 버퍼 쓰기 스레드(2.3), Vulkan 타임라인 세마포어 버전 요구, 링 용량 64 MiB 와 프레임당 상한의 적정값(M1).
