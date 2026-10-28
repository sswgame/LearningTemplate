# RHI 비동기 업로드 · 머티리얼 초기화 · PSO 준비 상태

> 상태: 구현중 · 선행: 없음 · 영역: graphics · 진행: 13/15

게임 스레드(GT)와 렌더 스레드(RT)가 GPU 완료를 기다리는 지점을 없앤다. 업로드와 되읽기는 **"제출 → 티켓 → 폴링"** 계약 하나로 바꾸고(RHI ABI 버전을 한 번 높여 옛 동기 함수 세 개를 지움), 새 머티리얼과 PSO 는 **"준비 전 / 준비됨 / 실패"** 상태를 데이터로 두어 준비 전에는 그리지 않는다. 프레임 경로에는 `waitIdle`(GPU 전체를 비우는 대기)을 새로 만들지 않는다. 네 백엔드의 새 전송 계약과 호출처 이전은 들어가 GPU 확인까지 통과했다. 남은 것은 머티리얼 GPU 상태(U10)와 PSO 백그라운드 빌더(U12), 그리고 새로 쓴 단위의 컴파일 · GPU 확인이다. 근거 표시: **[코드]** 읽어서 확인, **[추정]**, **[확인 전]**. 관련 문서: [PreloadAndLod](PreloadAndLod.md)(PSO 미리 만들기와 텍스처 밉 스트리밍).

## 진행

| 단위 | 내용 | 상태 |
|---|---|---|
| U1 | 계약(ABI 34, 옛 동기 함수 삭제, 블로킹 헬퍼 `...AndWait`) | 끝(GPU 네 백엔드 통과) |
| U2 | Vulkan 일회성 제출의 대기 범위를 제출한 일로 좁힘 | 끝 |
| U3 | DX11 새 계약 구현 | 끝 |
| U4 | GL 새 계약 구현 | 끝 |
| U5 | DX12 비동기 전송(전용 링 · 전송 펜스) | 끝 |
| U6 | Vulkan 비동기 전송(링 둘 · 전송 작업 · 펜스) | 끝(GPU 통과, UI 예제 프레임 5.4 → 2.8 ms) |
| U7 | 계약 GPU 테스트 다섯 케이스 | 코드 끝, 실행 전 |
| U8 | 새 텍스처 업로드의 세 단계(읽기 → 제출 → 폴링 후 공개) | 코드 끝, 컴파일 · GPU 확인 전 |
| U9 | 렌더 타깃 읽기 티켓(글리프 아틀라스는 끝) | 코드 끝, 컴파일 · GPU 확인 전 |
| U10 | 머티리얼 GPU 상태와 프레임 경계 초기화 | 남음(선행 U8) |
| U11 | PSO 상태 데이터와 드로우 규칙 | 코드 끝, 컴파일 · GPU 확인 전 |
| U12 | PSO 백그라운드 빌더 | 남음(선행 U11 · DX11 · GL 워커 안전성 점검) |
| U13 | 렌더 타깃 크기 변경의 `waitIdle` 제거 | 코드 끝, 컴파일 · GPU 확인 전 |
| U14 | `waitIdle` 이유 인자(호출처 약 70곳) | 코드 끝, 빌드 전. 프레임 경로 호출 금지 게이트는 보류 |
| GL-1 | GL 영속 매핑 PBO 스테이징 링 | 끝 |

첫 일: 컴파일하고 U7 · U8 · U9 · U11 · U13 의 GPU 확인을 돌린다. 그다음 M0 로 W1 · W2 · W3 대기가 실제로 큰지 재서 U10 · U12 의 우선순위를 정한다(Vulkan 이 아닌 백엔드는 W2 에 이미 대기가 없어 이득이 작으면 미룬다).

## 1. 지금 동기 대기 지점

