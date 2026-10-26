# RHI 비동기 업로드 · 머티리얼 초기화 · PSO 준비 상태

> 상태: 구현중 · 선행: 없음 · 영역: graphics · 옛번호: 없음 · 진행: 8/15

> 질문: 게임 스레드와 렌더 스레드가 GPU 완료를 기다리는 지점을 어떤 계약으로 없애고, 무엇부터 바꾸는가?
> 독자: RHI 백엔드와 렌더러를 고치는 작업자, 통합 담당.
> 결론: 업로드와 되읽기는 "제출 → 티켓 → 폴링" 계약 하나로 바꾸고(RHI ABI 버전을 한 번 높여 옛 동기 함수 세 개를 지운다), 새 머티리얼과 PSO 는 "준비 전 / 준비됨 / 실패" 상태를 데이터로 두어 준비 전에는 그리지 않는다. 프레임 경로에는 `waitIdle` 을 새로 만들지 않는다. ABI 변경이 없는 Vulkan 대기 좁히기(U2)는 B0 에 들어갔다.

2026-10-25 코드 읽기로 정리했습니다. 근거는 **[코드]**(읽어서 확인), **[추정]**, **[확인 전]** 으로 표시합니다. 프로파일 스코프 `GT.Material.waitIdle`, `RT.Upload.waitIdle`, `RT.PSO.create`, `GT.Load.sync` 는 `wt/profscopes` 브랜치가 붙이는 중이라 모든 측정(M0, M1)은 그 스코프로 합니다(스코프가 어느 줄을 감싸는지는 확인하지 못했습니다).
관련 문서: [AsyncResourceLoading](AsyncResourceLoading.md)(핸들 상태와 로드 중 규칙), [PreloadAndLod](PreloadAndLod.md)(PSO 미리 만들기와 텍스처 밉 스트리밍), [GraphicsFormatBundle](GraphicsFormatBundle.md)(머티리얼 PSO 변형이 실패하면 그리지 않는 규칙, G11). 이 문서는 그 위에서 GPU 쪽 대기만 다룹니다.

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

이 계획이 고치지 않는 것: `RT.BeginFrame` 의 GPU 백프레셔 꼬리는 대기 가능 스왑체인으로 고칠 히치가 없다고 이미 기각했습니다(결정 기록 3절 "히치"). 비슷한 선례로 메시를 미리 읽어 보관하게 한 변경이 StarSkirmish LowEnd 최악 프레임을 25.8 → 4.1 ms 로 줄였습니다(결정 기록 5-2). 이 계획의 기대도 "새 머티리얼 · 업로드가 있는 프레임의 추가 히치 제거"로 한정합니다.

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

**계약** (정본은 `IRHIResourceFactory.h` 의 전송 절 머리말)

1. 제출 함수는 반환 전에 소스 메모리를 복사합니다(호출자는 바로 버퍼를 재사용).
2. 제출은 GPU 완료를 기다리지 않습니다. `Accepted` 는 티켓 유효, `Busy` 는 **다시 시도하면 될 수 있을 때만**(스테이징 링이나 단독 버퍼 총량이 지금 차 있지만 GPU 가 진행하면 비는 경우, 호출자가 다음 프레임에 다시 제출), `Rejected` 는 다시 시도해도 안 되는 요청(검사 실패, 링이나 단독 버퍼 총량 상한보다 **큰 요청**, 장치 손실)이며 이유를 로그로 남깁니다. 장치 손실은 `RHITransferSerials` 의 손실 표시 하나가 쥐고 네 백엔드 제출 진입이 `isLost` 를 보고 `Rejected`(`RHIDeviceTest.LostDeviceRejectsTransferSubmitsAtEntry`). DX11 은 `isDeviceRemoved` 한 함수로 `GetDeviceRemovedReason` 을 모읍니다.
3. 순서는 양방향입니다. 제출한 전송은 **앞서 제출된**(`endFrame` 이 이미 반환한) 프레임 뒤, **뒤에 제출될** 프레임 커맨드 앞에 실행됩니다. 진행 중인 프레임 안에서 `executeCommandList` 로 넘긴 리스트는 앞서 제출된 것에 들지 않습니다(DX12 · Vulkan 은 `endFrame` 까지 모아 한 번에 내므로 전송이 그 앞에 끼고, DX11 · GL 은 곧바로 실행). 같은 텍스처의 제출끼리는 제출 순서대로, 되읽기는 요청 시점까지 제출된 프레임(직전 프레임까지)의 쓰기를 봅니다. 기록 중인 리스트가 어느 내용을 보는지는 백엔드마다 달라 호출자는 전송을 그리기 기록 전에 제출합니다. **렌더 타깃처럼 프레임 커맨드가 상태(레이아웃 · 리소스 상태)를 바꾸는 텍스처의 전송은 RT 기록 밖 구간에서만** 제출합니다.
4. **스레드.** 제출 · 폴링은 어느 스레드에서나 부릅니다. DX12 · Vulkan 은 자기 락으로 직렬화, DX11 은 즉시 컨텍스트의 `SetMultithreadProtected` 와 같은 임계 구역을 잡는 가드(`D3D11ImmediateContext::lock()`) 안에서만 즉시 컨텍스트를 씁니다. GL 은 제출이 `ScopedOpenGLContext` 로 컨텍스트를 빌리고(U8 의 동기 `acquire` 가 비 RT 스레드에서 부르기 때문) 스테이징 링 복사는 컨텍스트 밖에서 하며 빌리는 시간은 PBO 바인딩 · 텍스처 명령 · 펜스뿐입니다(GL-1). RT 가 컨텍스트를 쥐면 프레임 끝까지 기다리므로 GL 의 비 RT 제출은 프레임 경로 밖에서만 씁니다. **GL 의 `pollTransfer` 는 컨텍스트를 기다리며 빌리지 않습니다** — 원자 워터마크를 읽고, 이 스레드가 컨텍스트를 이미 쥐었거나 비어 있으면 한 번 집어 펜스를 본 뒤 놓습니다(`OpenGLRHIDevice::tryRetireFinishedTransfers`). 다른 스레드가 쥐고 있으면 그쪽이 올린 워터마크를 다음 폴링이 봅니다(RT 가 프레임을 돌리지 않는 동기 로드 중에도 진행).
5. **시리얼과 장치 세대.** 시리얼은 큐에 낸 순서대로 같은 락 안에서 매깁니다(`RHITransferSerials::issue`) — 락 밖에서 매기면 큐 실행 순서와 어긋나 워터마크가 끝나지 않은 제출을 덮습니다.
6. **파괴.** 전송 중인 텍스처를 파괴하면 백엔드가 GPU 자원 해제를 전송이 끝난 뒤로 미룹니다(해제 큐).
7. **진행 보장.** 프레임을 돌리지 않아도 `pollTransfer` 를 되풀이하면 결국 `Complete` 나 `Failed` 가 됩니다. 아직 GPU 에 내보내지 않은 일(DX12 열린 복사 리스트, GL flush 안 된 명령)은 제출이나 폴링이 내보냅니다.
8. `Complete` 는 "완료 워터마크 이상"(연속으로 끝난 시리얼의 끝)이라 `Complete` 를 본 뒤 다시 물어도 `Complete` 이고 티켓 해제 함수가 필요 없습니다. 완료 순서가 제출 순서와 다를 수 있는 Vulkan 은 연속 구간만 올립니다(`RHITransferFenceQueue` 가 앞에서부터만).
9. `Failed` 는 장치 손실처럼 끝나지 않을 일이거나 잘못된 티켓입니다. 오류를 로그로 남기고 대기하지 않습니다.
10. **소비자는 `Complete` 를 본 뒤에야 텍스처를 bindless 에 등록합니다.** 코드로 막습니다: 백엔드 텍스처 레코드가 마지막 전송 시리얼을 들고, 등록 시 그 시리얼이 워터마크 아래가 아니면 오류와 `kInvalidDescriptorIndex`. 로드 중 회색 텍스처는 정해진 데이터입니다([AsyncResourceLoading](AsyncResourceLoading.md) 2절).

