# RHI 비동기 업로드 · 머티리얼 초기화 · PSO 준비 상태

> 상태: 구현중 · 선행: 없음 · 영역: graphics · 옛번호: 없음 · 진행: 6/15

> 질문: 게임 스레드와 렌더 스레드가 GPU 완료를 기다리는 지점을 어떤 계약으로 없애고, 무엇부터 바꾸는가?
> 독자: RHI 백엔드와 렌더러를 고치는 작업자, 통합 담당.
> 결론: 업로드와 되읽기는 "제출 → 티켓 → 폴링" 계약 하나로 바꾸고(RHI ABI 버전을 한 번 높여 옛 동기 함수 세 개를 지운다), 새 머티리얼과 PSO 는 "준비 전 / 준비됨 / 실패" 상태를 데이터로 두어 준비 전에는 그리지 않는다. 프레임 경로에는 `waitIdle` 을 새로 만들지 않는다. ABI 변경이 없는 Vulkan 대기 좁히기(U2)는 B0 에 들어갔다.

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
- W2 에서 RT 가 한 프레임에 부르는 업로드는 `completePendingLoads` 8개 이하(`kMaxUploadsPerFrame`), `updateStreaming` 2개 이하, 구간 갱신(`flushRegionUpdates`), 글리프 아틀라스 페이지(`CanvasRenderer::uploadAtlasPage`)입니다. Vulkan 은 이 호출마다 큐를 비웁니다 **[코드]**.
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

`IRHIResourceFactory` 의 동기 함수 세 개(`uploadTexture2D`, `uploadTexture2DRegion`, `readbackTexture2D`)를 지우고 아래 여섯 개로 바꿉니다. 시그니처는 여기서 고정했고 B0 안에서 design-reviewer 검토를 반영했습니다(ABI 34 유지).

| 함수 | 뜻 | 대체하는 것 |
|---|---|---|
| `submitTextureUpload( texture, desc )` | 새 텍스처의 밉 체인 전체를 제출한다 | `uploadTexture2D` |
| `submitTextureRegionUpload( texture, desc )` | 쓰고 있는 텍스처의 사각형 구간을 제출한다 | `uploadTexture2DRegion` |
| `submitTextureReadback( texture, mip, slice, outLayout )` | 읽기를 요청한다. 행 배치는 바로 채워 준다 | `readbackTexture2D` |
| `pollTransfer( ticket )` | `Pending`, `Complete`, `Failed` 를 값싸게 돌려준다 | 없음 |
| `takeTextureReadback( ticket, outBytes )` | 완료된 읽기 결과를 한 번 가져간다 | 없음 |
| `discardTextureReadback( ticket )` | 가져가지 않을 읽기 결과를 버린다(`Pending` 이어도 된다) | 없음 |

제출 함수는 `RHITransferSubmit` 을 돌려줍니다. 안에는 `RHISubmitResult`(`Accepted`, `Busy`, `Rejected`)와 `RHITransferTicket`(단조 증가하는 64비트 시리얼과 장치 세대, 시리얼 0 은 무효)이 있고 `[[nodiscard]]` 입니다. 티켓은 구조체라 `uint64` 를 티켓 자리에 넘기는 실수는 컴파일에서 막히지만 멤버가 공개라 값을 직접 짓는 코드까지 막지는 못합니다. 그 틈은 백엔드가 막습니다 — 시리얼 0, 다른 장치 세대, 발급한 적 없는 시리얼은 `pollTransfer` 가 오류를 남기고 `Failed` 로 돌려줍니다(`RHITransferSerials::classify`). `accepted( 0, … )` 는 Debug 단언입니다. 장치 세대(`allocateRHIDeviceGeneration`)는 백엔드 인스턴스마다 하나라 백엔드를 바꾼 뒤의 옛 티켓이 새 장치에서 `Complete` 로 보이지 않습니다.

**계약** (정본은 `IRHIResourceFactory.h` 의 전송 절 머리말입니다)

1. 제출 함수는 반환하기 전에 소스 메모리를 복사합니다. 호출자는 바로 버퍼를 재사용합니다.
2. 제출 함수는 GPU 완료를 기다리지 않습니다. 결과는 셋입니다. `Accepted` 는 티켓이 유효합니다. `Busy` 는 **다시 시도하면 될 수 있을 때만** 씁니다(스테이징 링이나 단독 버퍼 총량이 지금 차 있지만 GPU 가 진행하면 비는 경우). 호출자가 다음 프레임에 다시 제출합니다. `Rejected` 는 다시 시도해도 안 되는 요청입니다. 검사에 걸린 것, 링이나 단독 버퍼 총량 상한보다 **큰 요청**, 장치 손실이 여기에 들고 이유를 로그로 남깁니다. 장치를 잃었는지는 `RHITransferSerials` 의 손실 표시 하나가 쥐고(백엔드는 따로 플래그를 두지 않는다), 네 백엔드의 제출 함수는 진입에서 `isLost` 를 보고 `Rejected` 를 돌려줍니다(GPU 테스트 `RHIDeviceTest.LostDeviceRejectsTransferSubmitsAtEntry`). DX11 은 `GetDeviceRemovedReason` 을 `isDeviceRemoved` 한 함수로 모아 묻고 Present · `GetData` 의 실패도 거기서 표시합니다.
3. 순서는 양방향입니다. 제출한 전송은 **앞서 제출된** 프레임(`endFrame` 이 이미 반환한 프레임) 뒤에 실행되고 **뒤에 제출될** 모든 프레임 커맨드 앞에 실행됩니다. 진행 중인 프레임 안에서 `executeCommandList` 로 넘긴 리스트는 앞서 제출된 것에 들지 않습니다(DX12 와 Vulkan 은 그 리스트를 `endFrame` 까지 모아 한 번에 내므로 전송이 그 앞에 끼고, DX11 과 GL 은 곧바로 실행합니다). 지금 DX12 가 복사 리스트를 프레임 리스트 앞에 끼우는 것과 같은 의미입니다 **[코드]**. 같은 텍스처에 대한 제출끼리는 제출 순서대로 실행되고, 되읽기는 요청 시점까지 제출된 프레임(직전 프레임까지)의 쓰기를 봅니다. 아직 제출하지 않고 기록 중인 리스트가 어느 내용을 보는지는 백엔드마다 달라 정해져 있지 않으므로 호출자는 전송을 그리기 기록 전에 제출합니다. 지금 RT 가 셋업 구간에서 하는 것과 같습니다. **렌더 타깃처럼 프레임 커맨드가 상태(레이아웃 · 리소스 상태)를 바꾸는 텍스처의 전송은 RT 기록 밖 구간에서만** 제출합니다.
4. **스레드.** 제출 함수는 어느 스레드에서나 부를 수 있습니다. DX12 와 Vulkan 은 자기 락으로 직렬화합니다. DX11 은 즉시 컨텍스트의 런타임 다중 스레드 보호(`ID3D11Multithread::SetMultithreadProtected`)를 켜고, 제출 · 폴링 · `waitIdle` 은 같은 임계 구역을 잡는 가드(`D3D11ImmediateContext::lock()`) 안에서만 즉시 컨텍스트를 씁니다. 렌더 스레드의 프레임 스트림 호출도 그 임계 구역을 지나므로 다른 스레드의 제출 사이에 끼지 않습니다. GL 은 제출 함수가 `ScopedOpenGLContext` 로 컨텍스트를 빌립니다(U8 의 동기 `acquire` 가 비 RT 스레드에서 부르기 때문입니다). 텍스처 업로드는 스테이징 링 복사를 컨텍스트 밖에서 하고, 빌리는 시간은 PBO 바인딩 · 텍스처 명령 · 펜스뿐입니다(GL-1). RT 가 컨텍스트를 쥐고 있으면 프레임 끝에 놓을 때까지 기다리므로 GL 의 비 RT 제출은 프레임 경로 밖에서만 씁니다. `pollTransfer` 도 어느 스레드에서나 부릅니다. **GL 의 `pollTransfer` 는 컨텍스트를 기다리며 빌리지 않습니다.** 원자 워터마크를 읽고, 이 스레드가 컨텍스트를 이미 쥐었거나 컨텍스트가 비어 있으면 한 번 집어 펜스를 본 뒤 놓습니다(`OpenGLRHIDevice::tryRetireFinishedTransfers`). 다른 스레드가 쥐고 있으면 그쪽(RT 의 `endFrame`, 제출 스레드)이 올린 워터마크를 다음 폴링이 봅니다 — 그래서 RT 가 프레임을 돌리지 않는 동안(동기 로드)에도 GL 의 비 RT 대기가 진행합니다.
5. **시리얼과 장치 세대.** 시리얼은 큐에 낸 순서대로 같은 락 안에서 매깁니다(`RHITransferSerials::issue`). 락 밖에서 매기면 시리얼 순서가 큐 실행 순서와 어긋나 워터마크가 끝나지 않은 제출을 덮습니다. 티켓에는 장치 세대가 실려 있어 백엔드를 바꾼 뒤의 옛 티켓은 새 장치에서 `Failed` 입니다.
6. **파괴.** 전송 중인 텍스처를 파괴하면 백엔드가 GPU 자원 해제를 전송이 끝난 뒤로 미룹니다(해제 큐).
7. **진행 보장.** 프레임을 돌리지 않아도 `pollTransfer` 를 되풀이하면 결국 `Complete` 나 `Failed` 가 됩니다. 아직 GPU 에 내보내지 않은 일(DX12 의 열린 복사 리스트, GL 의 flush 안 된 명령)은 제출이나 폴링이 내보냅니다.
8. 티켓의 `Complete` 는 "완료 워터마크 이상"으로 판정합니다. 워터마크는 연속으로 끝난 시리얼의 끝입니다. 그래서 `Complete` 를 본 뒤 같은 티켓을 다시 물어도 `Complete` 이고, 티켓을 해제하는 함수가 필요 없습니다. 큐 안에서 완료 순서가 제출 순서와 다를 수 있는 백엔드(Vulkan)는 연속 구간만 워터마크로 올립니다(`RHITransferFenceQueue` 가 앞에서부터만 올립니다).
9. `Failed` 는 장치 손실처럼 끝나지 않을 일이거나 잘못된 티켓입니다. 오류를 로그로 남기고 `Failed` 를 돌려주며 대기하지 않습니다.
10. **소비자는 `Complete` 를 본 뒤에야 텍스처를 bindless 에 등록합니다.** 이것을 주석이 아니라 코드로 막습니다: 백엔드 텍스처 레코드가 마지막 전송 시리얼을 들고, 등록 시 그 시리얼이 워터마크 아래가 아니면 오류를 남기고 `kInvalidDescriptorIndex` 를 돌려줍니다. 로드 중에 보이는 회색 텍스처는 지금처럼 정해진 데이터입니다([AsyncResourceLoading](AsyncResourceLoading.md) 2절).