| # | 위치 | 기다리는 것 | 스레드 | 빈도 |
|---|---|---|---|---|
| W1 | `Engine.cpp` 의 머티리얼 초기화 대기 | RT 큐가 빌 때까지, 이어서 GPU 전체 | GT | 초기화를 기다리는 머티리얼이 있는 프레임(씬 로드 · 새 머티리얼 스폰) |
| W2 | Vulkan 업로드 경로 | 제출한 일이 아니라 큐 전체 | RT, GT | 텍스처 업로드가 있는 프레임, 업로드 수만큼 |
| W3 | `FrameRendererPSO.cpp` · `FrameRendererDraw.cpp` | 동기 `createPipelineState` | RT | 새 PSO 키가 처음 나온 프레임 |
| W4 | `TextureCache` · `Material` · `MeshCache` · `PrefabAsset` 의 동기 API | 디스크 읽기 · 디코드, 텍스처는 GPU 업로드까지 | GT | 동기 API 를 부르는 시점 |
| W5 | `GPUUploadQueue::flush` | 워커의 메시 GPU 생성(CPU 대기) | GT | 새 메시가 있는 프레임. 대상 아님 |
| W6 | `CanvasRenderer` → `TextureCache::resizeRenderTarget` | GPU 전체 | RT | 캔버스 렌더 텍스처 크기 변경 → U13 이 지움 |
| W7 | 수명 이벤트 · 도구 | GPU 전체 | 여럿 | 씬 활성화 · 종료 · 창 크기 변경 · 백엔드 교체 · 핫 리로드 · 캡처 도구 — 남는 허용 목록 |
| W8 | `RenderThread` · `FrameCapture` · `EditorUICapture` | `readbackTexture2D` 의 GPU 대기 | RT, 도구 | 캡처 요청이 있을 때만 |

W1 ~ W3 이 프레임 경로이고 이 계획의 대상이다.

- **W1 의 조건**은 원자 카운터 `MaterialCache::hasPendingInitialize` 하나이고 `requestInitialize` 와 `pinAsset` 이 올린다. 안정 상태에서는 0 번이다 **[코드]**. 비용은 한 프레임이 아니다 — `RenderThread::waitIdle` 은 RT 큐가 빌 때까지 기다린 뒤 장치 대기를 해 파이프라인의 프레임을 전부 끝내야 돌아온다 **[코드]**.
- **W2.** RT 는 한 프레임에 `completePendingLoads` 8개 이하, `updateStreaming` 2개 이하, 구간 갱신, 글리프 아틀라스 페이지를 올린다. Vulkan 은 호출마다 큐를 비웠고 큐 락을 쥔 채 `vkQueueWaitIdle` 을 불러 RT 의 `endFrame` 제출까지 막았다 **[코드]**. DX11 · DX12 · GL 의 같은 함수는 GPU 를 기다리지 않는다.
- **W3 의 키**는 (패스 PSO, 퍼뮤테이션 해시, 뷰 모드, 컬 반전)이고 캐시에 있으면 만들지 않아 안정 상태에서는 0 번이다 **[코드]**.
- **고치지 않는 것:** `RT.BeginFrame` 의 GPU 백프레셔 꼬리(대기 가능 스왑체인으로 고칠 히치가 없다고 이미 기각). 기대도 "새 머티리얼 · 업로드가 있는 프레임의 추가 히치 제거"로 한정한다.

## 2. 설계

### 2.1 공통 원칙

- 기다리는 쪽에 기다릴 대상을 주지 않고 대상이 준비 전 / 준비됨 / 실패 중 어디인지를 데이터로 준다. 준비 전이면 그리지 않는다. 폴백은 없고 실패는 오류로 남는다. 상태는 만든 쪽이 release 로 쓰고 읽는 쪽이 acquire 로 읽으며, `Ready` 를 본 뒤에야 GPU 필드를 읽는다.
- GPU 자원을 다른 스레드가 볼 수 있게 바꾸는 지점(bindless 등록, PSO 캐시 삽입)은 **RT 프레임 셋업 구간 한 곳**이다: `FrameRenderer::executePacket` 시작부터 병렬 기록이 켜지기 전까지. RT 한 스레드가 직렬로 도는 구간이고 이전 프레임의 병렬 기록이 끝나 있어, 병렬 기록 중 bindless 테이블을 바꾸지 않는 규칙을 지키는 가장 단순한 위치다 **[코드]**.
- 셋업 구간 작업 순서: (1) 끝난 전송을 `pollTransfer` 로 확인해 bindless 에 등록 · 발행, (2) 머티리얼 GPU 초기화, (3) 새 전송 제출, (4) PSO 상태 갱신.

### 2.2 업로드와 되읽기: 제출 → 티켓 → 폴링

`IRHIResourceFactory` 의 동기 함수 세 개(`uploadTexture2D`, `uploadTexture2DRegion`, `readbackTexture2D`)를 지우고 아래 여섯 개로 바꿨다(ABI 33 → 34).