**동기 변형은 ABI 밖의 헬퍼입니다.** `RHITransferUtil::uploadTexture2DAndWait`, `uploadTexture2DRegionAndWait`, `readbackTexture2DAndWait` 를 Engine 에 둡니다(vtable 에 없음). 제출 뒤 `pollTransfer` 를 되풀이하며 처음 32 번은 `yield`, 그 뒤 200 µs 씩 자고 상한은 DX11 `waitIdle` 과 같은 10 초(넘으면 `RHITransferWaitResult::TimedOut` 오류)입니다. 읽기가 시간 초과나 `Failed` 면 `RHITransferUtil::PendingReadback`(소멸자가 `discardTextureReadback`)이 결과를 버려 스테이징이 새지 않습니다. 사용처는 테스트, 도구, 스크린샷, 에디터 캡처, 동기 텍스처 `acquire` 입니다. RT 프레임 셋업 구간에서는 쓰지 않으며 그 구간을 `ScopedForbidBlockingTransfer` 로 표시하면 `...AndWait` 가 Debug 에서 단언합니다(표시는 셋업 구간을 소유한 U10 의 몫). 백엔드에 두 번째 경로가 생기지 않는 것이 핵심입니다.

**콜백이 아니라 폴링인 이유.** `enqueueGPURelease` 콜백은 완료를 확인하는 스레드에서 불리는데 bindless 등록은 RT 셋업 구간에서만 가능해 콜백으로 받아도 RT 큐에 넣고 다시 읽게 됩니다. 폴링은 RT 가 셋업 구간에서 직접 읽어 스레드 경계가 한 번 줄어듭니다. 해제용 `enqueueGPURelease` 는 그대로입니다. **버퍼 되읽기는 반대로 콜백**입니다(`createReadbackBuffer` + `readReadbackBuffer`, 프레임 완료 콜백 안에서 곧바로 복사하고 bindless 등록이 없음). 기준: 결과를 받는 쪽이 RT 셋업 구간에서 상태(bindless 인덱스)를 바꿔야 하면 티켓과 폴링(텍스처 업로드 · 되읽기), 프레임에 묶인 읽기 버퍼를 완료 콜백에서 한 번 비우면 끝나면 콜백.

**ABI 결정.** `kRHIModuleAbiVersion` 33 → 34, 옛 가상 함수 세 개를 지웁니다. 백엔드 DLL 이 엔진과 같은 vtable 을 봐야 로드되므로(`RHIModuleAbi.h`) 한 번에 들어가야 하고 백엔드마다 쪼갤 수 없습니다. 옛 함수를 남기고 새 함수 위에서 구현하는 기본 구현(shim)은 기각합니다(같은 일을 하는 경로가 둘 남아 폴백 금지 원칙과 부딪힘).

**스테이징 링.** 장치마다 고정 용량 영속 매핑 업로드 링을 둡니다. 구간은 티켓 시리얼로 표시하고 워터마크가 지나면 회수합니다. 장부는 `RHIStagingAllocator`(`RHI/Support`) 하나이고 백엔드는 링 메모리만 쥡니다. 시리얼을 붙인 구간은 그 시리얼이 워터마크를 지나면 할당 순서와 무관하게 돌아오고, 놓은 구간(제출하지 못한 업로드, 가져간 되읽기)은 바로 돌아옵니다. 빈 자리는 `SpanAllocator`(정렬 단위 한 칸, `freeDeferred`)가 쥐어 가져가지 않은 되읽기 하나가 링 맨 앞을 막지 않습니다. 규칙은 GPU 없이 `RHIStagingRingTest` 가 봅니다. 용량은 `gv_rhiStagingRingMegabytes` 가 `RHISwapChainDesc::_stagingRingBytes` 로 넘기고 0 이하면 링을 쓰는 백엔드가 초기화를 거절합니다. 시작 용량 64 MiB(4096² BC7 한 장 약 21 MiB 를 몇 장 동시에 담는 추정, M1 이 조정)는 `RHIMemoryLedger` 의 Staging 항목으로 보입니다. 링보다 큰 요청은 단독 스테이징 버퍼를 만들고 완료 뒤 해제하며 총량에 상한을 둡니다. 총량 상한이 차서 지금은 못 받지만 GPU 가 진행하면 비는 요청은 `Busy`, **링이나 단독 버퍼 총량 상한 자체보다 큰 요청은 `Rejected`** 입니다.

**전송 큐.** 1단계는 그래픽스 큐 하나입니다. 전용 복사 큐(Vulkan 전송 큐 패밀리, DX12 COPY 큐)는 큐 소유권 이전 · 레이아웃 전이 · 큐 사이 동기화가 필요하고 이득이 큰 텍스처 스트리밍 중의 프레임 시간뿐이라 측정 없이 하지 않습니다. 같은 큐에서는 제출 순서가 곧 실행 순서라 `Complete` 이전 사용도 GPU 에는 안전하지만, 나중에 전용 큐를 도입할 때 규칙을 바꾸지 않으려고 소비자 규칙을 `Complete` 로 둡니다.

**백엔드별 구현.**

| 백엔드 | 지금 | 새 계약 구현 | 난이도 |
|---|---|---|---|
| DX11 | `UpdateSubresource`(즉시 컨텍스트, 잠금) | `gv_rhiDirectUploadMaxKilobytes`(256 KiB) 이하는 같은 호출, 그보다 크면 장치가 초기 데이터로 만든 `STAGING` 텍스처(잠금 밖의 소스 복사)를 잠금 안에서 `CopySubresourceRegion`. 티켓마다 이벤트 쿼리 하나(풀)이고 `pollTransfer` 가 `GetData`. 스테이징은 전송 큐 항목이 쥐어 워터마크가 지나면 놓고, 합은 `gv_rhiStagingRingMegabytes` 상한(차면 `Busy`, 한 장이 넘으면 예산 전부를 잡고 혼자 들어간다). 되읽기는 `Map` · `Unmap` 에만 잠금을 잡고 행 복사는 잠금 밖. 링은 없다 — D3D11 에는 버퍼 → 텍스처 복사가 없다 | 낮음. 큰 업로드의 소스 복사가 즉시 컨텍스트 잠금 밖으로 나갔다(측정 전, 확인은 매니페스트 id `rhi-transfer-dx11-staged-upload`) |
| GL | 영속 매핑 PBO 링(`ARB_buffer_storage`, GL 4.4)에 컨텍스트 밖에서 복사하고 컨텍스트를 빌려 PBO 오프셋으로 `glTexSubImage*` · `glCompressedTexSubImage*` | 업로드 뒤 `glFenceSync` 와 `glFlush`, 링 구간은 시리얼이 워터마크를 지나면 돌아온다. `pollTransfer` 는 컨텍스트를 기다리지 않고 원자 워터마크를 읽으며 컨텍스트를 쥐었거나 비어 있을 때만 `glClientWaitSync( 0 )` 으로 올린다. GL 4.4 가 없으면 링이 없고(`_bPersistentStagingRing` 거짓) 업로드는 모두 `Rejected` | 끝남(GL-1). 비 RT 제출은 컨텍스트 점유 경합(`GPUUploadQueue.cpp` 주석)이 있어 프레임 경로 밖에서만. 텍스처 되읽기는 영속 매핑 PBO 되읽기 링(`GL_MAP_READ_BIT`, `gv_rhiReadbackRingMegabytes`)에 `glGetTextureSubImage` 와 펜스만 내고 `takeTextureReadback` 이 펜스 뒤 컨텍스트 밖에서 복사한다. 링보다 큰 읽기는 일회성 버퍼로 혼자 받고 단독 상한 초과 `Rejected`, 가져가지 않은 읽기로 차면 `Busy`, GL 4.4 없으면 `_bPersistentReadbackRing` 거짓에 `Rejected`(확인 `rhi-transfer-gl-readback-ring`) |
| DX12 | 프레임 슬롯의 업로드 힙과 복사 리스트가 프레임 리스트 앞에 끼는 구조 | 전용 업로드 링(`D3D12StagingRing`), 전송마다 얼로케이터를 쥔 전송 작업(`D3D12TransferJob`, 풀), 전용 `ID3D12Fence`. 제출은 즉시 `ExecuteCommandLists` + `Signal`, 시리얼은 `_transferSubmitMutex` 안에서 `RHITransferFenceQueue` 가 매긴다. 링보다 큰 요청은 일회성 업로드 힙 버퍼로 혼자, 찬 링은 `Busy` | 끝남(U5, 아래). 구조 버퍼 갱신(`updateStructuredBufferRegions`)만 프레임 슬롯 모델에 남았다. 되읽기는 읽기마다 읽기 힙 버퍼를 만든다(되읽기 링 없음) |
| Vulkan | 영속 매핑 호스트 가시 · 일관 링 둘(업로드 · 되읽기)에 전송 락 밖에서 복사하고 전송 작업(`VulkanTransferJob`: 전용 풀 커맨드 버퍼 + 펜스, 재사용)에 기록해 그래픽스 큐에 낸다 | 제출은 기다리지 않는다. 시리얼은 큐 락 안에서 `RHITransferFenceQueue::enqueue` 가 매기고 `pollTransfer` · 링이 찬 제출 · `beginFrame` 이 `vkGetFenceStatus` 로 앞에서부터 걷어 워터마크를 올린다. 링보다 큰 요청은 `Rejected`, 찬 링은 `Busy` | 끝남(U6, 아래). 프레임 밖 구조 버퍼 갱신과 GPU 시계 읽기는 여전히 `VulkanOneShotCommands` 로 기다린다 |