**동기 변형은 ABI 밖의 헬퍼입니다.** `RHITransferUtil::uploadTexture2DAndWait`, `uploadTexture2DRegionAndWait`, `readbackTexture2DAndWait` 를 Engine 에 둡니다(백엔드가 아니라 Engine 코드라 vtable 에 없습니다). 제출한 뒤 `pollTransfer` 를 되풀이하고, 처음 32 번은 `yield`, 그 뒤로는 200 µs 씩 자며 기다립니다. 상한은 DX11 `waitIdle` 의 상한(10 초)과 같게 두며 넘으면 오류입니다. 시간 초과는 `RHITransferWaitResult::TimedOut` 이라 `RHITransferStatus` 의 뜻을 바꿔 쓰지 않습니다(`submitWhileBusy` 는 bool 을 돌려줍니다). 읽기가 시간 초과나 `Failed` 로 끝나면 `RHITransferUtil::PendingReadback`(읽기 티켓의 소유자, 소멸자가 `discardTextureReadback`)이 결과를 버려 백엔드의 스테이징이 새지 않습니다. 사용처는 테스트, 도구, 스크린샷, 에디터 캡처, 동기 텍스처 `acquire` 입니다. RT 프레임 셋업 구간에서는 쓰지 않으며, 그 구간을 `RHITransferUtil::ScopedForbidBlockingTransfer` 로 표시하면 `...AndWait` 가 Debug 에서 단언합니다(표시를 거는 일은 셋업 구간을 소유한 U10 의 몫 — `FrameRenderer::executePacket` 시작부터 `setParallelRecording( true )` 전까지). 백엔드에 두 번째 경로가 생기지 않는 것이 핵심입니다.

**콜백이 아니라 폴링인 이유.** `enqueueGPURelease` 콜백은 완료를 확인하는 스레드에서 불립니다. bindless 등록은 RT 셋업 구간에서만 할 수 있으므로 콜백으로 받아도 결국 RT 의 큐에 넣고 다시 읽게 됩니다. 폴링은 RT 가 셋업 구간에서 직접 읽으므로 스레드 경계가 한 번 줄어듭니다. 해제용 `enqueueGPURelease` 는 그대로 둡니다. **버퍼 되읽기는 반대로 콜백을 씁니다**(`createReadbackBuffer` + `readReadbackBuffer`, 결과를 읽는 쪽이 프레임 완료 시점의 콜백 안에서 곧바로 복사해 가고 bindless 등록이 없다). 기준은 두 가지입니다: 결과를 받는 쪽이 RT 셋업 구간에서 상태(bindless 인덱스)를 바꿔야 하면 티켓과 폴링, 프레임에 묶인 읽기 버퍼를 완료 콜백에서 한 번 비우면 끝나면 콜백입니다. 텍스처 업로드와 텍스처 되읽기는 앞의 경우(등록, 다음 프레임 폴링)라 티켓입니다.

**ABI 결정.** `kRHIModuleAbiVersion` 을 33 에서 34 로 높이고 옛 가상 함수 세 개를 지웁니다. 백엔드 DLL 이 엔진과 같은 vtable 을 봐야 로드되므로(`RHIModuleAbi.h`) 이 변경은 한 번에 들어가야 하고 백엔드마다 쪼갤 수 없습니다. 옛 함수를 남기고 새 함수를 그 위에서 구현하는 기본 구현(shim)을 두는 안은 기각합니다. 같은 일을 하는 경로가 둘 남아 폴백 금지 원칙과 부딪히고, 호출자가 어느 쪽을 쓸지 매번 골라야 하기 때문입니다.

**스테이징 링.** 장치마다 고정 용량의 영속 매핑 업로드 링을 둡니다. 구간은 티켓 시리얼로 표시하고 워터마크가 지나면 회수합니다. 장부는 `RHIStagingAllocator`(`RHI/Support`) 하나이고 백엔드는 링 메모리만 쥡니다. 회수는 구간마다 합니다 — 시리얼을 붙인 구간은 그 시리얼이 워터마크(연속으로 끝난 시리얼의 끝)를 지나면 할당 순서와 무관하게 돌아오고, 놓은 구간(제출하지 못한 업로드, 가져간 되읽기)은 바로 돌아옵니다. 빈 자리는 `SpanAllocator`(정렬 단위 한 칸, 시리얼을 번호로 쓰는 `freeDeferred`)가 쥐므로 가져가지 않은 되읽기 하나는 자기 바이트만 막고 링 맨 앞을 막지 않습니다. 규칙은 GPU 없이 `RHIStagingRingTest` 가 봅니다. GL(GL-1)과 Vulkan(U6, 되읽기 링 포함)이 쓰고 U5 가 같은 장부를 씁니다. 용량은 `gv_rhiStagingRingMegabytes` 가 `RHISwapChainDesc::_stagingRingBytes` 로 넘기고, 0 이하면 링을 쓰는 백엔드가 초기화를 거절합니다. 시작 용량은 64 MiB 로 두고 `RHIMemoryLedger` 의 Staging 항목으로 보이게 합니다. 이 값은 측정으로 정한 것이 아니라 4096² BC7 텍스처 한 장(밉 포함 약 21 MiB)을 몇 장 동시에 담는 크기로 잡은 추정이며 M1 이 조정합니다. 링보다 큰 요청은 단독 스테이징 버퍼를 만들고 완료 뒤 해제하며, 그 총량에 상한을 둡니다. 총량 상한이 차 있어 지금은 못 받지만 GPU 가 진행하면 비는 요청은 `Busy` 이고, **링이나 단독 버퍼 총량 상한 자체보다 큰 요청은 영원히 못 받으므로 `Rejected`** 입니다.