| 함수 | 뜻 |
|---|---|
| `submitTextureUpload( texture, desc )` | 새 텍스처의 밉 체인 전체를 제출한다 |
| `submitTextureRegionUpload( texture, desc )` | 쓰고 있는 텍스처의 사각형 구간을 제출한다 |
| `submitTextureReadback( texture, mip, slice, outLayout )` | 읽기를 요청한다. 행 배치는 바로 채워 준다 |
| `pollTransfer( ticket )` | `Pending` · `Complete` · `Failed` 를 값싸게 돌려준다 |
| `takeTextureReadback( ticket, outBytes )` | 완료된 읽기 결과를 한 번 가져간다 |
| `discardTextureReadback( ticket )` | 가져가지 않을 읽기 결과를 버린다(`Pending` 이어도 됨) |

제출 함수는 `[[nodiscard]] RHITransferSubmit` 을 돌려준다: `RHISubmitResult`(`Accepted` · `Busy` · `Rejected`)와 `RHITransferTicket`(단조 증가 64비트 시리얼 + 장치 세대, 시리얼 0 은 무효). 값을 직접 짓는 코드는 형식으로 못 막아 백엔드가 막는다 — 시리얼 0 · 다른 장치 세대 · 발급한 적 없는 시리얼은 `pollTransfer` 가 오류를 남기고 `Failed` 다. 장치 세대는 백엔드 인스턴스마다 하나라 백엔드 교체 뒤의 옛 티켓이 새 장치에서 `Complete` 로 보이지 않는다. 계약 10항의 정본은 `IRHIResourceFactory.h` 전송 절 머리 주석과 `pollTransfer` · `registerBindless*` 의 doc 주석이다.

- **소비자는 `Complete` 를 본 뒤에야 텍스처를 bindless 에 등록한다.** 주석이 아니라 코드가 막는다(마지막 전송 시리얼이 워터마크 아래가 아니면 오류와 `kInvalidDescriptorIndex`).
- 렌더 타깃처럼 프레임 커맨드가 상태를 바꾸는 텍스처의 전송은 RT 기록 밖에서만 제출한다. GL 의 `pollTransfer` 는 컨텍스트를 기다리지 않는다.
- **동기 변형은 ABI 밖의 헬퍼다.** `RHITransferUtil::...AndWait`(vtable 에 없음)가 제출 뒤 `pollTransfer` 를 되풀이하며 10 초 상한을 넘으면 `TimedOut` 오류다. 사용처는 테스트 · 도구 · 캡처 · 동기 `acquire` 이고 RT 셋업 구간은 `ScopedForbidBlockingTransfer` 로 표시해 Debug 에서 단언한다. 백엔드에 두 번째 경로가 생기지 않는 것이 핵심이다.
- **콜백이 아니라 폴링인 이유.** bindless 등록은 RT 셋업 구간에서만 가능해 콜백으로 받아도 RT 큐에 넣고 다시 읽게 된다. 버퍼 되읽기는 반대로 프레임 완료 콜백 안에서 한 번 복사하면 끝나 콜백이다.
- **ABI 결정.** 백엔드 DLL 이 같은 vtable 을 봐야 하므로 옛 함수 세 개를 한 번에 지운다. 옛 함수 위에 새 함수를 얹는 shim 은 경로가 둘 남아 폴백 금지와 부딪혀 기각했다. 옛 동기 함수를 지우면 네 백엔드와 모든 호출처가 한 번에 새 계약 위에 서야 컴파일되므로 쪼갤 수 없어 한 묶음(B0)으로 넣었다.
- **스테이징 링.** 장치마다 고정 용량 영속 매핑 링(`gv_rhiStagingRingMegabytes`, 시작 64 MiB 는 추정)을 두고 구간은 티켓 시리얼로 표시해 워터마크가 지나면 회수한다. 링보다 큰 요청은 링이 비었을 때 일회성 단독 버퍼로 받고(`gv_rhiStagingSingleMaxMegabytes` 초과는 `Rejected`), 지금 차서 못 받는 요청은 `Busy` 다.
- **전송 큐.** 1단계는 그래픽스 큐 하나다. 전용 복사 큐는 소유권 이전 · 레이아웃 전이가 필요하고 이득이 텍스처 스트리밍 중 프레임 시간뿐이라 측정 없이 하지 않는다. 소비자 규칙을 `Complete` 로 둔 것은 전용 큐를 도입할 때 규칙을 바꾸지 않으려는 것이다.