DX12 의 인덱스 반환 지연(`deferFreeBindlessIndex`)과 Vulkan 의 해제 큐는 이미 GPU 완료를 기준으로 합니다 **[코드]**. Vulkan 타임라인 세마포어 버전 요구는 확인하지 않아 펜스를 씁니다 **[확인 전]**.

### 2.3 A. 새 머티리얼 첫 사용을 프레임 경계로

지금 흐름: 컴포넌트가 머티리얼 경로를 잡으면 `requestInitialize` 가 카운터를 올리고, 게임 스레드의 `submitRenderPacket` 이 카운터를 보고 RT 와 GPU 를 비운 뒤(W1) `initializePending` 으로 파일을 읽고 상수 버퍼를 만들고 bindless 에 등록하고 인덱스를 패킷에 싣습니다.

바꾸는 흐름:
1. `requestInitialize` 는 그대로(컴포넌트는 디바이스를 모름).
2. 파일 읽기와 파싱(`Material::loadFromFile`)은 워커 단계로(텍스처 로드의 두 단계 구조와 같음, [AsyncResourceLoading](AsyncResourceLoading.md) 3절).
3. GPU 단계(상수 버퍼 생성과 첫 쓰기, `registerBindlessBufferSrv`, 텍스처 SRV 해석)는 RT 셋업 구간에서 `MaterialCache::initializePending` 이 하며 프레임당 개수 상한은 M1 이 정합니다.
4. `Material` 에 원자 상태 `GpuState`(`NotRequested`, `Pending`, `Ready`, `Failed`)를 둡니다(`Pending` 은 `requestInitialize`, `Ready` · `Failed` 는 RT 가 쓰고 게임 스레드가 읽음). 반쯤 초기화된 상태를 못 구분하는 `isRHIValid()` 를 대체합니다.
5. 패킷 빌더(`GPUSceneBuilder.cpp` 1558 주변)는 `Ready` 가 아닌 머티리얼의 배치를 만들지 않고 건너뛴 수를 `GT.Material.notReadyBatches` 로 셉니다. `Failed` 는 `initializePending` 이 오류를 한 번 남기고 그 메시는 그려지지 않습니다(`MeshComponent.cpp` 97 주석과 같은 동작 **[코드]**).
6. `Engine.cpp` 612-617 의 `waitIdle` 블록을 지웁니다.

보이는 변화는 새 머티리얼을 쓰는 오브젝트가 파일을 읽는 시간과 패킷 링 깊이만큼(수 프레임) 늦게 나타나는 것이며, 이를 숨기는 등장 디더([AsyncResourceLoading](AsyncResourceLoading.md) 3절 B2)를 준비 시점에 맞추는 일은 O1 입니다.

인덱스 재사용: 새 등록은 새 인덱스를 받고 게임 스레드가 놓은 핸들은 `releaseHandle` 이 "넘긴 프레임 + 1" 뒤까지 미루므로(`IRHIDevice.h` 195-205 주석) 이미 만든 패킷이 옛 인덱스를 값으로 보관하는 문제는 막혀 있습니다 **[코드]**. RT 가 직접 해제하는 DX11 · GL 경로에서 같은 보장이 서는지는 확인하지 못했습니다 **[확인 전]**. `MaterialInstance::updateRHI`(`MaterialInstance.cpp` 306)와 `refreshTextureBindings` 가 상수 버퍼를 어느 스레드에서 쓰는지도 확인하지 못했습니다 **[확인 전]**(U10 작업자가 먼저 읽고 이 절에 적는다).

기각: 게임 스레드가 레지스트리 락을 잡고 직접 등록(공유하지 않는다는 규칙과 반대), 패킷이 인덱스 대신 머티리얼 핸들을 보관하고 RT 가 해석(형식 변경이 큰 데 비해 이득은 몇 프레임 지연뿐).

### 2.4 D. PSO 준비 상태

`RenderPSOCache` 의 머티리얼 변형 항목은 지금 (핸들, 소유 여부)이고 실패는 핸들 0, 미생성은 항목 없음으로 표현해 "만드는 중"이 없습니다. 이 상태를 데이터로 드러냅니다.

- 상태 `Absent`, `Building`, `Ready`, `Failed` 를 항목에 둡니다(키는 그대로). `FrameRenderer::psoForBatch` 는 `Ready` 의 핸들만 돌려주고 `Building` 이면 그 배치를 이번 프레임에 그리지 않으며 `RT.PSO.pendingBatches` 로 셉니다. `Failed` 는 만들 때 오류를 한 번 남깁니다(지금도 핸들 0 이면 `drawBatchGroup` 이 그리지 않음 **[코드: `FrameRendererDraw.cpp` 447-450]** — 새로 생기는 것은 "아직 안 됨"과 "실패"의 구분).
- `RenderPSOCache.h` 의 `findMaterialPSO` 주석("0 이면 패스 PSO 로 그립니다")은 G11 이후 동작과 다르므로 U11 이 고칩니다.
- 요청은 셋업 구간의 `ensureMaterialPSOs` 가 모아 `requestMaterialPSO( key, desc )` 로 넣고, 이 함수가 `Absent` 를 `Building` 으로 바꿔 빌더 큐에 넣습니다.