**전송 큐.** 1단계는 지금처럼 그래픽스 큐 하나입니다. 전용 복사 큐(Vulkan 의 전송 큐 패밀리, DX12 의 COPY 큐)는 큐 소유권 이전, 레이아웃 전이, 큐 사이 동기화가 필요하고 이득은 큰 텍스처를 스트리밍하는 중의 프레임 시간뿐이라 측정 없이 하지 않습니다. 같은 큐에서는 제출 순서가 곧 실행 순서이므로 `Complete` 이전에 사용해도 GPU 는 안전합니다. 그래도 소비자 규칙을 `Complete` 로 두는 것은 나중에 전용 큐를 도입할 때 규칙을 바꾸지 않기 위해서입니다.

**백엔드별 구현.**

| 백엔드 | 지금 | 새 계약 구현 | 난이도 |
|---|---|---|---|
| DX11 | `UpdateSubresource`(즉시 컨텍스트, 잠금) | `gv_rhiDirectUploadMaxKilobytes`(256 KiB) 이하는 같은 호출, 그보다 크면 장치가 초기 데이터로 만든 `STAGING` 텍스처(잠금 밖의 소스 복사)를 잠금 안에서 `CopySubresourceRegion`. 티켓마다 이벤트 쿼리 하나(풀)이고 `pollTransfer` 가 `GetData`. 스테이징은 전송 큐 항목이 쥐어 워터마크가 지나면 놓고, 합은 `gv_rhiStagingRingMegabytes` 상한(차면 `Busy`, 한 장이 넘으면 예산 전부를 잡고 혼자 들어간다). 되읽기는 `Map` · `Unmap` 에만 잠금을 잡고 행 복사는 잠금 밖. 링은 없다 — D3D11 에는 버퍼 → 텍스처 복사가 없다 | 낮음. 큰 업로드의 소스 복사가 즉시 컨텍스트 잠금 밖으로 나갔다(측정 전, 확인은 매니페스트 id `rhi-transfer-dx11-staged-upload`) |
| GL | 영속 매핑 PBO 링(`ARB_buffer_storage`, GL 4.4)에 컨텍스트 밖에서 복사하고, 컨텍스트를 빌려 PBO 오프셋으로 `glTexSubImage*` · `glCompressedTexSubImage*` | 업로드 뒤 `glFenceSync` 와 `glFlush`, 링 구간은 그 시리얼이 워터마크를 지나면 돌아온다. `pollTransfer` 는 컨텍스트를 기다리지 않고 원자 워터마크를 읽으며, 컨텍스트를 쥐었거나 비어 있을 때만 `glClientWaitSync( 0 )` 으로 워터마크를 올린다. GL 4.4 가 없으면 링이 없고(`RHICapabilities::_bPersistentStagingRing` 거짓) 업로드는 모두 `Rejected` | 끝남(GL-1). 비 RT 스레드의 제출은 가능하나 컨텍스트 점유 경합(`GPUUploadQueue.cpp` 주석)이 있어 프레임 경로 밖에서만. 텍스처 되읽기는 영속 매핑 PBO 되읽기 링(`GL_MAP_READ_BIT`)에 `GL_PIXEL_PACK_BUFFER` 오프셋으로 `glGetTextureSubImage` 와 펜스만 내고, `takeTextureReadback` 이 펜스가 지난 뒤 컨텍스트 밖에서 복사한다. 구간은 가져가거나 버린 읽기의 펜스가 지나면 돌아오며, 링보다 큰 읽기는 업로드와 같이 링이 빌 때 일회성 버퍼로 혼자 받고 단독 상한을 넘으면 `Rejected`, 가져가지 않은 읽기로 차 있으면 `Busy`, GL 4.4 가 없으면 `_bPersistentReadbackRing` 거짓에 모두 `Rejected`(`gv_rhiReadbackRingMegabytes`, 매니페스트 id `rhi-transfer-gl-readback-ring`) |
| DX12 | 프레임 슬롯의 업로드 힙과 복사 리스트가 프레임 리스트 앞에 끼는 구조, 얼로케이터 펜스 대기 | 전용 업로드 링, 복사 리스트와 얼로케이터 풀, 전용 `ID3D12Fence`. 제출은 즉시 `ExecuteCommandLists` 하고 `Signal` | 중간. `_uploadSlotMutex` 가 업로드와 프레임 제출에 같이 걸려 있어 범위를 줄여야 하고, 텍스처 상태 추적 갱신과 RT 기록이 겹치는지 점검해야 한다 |
| Vulkan | 영속 매핑 호스트 가시 · 일관 링 둘(업로드 · 되읽기, 각각 `gv_rhiStagingRingMegabytes`)에 전송 락 밖에서 복사하고, 전송 작업(`VulkanTransferJob`: 전용 풀의 커맨드 버퍼 + 펜스, 재사용)에 기록해 그래픽스 큐에 낸다 | 제출은 기다리지 않는다. 시리얼은 큐 락 안에서 `RHITransferFenceQueue::enqueue` 가 매기고, `pollTransfer` · 링이 찬 제출 · `beginFrame` 이 `vkGetFenceStatus` 로 앞에서부터 걷어 워터마크를 올린다. 업로드 구간은 시리얼로, 되읽기 구간은 가져갈 때 돌아온다(끝나기 전에 버리면 시리얼을 붙인다). 링보다 큰 요청은 `Rejected`, 찬 링은 `Busy` | 끝남(U6). 같은 큐라 큐 소유권 이전이 없다. 레이아웃은 전송 락 안에서 제출 직후 기록한다. 전송 중 파괴한 텍스처의 해제는 시리얼 키 해제 큐(`_transferReleaseQueue`)가 미룬다. 프레임 밖 구조 버퍼 갱신과 GPU 시계 읽기는 여전히 `VulkanOneShotCommands` 로 기다린다 |

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

**빌더의 안전성은 확인하지 못했습니다 [확인 전].** 네 백엔드의 `createPipelineState` 를 워커에서 불러도 되는지(PSO 핸들 테이블 잠금, Vulkan 파이프라인 캐시의 외부 동기화), Dev 빌드의 런타임 셰이더 컴파일(`ShaderCache::getOrCompile`)이 워커에서 안전한지 점검해야 합니다. 이 점검은 U5 와 U6 이 자기 백엔드에서 하고 결과를 여기에 적으며, **DX11 과 GL 은 B0 에서 하지 않았으므로** U12 가 시작하기 전에 같이 해야 합니다. U12 는 그 결과를 읽고 시작합니다.

**Vulkan 점검 결과(U6, 코드 읽기만) — 지금은 워커에서 안전하지 않습니다 [코드].** `vkCreateGraphicsPipelines` · `vkCreateComputePipelines` 의 `_pipelineCache` 는 `EXTERNALLY_SYNCHRONIZED` 없이 만들어 드라이버가 동기화하므로 문제가 아닙니다. 막는 것은 둘입니다. ① `VulkanRHIRenderPassCache::ensurePipelineRenderPass` 의 `_mapPipelineRenderPass` 는 락 없이 찾고 넣습니다(같은 캐시의 합성 프레임버퍼는 `_compositeMutex` 가 지킨다). ② `_pipelineStates`(`RHIHandleTable`)의 `insert` · `take` 에 락이 없습니다. U12 는 이 둘에 락을 두거나 생성 결과의 등록을 RT 로 넘긴 뒤 시작합니다. Dev 의 `ShaderCache::getOrCompile` 은 보지 않았습니다.

### 2.5 열린 문제

- **O1. 등장 디더와 준비 시점.** 등장 디더는 게임 스레드가 비율을 높여 갑니다. 머티리얼이나 PSO 때문에 RT 가 배치를 건너뛰면 디더가 이미 진행된 채로 갑자기 나타납니다. 권고안은 게임 스레드가 준비 여부(`Material::GpuState`, PSO 는 퍼뮤테이션 해시 단위의 원자 조회)를 읽고 `Ready` 가 된 프레임에 `beginAppearance` 를 시작하는 것입니다. PSO 쪽 조회 API 는 U12 가 정합니다.
- **O2. 링 용량과 `Busy` 의 빈도.** 64 MiB 와 프레임당 상한은 추정입니다. M1 이 `Busy` 횟수를 보고 정합니다.

## 3. 단위