**백엔드별 구현**

| 백엔드 | 구현 | 상태 |
|---|---|---|
| DX11 | `gv_rhiDirectUploadMaxKilobytes`(256 KiB) 이하는 `UpdateSubresource`, 큰 것은 `STAGING` 텍스처를 잠금 안에서 복사. 티켓마다 이벤트 쿼리(풀)를 `GetData` 로 폴링. 링 없음(D3D11 에는 버퍼 → 텍스처 복사가 없음) | 끝 |
| GL | 영속 매핑 PBO 링(GL 4.4)에 컨텍스트 밖에서 복사한 뒤 PBO 오프셋으로 `glTexSubImage*`, `glFenceSync` + `glFlush`. `pollTransfer` 는 원자 워터마크를 읽고 컨텍스트를 쥐었거나 비어 있을 때만 `glClientWaitSync( 0 )`. 4.4 가 없으면 업로드 `Rejected`. 되읽기는 PBO 링(`gv_rhiReadbackRingMegabytes`) | 끝(GL-1) |
| DX12 | 전용 업로드 링 + 전송 작업(`D3D12TransferJob`) + 전용 `ID3D12Fence`, 제출은 즉시 `ExecuteCommandLists` + `Signal`(락 순서 `_transferSubmitMutex` → 펜스 줄 잎 락). 되읽기는 링 없이 읽기마다 읽기 힙 버퍼 | 끝(U5) |
| Vulkan | 업로드 · 되읽기 링 둘 + 전송 작업(`VulkanTransferJob`: 커맨드 버퍼 + 펜스). 시리얼은 큐 락 안에서 매기고 `vkGetFenceStatus` 로 앞에서부터 걷어 워터마크를 올린다. 프레임 밖 구조 버퍼 갱신과 GPU 시계 읽기는 아직 `VulkanOneShotCommands` 로 기다린다 | 끝(U6) |

DX12 의 인덱스 반환 지연과 Vulkan 의 해제 큐는 이미 GPU 완료를 기준으로 한다 **[코드]**. Vulkan 타임라인 세마포어 버전 요구는 확인하지 않아 펜스를 쓴다 **[확인 전]**.

**구현에서 정한 것:**
- 결과 옮기기는 DX12 · Vulkan · GL 공용 `RHIStagingRingUtil::settleAllocation` · `RHITransferSubmit::fromUnaccepted` 한 곳이다. 복사 오프셋은 텍셀 블록 바이트와 4 의 최소공배수로 맞춘다. 전송 중 파괴한 텍스처의 해제는 `_transferReleaseQueue` 가 미룬다. 장치 손실은 제출 진입의 `isLost` 가 `Rejected` 로 돌려준다.
- **GL 폴링 수정:** 컨텍스트 없는 스레드의 `pollTransfer` 는 컨텍스트가 비어 있으면 한 번 집어 펜스를 본다(게임 스레드가 동기 로드로 막혀 RT 가 프레임을 돌리지 않으면 10 초 시간 초과까지 멈추던 것).
- **B0 로 생긴 GPU 왕복과 해소:** 이전에 GPU 를 기다리지 않던 DX11 · DX12 · GL 의 프레임 경로 호출처가 `...AndWait` 로 바뀌어 런타임 UI 가 눈에 띄게 느려졌다. 가장 잦은 둘을 비동기로 바꿨다. 글리프 아틀라스는 제출만 하고 새 페이지는 첫 업로드 티켓이 `Complete` 가 된 이후 프레임에 등록한다(그 전에는 그 페이지 글리프를 안 그림). `Busy` 는 더티를 남겨 다음 프레임에 다시 내고 `Rejected` · `Failed` 는 오류를 남기고 그 페이지를 멈춘다. 텍스처 구간 갱신은 `Busy` 면 그 차례의 나머지를 다음 프레임으로 미룬다(순서 유지). 측정: `RT.Canvas.Upload` 최악 DX12 62.9 → 4.2 ms(DX11 · GL 도 같은 규모).

### 2.3 새 머티리얼 첫 사용을 프레임 경계로 (U10)