**빌더.** 전용 스레드 하나가 요청을 순서대로 만듭니다(이번 프레임 배치가 필요한 키가 먼저, 미리 만들기 `warmPinnedMaterialPSOs` 가 나중). 결과는 완료 목록에 쌓고 RT 가 셋업 구간에서 캐시에 합치므로 캐시를 쓰는 스레드가 RT 하나로 유지되어 "삽입은 기록 시작 전에만"이라는 불변식이 서고 `bCreatedVariant` 후처리(`ensureMaterialFallbackBuffers`)도 합치는 자리에서 합니다.
**GL.** `_bThreadSafeResourceCreation` 이 거짓이라 빌더 스레드를 쓸 수 없어 RT 가 프레임당 시간 예산 안에서 만들고 남은 키는 `Building` 으로 둡니다(폴백이 아니라 같은 상태 규칙을 다른 방식으로 채우는 것).
**기각된 결정과의 관계.** PSO 병렬 생성은 이득 9 %(드라이버가 직렬)로 기각됐습니다(결정 기록 3절). 이 설계는 빠르게 만들려는 것이 아니라 RT 임계 경로에서 빼려는 것이라 스레드는 하나입니다. 레인 없이 공유 풀에서 돌린 GT 병렬화가 RT 를 170 → 261 µs 로 늦춘 선례가 있어(결정 기록 3절 "태스크") 빌더는 풀 작업이 아니라 전용 스레드입니다(드라이버 컴파일이 ms 단위라 풀 워커를 오래 붙잡음 **[추정]**).
**`warmPinnedMaterialPSOs`.** 루프(등장 디더 둘 × 패스 × 뷰 모드)는 "우선순위가 낮은 요청"을 넣는 일이 되어 별도 생성 루프가 사라지고, "고정한 머티리얼의 PSO 가 모두 `Ready`" 를 물을 수 있어 [AsyncResourceLoading](AsyncResourceLoading.md) 의 준비 집합이 로딩 화면 종료 조건으로 쓸 수 있습니다.
**키 공간 전망.** 키 = (씬 메시 패스 수) × (머티리얼 퍼뮤테이션 수) × (마스크에 포함된 뷰 모드 수) × (컬 반전 2) **[코드: `materialPSOKey`, `collectFrameViewModeMask`]** 이라 불리언 스위치 하나가 키 수를 두 배로 만듭니다. 에디터에서 뷰 모드를 바꾸면 (머티리얼 수 × 패스 수)만큼 새 키가 한꺼번에 생겨 가장 큰 히치 후보입니다 **[추정]**. 실제 키 수와 생성 시간 분포는 M0 이 `RT.PSO.create` 로 잽니다. 쿠킹 산출물도 같은 곱을 따르며 게임 셰이더 하나가 백엔드당 180 파일입니다(결정 기록 5-5). Shipping 에서는 드라이버 컴파일만 남을 것입니다 **[추정]**.

**빌더 안전성 점검 [확인 전]**: 네 백엔드의 `createPipelineState` 를 워커에서 불러도 되는지, Dev 의 `ShaderCache::getOrCompile` 이 워커에서 안전한지 점검해야 합니다. **DX11 과 GL 은 아직 점검하지 않았고** U12 가 시작하기 전에 해야 합니다.
- **Vulkan(U6, 코드 읽기)**: `_pipelineCache` 는 `EXTERNALLY_SYNCHRONIZED` 없이 만들어 문제없고 `_pipelineStates`(`RHIHandleTable` → `SlotHandleTable`)의 `insert` · `take` · `erase` 는 내부 뮤텍스로 직렬화됩니다(U5 가 정정). 막는 것은 `VulkanRHIRenderPassCache::ensurePipelineRenderPass` 의 `_mapPipelineRenderPass` 가 락 없이 찾고 넣는다는 하나였고 vkrpcache 가 풀었습니다 — `_pipelineMutex`(잎 락)로 찾고 넣고 `destroyAll` 도 같은 락으로 비우며, desc 렌더패스 기록(`addRenderPassRecord` · `destroyRenderPassRecord` · `destroyAll`)은 `_recordMutex`(잎 락)로 직렬화하고 벡터 안 포인터를 내주던 `resolveRenderPassRecord` 는 지웠습니다. DX11 · DX12 · Vulkan 의 bindless 레지스트리 벡터는 `_bindlessMutex` 안에서만 늘고 읽힙니다 (GL 은 잠금이 없었다 — 아래 락 점검). 이제 Vulkan 의 `createPipelineState` 는 워커에서 부를 수 있습니다 **[코드]**.
- **DX12(U5, 코드 읽기)**: 워커에서 부를 수 있습니다 — 만지는 것은 free-threaded 장치의 `CreateGraphicsPipelineState`, 읽기만 하는 루트 시그니처, 직렬화되는 `_pipelineStates`, 셰이더 컴파일(`RHIShaderRequest::compileGraphics`)입니다. 실패 경로의 Debug 전용 `flushDebugMessages` 는 다른 스레드가 큐를 비우면 로그만 빠질 수 있고(결과 검사해 건너뜀), `createRenderPass` 의 `_listRenderPass` 는 락 없이 넣었으나 PSO 생성 경로가 부르지 않았습니다. 같은 모양이 DX11 · GL 에도 있어 세 백엔드의 벡터를 락을 가진 `RHIRenderPassLedger`(`RHI/Support`, `add` · `retire` · `clear`)로 바꿨습니다(`RHIRenderPassLedgerTest`). `destroyPipelineState` 는 RT 소유 상태를 쓰므로 RT 가 부릅니다.

**락 점검 결과(rhilocks, 코드 읽기만) [코드].** vkrpcache 가 고친 "락 없이 `push_back` 한 벡터의 포인터를 밖으로 내줌" 모양을 RHI 전체에서 다시 찾았습니다(호출 스레드는 호출처까지 따라감).

- **문제 없음**: 백엔드별 `_listLiveCmd`(`_liveCmdListMutex` 가 등록 · 해제 · 순회를 지킴), DX12 · Vulkan 의 `_listPendingSubmit` · 프레임 세그먼트(프레임 스트림을 쥔 스레드 하나, `data()` 를 제출 호출에 바로 넘기고 그 안에서 끝남), `IRHIDevice` 지연 핸들 큐(락 안에서 목록을 옮긴 뒤 락 밖에서 해제), `RHIHandleTable::get` → `SlotHandleTable`(`PagedArray` 청크 고정 배열이라 슬롯이 늘어도 주소가 안 옮겨지고 점유 · 세대가 원자값 하나 — 값의 수명은 상위 계층 몫).
- **경쟁 가능해서 고친 것**: ① GL 의 bindless 등록 · 해제 아홉 함수는 가드가 없었다(드로우의 읽기와 겹치는 것을 호출 규약 `GT.Material.waitIdle` 만 막았음). 이제 `ScopedOpenGLContext` 안에서 표를 바꾼다("표는 컨텍스트를 쥔 스레드만 읽고 쓴다" — DX · Vulkan 의 `_bindlessMutex` 와 같은 역할, 렌더 스레드가 프레임 중이면 프레임 끝까지 기다림). ② `IRHIDevice` 의 `_pfnRenderThreadDrain` · `_pRenderThreadDrainContext` · `_renderThreadID` · `_bParallelRecording` 이 일반 변수라 렌더 스레드를 세우고 풀 때 `releaseHandle` 과 겹쳤다 — atomic 으로(함수를 마지막에 release 로 쓰고 읽는 쪽이 acquire 로 먼저 읽음). ③ DX11 의 `destroyBuffer` · `destroyPipelineState` 가 `requireRegistryMutableNow` 로 병렬 기록 중 호출을 모든 구성에서 감시한다(어긴 호출은 크래시가 아니라 `forget*InRecordingStates` 가 쓰는 변수가 덮여 한 프레임 낡은 바인딩으로 그리는 조용한 어긋남이고 비용이 원자 읽기 한 번이라 Release 에서도 멈춘다 — 기존 `assertRegistryMutableNow` 는 모든 백엔드의 등록 경로가 써서 이 변경에서 안 바꿈. 직접 호출처는 모두 렌더 스레드 셋업이나 초기화 · 해제로 병렬 기록 구간에서 부르는 곳이 없었다). ④ 에디터 `EditorRenderViews` · `RenderTargetPanel` 의 렌더러 없을 때 분기는 `releaseHandle` 로 미룬다. ⑤ `MaterialCache::acquire`(디바이스를 준 호출)와 `reload` 가 파일 읽기 · 버퍼 생성 · 등록 · `waitIdle` 을 `_impl->_mutex` 밖에서 한다.
- **락 순서**: GL 가드가 렌더 스레드의 프레임 끝을 기다리므로, 렌더 스레드가 아닌 스레드가 락 L 을 쥔 채 등록 · 해제를 부르고 렌더 스레드가 프레임 안에서 L 을 잡으면 교착이다. 호출처별로 따라간 결과 순환은 `MaterialCache` 둘뿐이었다 — `acquire` 는 초기화가 렌더 스레드의 프레임 끝을 기다리는데 렌더 스레드가 `takePSOWarmMaterials` 로 같은 락을 잡고, `reload` 는 락을 쥔 채 `waitIdle` 하는데 그 일이 같은 락을 기다린다(백엔드와 무관). 초기화를 락 밖으로 옮기고 락이 막던 동시 초기화는 `MaterialCache::Impl::DeviceInitScope`(소유 스레드를 원자 비교로 쥐고 다른 스레드가 들어오면 모든 구성에서 오류를 남기고 멈춤, 같은 스레드 중첩은 허용)가 막는다. 호출처(`Scene::initialize` · `initializePending` · 에디터 `MaterialPanel`)는 모두 한 스레드였다. `TextureCache` 는 장치를 쓰기 전에 락을 풀고, DX11 · DX12 · Vulkan 의 `_bindlessMutex` 는 컨텍스트 · 제출 락을 잡는 곳이 없는 잎 락이다.
- **`SlotHandleTable::get` 수명 계약**: 주소는 테이블이 살아 있는 동안 옮겨지지 않는다(`ensure` 는 뮤텍스 안에서만, `releaseChunks` 는 소멸자에서만). 값은 `take` · `erase` 가 렌더 스레드의 프레임 밖 `releaseNow` 나 렌더 스레드 소유 직접 해제뿐이고, 렌더 스레드 밖에서 `resolve*` 로 읽는 전송 · 등록 경로(머티리얼 · 상수/구조 버퍼 슬롯의 `registerBindless*`, `Texture2D`, `Mesh::initRHI` 의 `GPUUploadQueue` 워커, 에디터 스냅샷, `FrameCapture` 되읽기)의 전제("읽는 동안 다른 스레드가 그 자원을 destroy 하지 않음")를 호출처별로 봤고 위반 가능성은 찾지 못했다. 한계: 백엔드 `resolve*` 래퍼 34곳 안쪽의 포인터 사용 길이는 읽지 않았다 **[확인 전]**.