| 단위 | 내용 | 선행 | GPU |
|---|---|---|---|
| U5 | DX12 최종 구현: 전용 업로드 링, 복사 리스트와 얼로케이터 풀(B0 는 슬롯 모델 + 전용 전송 펜스) | B0 | 예 |
| U6 | Vulkan 비동기화: 업로드 · 되읽기 링, 제출 뒤에도 사는 전송 작업 객체, 연속 구간 워터마크 — 코드 끝, 매니페스트 id `rhi-transfer-vk-u6` | B0 | 예 |
| U7 | 계약 GPU 테스트(옛 호출 이전은 B0 가 했다) | B0 | 예 |
| U8 | 새 텍스처 업로드의 세 단계(B0 는 `uploadTexture2DAndWait`, 구간 갱신 몫은 끝남) | B0 | 예 |
| U9 | 렌더 타깃 읽기 티켓(B0 는 `readbackTexture2DAndWait`, 글리프 아틀라스 몫은 끝남) | B0 | 예 |
| U10 | 머티리얼 GPU 상태와 프레임 경계 초기화 | U8 | 예 |
| U11 | PSO 상태 데이터와 드로우 규칙 | 없음 | 예 |
| U12 | PSO 백그라운드 빌더 | U11, U5, U6, DX11 · GL 의 워커 안전성 점검 | 예 |
| U13 | 렌더 타깃 크기 변경의 `waitIdle` 제거 | U8 | 예 |
| U14 | `waitIdle` 이유 인자와 게이트 | U13 | 아니오 |

**끝난 단위: B0 골격 번들.** U1(계약, ABI 34, 블로킹 헬퍼), U2(Vulkan 일회성 제출의 펜스 대기), U3(DX11), U4(GL)가 끝났고, U5 ~ U9 는 **골격**까지 들어갔습니다: 네 백엔드가 새 계약을 구현하고(DX11 과 GL 은 사실상 최종, DX12 는 슬롯 복사 리스트에 전용 전송 펜스를 단 모양, Vulkan 은 U6 의 임시 구현), 옛 호출처와 테스트가 `...AndWait` 위에서 컴파일됩니다. GPU 네 백엔드 확인(`RHIDeviceTest` 업로드 · 되읽기 · 구간 갱신 · 계산 UAV, `RenderPassGPUTest` 캡처)은 통합 L 에서 Release 로 통과했고(2026-10-25), 남은 확인은 매니페스트 id `rhi-transfer-b0-measure` (측정 질문 넷)입니다. 통합 L 이 GL 폴링을 고쳤습니다 — 컨텍스트가 없는 스레드의 `pollTransfer` 가 컨텍스트가 비어 있으면 한 번 집어 펜스를 본다(게임 스레드가 동기 로드로 막혀 RT 가 프레임을 돌리지 않으면 10 초 시간 초과까지 멈추던 것, `AppTest_Race` 의 GL 재생이 잡았다). nogpu 테스트 `RHITransferTest.*` 는 `ctest-nogpu` 가 돕니다.

**병합 전략: B0 골격 번들 하나, 그 뒤는 단위별.** 옛 동기 가상 함수 셋을 지우면 네 백엔드와 모든 호출처가 한 번에 새 계약 위에 서야 컴파일되므로 이 일은 쪼갤 수 없습니다. 그래서 번들 B0 는 계약(단계 1: 문구 수정, `discardTextureReadback`, 장치 세대, 대기 결과 분리)과 구현(단계 2: DX11 → GL → Vulkan → DX12 → 호출처 · 테스트)을 커밋만 나눠 한 번에 병합하고, 통합이 한 번에 빌드합니다. 별칭이나 shim 은 두지 않으므로 main 에 두 경로가 함께 남는 구간이 없습니다. B0 가 선 뒤에는 U5, U6, U7, U8, U9 가 각각 따로 병합됩니다(번들 밖, 서로 없이도 컴파일됩니다). 이어서 U10, U11, U12, U13, U14 도 따로 갑니다. 동시에 띄울 수 있는 작업자는 여섯 안팎이고, 다음 차례는 {DX12(U5), Vulkan(U6), 테스트(U7), 텍스처 소비자(U8), 구간 갱신과 되읽기 소비자(U9), PSO 상태(U11)} 입니다.

**B0 의 구현 수준과 알려진 비용.**

- DX11: 이벤트 쿼리(풀) + 스테이징 복사 + `Map( DO_NOT_WAIT )`. 최종에 가깝습니다.
- GL: 업로드는 스테이징 링(아래 GL-1) + `glFenceSync` + `glFlush`, 되읽기는 PBO 되읽기 링 + 펜스이고 펜스가 지나면 `Complete` 입니다(2.2 백엔드 표 GL). 폴링은 컨텍스트를 쥔 스레드이거나 컨텍스트가 비어 있을 때(기다리지 않고 한 번 집는다) 펜스를 봅니다.
- DX12: 열린 복사 리스트가 내보내질 때(프레임 끝, 폴링, 즉시 제출) 그때까지의 시리얼을 한 묶음으로 전송 전용 `ID3D12Fence` 에 매답니다. 폴링이 아직 내보내지 않은 시리얼을 내보내고 펜스를 올리므로 프레임을 돌리지 않아도 끝에 이릅니다. 업로드 링과 얼로케이터 풀은 U5 입니다.
- Vulkan: B0 는 제출 안에서 U2 의 펜스(`VkFence`)를 기다렸고, U6 이 기다리지 않는 전송으로 바꿨습니다(아래 "끝난 단위: U6").
- 비용: 이전에 GPU 를 기다리지 않던 DX11, DX12, GL 의 프레임 경로 호출처가 `...AndWait` 로 바뀌어 **GPU 왕복이 새로 생겼고**, 런타임 UI 예제가 눈에 띄게 느려졌습니다(사용자 확인). 가장 잦은 둘은 비동기로 바꿨습니다.
  - 글리프 아틀라스(`CanvasRenderer::uploadAtlasPage`): 페이지 전체와 구간을 제출만 하고, 새 페이지는 첫 업로드 티켓이 `Complete` 가 된 이후 프레임에 등록합니다(그 전에는 그 페이지의 글리프를 그리지 않음). `Busy` 는 더티 상태를 남겨 다음 프레임에 다시 내고, `Rejected` · `Failed` 는 오류를 남기고 그 페이지를 멈춥니다(`release` 가 풉니다).
  - 텍스처 구간 갱신(`TextureCache::flushRegionUpdates`, `Texture2D::writeCPUBytes`): 등록된 텍스처라 제출만 합니다. `Busy` 가 나면 그 차례의 나머지를 다음 프레임으로 미루고(순서 유지), `Rejected` 는 오류를 남기고 버립니다.
  - 두 함수의 제출 구간은 `ScopedForbidBlockingTransfer` 로 표시해 `...AndWait` 가 다시 들어오면 Debug 단언이 걸립니다. 셋업 구간 전체의 표시는 그 구간을 소유한 U10 의 몫입니다.
  - 남은 대기는 새 텍스처 업로드(`completePendingLoads` · `updateStreaming` · 동기 `acquire`, 프레임당 상한 있음, U8)와 `RenderThread` 렌더 타깃 읽기(요청이 있을 때만, U9)입니다. Vulkan 은 U6 부터 제출이 기다리지 않습니다.
  - 측정(2026-10-25 통합 M, Release, GPU 독점, 전 = B0 만 든 main `ab3b917cb` · 후 = 이 두 비동기화, 번갈아 3 판 중앙값): 새 한글 음절 400 개를 띄우는 판(`-lang=ko -gv_benchMeshes=100 -gv_benchUiHangulGlyphs=400 -gv_profileFrames=600`)에서 `RT.Canvas.Upload` 최악이 DX12 62.9 → 4.2 ms, DX11 93.8 → 1.8 ms, GL 62.5 → 1.0 ms, 프레임 벽시계가 DX12 10.9 → 3.4 ms, DX11 12.5 → 3.4 ms, GL 11.9 → 4.1 ms 로 줄었다(전은 `RT.Canvas.Upload` p99 가 46 ~ 75 ms — 예산으로 나눈 글리프 프레임마다 왕복을 기다렸다). Vulkan 은 5.4 ms 그대로(U6). 글리프가 이미 찬 정적 화면(`-gv_uiDemo=1`, 5 판)과 `perf-compare uiPaintOrder`(바이너리 A/B, 5 판)는 네 백엔드 모두 퍼짐 안이고, `uidemo.scenario.xml` 스크린샷은 전후 픽셀이 같다.