지금 흐름: 컴포넌트가 머티리얼 경로를 잡으면 `requestInitialize` 가 카운터를 올리고, 게임 스레드의 `submitRenderPacket` 이 카운터를 보고 RT 와 GPU 를 비운 뒤(W1) `initializePending` 으로 파일을 읽고 상수 버퍼를 만들고 bindless 에 등록해 인덱스를 패킷에 싣는다.

바꾸는 흐름:
1. `requestInitialize` 는 그대로(컴포넌트는 디바이스를 모름).
2. 파일 읽기와 파싱은 워커 단계로(텍스처 로드의 두 단계 구조와 같음).
3. GPU 단계(상수 버퍼 생성 · 첫 쓰기 · `registerBindlessBufferSrv` · 텍스처 SRV 해석)는 RT 셋업 구간에서 `MaterialCache::initializePending` 이 하며 프레임당 개수 상한은 측정이 정한다.
4. `Material` 에 원자 상태 `GpuState`(`NotRequested` · `Pending` · `Ready` · `Failed`)를 둔다. 반쯤 초기화된 상태를 못 구분하는 `isRHIValid()` 를 대체한다.
5. 패킷 빌더는 `Ready` 가 아닌 머티리얼의 배치를 만들지 않고 건너뛴 수를 `GT.Material.notReadyBatches` 로 센다. `Failed` 는 오류를 한 번 남기고 그 메시는 그려지지 않는다.
6. `Engine.cpp` 의 `waitIdle` 블록을 지운다(U14 의 `MaterialInit` 이유도 함께).

보이는 변화는 새 머티리얼을 쓰는 오브젝트가 파일 읽는 시간과 패킷 링 깊이만큼(수 프레임) 늦게 나타나는 것이다.

- **인덱스 재사용:** 새 등록은 새 인덱스를 받고 GT 가 놓은 핸들은 `releaseHandle` 이 "넘긴 프레임 + 1" 뒤까지 미루므로 이미 만든 패킷이 옛 인덱스를 보관하는 문제는 막혀 있다 **[코드]**. RT 가 직접 해제하는 DX11 · GL 경로에서 같은 보장이 서는지와 `MaterialInstance::updateRHI` 가 상수 버퍼를 어느 스레드에서 쓰는지는 확인하지 못했다 **[확인 전]**(U10 시작 때 먼저 읽고 이 절에 적는다).
- **기각:** GT 가 레지스트리 락을 잡고 직접 등록(공유하지 않는다는 규칙과 반대), 패킷이 인덱스 대신 머티리얼 핸들을 보관하고 RT 가 해석(형식 변경이 큰 데 비해 이득은 몇 프레임 지연뿐).

### 2.4 PSO 준비 상태 (U11 · U12)

`RenderPSOCache` 의 머티리얼 변형 항목은 (핸들, 소유 여부)이고 실패는 핸들 0, 미생성은 항목 없음이라 "만드는 중"이 없었다.