### 2.5 열린 문제

- **O1. 등장 디더와 준비 시점.** 머티리얼이나 PSO 때문에 RT 가 배치를 건너뛰면 디더가 진행된 채 갑자기 나타납니다. 권고안: 게임 스레드가 준비 여부(`Material::GpuState`, PSO 는 퍼뮤테이션 해시 단위 원자 조회)를 읽고 `Ready` 가 된 프레임에 `beginAppearance` 를 시작합니다. PSO 쪽 조회 API 는 U12 가 정합니다.
- **O2. 링 용량과 `Busy` 의 빈도.** 64 MiB 와 프레임당 상한은 추정이며 M1 이 `Busy` 횟수를 보고 정합니다.

## 3. 단위

| 단위 | 내용 | 선행 | GPU |
|---|---|---|---|
| U7 | 계약 GPU 테스트(옛 호출 이전은 B0 가 했다) | B0 | 예 |
| U8 | 새 텍스처 업로드의 세 단계(B0 는 `uploadTexture2DAndWait`, 구간 갱신 몫은 끝남) | B0 | 예 |
| U9 | 렌더 타깃 읽기 티켓(B0 는 `readbackTexture2DAndWait`, 글리프 아틀라스 몫은 끝남) | B0 | 예 |
| U10 | 머티리얼 GPU 상태와 프레임 경계 초기화 | U8 | 예 |
| U11 | PSO 상태 데이터와 드로우 규칙 | 없음 | 예 |
| U12 | PSO 백그라운드 빌더 | U11, U5, U6, DX11 · GL 의 워커 안전성 점검 | 예 |
| U13 | 렌더 타깃 크기 변경의 `waitIdle` 제거 | U8 | 예 |
| U14 | `waitIdle` 이유 인자와 게이트 | U13 | 아니오 |

**끝난 단위: B0 골격 번들.** U1(계약, ABI 34, 블로킹 헬퍼), U2(Vulkan 일회성 제출의 펜스 대기), U3(DX11), U4(GL)가 끝났고 U5 ~ U9 는 **골격**까지 들어갔습니다: 네 백엔드가 새 계약을 구현하고 옛 호출처와 테스트가 `...AndWait` 위에서 컴파일됩니다. GPU 네 백엔드 확인(`RHIDeviceTest`, `RenderPassGPUTest` 캡처)은 통합 L 에서 Release 로 통과했고(2026-10-25), 남은 확인은 매니페스트 id `rhi-transfer-b0-measure`(측정 질문 넷)입니다. 통합 L 이 GL 폴링을 고쳤습니다(컨텍스트 없는 스레드의 `pollTransfer` 가 컨텍스트가 비어 있으면 한 번 집어 펜스를 봄 — 게임 스레드가 동기 로드로 막혀 RT 가 프레임을 돌리지 않으면 10 초 시간 초과까지 멈추던 것). nogpu `RHITransferTest.*` 는 `ctest-nogpu` 가 돕니다.

**병합 전략: B0 골격 번들 하나, 그 뒤는 단위별.** 옛 동기 가상 함수 셋을 지우면 네 백엔드와 모든 호출처가 한 번에 새 계약 위에 서야 컴파일되므로 쪼갤 수 없습니다. B0 는 계약(문구 수정, `discardTextureReadback`, 장치 세대, 대기 결과 분리)과 구현(DX11 → GL → Vulkan → DX12 → 호출처 · 테스트)을 커밋만 나눠 한 번에 병합하고 통합이 한 번에 빌드합니다. shim 이 없으므로 main 에 두 경로가 함께 남는 구간이 없습니다. 그 뒤 U5 ~ U9, U10 ~ U14 는 각각 따로 병합합니다. 동시에 띄울 수 있는 작업자는 여섯 안팎입니다.

**B0 의 알려진 비용.** 이전에 GPU 를 기다리지 않던 DX11 · DX12 · GL 의 프레임 경로 호출처가 `...AndWait` 로 바뀌어 **GPU 왕복이 새로 생겨** 런타임 UI 예제가 눈에 띄게 느려졌습니다(사용자 확인). 가장 잦은 둘은 비동기로 바꿨습니다.
- 글리프 아틀라스(`CanvasRenderer::uploadAtlasPage`): 페이지 전체와 구간을 제출만 하고 새 페이지는 첫 업로드 티켓이 `Complete` 가 된 이후 프레임에 등록합니다(그 전에는 그 페이지 글리프를 그리지 않음). `Busy` 는 더티를 남겨 다음 프레임에 다시 내고 `Rejected` · `Failed` 는 오류를 남기고 그 페이지를 멈춥니다(`release` 가 풂).
- 텍스처 구간 갱신(`TextureCache::flushRegionUpdates`, `Texture2D::writeCPUBytes`): 제출만 합니다. `Busy` 면 그 차례의 나머지를 다음 프레임으로 미루고(순서 유지) `Rejected` 는 오류를 남기고 버립니다. 두 함수의 제출 구간은 `ScopedForbidBlockingTransfer` 로 표시합니다.
- 남은 대기: 새 텍스처 업로드(`completePendingLoads` · `updateStreaming` · 동기 `acquire`, U8)와 `RenderThread` 렌더 타깃 읽기(요청이 있을 때만, U9).
- 측정(2026-10-25 통합 M, Release, GPU 독점, 전 = B0 만 든 main `ab3b917cb` · 후 = 이 두 비동기화, 번갈아 3 판 중앙값, 새 한글 음절 400 개 판 `-lang=ko -gv_benchMeshes=100 -gv_benchUiHangulGlyphs=400 -gv_profileFrames=600`): `RT.Canvas.Upload` 최악 DX12 62.9 → 4.2 ms, DX11 93.8 → 1.8 ms, GL 62.5 → 1.0 ms, 프레임 벽시계 DX12 10.9 → 3.4 ms, DX11 12.5 → 3.4 ms, GL 11.9 → 4.1 ms. Vulkan 은 5.4 ms 그대로(U6). 정적 화면(`-gv_uiDemo=1`)과 `perf-compare uiPaintOrder` 는 퍼짐 안이고 `uidemo.scenario.xml` 스크린샷은 전후 픽셀이 같습니다.