**끝난 단위: GL-1(GL 스테이징 링).** 텍스처 업로드(밉 전체, BC 포함)와 구간 갱신은 영속 매핑 PBO 링(`GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT`)에 제출 스레드가 컨텍스트 밖에서 복사하고, 컨텍스트를 빌리는 시간은 `GL_PIXEL_UNPACK_BUFFER` 바인딩, 밉마다 PBO 오프셋의 `glTexSubImage*` · `glCompressedTexSubImage*`, 펜스 하나뿐입니다. 언팩 상태(PBO 바인딩, `GL_UNPACK_ALIGNMENT` 1)와 되읽기의 팩 상태(되읽기 링 바인딩, `GL_PACK_ALIGNMENT` 1)는 RAII 가드(`ScopedStagingUnpack` · `ScopedStagingPack`)가 GL 기본값(0, 4)으로 되돌립니다. 규칙은 셋입니다. 링 용량보다 큰 업로드 하나는 나눠 올리지 않고, 장부(`RHIStagingAllocator`)가 링이 비었을 때 단독 구간으로 내주면 그 크기의 일회성 영속 매핑 버퍼에 혼자 들어가며 단독 상한(`gv_rhiStagingSingleMaxMegabytes`)을 넘으면 `Rejected` 이고 설정 이름을 로그로 알립니다(계약 2 의 "상한보다 큰 요청"). 링이 지금 차 있으면 끝난 펜스를 한 번 모은 뒤에도 차 있을 때 `Busy` 입니다(프레임을 돌리지 않는 `submitWhileBusy` 도 링을 비운다, 계약 7). GL 4.4 가 없으면 링이 없고 `_bPersistentStagingRing` 이 거짓이며 초기화가 오류를 남기고 업로드는 모두 `Rejected` 입니다(클라이언트 메모리에서 바로 올리는 두 번째 경로는 두지 않는다). 장치를 잃은 뒤(펜스 확인이 `GL_WAIT_FAILED`)에는 워터마크가 멈춰 링이 비지 않으므로 제출 진입이 `RHITransferSerials::isLost` 를 보고 `Rejected` 를 돌려주고, 링 할당 결과를 제출 결과로 옮기는 곳은 `RHITransferSerials::classifyStagingAllocation` 하나라 잃은 장치에서 `Full` 이 `Busy` 가 되지 않습니다(nogpu `RHIStagingRingTest.LostDeviceRejectsInsteadOfBusyWhenTheRingIsFull`, U5 · U6 의 링도 같은 함수로 옮긴다). 상수 · 구조 · 정점 버퍼의 구간 갱신(`updateConstantBuffer` · `updateStructuredBufferRegions` · `updateVertexBufferRegions`)은 링을 쓰지 않고 `glBufferSubData` 그대로입니다(이유는 [결정 기록 5-5](../decisions/5-5_GraphicsRHI.md)). WGL 은 4.6 · 4.5 · 4.3 · 4.1 · 3.3, GLX 는 4.6 부터 3.3 까지 내려가며 컨텍스트를 만들므로 4.4 미만 컨텍스트가 실제로 생길 수 있지만, 초기화가 DSA(`glBindTextureUnit`, 4.5)를 확인 없이 부르고 `ARB_gl_spirv` 가 없으면 거절하므로 그런 드라이버에서 GL 백엔드가 서는 경우는 사실상 없습니다 **[코드]**. 확인은 매니페스트 id `rhi-transfer-gl1-staging-ring` 입니다.

**끝난 단위: U6(Vulkan 비동기 전송).** 업로드 · 구간 업로드 · 되읽기가 제출 안에서 기다리지 않습니다. 링은 둘이고(업로드 `gv_rhiStagingRingMegabytes` · 되읽기 `gv_rhiReadbackRingMegabytes`, 호스트 가시 · 일관 메모리에 영속 매핑, 장부의 Staging 두 항목) 장부는 GL 과 같은 `RHIStagingAllocator`(구간 단위 회수) 입니다. 용량이 0 이거나 링을 만들지 못하면 Vulkan 초기화가 실패하고(`_bPersistentStagingRing` · `_bPersistentReadbackRing` 은 늘 참), 링보다 큰 전송 한 장은 링이 빌 때 그 크기의 일회성 호스트 가시 버퍼로 혼자 받고(통합 N, `gv_rhiStagingSingleMaxMegabytes` 를 넘으면 설정 이름을 로그로 남기고 `Rejected`, GPU 확인 `RHIStagingRingGPUTest.VulkanTransferLargerThanTheRingIsAcceptedAlone`), 찬 링은 끝난 펜스를 한 번 걷은 뒤에도 차 있을 때 `Busy` 입니다(옮기기는 `classifyStagingAllocation`). 복사 오프셋은 텍셀 블록 바이트와 4 의 최소공배수로 링 안에서 다시 맞춥니다(소스의 빈틈없는 밉 배치는 R8 의 작은 밉 · 12 바이트 텍셀에서 그 규칙을 어긴다). 전송 작업(`VulkanTransferJob`)은 전용 풀의 커맨드 버퍼와 펜스 한 쌍이고 펜스를 지나면 재사용 목록으로 돌아갑니다. 기록과 제출은 `_transferMutex` 로 직렬화하고, 제출은 `_queueMutex` 안에서 내며 같은 락 안에서 `RHITransferFenceQueue::enqueue` 가 시리얼을 매깁니다(계약 5, RT 의 `endFrame` 제출과 같은 락). 완료는 `pollTransfer` · 링이 찬 제출 · `beginFrame` 이 `vkGetFenceStatus` 로 앞에서부터 걷어 연속 구간만 워터마크로 올립니다(계약 7·8). 전송은 그래픽스 큐로 내므로 큐 소유권 이전이 없고, 첫 배리어(`ALL_COMMANDS` → `TRANSFER`)가 앞서 낸 프레임 뒤에, 끝 배리어(`TRANSFER_DST` → `SHADER_READ_ONLY`, 셰이더 단계)가 뒤에 낼 프레임 앞에 둡니다(계약 3). 레코드의 `_layout` 은 제출 직후 전송 락 안에서 바꿉니다. 되읽기는 복사 뒤 `HOST` 배리어를 걸고, 결과는 `takeTextureReadback` 이 링에서 복사해 가며(D24 변환도 그때) 구간을 놓습니다. 끝나기 전에 버린 되읽기의 구간에는 시리얼을 붙여 복사가 끝난 뒤에 돌아오게 합니다. 전송 중에 파괴한 텍스처(마지막 쓰기 · 읽기 시리얼이 워터마크 아래가 아닌 것)의 해제는 시리얼 키 해제 큐(`_transferReleaseQueue`)에 두었다가 전송이 끝나면 그때의 다음 프레임 펜스 뒤로 넘깁니다(계약 6). 장치 손실은 제출 진입의 `isDeviceLost` 가 `Rejected` 로 돌려주고, 펜스 확인이 `VK_ERROR_DEVICE_LOST` 면 `reportQueueResult` 가 손실을 표시합니다. 임시 구현(제출 안의 펜스 대기, `RHITransferSerials::issueCompleted`)은 지웠습니다. 같은 브랜치에서 `recreateSwapChain` · `recreateSurfaceAndSwapChain` 의 `vkDeviceWaitIdle` 결과를 보고하고, `beginFrame` 이 첫 펜스 대기에서 손실을 보면 그 프레임을 바로 건너뜁니다. nogpu 는 `RHIStagingRingTest.ReadbackSpansStayUntilTakenAndDiscardedOnesWaitForTheirCopy` · `FakeDeviceReusesRingSpansOnlyAfterTheFencePrefixPasses`, GPU 확인은 매니페스트 id `rhi-transfer-vk-u6` 입니다. 측정(M1 의 RT 프레임 p99 전후)은 `rhi-transfer-b0-measure` 가 맡습니다 — `RT.Upload.waitIdle` 은 이제 Vulkan 의 프레임 밖 버퍼 갱신 · 시계 읽기만 잽니다.

**파일 소유(겹치지 않게).** `Source/Engine/Graphics/RHI/` 아래의 폴더는 하나의 작업자만 맡습니다.