- **상태(U11, 코드 끝):** `MaterialPSOState`(`Absent` · `Building` · `Ready` · `Failed`)를 항목에 두고 항목은 `building()` · `ready( pso, bOwned )` · `failed()` 로만 만든다(핸들 0 의 `ready` 는 `failed`). `beginMaterialPSO( key )` 가 "처음 올린 쪽만 만든다" 를 한 번에 정한다. `findMaterialPSO` 는 `Ready` 의 핸들만, `findMaterialPSOState` 가 0 의 이유를 답한다. `psoForBatch` 가 상태를 내주고 `drawBatchGroup` 이 `Building` 배치를 `RT.PSO.pendingBatches` 로 센다(생성이 아직 RT 에서 동기라 값은 늘 0 — 이 0 이 U12 의 비교 기준).
- **빌더(U12, 남음):** 요청은 셋업 구간의 `ensureMaterialPSOs` 가 모아 `requestMaterialPSO( key, desc )` 로 넣고 `Absent` 를 `Building` 으로 바꿔 빌더 큐에 넣는다. **전용 스레드 하나**가 순서대로 만든다(이번 프레임 배치가 필요한 키가 먼저, `warmPinnedMaterialPSOs` 의 미리 만들기가 나중). 결과는 완료 목록에 쌓고 RT 가 셋업 구간에서 캐시에 합친다. 그래서 캐시를 쓰는 스레드가 RT 하나로 유지되어 "삽입은 기록 시작 전에만" 불변식이 서고 `ensureMaterialFallbackBuffers` 후처리도 합치는 자리에서 한다. `-gv_psoAsync=0` 이면 동기 생성(A/B 측정용).
- **GL:** `_bThreadSafeResourceCreation` 이 거짓이라 빌더 스레드를 쓸 수 없어 RT 가 프레임당 시간 예산 안에서 만들고 남은 키는 `Building` 으로 둔다(폴백이 아니라 같은 상태 규칙을 다른 방식으로 채움).
- **스레드가 하나인 이유:** PSO 병렬 생성은 이득 9 %(드라이버가 직렬)로 기각됐다. 이 설계는 빠르게 만들려는 것이 아니라 RT 임계 경로에서 빼려는 것이다. 공유 풀에서 돌린 GT 병렬화가 RT 를 170 → 261 µs 로 늦춘 선례가 있어 전용 스레드다.
- **`warmPinnedMaterialPSOs`:** 별도 생성 루프가 "우선순위가 낮은 요청" 넣기로 바뀌어 "고정한 머티리얼의 PSO 가 모두 `Ready`" 를 물을 수 있다.
- **키 공간:** 키 = 씬 메시 패스 수 × 머티리얼 퍼뮤테이션 수 × 마스크에 포함된 뷰 모드 수 × 컬 반전 2 라 불리언 스위치 하나가 키 수를 두 배로 만든다. 에디터에서 뷰 모드를 바꾸면 (머티리얼 수 × 패스 수)만큼 새 키가 한꺼번에 생겨 가장 큰 히치 후보다 **[추정]**. 실제 키 수와 생성 시간 분포는 M0 가 `RT.PSO.create` 로 잰다.
- **빌더 안전성(U12 시작 전 점검):** Vulkan · DX12 의 `createPipelineState` 는 워커에서 부를 수 있다. **DX11 과 GL 은 아직 점검하지 않았고 Dev 의 `ShaderCache::getOrCompile` 도 보지 않았다 [확인 전].**
- **락 점검 결과:** 락 없이 `push_back` 한 벡터의 포인터를 밖으로 내주는 모양을 RHI 전체에서 찾아 고쳤다(GL bindless 등록 · 해제를 `ScopedOpenGLContext` 안으로, `IRHIDevice` 의 렌더 스레드 상태 변수를 atomic 으로, `MaterialCache::acquire` · `reload` 의 초기화를 락 밖으로 + `DeviceInitScope` 가 동시 초기화를 오류로 막음). **락 순서 규칙:** GL 가드가 렌더 스레드의 프레임 끝을 기다리므로, RT 가 아닌 스레드가 락을 쥔 채 등록 · 해제를 부르고 RT 가 프레임 안에서 같은 락을 잡으면 교착이다.

### 2.5 단위별 구현 요약