**끝난 단위: GL-1(GL 스테이징 링).** 텍스처 업로드(밉 전체, BC 포함)와 구간 갱신은 영속 매핑 PBO 링(`GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT`)에 제출 스레드가 컨텍스트 밖에서 복사하고 컨텍스트를 빌리는 시간은 PBO 바인딩 · 밉마다 `glTexSubImage*` · 펜스 하나뿐입니다(언팩 · 팩 상태는 RAII 가드 `ScopedStagingUnpack` · `ScopedStagingPack` 가 기본값으로 복원). 링보다 큰 업로드는 나누지 않고 링이 비었을 때 일회성 영속 매핑 버퍼로 혼자 들어가며 단독 상한(`gv_rhiStagingSingleMaxMegabytes`) 초과는 설정 이름을 로그로 알리고 `Rejected`, 링이 차 있으면(끝난 펜스를 한 번 모은 뒤에도) `Busy`. GL 4.4 가 없으면 링이 없고 업로드는 모두 `Rejected`이며 클라이언트 메모리 경로는 두지 않습니다(WGL · GLX 가 4.4 미만 컨텍스트를 만들 수는 있으나 초기화가 DSA 4.5 와 `ARB_gl_spirv` 를 요구해 그런 드라이버에서 백엔드가 서는 경우는 사실상 없음 **[코드]**). 장치를 잃으면(`GL_WAIT_FAILED`) 제출 진입이 `isLost` 로 `Rejected` 를 돌려주고 링 할당 결과를 제출 결과로 옮기는 곳은 `RHITransferSerials::classifyStagingAllocation` 하나입니다(nogpu `RHIStagingRingTest.LostDeviceRejectsInsteadOfBusyWhenTheRingIsFull`). 상수 · 구조 · 정점 버퍼 구간 갱신은 링 없이 `glBufferSubData` 그대로입니다([결정 기록 5-5](../decisions/5-5_GraphicsRHI.md)). 확인은 매니페스트 id `rhi-transfer-gl1-staging-ring`.

**끝난 단위: U6(Vulkan 비동기 전송).** 업로드 · 구간 업로드 · 되읽기가 제출 안에서 기다리지 않습니다. 링 둘(`gv_rhiStagingRingMegabytes` · `gv_rhiReadbackRingMegabytes`, 장부 Staging 두 항목)과 장부 `RHIStagingAllocator`. 용량 0 이거나 링을 못 만들면 초기화 실패, 링보다 큰 전송은 링이 비었을 때 일회성 호스트 가시 버퍼로 혼자 받고(단독 상한 초과 `Rejected`, `RHIStagingRingGPUTest.VulkanTransferLargerThanTheRingIsAcceptedAlone`) 찬 링은 `Busy`. 결과 옮기기 · 구간 놓기 · 이유 로그는 DX12 · Vulkan · GL 공용 `RHIStagingRingUtil::settleAllocation` 한 곳, 받지 못한 결과를 제출 결과로 바꾸는 곳은 `RHITransferSubmit::fromUnaccepted` 한 곳입니다. 복사 오프셋은 텍셀 블록 바이트와 4 의 최소공배수로 링 안에서 다시 맞춥니다(R8 작은 밉 · 12 바이트 텍셀이 빈틈없는 소스 배치 규칙을 어김). 기록 · 제출은 `_transferMutex` 로 직렬화하고 제출은 `_queueMutex` 안에서 내며 같은 락 안에서 시리얼을 매깁니다(계약 5). 그래픽스 큐라 소유권 이전이 없고 첫 배리어(`ALL_COMMANDS` → `TRANSFER`)가 앞서 낸 프레임 뒤에, 끝 배리어(`TRANSFER_DST` → `SHADER_READ_ONLY`)가 뒤에 낼 프레임 앞에 둡니다(계약 3). 레코드 `_layout` 은 제출 직후 전송 락 안에서 바꿉니다. 되읽기는 복사 뒤 `HOST` 배리어, `takeTextureReadback` 이 링에서 복사합니다(D24 변환도 그때). 전송 중 파괴한 텍스처의 해제는 `_transferReleaseQueue` 가 전송이 끝난 뒤 다음 프레임 펜스 뒤로 미룹니다(계약 6). 장치 손실은 제출 진입의 `isDeviceLost` 와 펜스의 `VK_ERROR_DEVICE_LOST` 가 표시하고 `beginFrame` 이 첫 펜스 대기에서 손실을 보면 그 프레임을 건너뜁니다. nogpu `RHIStagingRingTest.ReadbackSpansStayUntilTakenAndDiscardedOnesWaitForTheirCopy` · `FakeDeviceReusesRingSpansOnlyAfterTheFencePrefixPasses`. GPU 확인(`rhi-transfer-vk-u6`)은 통합 N 에서 통과했습니다(Release, Debug 검증 레이어 오류 0, UI 예제 프레임 벽시계 Vulkan 5.4 → 2.79 ms, 부하가 있던 PC 의 한 판이라 참고값). M1 의 RT p99 전후는 `rhi-transfer-b0-measure` 가 맡고 `RT.Upload.waitIdle` 은 이제 Vulkan 의 프레임 밖 버퍼 갱신 · 시계 읽기만 잽니다.

**끝난 단위: U5(DX12 비동기 전송).** 업로드 · 구간 업로드 · 되읽기가 제출 안에서 기다리지 않고 텍스처 전송이 프레임 슬롯 복사 리스트를 거치지 않습니다. 업로드 링 하나(`gv_rhiStagingRingMegabytes`, 영속 매핑 업로드 힙 버퍼)와 `RHIStagingAllocator`, 구간 시작 정렬은 `D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT`(512)라 `GetCopyableFootprints` 의 `BaseOffset` 에 그대로 넣습니다. 링보다 큰 한 장은 일회성 업로드 힙 버퍼로 혼자, 찬 링은 `Busy`(`settleAllocation` · `fromUnaccepted` 공용). 전송 작업(`D3D12TransferJob`: 얼로케이터 + DIRECT 커맨드 리스트, 풀)은 전송 펜스를 지난 것만 돌아오므로 프레임 펜스를 기다리며 얼로케이터를 Reset 하던 일(`openUploadSlot` 의 `waitForFenceValue`)이 텍스처 전송에서 사라졌습니다. 제출은 호출 스레드가 링에 복사하고 리스트를 기록한 뒤 `_transferSubmitMutex` 안에서 `ExecuteCommandLists` · `Signal`(전송마다 한 값) · 시리얼 발급을 합니다(락 순서 `_transferSubmitMutex` → 펜스 줄 잎 락). 프레임 제출 락과 따로이고 큐가 free-threaded 라 서로 순서만 갖습니다 — 계약 3 은 `endFrame` 이 프레임 리스트를 모아 한 번에 내는 구조가 만듭니다. 완료는 `pollTransfer` · 링이 찬 제출 · 프레임 끝 · `waitIdleInternal` 이 `retireCompletedTransfers` 로 앞에서부터 걷어 워터마크를 올립니다. 전송 중 파괴한 텍스처의 해제와 끝나기 전에 버린 읽기 버퍼는 `_transferReleaseQueue` 가 전송이 끝날 때까지 쥡니다. 텍스처의 마지막 전송 시리얼은 `max` 로 기록합니다(스레드마다 제출). 장치 손실은 제출 진입과 큐에 내기 직전 재확인이 `Rejected` 로 돌려주고 손실 뒤에는 펜스를 묻지 않아 남은 티켓이 `Failed` 로 남습니다. `_uploadSlotMutex` 는 이제 구조 버퍼 복사 리스트 기록, 프레임 리스트 제출, 프레임 펜스 Signal · 값 올리기와 구조 버퍼 갱신의 `_fenceValue` 읽기 · 복사 실행만 지킵니다([결정 5-5](../decisions/5-5_GraphicsRHI.md) `withNativeContext` 항목, 확인 `rhi-native-context-guard`). 텍스처 상태 추적은 `_resourceStateMutex` 로 읽고 전송 리스트가 같은 상태로 돌아갑니다(계약 3 이 겹침을 막음). GPU 확인은 `rhi-transfer-dx12-u5`(통합이 돌립니다). 남은 것: DX12 되읽기는 링 없이 읽기마다 읽기 힙 버퍼를 만듭니다(`CreateCommittedResource`) — 잦아지면 `RHIStagingAllocator` 로 되읽기 링을 만들고 M1 이 정합니다. GL 의 `allocateReadbackSpan` 도 같은 `settleAllocation` 을 거친 뒤 단독 구간일 때만 `attachSoloBuffer` 를 부릅니다(업로드 쪽 `allocateStagingSpan` 과 같은 모양).