| 단위 | 소유 파일 |
|---|---|
| U5 | `RHI/DX12/` 전체 |
| U6 | `RHI/Vulkan/` 전체 |
| U7 | `Test/EngineTest/Graphics/TestRHIDevice.cpp`, `Test/EngineTest/Renderer/TestRenderPassGPU.cpp`, `Test/EngineTest/Graphics/TestRenderTargetRegistry.cpp` |
| U8 | `Graphics/Texture/` |
| U9 | `Renderer/Canvas/CanvasRenderer.*`, `Renderer/Frame/Capture/FrameCapture.*`, `Renderer/RenderThread.cpp`, `Graphics/Debug/RenderTargetRegistry.*`, `Editor/Common/Backend/EditorUICapture.cpp`, `Editor/Common/Workspace/EditorRenderViews.cpp`, `Editor/Panels/RenderTarget/RenderTargetPanel.cpp` |
| U10 | `Graphics/Material/`, `Engine.cpp`, `Renderer/Scene/GPUSceneBuilder.cpp`, `Renderer/Scene/GPUScene.cpp`, `Renderer/Frame/FrameRenderer.cpp` |
| U11, U12 | `Renderer/Frame/Resource/RenderPSOCache.*`, `Renderer/Frame/FrameRendererPSO.cpp`, `Renderer/Frame/FrameRendererDraw.cpp`, `Renderer/Frame/FrameRenderer.h`, 새 파일 |
| U13 | `Graphics/Texture/TextureCache.cpp`, `RHI/DX11/` 와 `RHI/GL/` 의 bindless 파일 |
| U14 | 호출처 전부(기계적 변경이라 혼자) |

`FrameRenderer.cpp` 는 U10 만 고칩니다(셋업 구간의 `initializePending` 호출과 `ScopedForbidBlockingTransfer` 표시). U8 은 `completePendingLoads` 와 `updateStreaming` 의 구현을 `TextureCache` 안에서 바꾸고 호출 줄은 그대로 둡니다. U11 과 U12 는 `warmPinnedMaterialPSOs` 의 호출 줄(`FrameRenderer.cpp` 817)을 건드리지 않습니다.

**단위별 내용과 검증.** GPU 검증은 통합이 `gpu-verifier`(Shipping, 네 백엔드)로 한 번에 돌립니다. 항목은 구현하는 단위가 `Test/QA/VerifyBatch.json` 에 더합니다.

- **U5.** DX12 의 B0 구현을 최종으로 바꿉니다: 전용 업로드 링과 `Busy`(링 · 단독 버퍼 총량), 복사 리스트와 얼로케이터 풀(지금은 폴링이 새 펜스 구간을 만들지 않지만 `openUploadSlot` 이 얼로케이터 Reset 전에 펜스를 기다리는 비용이 남아 있다), `_uploadSlotMutex` 범위 축소(업로드와 프레임 제출이 같이 걸려 있다. 프레임 펜스 Signal · 값 올리기와 업로드의 `_fenceValue` 읽기 · 복사 실행은 줄여도 같은 락에 둔다 — [결정 5-5](../decisions/5-5_GraphicsRHI.md) `withNativeContext` 항목, 확인은 매니페스트 id `rhi-native-context-guard`), 텍스처 상태 추적(`getTrackedTextureState`)과 RT 기록이 겹치는지 점검. `createPipelineState` 가 워커에서 안전한지 점검한 결과를 2.4 에 적습니다. 검증은 DX12 GPU(`rhi-transfer-b0-gpu` 와 U7 의 계약 테스트)입니다.
- **U6.** 코드가 끝났습니다(위 "끝난 단위: U6"). `createPipelineState` 의 워커 안전성 점검 결과는 2.4 에 적었습니다. 남은 것은 통합의 GPU 확인 `rhi-transfer-vk-u6` 과 M1 의 RT 프레임 p99 전후 비교(`rhi-transfer-b0-measure`)입니다.
- **U7.** `RHIDeviceTest` 에 계약 테스트를 더합니다: 제출 → `Pending` 또는 `Complete` → 폴링, 프레임 없이 폴링만 도는 경로(진행 보장), 구간 갱신 뒤 업로드된 내용이 보임(순서), 업로드 직후 텍스처 파괴, `Busy` 경로, 장치 손실 뒤 `Failed`(Vulkan 은 `VulkanLostDeviceStopsWaitingOnTheGPU` 와 같은 모양), 읽기를 가져가지 않고 버리는 경로(`discardTextureReadback`), 끝나지 않은 전송이 있는 텍스처의 bindless 등록 거절. 옛 함수를 부르던 테스트의 이전은 B0 가 했습니다. GPU 네 백엔드입니다.
- **U8.** 텍스처 로드를 세 단계로 나눕니다: 워커 읽기 → RT 펌프의 `beginUpload`(텍스처 생성과 `submitTextureUpload`) → 이후 프레임의 `finishUpload`(`pollTransfer` 가 `Complete` 면 bindless 등록과 발행). `Busy` 는 다음 프레임에 다시 제출합니다. 프레임당 상한은 제출 수에 적용합니다. `updateStreaming` 의 위쪽 밉 교체도 같은 세 단계입니다. 동기 `acquire` 와 `wait()` 는 블로킹 헬퍼로 `beginUpload → 대기 → finishUpload` 를 합니다(B0 의 `Texture2D::createTextureWithMips` 와 `loadFromCPUBytes` 가 쓰는 `uploadTexture2DAndWait` 를 이 흐름으로 바꿉니다. 구간 갱신은 끝났습니다). 걸림돌은 `AssetLoadBlock` 의 완료 단계가 한 번에 끝나는 모양이라는 점입니다 — 펌프(RT)가 제출한 뒤 다른 스레드의 `wait()` 가 같은 블록의 완료를 집지 않게 하는 "업로드 중" 단계를 `AssetHandle` 에 더해야 합니다(`Resource/` 폴더라 이 단위의 소유에 더합니다). 검증은 `RenderPassGPUTest.AsyncTextureStreamsTopMipsAfterBase`, `StreamingUploadsTheTextureNotedAsLargestOnScreenFirst`, `AssetStreamingTest.*` 와 텍스처가 있는 시나리오(`kaykitcharacters`) 네 백엔드입니다. 스크린샷으로 회색 텍스처가 진짜 텍스처로 바뀌는지 봅니다.
- **U9.** 글리프 아틀라스는 끝났습니다(위 "B0 의 구현 수준과 알려진 비용", 매니페스트 id `rhi-transfer-u9-canvas-gpu`). 남은 것: `RenderThread::readRequestedRenderTarget` 는 요청 프레임에 읽지 않고 읽기 티켓(`RHITransferUtil::PendingReadback`)을 레지스트리에 두었다가 이후 프레임에 `take` 합니다. 스크린샷과 에디터 캡처는 블로킹 헬퍼를 그대로 씁니다. 검증은 `engine/automation/uidemo.scenario.xml` 네 백엔드(글자), `-gv_screenshot` 이 쓰는 캡처 경로, `RenderTargetRegistry` 읽기 테스트, 에디터 `engine/automation/editor/uiaudit.scenario.xml` 입니다.
- **U10.** 2.3 의 여섯 항목입니다. 검증은 `material-cache-gpu-test`(매니페스트 id), 새 시나리오 `game/empty/automation/materialfirstuse.scenario.xml`(처음 보는 머티리얼의 오브젝트를 스폰하고 프레임마다 캡처해 몇 프레임 안에 나타나는지, Error 로그가 없는지 봄), 그리고 M1 의 `GT.Material.waitIdle` 호출 수 0 입니다.
- **U11.** 2.4 의 상태 데이터, `psoForBatch` 의 구분, `RT.PSO.pendingBatches` 카운터, `findMaterialPSO` 주석 수정입니다. 이 단위에서는 생성이 여전히 RT 에서 동기로 끝나므로 항목이 `Absent` → `Ready` 로 한 프레임 안에 가서 화면은 지금과 같아야 합니다. `RenderPSOCache` 상태 전이는 GPU 없이 테스트합니다(nogpu). GPU 는 기존 골든이 그대로인지만 봅니다.
- **U12.** 빌더 스레드, 우선순위, 완료 목록 합치기, GL 의 시간 예산, `-gv_psoAsync`(0 이면 U11 의 동기 생성, 상태 규칙은 같다). 이 스위치는 A/B 측정용이며 `gv_gpuUploadQueue` 와 같은 선례입니다. 검증은 새 시나리오 `game/empty/automation/psoasync.scenario.xml` 네 백엔드입니다. 뷰 모드를 번갈아 바꾸고 새 머티리얼을 스폰한 뒤, 몇 프레임 안에 동기 생성(`-gv_psoAsync=0`)과 같은 이미지에 이르는지, Error 로그가 없는지 봅니다.
- **U13.** `TextureCache::resizeRenderTarget` 의 `waitIdle`(W6)을 지웁니다. 옛 텍스처를 지연 해제로 넘기고 새 핸들과 인덱스를 받습니다. 전제는 DX11 과 GL 의 bindless 인덱스 반환이 GPU 완료 뒤로 미뤄지는 것입니다. DX12 는 이미 `deferFreeBindlessIndex` 로 그렇고, DX11 은 `releaseFreeListIndex` 로 즉시 돌려줍니다 **[코드]**. 핫 리로드의 `waitIdle` 은 도구 경로라 남깁니다. 검증은 에디터에서 캔버스 타깃 크기를 바꾸는 시나리오(`engine/automation/editor` 의 기존 시나리오 가운데 크기를 바꾸는 것이 있는지 확인해야 함 **[확인 전]**)와 `engine/automation/hotreload.scenario.xml` 입니다.
- **U14.** `IRHIDevice::waitIdle` 에 이유 열거형(`Shutdown`, `BackendSwap`, `Resize`, `HotReload`, `SceneActivation`, `Capture`, `Tool`)을 필수 인자로 더합니다. 호출처는 W7 의 20곳 안팎이고 U13 뒤에 남은 것입니다. 이유 열거형으로 이름을 가르면 프레임 경로에 새로 생기는 호출이 눈에 띄고, 린트 게이트가 프레임 경로 폴더(`Renderer/Frame/`, `Graphics/Material/`, `Graphics/Texture/` 의 비 reload 함수)에서 이유 `Tool` 이 아닌 호출을 막을 수 있습니다. 게이트는 열거형으로 막지 못하는 틈(함수 단위 허용)에만 둡니다. 기계적 변경이라 혼자 하고 커밋만 단계별로 나눕니다.