- **U8 텍스처 업로드 세 단계.** 워커 읽기 → RT 펌프(`completePendingLoads`)의 `beginUploadFromImage`(텍스처 생성 + `submitTextureUpload`, 기다리지 않음) → 이후 프레임 펌프가 `finishUpload`(`Complete` 면 bindless 등록과 공개). `AssetLoadBlock` 에 "업로드 중" 단계와 완료 한 걸음의 결과(`Done` · `Failed` · `Submitted` · `Busy`)를 더했다. 제출 뒤에는 펌프와 `wait()` 가 CAS 로 집은 한 스레드만 확인하고, 다른 스레드의 `wait()` 가 집으면 그 자리에서 블로킹으로 끝낸다. `Busy` 는 완료 대기로 돌아가 다음 펌프가 다시 내고, 프레임당 상한은 제출 수다. 동기 `acquire` 등은 같은 begin + finish 를 블로킹으로 한다. 위쪽 밉 교체도 같은 세 단계다(`beginUpgradeResidency` → `updateStreaming` 이 폴링 → `finishUpgradeResidency`, 반환값은 끝낸 수이고 상한 0 은 폴링만). **남은 위험:** 핫 리로드의 `reload` 는 업그레이드가 끝나길 기다리므로 폴링할 RT 가 없으면 멈춘다(도구 경로).
- **U9 렌더 타깃 읽기.** `RenderThread::readRequestedRenderTarget` 가 요청 프레임에 기다리지 않고 `submitTextureReadback` 만 한 뒤 이후 프레임에 `pollTransfer` → `takeTextureReadback` 해 레지스트리에 게시한다. 읽는 중에는 새 요청을 받지 않고, `Busy` 면 다음 프레임에 다시 내며, `Rejected` · `Failed` · 죽은 핸들은 조용한 실패로 게시한다. `stop()` 이 남은 티켓을 버린다. 스크린샷과 에디터 캡처는 블로킹 헬퍼를 그대로 쓴다. 같은 결함 후보: 에디터 `RenderTargetPanel` 의 `...AndWait` 는 UI 스레드의 도구 경로라 남겼다.
- **U13 렌더 타깃 크기 변경.** `resizeRenderTarget` 의 `waitIdle` 을 지웠다. 옛 GPU 텍스처 · SRV 는 `detachRHI` 로 떼어 스트리밍 교체와 같은 지연 해제 줄(`_retired`)에 넘기고 새 텍스처는 새 핸들 · 인덱스를 받는다. DX11 · GL 이 인덱스를 곧바로 돌려줘도 새 텍스처가 옛 인덱스를 물려받지 않는다(백엔드 대신 이 호출처가 지연 해제로 채움). 핫 리로드의 `waitIdle` 은 도구 경로라 남긴다. **[확인 전]** DX11 · GL 에서 `releaseHandle` 의 "넘긴 프레임 + 1" 이 GPU 가 옛 SRV 를 다 읽은 뒤인지.
- **U14 `waitIdle` 이유.** `RHIWaitIdleReason`(`Shutdown` · `BackendSwap` · `Resize` · `HotReload` · `SceneActivation` · `Capture` · `Tool`, 그리고 U10 이 지울 `MaterialInit`)을 `waitIdle` 의 필수 인자로 더하고 호출처 전부에 이유를 적었다. 호출은 `SW_LOG_TRACE` 로 이유를 남긴다. 이유로 이름을 가르면 프레임 경로의 새 호출이 눈에 띄어, 프레임 경로 폴더에서 `Tool` 이 아닌 호출을 막는 린트를 나중에 달 수 있다(게이트 일부가 정리 중이라 보류).
- **U7 계약 테스트 다섯 케이스(네 백엔드).** 프레임 없이 폴링만으로 `Complete`, 구간 갱신이 업로드 뒤에 보임(제출 순서), 제출 직후 텍스처 파괴가 안전, 가져가지 않고 버린 읽기 16 회 반복 뒤에도 다음 읽기 성공, 전송 대기 중 bindless 등록 거절. 장치 손실 뒤 `Rejected` 와 링이 찬 `Busy` 는 이미 있다.

### 2.6 열린 문제

- **O1. 등장 디더와 준비 시점.** 머티리얼이나 PSO 때문에 RT 가 배치를 건너뛰면 디더가 진행된 채 갑자기 나타난다. 권고안: GT 가 준비 여부(`GpuState`, PSO 는 퍼뮤테이션 해시 단위 원자 조회)를 읽고 `Ready` 가 된 프레임에 `beginAppearance` 를 시작한다. PSO 쪽 조회 API 는 U12 가 정한다.
- **O2. 링 용량과 `Busy` 의 빈도.** 64 MiB 와 프레임당 상한은 추정이며 측정에서 `Busy` 횟수를 보고 정한다.

## 3. 측정

- **M0 (착수 전).** Release 에서 혼자(GPU 독점) 스코프 `GT.Material.waitIdle` · `RT.Upload.waitIdle` · `RT.PSO.create` · `GT.Load.sync` 의 호출 수와 ms 분포, RT · GT 프레임 p99 와 최악 프레임을 잰다. 시나리오는 게임 시작 · 씬 로드 · 새 머티리얼 스폰 · 뷰 모드 전환 · 글자 많은 UI 변경, 설정은 LowEnd 포함. W2 의 합이 작거나 Vulkan 이 아닌 백엔드에서 W1 · W3 이 작으면 U10 · U12 의 우선순위를 낮춘다. 같은 시나리오를 마지막 단위 뒤에 다시 잰다(M1).
- **측정 질문 넷(`RT.Transfer.*` 스코프가 생긴 뒤).** (1) 제출에서 `Complete` 까지 몇 프레임인가(같은 프레임: DX11 · GL · Vulkan, 한 프레임 이상: DX12). (2) `pollTransfer` 한 번의 비용과 프레임당 폴링 수가 늘 때 락 경합. (3) U2 · U6 의 `RT.Upload.waitIdle` 합계와 RT 프레임 p99 전후. (4) `...AndWait` 대기의 CPU 사용(yield 32 번 뒤 200 µs 잠)과 10 초 상한 도달 여부.