**파일 소유(겹치지 않게).** `Source/Engine/Graphics/RHI/` 아래 폴더는 하나의 작업자만 맡습니다.

| 단위 | 소유 파일 |
|---|---|
| U7 | `Test/EngineTest/Graphics/TestRHIDevice.cpp`, `Test/EngineTest/Renderer/TestRenderPassGPU.cpp`, `Test/EngineTest/Graphics/TestRenderTargetRegistry.cpp` |
| U8 | `Graphics/Texture/` |
| U9 | `Renderer/Canvas/CanvasRenderer.*`, `Renderer/Frame/Capture/FrameCapture.*`, `Renderer/RenderThread.cpp`, `Graphics/Debug/RenderTargetRegistry.*`, `Editor/Common/Backend/EditorUICapture.cpp`, `Editor/Common/Workspace/EditorRenderViews.cpp`, `Editor/Panels/RenderTarget/RenderTargetPanel.cpp` |
| U10 | `Graphics/Material/`, `Engine.cpp`, `Renderer/Scene/GPUSceneBuilder.cpp`, `Renderer/Scene/GPUScene.cpp`, `Renderer/Frame/FrameRenderer.cpp` |
| U11, U12 | `Renderer/Frame/Resource/RenderPSOCache.*`, `Renderer/Frame/FrameRendererPSO.cpp`, `Renderer/Frame/FrameRendererDraw.cpp`, `Renderer/Frame/FrameRenderer.h`, 새 파일 |
| U13 | `Graphics/Texture/TextureCache.cpp`, `RHI/DX11/` 와 `RHI/GL/` 의 bindless 파일 |
| U14 | 호출처 전부(기계적 변경이라 혼자) |

`FrameRenderer.cpp` 는 U10 만 고칩니다(셋업 구간의 `initializePending` 호출과 `ScopedForbidBlockingTransfer` 표시). U8 은 `completePendingLoads` 와 `updateStreaming` 의 구현을 `TextureCache` 안에서 바꾸고 호출 줄은 그대로 둡니다. U11 · U12 는 `warmPinnedMaterialPSOs` 호출 줄(`FrameRenderer.cpp` 817)을 건드리지 않습니다.

**단위별 내용과 검증.** GPU 검증은 통합이 `gpu-verifier`(Shipping, 네 백엔드)로 한 번에 돌리고 항목은 구현하는 단위가 `Test/QA/VerifyBatch.json` 에 더합니다.

- **U7.** `RHIDeviceTest` 계약 테스트: 제출 → `Pending` 또는 `Complete` → 폴링, 프레임 없이 폴링만 도는 경로(진행 보장), 구간 갱신 뒤 업로드된 내용이 보임(순서), 업로드 직후 텍스처 파괴, `Busy` 경로, 장치 손실 뒤 `Failed`(Vulkan 은 `VulkanLostDeviceStopsWaitingOnTheGPU` 와 같은 모양), 읽기를 가져가지 않고 버리는 경로, 끝나지 않은 전송이 있는 텍스처의 bindless 등록 거절. GPU 네 백엔드.
- **U8.** 텍스처 로드를 세 단계로 나눕니다: 워커 읽기 → RT 펌프의 `beginUpload`(텍스처 생성과 `submitTextureUpload`) → 이후 프레임의 `finishUpload`(`pollTransfer` 가 `Complete` 면 bindless 등록과 발행). `Busy` 는 다음 프레임에 다시 제출하고 프레임당 상한은 제출 수에 적용합니다. `updateStreaming` 의 위쪽 밉 교체도 같은 세 단계. 동기 `acquire` 와 `wait()` 는 블로킹 헬퍼로 `beginUpload → 대기 → finishUpload` 를 합니다(B0 의 `Texture2D::createTextureWithMips` · `loadFromCPUBytes` 의 `uploadTexture2DAndWait` 를 이 흐름으로). 걸림돌: `AssetLoadBlock` 의 완료 단계가 한 번에 끝나는 모양이라 펌프(RT)가 제출한 뒤 다른 스레드의 `wait()` 가 같은 블록의 완료를 집지 않게 하는 "업로드 중" 단계를 `AssetHandle` 에 더해야 합니다(`Resource/` 도 이 단위 소유에 더함). 검증: `RenderPassGPUTest.AsyncTextureStreamsTopMipsAfterBase`, `StreamingUploadsTheTextureNotedAsLargestOnScreenFirst`, `AssetStreamingTest.*`, 텍스처가 있는 시나리오(`kaykitcharacters`) 네 백엔드, 스크린샷으로 회색 → 진짜 텍스처.
- **U9.** 글리프 아틀라스는 끝났습니다(`rhi-transfer-u9-canvas-gpu`). 남은 것: `RenderThread::readRequestedRenderTarget` 가 요청 프레임에 읽지 않고 읽기 티켓(`PendingReadback`)을 레지스트리에 두었다가 이후 프레임에 `take`. 스크린샷과 에디터 캡처는 블로킹 헬퍼를 그대로 씁니다. 검증: `engine/automation/uidemo.scenario.xml` 네 백엔드, `-gv_screenshot` 캡처 경로, `RenderTargetRegistry` 읽기 테스트, 에디터 `uiaudit.scenario.xml`.
- **U10.** 2.3 의 여섯 항목. 검증: `material-cache-gpu-test`(매니페스트 id), 새 시나리오 `game/empty/automation/materialfirstuse.scenario.xml`(처음 보는 머티리얼의 오브젝트를 스폰하고 프레임마다 캡처해 몇 프레임 안에 나타나는지, Error 로그 없음), M1 의 `GT.Material.waitIdle` 호출 수 0.
- **U11.** 2.4 의 상태 데이터, `psoForBatch` 구분, `RT.PSO.pendingBatches` 카운터, `findMaterialPSO` 주석 수정. 생성이 여전히 RT 에서 동기라 항목이 한 프레임 안에 `Absent` → `Ready` 로 가 화면은 지금과 같아야 합니다. 상태 전이는 nogpu 테스트, GPU 는 기존 골든만 봅니다.
- **U12.** 빌더 스레드, 우선순위, 완료 목록 합치기, GL 시간 예산, `-gv_psoAsync`(0 이면 U11 의 동기 생성, A/B 측정용으로 `gv_gpuUploadQueue` 와 같은 선례). 검증: 새 시나리오 `game/empty/automation/psoasync.scenario.xml` 네 백엔드 — 뷰 모드를 번갈아 바꾸고 새 머티리얼을 스폰한 뒤 몇 프레임 안에 동기 생성과 같은 이미지에 이르는지, Error 로그 없음.
- **U13.** `TextureCache::resizeRenderTarget` 의 `waitIdle`(W6)을 지웁니다(옛 텍스처를 지연 해제로 넘기고 새 핸들 · 인덱스를 받음). 전제: DX11 · GL 의 bindless 인덱스 반환이 GPU 완료 뒤로 미뤄지는 것(DX12 는 이미 `deferFreeBindlessIndex`, DX11 은 `releaseFreeListIndex` 로 즉시 돌려줌 **[코드]**). 핫 리로드의 `waitIdle` 은 도구 경로라 남깁니다. 검증: 에디터에서 캔버스 타깃 크기를 바꾸는 시나리오(기존 `engine/automation/editor` 중 있는지 확인 **[확인 전]**)와 `hotreload.scenario.xml`.
- **U14.** `IRHIDevice::waitIdle` 에 이유 열거형(`Shutdown`, `BackendSwap`, `Resize`, `HotReload`, `SceneActivation`, `Capture`, `Tool`)을 필수 인자로 더합니다(호출처는 W7 의 20곳 안팎, U13 뒤에 남은 것). 이유로 이름을 가르면 프레임 경로의 새 호출이 눈에 띄고 린트 게이트가 프레임 경로 폴더(`Renderer/Frame/`, `Graphics/Material/`, `Graphics/Texture/` 의 비 reload 함수)에서 `Tool` 이 아닌 호출을 막을 수 있습니다(열거형으로 못 막는 함수 단위 허용 틈에만 게이트). 기계적 변경이라 혼자 하고 커밋만 단계별로 나눕니다.