**측정.** M0 은 착수 전입니다. `wt/profscopes` 병합 뒤 Release 에서 혼자(GPU 독점) 네 스코프의 호출 수와 ms 분포, RT 와 GT 프레임의 p99 와 최악 프레임을 측정합니다. 시나리오는 게임 시작, 씬 로드, 새 머티리얼 스폰, 뷰 모드 전환, 글자가 많은 UI 변경이고 설정은 LowEnd 를 포함합니다. M0 에서 W2 의 합이 작거나 Vulkan 이 아닌 백엔드에서 W1 과 W3 이 작으면 U10 과 U12 의 우선순위를 낮춥니다. M1 은 B0 병합 뒤와 마지막 단위 뒤에 같은 시나리오로 다시 측정합니다. 결과는 [검증과 측정](../08_Verification.md) 에 적습니다.

**B0 측정 질문 넷**(매니페스트 id `rhi-transfer-b0-measure`, 아직 한 명령으로 안 돕니다 — 세는 스코프가 없습니다).

1. 제출에서 `Complete` 까지 몇 프레임 걸리는가. 백엔드별로 같은 프레임 안(DX11, GL, Vulkan B0)인지 한 프레임 이상(DX12, 이후 Vulkan)인지 봅니다. U8 의 `finishUpload` 가 생기면 제출 프레임과 처음 `Complete` 를 본 프레임의 차로 셉니다.
2. `pollTransfer` 한 번의 비용. DX11 은 즉시 컨텍스트 락 + `GetData`, DX12 는 `_uploadSlotMutex`(RT 의 프레임 제출과 공유) + 전송 펜스 읽기와 필요할 때의 내보내기, GL 은 원자 읽기(컨텍스트를 쥐었거나 비어 있으면 `glClientWaitSync`)입니다. 프레임당 폴링 수가 늘 때 락 경합이 생기는지 봅니다.
3. U2(Vulkan 대기 범위 축소)와 U6(비동기화)의 이득. `RT.Upload.waitIdle` 합계와 RT 프레임 p99 를 전후로 비교합니다.
4. `...AndWait` 대기의 CPU 사용. yield 32 번 뒤 200 µs 잠으로 대기 스레드가 얼마나 CPU 를 쓰는지, 10 초 상한에 걸린 적이 있는지 봅니다.

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
| ABI 34 를 한 번에 맞춰야 한다 | 한 백엔드가 늦으면 번들 전체가 대기 | 번들 B0 가 네 백엔드와 호출처를 한 번에 담았다. Vulkan 은 B0 의 임시 구현 위에 U6 이 따로 비동기화했다(ABI 그대로). DX11 과 GL 구현은 작다 |
| 시그니처를 구현 뒤에 바꾸면 ABI 를 다시 높여 모든 백엔드를 재빌드 | 되돌리기 어려운 결정 | `design-reviewer` 검토 결과(단계 1 의 `discardTextureReadback`, 장치 세대, 대기 결과 분리)를 B0 안에서 ABI 34 에 반영했다. B0 병합 뒤의 시그니처 변경은 ABI 를 다시 높인다 |
| B0 의 프레임 경로 호출처가 `...AndWait` 로 GPU 를 기다린다 | DX11, DX12, GL 에서 로드가 많은 프레임이 느려진다(UI 예제에서 사용자가 확인) | 글리프 아틀라스와 구간 갱신은 제출 → 이후 프레임 폴링으로 바꿨다(`rhi-transfer-u9-canvas-gpu` 통과, 새 글리프가 몰리는 프레임의 최악이 DX11 · DX12 · GL 에서 60 ~ 90 ms → 1 ~ 4 ms — 위 "B0 의 구현 수준과 알려진 비용"). 새 텍스처 업로드(U8)와 렌더 타깃 읽기(U9)가 남았다. 측정 질문 2, 4(`rhi-transfer-b0-measure`)가 크기를 본다 |
| 새 아틀라스 페이지가 첫 업로드 완료 뒤 프레임부터 그려진다 | 페이지가 새로 생긴 프레임에 그 페이지의 글자가 한두 프레임 빠진다(DX12 는 프레임 지연만큼) | 페이지는 처음 글자와 아틀라스가 넘칠 때만 생긴다. 등록 전 구간은 쌓아 두었다가 등록한 프레임에 낸다. 같은 큐라 등록 뒤의 구간 갱신은 다음 프레임 커맨드 앞에 실행된다(계약 3) — 전용 복사 큐를 들이면 등록된 페이지의 구간 갱신도 다시 정해야 한다 |
| DX12 폴링이 `_uploadSlotMutex` 를 잡고 열린 복사 리스트를 내보낸다 | 프레임 제출과 같은 락이라 폴링이 잦으면 RT 제출과 겨룸 | 폴링은 아직 내보내지 않은 시리얼일 때만 락을 잡는다. U5 가 락 범위를 줄인다 |
| GL 의 `pollTransfer` 가 컨텍스트를 기다리지 않는 점 | 다른 스레드(RT)가 컨텍스트를 쥔 동안 그 스레드의 폴링은 `Pending` | 제출이 `glFlush` 로 GPU 를 진행시키고, 컨텍스트가 비어 있으면 폴링이 한 번 집어 펜스를 보며, RT 가 쥔 동안은 RT 의 `endFrame` 이 워터마크를 올린다. 링이 찼을 때의 할당도 같은 방식으로 펜스를 모은다. 시간 초과가 틈을 오류로 드러낸다 |
| Vulkan 의 완료 순서가 제출 순서와 다를 수 있다 | 워터마크가 앞서 올라가면 미완 자원을 발행 | 전송 작업마다 펜스를 따로 보고 `RHITransferFenceQueue` 가 연속 구간만 워터마크로 올린다(U6). nogpu `RHIStagingRingTest.FakeDeviceReusesRingSpansOnlyAfterTheFencePrefixPasses` 가 뒤 펜스가 먼저 신호된 경우를 본다. GPU 위의 지연된 앞 제출은 U7 |
| Vulkan 전송이 프레임 기록과 겹친다 | 다른 스레드의 전송이 RT 가 기록 중인 텍스처의 레이아웃 기록을 바꾸면 프레임 제출이 검증 레이어 오류(레이아웃 불일치)를 낸다 | 계약 3 대로 렌더 타깃처럼 프레임이 레이아웃을 바꾸는 텍스처의 전송은 RT 기록 밖에서만 낸다. 레이아웃 읽기 · 쓰기는 `_imageLayoutMutex` 안이고 전송끼리는 `_transferMutex` 로 순서가 선다. 확인은 `rhi-transfer-vk-u6` 의 검증 레이어 오류 0 |
| Vulkan 전송 작업 수 | 작은 전송이 몰리면 링은 남아도 작업(커맨드 버퍼 + 펜스)이 늘어난다 | 펜스를 지난 작업은 재사용 목록으로 돌아가 다시 쓴다. 작업 수에는 상한이 없다 — 늘어나는 것이 보이면 M1 이 상한과 `Busy` 를 정한다 |
| 스테이징 메모리 | 큰 텍스처가 몰리면 `Busy` 가 길어지거나 메모리가 튄다. GL · Vulkan 은 링보다 큰 한 장을 링이 빌 때 일회성 버퍼로 혼자 받는다(최고점 max(링, 한 장)) | 링 용량 설정(`gv_rhiStagingRingMegabytes`)과 단독 한 장 상한(`gv_rhiStagingSingleMaxMegabytes`, 넘으면 `Rejected`), 장부의 Staging 항목, M1 이 조정 |
| DX12 `_uploadSlotMutex` 범위 | 새 제출이 RT 프레임 제출을 막을 수 있음 | U5 가 락 범위를 줄이고 텍스처 상태 추적과의 겹침을 점검 |
| `Material` GPU 필드의 스레드 간 읽기 | 게임 스레드가 쓰는 중인 인덱스를 읽는 경쟁 | `GpuState` 를 acquire 로 읽은 뒤에만 읽는 규칙, Debug 에서 단언 |
| PSO 빌더의 스레드 안전성 | 백엔드나 셰이더 컴파일러의 잠금 누락 | 네 백엔드의 점검(U5, U6 포함)이 끝나기 전 U12 를 시작하지 않는다. `-gv_psoAsync=0` 으로 동기 생성과 비교 |
| 보이는 변화(수 프레임 늦은 등장, 뷰 모드 전환 직후 빈 배치) | 사용자가 결함으로 느낄 수 있음 | 건너뛴 수 카운터로 드러내고 O1 로 디더를 맞춘다 |
| Vulkan 이 아닌 백엔드에서는 W2 가 이미 대기가 없다 | 이득이 Vulkan 에 몰림 | M0 이 먼저 측정한다. 이득이 작으면 U10 과 U12 를 미룬다 |