## 4. 규칙 (단위가 끝나면 정착 위치로 옮기고 이 절에서 지운다)

1. 게임 스레드가 부르는 새 API 는 GPU · 디스크 · 네트워크 완료를 기다리지 않는다(`request` → 핸들 → 폴링). 동기 변형은 이름에 드러낸다(`AndWait`). 정착 위치: `docs/agents/CppStyle.md` 스레드 항목.
2. RHI 업로드와 되읽기는 "제출 → 티켓 또는 완료 콜백"만 새로 만들고 새 동기 가상 함수는 만들지 않는다(옛 함수를 지운 것이 구조적 강제).
3. 프레임 경로에서 `waitIdle` 을 새로 만들지 않는다. 허용은 수명 이벤트와 도구뿐이며 이유 열거형이 드러낸다.
4. 늦게 도착한 자원은 준비 전엔 그리지 않는다. 폴백 없이 실패는 오류로 남긴다.
5. 쓰기 I/O 는 `ThreadedLocalStore` 를 거친다(형식으로 막지 못해 리뷰가 맡고, 기존 동기 쓰기 호출처 감사는 이 문서 밖 후보).
6. RT 에서 동기 생성이 생기면 `RT.*` 스코프로 감싼다(스코프 없는 동기 생성은 프로파일에 안 보여 다음 히치의 원인). 정착 위치: `docs/agents/CppStyle.md`.

## 5. 위험

| 위험 | 완화 |
|---|---|
| 시그니처를 구현 뒤에 바꾸면 ABI 를 다시 높여 모든 백엔드를 재빌드 | 설계 검토는 끝났고, 이후 변경은 ABI 를 다시 높인다 |
| Vulkan · DX12 전송이 RT 기록과 텍스처 상태를 두고 겹침 | 렌더 타깃 전송은 RT 기록 밖에서만. 디버그 레이어 오류 0 확인 |
| PSO 빌더의 스레드 안전성(백엔드 · 셰이더 컴파일러 잠금 누락) | 네 백엔드 점검 전 U12 시작 금지, `-gv_psoAsync=0` 으로 동기 생성과 비교 |
| Vulkan 이 아닌 백엔드는 W2 에 이미 대기가 없어 이득이 Vulkan 에 몰림 | M0 를 먼저. 이득이 작으면 U10 · U12 를 미룬다 |

되돌리기: B0 번들은 병합 커밋 하나를 revert 하고, 이후 단위는 각각 커밋 단위로 revert 한다(옛 함수와 새 함수가 공존하는 중간 상태는 없다).

## 6. 상용 엔진 비교와 정한 것

언리얼(`FRHIGPUFence` · `FRHIGPUTextureReadback`), 유니티(`AsyncGPUReadback`), Godot(`texture_get_data_async`)도 "제출 → 펜스나 폴링 → 사용"이라 방향이 같고, 다른 점은 콜백 대신 티켓 폴링(소비자가 RT 셋업 구간에서만 일해야 해서)과 1단계에서 전용 전송 큐를 쓰지 않는 것이다. 기억에 기댄 비교이며 결정 근거는 1절의 코드 읽기다.

정한 것(사용자 결정이 필요한 항목은 없다): ABI 34 에서 옛 동기 함수 세 개를 지우고 shim 을 두지 않는다. 완료는 콜백이 아니라 티켓 폴링이다. 1단계는 그래픽스 큐 하나이고 전용 전송 큐는 측정 뒤에 판단한다. GL 업로드는 PBO 링 하나로만 올린다. 글리프 아틀라스는 기다리지 않는다. 새 머티리얼의 GPU 초기화는 RT 셋업 구간에서 하고 준비 전에는 그리지 않으며, PSO 는 네 가지 상태를 데이터로 두고 생성 스레드는 하나다.

확인하지 못한 것: DX11 · GL 의 PSO 생성 워커 안전성, DX11 · GL 에서 RT 가 직접 인덱스를 해제하는 경로의 지연 보장과 `MaterialInstance::updateRHI` 의 상수 버퍼 쓰기 스레드, Vulkan 타임라인 세마포어 버전 요구, 링 용량 64 MiB 와 프레임당 상한의 적정값.