**측정.** M0 은 착수 전입니다. `wt/profscopes` 병합 뒤 Release 에서 혼자(GPU 독점) 네 스코프의 호출 수와 ms 분포, RT · GT 프레임의 p99 와 최악 프레임을 잽니다. 시나리오는 게임 시작, 씬 로드, 새 머티리얼 스폰, 뷰 모드 전환, 글자 많은 UI 변경이고 설정은 LowEnd 포함. M0 에서 W2 의 합이 작거나 Vulkan 이 아닌 백엔드에서 W1 · W3 이 작으면 U10 · U12 의 우선순위를 낮춥니다. M1 은 B0 병합 뒤와 마지막 단위 뒤에 같은 시나리오로 다시 잽니다. 결과는 [검증과 측정](../08_Verification.md) 에 적습니다.

**B0 측정 질문 넷**(매니페스트 id `rhi-transfer-b0-measure`, 아직 한 명령으로 안 돕니다 — 세는 스코프가 없음).
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
| GL 의 `pollTransfer` 가 컨텍스트를 기다리지 않는다 | RT 가 컨텍스트를 쥔 동안 그 스레드의 폴링은 `Pending` | 제출이 `glFlush` 로 진행시키고 컨텍스트가 비면 폴링이 한 번 집어 펜스를 보며, RT 가 쥔 동안은 `endFrame` 이 워터마크를 올린다. 시간 초과가 틈을 오류로 드러낸다 |
| Vulkan 의 완료 순서가 제출 순서와 다를 수 있다 | 워터마크가 앞서 올라가면 미완 자원을 발행 | `RHITransferFenceQueue` 가 연속 구간만 올린다(nogpu `FakeDeviceReusesRingSpansOnlyAfterTheFencePrefixPasses`). GPU 위의 지연된 앞 제출은 U7 |
| Vulkan · DX12 전송이 RT 기록과 텍스처 상태를 두고 겹친다 | 레이아웃 · 리소스 상태 불일치(검증 레이어 오류, 배리어 `StateBefore` 어긋남) | 계약 3: 프레임이 상태를 바꾸는 텍스처(렌더 타깃)의 전송은 RT 기록 밖에서만. Vulkan 은 `_imageLayoutMutex` · `_transferMutex`, DX12 는 `_resourceStateMutex`(기록 밖이면 추적 상태 = 마지막 프레임 뒤의 GPU 상태, 비 렌더 타깃은 늘 COMMON). 확인은 `rhi-transfer-vk-u6` · `rhi-transfer-dx12-u5` 의 디버그 레이어 오류 0 |
| 스테이징 메모리 | 큰 텍스처가 몰리면 `Busy` 가 길어지거나 메모리가 튄다(GL · Vulkan 최고점 max(링, 한 장)) | `gv_rhiStagingRingMegabytes` · `gv_rhiStagingSingleMaxMegabytes`, 장부 Staging 항목, M1 이 조정 |
| `Material` GPU 필드의 스레드 간 읽기 | 쓰는 중인 인덱스를 읽는 경쟁 | `GpuState` 를 acquire 로 읽은 뒤에만 읽는 규칙, Debug 단언 |
| PSO 빌더의 스레드 안전성 | 백엔드나 셰이더 컴파일러의 잠금 누락 | 네 백엔드 점검이 끝나기 전 U12 를 시작하지 않는다. `-gv_psoAsync=0` 으로 동기 생성과 비교 |
| 보이는 변화(수 프레임 늦은 등장, 뷰 모드 전환 직후 빈 배치) | 사용자가 결함으로 느낄 수 있음 | 건너뛴 수 카운터로 드러내고 O1 로 디더를 맞춘다 |
| Vulkan 이 아닌 백엔드는 W2 가 이미 대기가 없다 | 이득이 Vulkan 에 몰림 | M0 이 먼저 측정한다. 이득이 작으면 U10 · U12 를 미룬다 |

(해소: DX12 폴링과 프레임 제출 락 `_uploadSlotMutex` 의 경합은 U5 에서 텍스처 전송이 제출 때 이미 큐에 나가 폴링이 락 없이 펜스 값 하나만 보게 되어 사라졌습니다.)

**되돌리기.** U5 ~ U14 는 B0 가 선 뒤 각각 커밋 단위로 revert 합니다. 번들 B0 는 병합 커밋 하나를 revert 합니다(`git revert -m 1`). 옛 함수와 새 함수가 공존하는 단계가 없어 중간 상태로 되돌리는 방법은 없습니다.

## 6. 상용 엔진 비교

언리얼 RHI 는 업로드를 `FRHICommandList` 명령으로 두고 텍스처 스트리밍은 `RHIAsyncCreateTexture2D` 후 `FRHIGPUFence` 를 폴링하며 되읽기는 `FRHIGPUTextureReadback::IsReady` 를 폴링합니다. 유니티는 `AsyncGPUReadback.Request` 의 `done` 을 폴링하고 업로드는 Async Upload Buffer 링 파이프라인, Godot `RenderingDevice` 는 스테이징 블록 전송과 `texture_get_data_async` 콜백입니다. 모두 "제출 → 펜스나 폴링 → 사용"으로 이 설계와 같은 방향이고, 다른 점은 콜백 대신 티켓 폴링(소비자가 RT 셋업 구간에서만 일해야 해서)과 1단계에서 전용 전송 큐를 쓰지 않는 것입니다. 이 절은 기억에 기댔고 API 가용 범위 · 전용 복사 큐 사용 여부 · 스테이징 블록 크기는 **[확인 전]** 이며 결정 근거는 1절의 코드 읽기입니다.

## 7. 정한 것과 확인하지 못한 것

정한 것(사용자 결정이 필요한 항목은 없습니다):
- ABI 34 에서 옛 동기 함수 세 개를 지우고 shim 을 두지 않는다. 완료는 콜백이 아니라 티켓 폴링이다(2.2). B0 는 쪼개지 않는 골격 번들 하나로 병합하고 U5 ~ U14 는 그 뒤 단위별로 병합한다(3절).
- 이 결정들은 `docs/decisions/` 의 영역 파일이 없어 이 문서와 B0 커밋 메시지에 둔다(통합이 옮길 곳을 정한다). 1단계는 그래픽스 큐 하나이고 전용 전송 큐는 측정 뒤에 판단한다.
- GL 업로드는 영속 매핑 PBO 링 하나로만 올린다(GL-1, 4.4 없으면 `Rejected`, 클라이언트 메모리 경로 없음). 글리프 아틀라스는 기다리지 않는다(새 페이지는 첫 업로드 `Complete` 프레임에 등록, 등록된 페이지의 구간 갱신은 제출만). 새 머티리얼의 GPU 초기화는 RT 셋업 구간에서 하고 준비 전에는 그리지 않으며 PSO 는 네 가지 상태를 데이터로 두고 생성 스레드는 하나다(병렬 생성 없음). 상용 엔진(언리얼 OpenGL RHI · Slate 폰트 아틀라스 · 유니티 Async Upload Buffer)의 유사 구조는 기억 기반 **[확인 전]**.

확인하지 못한 것: `wt/profscopes` 스코프가 감싸는 줄(W1 ~ W4 와 스코프의 대응은 내 읽기), 네 백엔드와 셰이더 컴파일러의 워커 안전성(2.4), DX11 · GL 에서 RT 가 직접 인덱스를 해제하는 경로의 지연 보장(2.3), `MaterialInstance::updateRHI` 와 텍스처 교체 때 상수 버퍼를 쓰는 스레드(2.3), 타임라인 세마포어의 Vulkan 버전 요구(2.2), 링 용량 64 MiB 와 프레임당 상한의 적정값(M1 이 정함), 상용 엔진의 API 세부(6절).