**되돌리기.** U5 ~ U14 는 B0 가 선 뒤 각각 커밋 단위로 revert 합니다. 번들 B0 는 병합 커밋 하나를 revert 합니다(`git revert -m 1`). 옛 함수와 새 함수가 한동안 공존하는 단계는 만들지 않으므로 중간 상태로 되돌리는 방법은 없습니다.

**한 백엔드씩 옮기는 단계.** ABI 는 한 번에 높이지만, 새 계약이 약속하는 성질(제출이 GPU 를 기다리지 않음)은 백엔드마다 따로 채워집니다. 순서는 다음과 같습니다.

1. B0: 계약 + 네 백엔드 + 호출처. DX11 과 GL 은 "같은 호출 + 완료 확인"으로 최종에 가깝고, DX12 는 슬롯 모델에 전송 펜스를 달고, Vulkan 은 U2 의 펜스 대기(대기 범위가 큐 전체에서 내 제출로 줄고 큐 락을 놓는다)를 쓰는 임시 구현입니다.
2. U5, U6: DX12 와 Vulkan 의 최종 구현. Vulkan 이 비로소 비동기가 됩니다.
3. U8, U9: 소비자 쪽을 `...AndWait` 에서 제출 → 폴링으로 바꿉니다.
4. U10 → U11 → U12: 소비자 쪽 대기를 하나씩 지웁니다.

## 6. 상용 엔진 비교

언리얼의 RHI 는 업로드를 `FRHICommandList` 에 기록하는 명령으로 두고, 텍스처 스트리밍은 워커에서 텍스처를 만들어(`RHIAsyncCreateTexture2D`) `FRHIGPUFence` 를 폴링하며, 되읽기는 `FRHIGPUTextureReadback` 이 준비 여부(`IsReady`)를 폴링하는 것으로 알고 있습니다. 유니티는 `AsyncGPUReadback.Request` 가 요청 객체를 돌려주고 `done` 을 폴링하며, 텍스처 업로드는 품질 설정의 Async Upload Buffer Size 와 Time Slice 로 링 버퍼를 시간 예산 안에서 쓰는 파이프라인으로 알고 있습니다. Godot 의 `RenderingDevice` 는 `texture_update` 가 스테이징 버퍼 블록에 복사해 프레임 안의 전송 커맨드로 보내고, 되읽기는 비동기 변형(`texture_get_data_async`)이 콜백으로 돌려주는 것으로 알고 있습니다. 모두 "제출 → 펜스나 폴링 → 사용"이라 이 설계와 같은 방향입니다.

다른 점은 둘입니다. 우리는 콜백 대신 티켓 폴링을 쓰는데, 소비자(bindless 등록)가 RT 셋업 구간에서만 일해야 하기 때문입니다. 그리고 1단계에서 전용 전송 큐를 쓰지 않습니다. 이 단락은 모두 기억에 기댄 것이며 API 이름의 현재 가용 범위(플랫폼, 버전)와 전용 복사 큐의 사용 여부, 스테이징 블록의 크기는 **[확인 전]** 입니다. 결정 근거로는 쓰지 않았고, 근거는 1절의 코드 읽기입니다.

## 7. 정한 것과 확인하지 못한 것

정한 것(사용자 결정이 필요한 항목은 없습니다):

- ABI 34 에서 옛 동기 함수 세 개를 지우고 shim 을 두지 않는다. 동기 변형은 ABI 밖에 있는, 이름에 `AndWait` 가 드러나는 헬퍼다.
- 완료는 콜백이 아니라 티켓 폴링이다. 해제용 `enqueueGPURelease` 는 그대로 둔다. **텍스처는 티켓, 버퍼 되읽기는 `enqueueGPURelease` 콜백인 이유와 기준:** 결과를 받는 쪽이 RT 셋업 구간에서 상태(bindless 인덱스)를 바꿔야 하는 텍스처 전송은 티켓과 폴링이고(콜백으로 받아도 결국 RT 큐에 넣고 다시 읽는다), 프레임에 묶인 읽기 버퍼(`createReadbackBuffer`)를 완료 콜백 안에서 한 번 비우면 끝나는 되읽기는 콜백이다.
- **B0 전략:** 옛 동기 가상 함수 셋을 지우는 일은 네 백엔드와 모든 호출처가 한 번에 새 계약 위에 서야 컴파일되므로 쪼개지 않고 골격 번들 하나로 병합한다(계약 수정 → DX11 → GL → Vulkan → DX12 → 호출처 · 테스트 → 문서 순 커밋). 번들 밖의 U5 ~ U9 와 U10 ~ U14 는 B0 뒤에 단위별로 병합한다. 별칭과 shim 은 두지 않는다.
- 이 결정들은 `docs/decisions/` 의 영역 파일이 없어 이 문서와 B0 커밋 메시지에 둔다(통합이 옮길 곳을 정한다).
- 1단계는 그래픽스 큐 하나다. 전용 전송 큐는 측정 뒤에 다시 판단한다.
- GL 업로드는 영속 매핑 PBO 링 하나로만 올린다(GL-1). GL 4.4 가 없으면 능력 비트를 거짓으로 두고 업로드를 `Rejected` 로 돌려주며 클라이언트 메모리 경로를 남기지 않는다. 링보다 큰 업로드는 나누지 않고, 링이 빌 때 일회성 영속 매핑 버퍼로 혼자 들어간다(단독 상한 `gv_rhiStagingSingleMaxMegabytes` 를 넘으면 `Rejected`). 언리얼의 OpenGL RHI 도 텍스처 업로드에 PBO 를 쓰는 것으로 알고 있고, 유니티의 Async Upload Buffer 도 고정 크기 링이라 큰 텍스처가 오면 링을 키우는 설정을 둔다 **[확인 전]**.
- 글리프 아틀라스는 기다리지 않는다. 새 페이지는 첫 업로드가 `Complete` 인 프레임에 등록하고 그 전에는 그 페이지를 그리지 않으며, 등록된 페이지의 구간 갱신은 제출만 한다(같은 큐의 순서에 기댄다). `Busy` 는 다음 프레임 재시도, `Rejected` · `Failed` 는 오류와 함께 그 페이지를 멈춘다. 언리얼 Slate 의 폰트 아틀라스도 바뀐 텍스처를 렌더 스레드에서 갱신 명령으로만 내고 완료를 기다리지 않는 것으로 알고 있다 **[확인 전]**.
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
