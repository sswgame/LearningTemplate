# Core (코어 유틸리티)

엔진의 가장 밑바닥(Foundation)에 해당하는 정적 라이브러리(STATIC) 모듈입니다.
문자열, 로그, 파일, 델리게이트, 컨테이너 래퍼, 동시성, 메모리 진단이 여기 있습니다.

## 디렉터리
- **Common/**: `Types.h` · `Macros.h` · `Defines.h`(버퍼 크기 상수) · `StdHeaders.h` · `PlatformOsHeaders.h` · `EnumUtil.h`(비트플래그 연산자) · `BitFlagTrait.h`(`IsBitFlagEnum` 기본 템플릿만 — 강제 include 되는 생성 `*.gen.h` 가 이것만 든다) · `VarIntUtil.h`(LEB128 · ZigZag) ·
  `BuildInfo.h`(`sw::build::kConfigName` · `kPlatformName` — 값은 CMake 가 정한다) · `TopologicalSortUtil`(의존 위상 정렬 — 동점은 이름 순, 순환 경로 찾기. 엔진 기동 단계 · 모듈 적재 순서가 함께 쓴다) · `TargetMacroCheck.h`(아래 "타깃 매크로") ·
  `X11Headers.h`(Xlib 기본 헤더 + 매크로 지우기 — **X11 을 쓰는 `.cpp` 에서만** include 한다. `PlatformOsHeaders.h` 는 PCH 를 거쳐 모든 TU 에 들어가므로
  X11 을 넣지 않는다 — `Convex` · `None` 같은 매크로가 서드파티 헤더를 덮는다(`CheckX11Isolation.py`, 유니티 묶음에서도 그 `.cpp` 는 뺀다) ·
  `X11MacroUndef.h`(Xlib · GLX · XKB 를 더 포함한 바로 뒤에 다시 include)
- **Predefined/**: 엔진과 ReflectionParser 가 함께 include 하는 X-매크로 표(`*.xxx` — 명령줄 인자 · 고정 이름 · 컨테이너 종류 · 애노테이션 종류)와 `AnnotationMeta.txt`
- **Memory/**: `Memory`(`allocateAligned` · 바이트 유틸) · `sw_new` / `sw_delete` · `sw_new_array` / `sw_delete_array` · `make_unique<T>` / `make_unique<T[]>`(`Memory.h`) ·
  `MemoryTag`(아래 "메모리 태그") · `LinearAllocator` · `FrameArenaAllocator`(+ `FrameDoubleBuffer`) · `PoolAllocator` · `MemoryProfiler`(태그별 통계 · 콜스택 · 누수 검사) ·
  할당 관찰자(`Memory::setAllocationObserver` — 외부 프로파일러가 할당 · 해제를 받는다. 관찰 중에 잡힌 블록의 해제만 알린다, 배포본에는 없다)
- **Concurrency/**: `LockFreeObjectPool`, `LockFreeQueue`, `ConcurrentQueue`, `WorkStealingDeque`, `SpinLock`, `Futex`, `DeadlockDetector`, `DataRaceDetector`,
  `mutex`(데드락 탐지 내장 래퍼) · `atomic`(PROPERTY 로 노출 · 직렬화할 수 있는 래퍼) · `ThreadName`(스레드 진입 함수가 OS 에 이름을 적는다 —
  디버거와 Tracy 가 같은 이름을 읽는다: `GameThread` · `RenderThread` · `Worker N` · `IO` · `Logger` · `Net`)
- **Task/**: `TaskManager` · `TaskHandle` · `TaskFuture` (워커 풀 + DAG 스케줄러, `Task/README.md`)
- **Container/**: 표준 컨테이너 별칭(`vector.h` · `unordered_map.h` …) · `span` · `VectorUtil`(`removeAtSwap` 등) · `sparse_set` · `DynamicBitset` · `PagedArray`(주소가 옮겨지지 않는 청크 배열) ·
  `InlineAllocator`(SBO) · 핸들(`SlotHandle` · `SlotHandleTable` · `GameObjectHandle` · `ComponentHandle`) · `RegistrationList`(등록부의 공통 모양 —
  중복 거절 · 정렬 · 이름 찾기 · 이름 사본)
- **String/**: `StringUtil` · `StringBuilder` · `fixed_string` · `hashed_string` · `formatString` · `string_splitter` · `TagID` · `Base64Util`(표준 · URL 안전)
- **Delegate/**: `Delegate`
- **Event/**: `EventDispatcher` · `EventType`(엔진 예약 이벤트 ID — 이벤트 타입은 그 개념이 사는 층에 둔다)
- **Module/**: `ModuleImageUtil` — 동적 라이브러리(모듈 이미지)의 단일 자리: 이름(접두어 · 확장자 · 디버그 심볼) · 올리기 · 심볼 · 이미지 범위 · 의존 고정, 그리고 그 이미지의 코드를 쥔 등록을 떼고 내리기(`unloadModuleImage`). `IModuleUnloadListener`(`ModuleUnloadListener.h`) — 모듈 이미지를 내리기 전에 그 코드(델리게이트 스텁 · vtable)를 떼야 하는 등록부의 공통 계약과 목록.
  같은 수명 계약의 Engine 쪽은 `Engine/Module`, App 쪽(라이브 리로드)은 `App/Module` 이다.
- **Process/**: `Process::terminate` 는 다른 스레드가 `readOutputLine` · `waitForExit` 을 도는 중에 불러도 된다(pid 는 원자). 자식은 출력 파이프 하나만 물려받는다(남의 핸들 · 서술자 상속 없음). 기다리지 않는 실행은 `Process::launchDetached`.
- **Compression/**: `ICompressionCodec` · `CompressionCodecRegistry` · `CompressionStream` · `NullCompressionCodec` · `RleCompressionCodec`
  (zlib · zstd · LZ4 코덱은 외부 라이브러리를 쓰므로 `Engine/Compression` 에 있다)
  - 스트림은 28 바이트 머리(`CompressionHeader` — 'SWCS' · 판 · 코덱 · 크기 둘 · FNV-1a 체크섬)로 시작하고 지금 판만 읽는다.
    `CompressionCodecType` 값은 디스크에 실리므로 새 코덱은 뒤에 덧붙이기만 한다(목록 밖 알고리즘은 `Custom`).
  - 레지스트리는 엔진 서비스 하나(`engine::getCompressionCodecRegistry()`)이고 `CompressionCodecRegistry::setActive` 로 Core 의 슬롯에 걸린다.
    슬롯이 비면(Core 만 링크하는 도구) 스트림은 내장 코덱(None · RLE)만 쓴다. 외부 코덱은 `EngineCompressionCodecUtil::registerAll` 이 한 번에 올린다.
  - 주의: 로드 가능한 모듈이 코덱을 등록했으면 그 모듈의 shutdown 에서 `unregisterCodec` 한다 — 레지스트리는 `Engine.dll` 에 살아 모듈보다 오래 간다.
  - 리소스 팩의 압축 enum(`PackCompressionType`)은 따로인 디스크 형식이다. `PackCompressionUtil::kArrCodecMapping` 표 한 곳에서 옮기고,
    두 enum 을 `static_cast` 로 오가지 않는다.
- **Network/**: 네트워크 공통 계층 — 장르를 모른다. 장르별 방식(권위 서버 복제 · 락스텝 · 롤백 · 턴 중계 · MMO 관심 영역)은 GameFramework 의 `GF_Net*` 키트(DLL)로
  얹어, 싱글 게임은 그 키트를 링크하지 않는다.
  폴더가 층이다 — 뿌리(`NetTypes` · `BitStream`) ← `Transport/`(전송 · 루프백 · UDP · 회선 흉내) · `Security/`(암호 창구 `INetSecurityProvider` — AEAD · X25519 · HKDF · Argon2id · SHA-256 · 서명 RS256/ES256(확인 · 서명 · 키 쌍) · 메모리 TLS 세션, 구현은 Engine 의 OpenSSL ·
  `NetReplayWindow` 재전송 방지 창 · `NetSessionKeyUtil` 세션 키 유도) ← `Connection/`(`NetConnection` · `NetHost` ·
  `NetHostThread` · `SequenceBuffer`) ← `Message/`(`NetMessage` · `NetSendBudget`) ← `Replication/`(키트가 나눠 쓰는 복제 부품 — `NetPrioritizer` · `NetParallel` · `TickRingBuffer` · `NetInputWindow` ·
  `NetClock` · `InterpolationBuffer`).
  아래 층은 위 층을 include 하지 않는다(`CheckCoreNetworkLayers`).
  - `BitStream`(`BitWriter` · `BitReader` — 범위 정수 · 양자화 실수 · 가변 정수, 넘침 감지. 비트를 바이트 덩어리로 쓰고 읽고, 경계에 맞은 바이트는 `memcpy` —
    선 위 배치는 비트 단위 시절과 같다. 길이 붙인 덩어리 `writeBlob` / `readBlob( out, maxSize )` · `skipBlob` — 상한을 넘는 길이는 자르지 않고 넘침으로 거부한다.
    `BitMath::computeVarUintBits` · `computeBlobBits` 는 쓸 비트를 정확히 센다), `NetSendBudget`(메시지 하나의 비트 예산 — 보낼 채널의 상한(기본 `NetConnection::kMaxSingleMessageSize`, 신뢰 순서 하나에 묶으면 `kMaxReliableMessageSize`)으로 잘리고,
    종류 바이트 · 머리 · 목록 길이 · 끝 표시까지 센다. 넘는 메시지는 보내기가 오류와 함께 통째로 버리고, 확인이 안 와 같은 크기를 또 보내는 라이브락이 된다. 키트 틱 예산은 `computeTickBudget`(설정과 연결 상한 × 틱 간격 × 0.5 중 작은 것)), `NetPrioritizer`(관찰자
    하나의 엔티티마다 누적 우선도 — 언리얼 `NetPriority` × 지난 시간 · 유니티 고스트 중요도 × 나이. 틱마다 `우선도 × 시간` 을 쌓고 보낸 것만 0 으로, 순서는 큰 것부터 ·
    같으면 id 순이라 결정적이다. 예산이 늘 차도 낮은 우선도가 쌓여 차례를 얻는다 — 복제 키트 둘(`ReplicationServer` · `MmoReplicator`)이 같이 쓴다), `SequenceBuffer`(16 비트 감김 시퀀스 고리),
    `TickRingBuffer`(32 비트 틱 · 프레임으로 찾는 고리 — 키 전체를 적어 감김 · 건너뛴 칸 비우기가 없고, 무엇이 낡았나는 쓰는 쪽이 넣기 전에 본다. `acquire` 는 옛 값을
    비우지 않아 버퍼를 다시 쓴다. 예측 · 스냅숏 · 보낸 재구성 · 랙 보정 · 롤백 기록 · 락스텝 입력 · 체크섬이 같이 쓴다),
    `NetClock`(받은 서버 틱으로 서버 틱을 추정하고 지연만큼 과거의 렌더 틱을 흘린다 — 지연 = max( 최소값, 표본 간격 × 2 ). 추정은 받은 틱이 하한, 받은 가장 새 틱 +
    지연이 상한이고 렌더 틱은 되돌아가지 않는다. 복제 · 파괴 클라이언트가 같이 쓴다 — 유니티 N4E `NetworkTime` 의 자리),
    `InterpolationBuffer`(틱 순 표본 줄 — 렌더 틱 이하 가장 새 것 · 그보다 큰 첫 것 · 끝이면 멈춤, 첫 것 앞이면 그것을 알린다. 유니티 `BufferedLinearInterpolator` 의 자리),
    `NetInputWindow`(비신뢰 입력 묶음 — `NetInputSendWindow` 는 상대가 확인한 다음 틱부터 가장 새 틱까지를 싣고 예산이 모자라면 오래된 것부터(확인 전에는
    빠지지 않아 연속 손실에 빈틈이 남지 않는다 — GGPO 입력 큐 · 언리얼 `FSavedMove` 목록), `NetInputReceiveBuffer` 는 틱 고리 + 받는 창(위아래 — 고리를 덮지 않게,
    `Manual` · `FollowNewest`) + "빈틈없이 받은 다음 틱" 확인, 깨진 묶음은 하나도 넣지 않는다. 형식 `NetInputFormat` 은 고정 길이(롤백 버튼 1 바이트) · 덩어리 ·
    항목 스탬프(서브틱 입력의 자리) — 롤백 · 권위 서버 입력이 같이 쓴다), `NetTypes`(`NetAddress` ·
    채널
 · 연결 상태 · 메시지 첫 바이트 영역 `NetMessageRange` · 와이어 판 `NetWireVersion` · 프로토콜 id `NetProtocol`)
  - `NetConnection` — 연결 하나의 신뢰성: 패킷 시퀀스 · ack + 32 비트 묶음, 채널(신뢰 순서 · 신뢰 순서 없음 — 받는 대로 건네고 그 자리에 "건넸다" 표, id · 창 · 재전송은 신뢰 순서와 하나 · 순서만 — 메시지 첫 바이트(종류)마다 가장 새 것 하나, 다른 종류끼리는 서로 지우지 않는다 · 비신뢰), 재전송(RTT + 50 ms, 그리고 뒤 패킷 셋이 확인됐는데 확인이 없는 패킷은 바로 — 빠른 재전송), RTT · 손실률 · 대역폭(최근 1 초).
    받은 패킷은 몸을 다 읽은 뒤에야 시퀀스를 적는다 — 깨진 패킷을 확인하면 보낸 쪽이 그 안의 신뢰 메시지를 전달된 것으로 지운다.
    메시지 길이 칸 11 비트(0..1024). **신뢰 순서 메시지는 64 KB 까지 조각으로**(언리얼 partial bunch) — 1 KB 조각마다 신뢰 id 하나(재전송 · 확인 · 창이 조각 단위,
    64 KB = 창 64 칸), 조각 머리 1 비트 "다음 id 가 같은 메시지", 받는 쪽은 순서대로 모아 마지막 조각에서 건넨다(끊기면 `reset` 이 모으던 것을 버린다).
    다른 채널은 조각나지 않아 1 KB 까지. 상한을 넘는 보내기는 오류 로그 + false, 창이 메시지 전체를 못 받으면 로그 없이 false(반쪽 메시지가 없다).
    조각이 패킷을 채워 순서만 · 비신뢰가 남으면 다음 패킷은 그쪽부터(번갈아 — 큰 전송이 스냅샷을 굶기지 않는다). 패킷 끝 1 비트 "확인 요청" — 확인만 담은 답은 끄므로 한가할 때 답에 답이 꼬리를 물지 않는다(RTT · 손실률은 요청 패킷으로 잰다)
  - `NetHost` — 서버 · 클라이언트 끝점: 요청 → 도전 → 응답 → 수락 핸드셰이크(위조 주소 방지 — **상태 없는 도전**: 서버는 요청에 자리를 잡지 않고
    비밀 키 · 주소 · 소금 · 5 초 칸으로 만든 도전 값만 돌려준다. 그 값을 되돌려 준 응답이 만든 칸 · 다음 칸 안에 와야 자리를 잡는다), 프로토콜 id + 체크섬으로 남의 · 깨진 패킷 거르기, 유지 · 타임아웃 · 끊기.
    클라이언트는 `Accepted` 로만 연결된다 — 그것을 잃고 데이터 패킷이 먼저 와도 연결로 치지 않고 응답을 다시 보낸다(수락에만 서버가 준 번호가 있다).
    **와이어 판**: 프로토콜 id = 게임 id(`_gameId`) + Core 판(`NetWireVersion::kCore`) + 게임 · 키트 판(`_wireVersion`, 키트 판은 `NetKitWireVersion`).
    형식을 바꾸는 커밋은 그 층의 판을 올리고 옛 형식은 읽지 않는다. 판이 다르면 서버가 요청을 `VersionMismatch`(다른 게임이면 `Rejected`)로 거절하고 두 쪽 로그에
    두 프로토콜 id 를 남긴다 — 요청 · 거절 패킷만 판과 상관없는 고정 머리(`NetProtocol::kHandshakeId`)로 싸서 판을 넘어 읽힌다(두 패킷 배치는 바꾸지 않는다).
    보낼 것이 없으면 `_sendInterval` 이 아니라 `_keepAliveInterval`(0.25 초)마다만 보낸다. **연결 대역폭 상한**(`_maxBytesPerSecond`, 기본 100000 B/s — 언리얼 `NetSpeed`): 빚 모양 토큰 버킷, 몫이 남으면 차례에 패킷 여럿(`kMaxPacketsPerSend` 8),
    다 쓰면 메시지 없이 확인 · 유지만. 도전 소금 씨앗은 0 이면 OS 난수(`_saltSeed` 는 시험 재현용).
    주소 → 자리 해시(연결 수에 상관없이 받은 패킷 하나에 O(1)), 패킷 · 쓰기 버퍼는 다시 쓴다.
    **스레드 안전** — 공개 함수는 잠금 하나로 지켜져 아무 스레드에서나 보내고 꺼낸다. `update` 는 소켓 받기 · 보내기를 잠금 밖에서 묶어 하고(한 번에
    최대 512 개) 잠금 안에서는 패킷 처리만 한다. **비동기 연결** `connectAsync` → `TaskFuture<NetConnectResult>`(연결 · 가득 참 · 거절 · 타임아웃 · 끊음,
    `then` 은 잠금 밖에서). 여러 스레드가 쓰는 동안 연결 통계는 `getConnectionStats`(사본)
    **암호화**(`NetHostSettings::_security` — `NetHostSecurity.h`, 기본 꺼짐): 응답 · 수락에 일회 X25519 공개 키, 서버는 주소가 확인된 응답에만 키를 계산한다.
    키 = HKDF(공유 비밀, 소금 = 세션 비밀), 데이터 · 끊기 패킷은 AEAD(nonce = 방향 IV XOR 64 비트 번호, 1024 재전송 창 — 복호 뒤 표시). 토큰 결속(서버
    `INetConnectAuthenticator` · 클라이언트 `NetConnectCredentials`)이면 응답의 세션 비밀 증명 태그가 맞아야 자리를 잡고 클라이언트는 수락의 키 확인 태그를 본다.
    암호화 · 결속 여부는 프로토콜 id 에 섞여(`NetProtocolFeature`) 협상하지 않는다 — 한쪽만이면 `SecurityMismatch`. 인증기 없는 암호화는 개발 빌드만(Shipping 서버는 listen 실패)
  - `NetHostThread` — 전용 네트워크 스레드: 소켓을 기다렸다가(`poll` · `WSAPoll`, 최대 2 ms) `update` 를 돌린다. 게임 프레임이 멈춰도 확인 · 유지 · 재전송이
    돌아 끊기지 않고 RTT 에 프레임 길이가 섞이지 않는다. 기다리는 일이라 TaskManager 워커가 아니라 전용 스레드(로그 · 파일 감시와 같은 규칙)
  - `NetParallel` — 서버 키트가 연결(관찰자)마다의 일을 `TaskManager::runParallel` 로 나누는 `NetParallelFor` 와 스레드마다의 작업 자리
    `NetParallelScratch<T>`. 매니저가 없으면 지금 스레드가 돈다(결과는 같다)
  - `NetMessage` — `NetMessageWriter`(종류 바이트 + 몸, 버퍼 재사용), `INetMessageHandler`(영역 하나 + 그 안의 **종류 마스크**를 맡는 쪽 — `GF_Net*` 키트의
    서버 · 클라이언트. 몸은 종류 바이트 뒤의 `BitReader`, 결과는 `NetHandleResult`(Handled · Malformed), 연결 사건 `onConnectionOpened` · `onConnectionClosed`),
    `NetMessageRouter`(종류 256 칸 표로 맡은 처리기 하나에게 준다. `pump( host )` 는 `NetHost::drainInbound` 로 사건 + 메시지를 잠금 한 번에 꺼내
    **사건을 먼저** 모든 처리기에 알리고 메시지를 나눈다 — 같은 자리에 새로 온 연결이 옛 상태로 읽히지 않는다. 깨진 메시지는 세고(`getMalformedCount`) 버리고,
    처리기 없는 것만 돌려준다). 키트는 수신 가드 · `onDisconnected` 같은 손 배선 없이 자기 종류만 읽는다. 손 배달은 `INetMessageHandler::handleMessage`
  - 전송: `INetTransport`(`send` 는 아무 스레드, `receive` · `waitForReceive` 는 `update` 스레드 하나), 실제 UDP(`UdpNetTransport` — 플랫폼 차이는 `PlatformSocketUtil` 한 곳, 송수신 버퍼 1 MB, Windows 는 ICMP 포트 닿지 않음으로
    `recvfrom` 이 실패하지 않게 `SIO_UDP_CONNRESET` 을 끈다), 한 프로세스 루프백 망(`LoopbackNetwork` — 잠금 하나로 끝점마다 다른 스레드가 돌아도 된다. 보낸 순서대로
    다음 `update` 에 배달만 하고 회선을 나쁘게 하지 않는다, 시험 · 리슨 서버), 네트워크 흉내(`NetEmulationTransport` — 회선 나쁨은 이것 하나다. 어느 전송(UDP · 루프백)에나
    씌워 보내는 쪽에서 지연 · 흔들림 · 손실 · 중복 · 깨짐(한 바이트 뒤집기 — 체크섬 시험) · 순서 뒤바뀜 · 대역폭 상한(목적지마다 회선 줄 · 큐 넘침 버림)을 건다.
    조건은 기본값 + 연결별 덮어쓰기, `-gv_netEmuLatencyMs` · `JitterMs` ·
    `LossPercent` · `DuplicatePercent` · `ReorderPercent` · `BandwidthKilobytesPerSecond` 로 `NetEmulationConditions::makeFromGlobalVariables` —
    언리얼 PktLag · PktLoss · PktDup · PktOrder · 유니티 Network Simulator 의 자리. `NetHost` 는 그냥 전송으로 받는다. 조건의 거르개
    `_pDropFilter` 는 고른 패킷만 버린다 — `NetHost::peekPacketType` 과 함께 "`Accepted` 하나만 잃기" · "위조 주소로 간 답 전부 잃기" 같은 시험을 짓는다)
  - 스트림(TCP) 전송 — `IStreamTransport`(수락 · 연결 · 읽기 · 쓰기 완료를 `IStreamHandler` 로, I/O 스레드 N 또는 `pollIo`, 리슨은 묶을 주소를 받는다 —
    운영 끝점은 `NetAddress::makeLoopback`, 서버는 `makeAnyInterface`), 보낼 줄 `StreamSendQueue`
    (64 KB 덩어리 · 높은/낮은 물금 · 상한 — 구현들이 같은 배압 규칙), 루프백 `LoopbackStreamNetwork`(결정적 · 무작위 조각 · 한 번에 넘길 상한, I/O 스레드 없음).
    닫힘은 연결의 마지막 콜백이고 한 번이다. 저쪽 FIN 을 받으면 이쪽도 우아하게 닫는다(반쯤 열린 연결은 두지 않는다).
    구현(`StreamTransportFactory`): Windows IOCP(완료 포트 하나를 I/O 스레드 N 이 나눠 기다림, 연결마다 읽기 · 쓰기 하나씩, AcceptEx 16 · ConnectEx,
    연결 수명은 걸린 작업 수 — 마지막 작업이 돌아온 순간 닫힘 한 번, 시한은 0.1 초마다 한 스레드가 훑는다) · 리눅스 epoll(I/O 스레드마다 epoll(에지 트리거) +
    eventfd, 연결은 돌림차례로 한 루프가 소유, 다른 스레드의 send 는 줄이 비었으면 바로 sendmsg( MSG_NOSIGNAL ), 수락은 루프 0 이 열림 콜백 뒤 배정 루프에 등록)
  - 스트림 메시지 — 길이 접두 프레임(`StreamFrame`: `[u32 길이][종류][깃발][몸]`, 상한은 머리 4 바이트로 몸이 오기 전에 본다, 모르는 종류 · 깃발은 끊는다)과
    끝점(`StreamMessageEndpoint` — I/O 스레드에서 프레임을 잘라 연결별 줄에, 게임 스레드의 `pump` 가 한 잠금에 열림 → 프레임 → 닫힘 순서로. 줄이 상한을 넘으면 읽기를 멈춰
    TCP 창을 닫는다. 핑 · 퐁은 끝점이 I/O 스레드에서 스스로 답해 RTT 를 잰다. 보낼 줄이 넘치면 메시지를 버리지 않고 끊는다(SendQueueOverflow) — 버리면 그 위의 순서가 깨진다. `StreamEndpointSettings::_security` 에 TLS 컨텍스트를 주면
    연결마다 TLS 1.3 세션 — 열림은 핸드셰이크 뒤, 핸드셰이크 · 레코드 검증 실패는 SecurityFailure 로 열림 없이 닫힘만, 우아한 종료는 close_notify 먼저. 위 층은 TLS 를 모른다.
    `_compression` 이 켜져 있으면 프레임 몸을 압축 봉투로(깃발 `kCompressed`, 압축 → TLS 순서), 받는 쪽은 설정과 상관없이 풀고 원래 크기가 몸 상한을 넘으면 풀기 전에 끊는다).
  - 압축 봉투 `NetCompressionUtil`(`NetCompression.h`, 뿌리) — `[u8 코덱 id][varuint 원래 크기][압축 바이트]`, 코덱은 `CompressionCodecRegistry` 의 id 로만(LZ4 · zstd 는 Engine 이
    등록한다), 줄지 않으면 봉투를 쓰지 않는다. UDP 패킷은 측정(`NetCompressionBenchTest`)으로 끔
    시험 도우미 `test::StreamEndpointPair`(`TestFramework/TestStreamEndpointPair.h` — 루프백 위 끝점 한 쌍, 한 스레드로 돈다)
  - 서비스 요청-응답 — `NetRequestClient`(요청 id · 시한 · 취소 · 연결 끊김 · 과부하 중 정확히 하나로 콜백 한 번) · `NetRequestServer`(메서드마다 처리기, 바로 또는
    토큰으로 나중에 `respond`, 멱등 키는 (주체, 메서드, 키) 범위로 기억 — 끝난 키는 기억한 응답, 처리 중인 키는 첫 응답을 같이). 복제용 RPC 가 아니라 서비스 호출이다
- **Math/**: `VectorMath` · `MatrixMath` · `MathUtil` · `Frustum`
- **Time/**: `MonotonicClock`(아래 "시간") · `GameTimer` · `WallClock`(UTC 유닉스 밀리초 — 기간 · 만료 · 기록 시각, 경과 시간은 MonotonicClock) · **Uuid/**: `Uuid` · **CommandLine/**: `CommandLineManager` · **GlobalVariable/**: `GlobalVariableManager`(`SW_GLOBAL_VARIABLE`)
- **Log/**: 층이 둘이다 — **파사드**와 **장치**를 섞지 않는다.
  - `ILogSink` / `Logger` — 매크로가 말을 거는 파사드. 포맷 · 타임스탬프 · 리스너 · 비동기 큐 · 상세도 ·
    Caller 표를 맡는다. 테스트 프레임워크는 이 인터페이스를 구현해 기존 싱크를 **감싼다**(로그 가로채기).
  - `ILogOutput` / `ConsoleLogOutput` / `FileLogOutput` — 완성된 한 줄이 실제로 나가는 장치.
    **장치마다 제 락을 갖는다** — 느린 파일 I/O 가 콘솔 쓰기를 막지 않는다.
    출력을 더 붙이려면 `Logger::addOutput` 을 쓴다 — `Logger` 를 고칠 일은 없다.
  - 값 타입(`LogLevel` · `LogEntry` · `LogRecord`)은 `LogTypes.h` 에 있다. 두 층이 함께 쓰므로
    한쪽 헤더에 두면 장치가 파사드를 include 하게 되어 방향이 뒤집힌다.
  - 로그 문맥(`LogContext` — 스레드 로컬 요청 추적 id 128 비트 · 주체). 문맥이 있는 스레드의 줄에만 `[trace=… acct=…]` 가 붙고(서버 서비스 ·
    저장소 일), 없는 줄은 바이트가 같다. 비동기 경계는 넘기는 쪽이 복사해 들고 받는 쪽이 `ScopedLogContext` 로 다시 건다(`IServiceStoreWork` 가 그 예).
    요청 머리(`NetRequestOptions` · `NetRequestContext::_traceId`)가 클라이언트의 추적 id 를 서버까지 싣는다.

## 플랫폼 의존 코드는 어디에 두는가

같은 일을 두 방식으로 하고 있으면 플랫폼을 하나 더 지원할 때 한쪽을 빠뜨린다. 규칙은 하나다 —
**플랫폼 분기는 그 분기만 아는 자리에 둔다.**

| 표면 크기 | 두는 곳 | 예 |
|---|---|---|
| 타입·클래스 단위로 다르다 | `File/Windows` · `File/Linux` 처럼 **플랫폼 폴더** | `WindowsFileDialog`, `WindowsFileWatcher` |
| 함수 이름만 다르다 | 원시 연산 하나를 감싸는 **`*Util` 한 곳** | `PlatformFileUtil::openFile` / `seekTo` / `tellPosition` |

- `PlatformFileUtil` 은 이름이 다른 원시 연산만 담는다 — `openFile`(Windows 는 UTF-16 경로 · 공유 열기 `_wfsopen`) ·
  `seekTo`(`_fseeki64`↔`fseeko`) · `tellPosition`(`_ftelli64`↔`ftello`) · `replaceFile`(원자적 바꿔치기) · `getOpenFileSizeAndRewind`.
  같은 `#if` 를 `FileUtil` · `Logger` · `ResourcePackReader` 에 다시 쓰지 않는다.
- 표준 라이브러리가 이미 플랫폼을 덮어 주면 **분기를 만들지 않는다.** 파일 존재 · 크기 · 시각 · 순회 · 복사 · 삭제는 `std::filesystem` 이 하되
  `File/Std/FileUtilStdFileSystem.cpp` 한 TU 안에서만 쓴다. 엔진은 `FileUtil` 로만 본다 — 한 함수를 플랫폼 API 로
  바꿀 때는 그 정의만 `File/Windows` · `File/Linux` 로 옮긴다(측정에서 이길 때만).
- 플랫폼 · 아키텍처 · 컴파일러는 아래 "타깃 매크로" 의 `SW_*` 매크로로만 묻는다.

## 타깃 매크로
- 코드는 `SW_PLATFORM_WINDOWS` / `_LINUX` · `SW_X64` / `SW_ARM64` · `SW_COMPILER_*` 만 읽는다. 판정은
  `cmake/Modules/{Platform,Architecture,Compiler}/` 가 한다.
- 컴파일러 내장 매크로(`_WIN32` · `_MSC_VER` · `__clang__` · `__x86_64__` …)를 읽는 곳은 `Common/TargetMacroCheck.h` 하나뿐이다
  (`Scripts/lint/gate/CheckTargetMacros.py` 게이트). 이 헤더는 `Macros.h` 맨 위에서 포함되어 CMake 판정이 실제 컴파일러와 어긋나면 빌드를 세운다.
- 주의: clang-cl 은 `SW_COMPILER_CLANG` 이다. MSVC 확장(`__forceinline` · `__declspec` …)을 쓸 수 있는지는 `SW_COMPILER_MSVC` 가 아니라
  `SW_PLATFORM_WINDOWS` 로 묻는다(Windows 는 MS ABI 툴체인으로만 짓는다).
- 빌드 구성 · 플랫폼 **이름** 문자열은 `BuildInfo.h` 의 `sw::build::kConfigName` · `kPlatformName` 을 쓴다. `#if` 사슬로 다시 만들지 않는다.

## 비동기 파일 IO
- `AsyncFileIo`(`File/AsyncFileIo.h`)는 읽기 요청(파일 전체 `readFile` · 구간 `readRange` · 연 파일의 구간)을 우선순위 큐에 넣고, 백엔드가 높은 우선순위부터
  꺼내 OS 에 겁니다(UE `IAsyncReadFileHandle` + IoStore 우선순위 큐 · Unity `AsyncReadManager` 자리). 동시에 OS 에 걸린 수는 `_maxInFlightCount` 로 묶여
  뒤에 온 급한 요청이 대량 요청 뒤에 줄 서지 않습니다. 같은 우선순위는 들어온 순서입니다.
- 백엔드: Windows 오버랩드 IO + 완료 포트(`File/Windows/WindowsAsyncFileIoBackend.cpp`), 리눅스 io_uring(`File/Linux/LinuxAsyncFileIoBackend.cpp` — liburing 없이
  시스템 호출 직접, `IORING_OP_READV`), 어디서나 도는 스레드 풀(`AsyncFileIo.cpp`). `Auto` 는 플랫폼 것을 고르고, io_uring 을 쓸 수 없으면(ENOSYS · EPERM —
  옛 커널 · WSL1 · 컨테이너 seccomp) 로그 한 줄과 함께 스레드 풀로 내려갑니다. 명시한 백엔드를 쓸 수 없으면 `initialize` 가 실패합니다(폴백하지 않는다).
- 정책(우선순위 · 상한 · 취소 · 파일 열기와 버퍼 준비 · 완료 전달)은 `AsyncFileIoQueue`(`File/AsyncFileIoBackend.h`) 한 곳이고, 백엔드는 "꺼내 걸고, 끝나면 알린다" 만 합니다.
- 파일 열기 · 크기 확인 · 버퍼 할당은 IO 스레드가 합니다. 버퍼는 **요청한 스레드의 메모리 태그**로 셉니다.
- 완료 콜백은 요청마다 한 번(성공 · 실패 · 취소). `TaskManager` 를 넘기면 그 워커에서(Low · Normal → `TaskPriority::Low`, High · Critical → `Normal` — High 줄은 렌더 · 물리 몫),
  아니면 IO 스레드에서 돕니다. `readFileFuture` 는 결과를 `TaskFuture` 로 돌려줍니다.
- 결과: `Succeeded` · `Canceled` · `FileNotFound` · `OutOfRange`(구간이 파일 끝을 넘으면 짧게 읽지 않고 실패) · `ReadFailed` · `ShutDown`(시작 전 · 내린 뒤 요청).
- 취소: 큐에 있으면 OS 에 넘기지 않고, 이미 걸렸으면 읽은 뒤 결과를 버립니다(진행 중 취소는 "최선" — UE · Unity 와 같다).
- 엔진은 서비스 하나(`engine::getAsyncFileIo()`)를 기동 단계 `FileIo` 에서 세우고 Task · 모듈 이미지보다 먼저 내립니다(걸린 읽기와 완료 태스크를 다 기다린다).
  주의: 완료 콜백이 핫 리로드되는 모듈의 코드를 가리키면, 그 모듈을 내리기 전에 핸들을 취소하고 기다려야 합니다(델리게이트와 같은 규칙).
- `PlatformFileUtil::openNativeFileForRead` · `readNativeFileAt` 은 공유 파일 위치가 없는 위치 지정 읽기입니다 — 여러 스레드가 한 핸들을 잠금 없이 읽습니다. Windows 는
  `FILE_FLAG_OVERLAPPED` 로 열고, 동기 읽기는 낮은 비트를 세운 이벤트로 기다려 완료 포트에 묶인 핸들에서도 패킷이 가지 않게 합니다.

## 메모리
- **맨 `new` 는 쓰지 않는다**(`Style/RawNew` 린트). 객체는 `sw_new T( ... )` · `make_unique<T>`, 배열은 `sw_new_array<T>( n )` /
  `sw_delete_array( p, n )` · `make_unique<T[]>( n )` · `vector<T>`. CRT `new` 는 메모리 태그 · 누수 검사에 보이지 않는다.
- 주의: `sw_new T[n]` 은 원소 소멸자를 부르는 해제 짝이 없다 — 소멸자가 있는 원소는 `sw_new_array` · `vector` 로 담는다
  (`sw_delete_array` 의 static_assert 가 막는다). `make_unique<T[]>` 의 해제자도 원소 소멸자를 부르지 않는다.
- 정렬 할당 여부는 `kUsesAlignedAllocation<T>` 한 기준을 `sw_new` · `sw_delete` · `make_unique` · `Allocator` 가 같이 쓴다.
- 외부 라이브러리(pugixml · JSON · zlib · zstd · LZ4 · stb_image · ImGui)도 할당 훅으로 sw 할당자에 보낸다 — 태그 보고에 잡히게.

## 메모리 태그
- `MemoryTag`(`Memory/MemoryTag.h`)는 할당을 **용도**로 나눈다(UE LLM 의 태그와 같은 역할 — Texture · Mesh · Audio · Animation · Physics · UI · Script …). 스레드 로컬 값 하나를 할당 헤더에 적고
  `MemoryProfiler` 가 태그별로 센다. 해제는 헤더의 태그로 빼므로 어느 스레드에서 풀어도 같은 줄에서 빠진다.
- 거는 자리는 하위 시스템의 **진입점**(기동 단계 · 서비스 생성 · 에셋 종류별 로드 · 씬 로드 · 렌더러 · 모듈 호출)이다 — `SW_MEMORY_SCOPE( Tag )` / `ScopedMemoryTag`.
- 태스크는 만든 쪽의 태그를 노드에 담아 실행 중에 쓰고(`Task/README.md`), 엔진이 띄우는 스레드는 띄운 쪽의 태그를 받아 첫 줄에서 건다.
  새 스레드는 `Unknown` 에서 시작한다 — `Unknown` 줄이 크면 진입점이 빠진 것이다.
- 스코프 · 할당 헤더 · `MemoryProfiler` 는 배포본이 아닌 모든 구성에 있다. 추적은 Debug · 시험 하네스에서 켜져 있고 Release 는 꺼 둔 채 `-gv_memoryTracking=1` 로 켠다 —
  꺼져 있으면 할당마다 분기 하나다(Release 64 B 할당 + 해제 63 ns → 켜면 73 ns, `MemoryTagBenchTest`). 배포본에는 헤더도 프로파일러도 없다.
- 명시 태그: 잡는 곳과 쓰는 하위 시스템이 다른 버퍼는 `Memory::allocate( size, tag )` · `allocateAligned( size, align, tag )` 로 그 자리에서 태그를 준다.
- 태그마다 살아 있는 바이트 · 블록, **최고치**(`_peakAllocatedBytes` · `resetPeaks`), **예산**(`setBudget` · `reportExceededBudgets` — 넘을 때 경고 한 번, 90 % 아래로 내려가면 다시 건다)을 든다.
  예산 데이터 · 프레임 검사 · 표 보고(`-gv_memoryReport=1`) · FrameProfiler 카운터(`Mem.LiveKB` · 예산 있는 태그의 `Mem.<태그>KB`)는 Engine 의 `MemoryBudgetMonitor` 가 한다.
- 누수 보고: `captureMemoryLeakBaseline` 이 태그별 기준선도 찍고, Debug 종료 끝(`EngineBootstrap::shutdown`, 프로파일러만 남은 때)에 기준선보다 늘어난 태그를
  stderr 로 남긴다(`[MemoryLeak] shutdown - tag …`). CRT 검사는 힙 **합계**만 견주므로 서비스가 내려가 합계가 줄면 남은 블록을 보지 못한다 — 태그 보고는 본다.
- 새 태그를 더하면 `MemoryProfiler::getMemoryTagName` 의 이름 표에도 한 줄을 더한다(줄 수는 static_assert 가 본다).

## 시간
- 엔진의 단조 시계는 `Time/MonotonicClock.h` 하나다 — 지금 시각 `MonotonicClock::nowNanoseconds` / `nowMicroseconds`, 경과 시간 `Stopwatch`,
  기한 `Deadline::afterMilliseconds( ms )`(`isExpired` · `getRemainingMilliseconds`). 엔진 코드에서 `std::chrono::steady_clock::now()` 를
  직접 읽지 않는다(시험 코드 포함) — 프로파일러 · 로그 · 기한이 같은 시각을 봐야 한다. `Scripts/lint/gate/CheckClockReads.py` 가 막는다.
- 기다리는 루프는 횟수가 아니라 시간(`Deadline`)으로 묶는다. 횟수 상한은 느린 머신에서 정상을 실패로 만든다.
- 프레임 델타 · 일시정지가 필요하면 `GameTimer`(같은 OS 카운터)를 쓴다.
- 기준점(epoch)이 있는 UTC 시각(서버의 만료 · 기록 시각 · 기간)은 `Time/WallClock.h` 하나다(`WallClock::nowUnixMilliseconds`) — `system_clock` 을 읽는 유일한 파일.
  NTP 보정으로 거꾸로 갈 수 있으니 경과 시간에는 쓰지 않고, 서비스에는 `nowMs` 매개변수로 넘긴다(시험이 가짜 시각을 넣는다).

## 빌드 모델
- 소스는 `Core_objects`(OBJECT)에서 **한 번만** 컴파일됩니다.
- `Core` STATIC = 그 OBJECT 아카이브 → `Tools/ReflectionParser`가 직접 링크합니다.
- `Engine`는 동일 OBJECT를 링크해 Dev에서 `Engine.dll`로 foundation 심볼을 export합니다 (App/Editor는 `SW_IMPORTS`로 dllimport).
- 소스 목록은 `CMakeLists.txt` 에 **경로 문자열로 적혀 있다**(GLOB 이 아니다). 새 `.cpp` 를
  추가하면 거기도 같이 고쳐야 하고, 잊으면 컴파일러가 알려주지 않는다.

## 핵심 규칙
- **독립성 유지**: `Core`는 `Engine`이나 `GameFramework`, `Game` 폴더의 코드에 **절대 의존해서는 안 됩니다.**
- **어디서나 쓰임**: ReflectionParser에도 직접 링크되므로 무거운 GPU/에디터 의존성은 피해야 합니다.

## 함정 · 계약

- **`drainEvents` 는 붙이고 비운다(2026-10-03 통일).** 바꿔치기(`swap` · `std::move`) 하던 18 곳을 맞췄다 — 바꿔치기는 받는 쪽 목록의 앞 내용을 지우고, 붙이기에
  익숙한 호출부는 같은 알림을 매 프레임 다시 받는다(StarSkirmish 에서 패배 로그가 두 번). 매 프레임 같은 목록을 쓰는 쪽이 먼저 `clear()` 한다.
- **X11 헤더는 X11 을 쓰는 `.cpp` 에서만**(`Core/Common/X11Headers.h`, 게이트 `CheckX11Isolation.py`). `PlatformOsHeaders.h` 가 X11 을 들고 있을 때 PCH 로
  모든 TU 에 `Convex` · `None` 같은 매크로가 퍼져 Jolt(`EShapeType::Convex`)가 리눅스 다섯 잡을 세웠다 — Windows 빌드는 원리상 못 본다. 유니티 빌드는 X11 `.cpp` 를
  include 줄을 보고 묶음에서 뺀다(`sw_skipUnityForX11Sources`).
- **플랫폼 스텁도 인터페이스를 따라간다** — `IWindow` 에 가상 함수를 더하면 `Win32Window` 의 비-Windows `#else` 스텁에도 정의를 둔다(빠지면 리눅스 링크만 진다).
- **Win32 는 W 판을 이름으로 부른다** — 이 저장소는 UNICODE 를 정의하지 않아 일반 이름(`DefWindowProc` · `LoadCursor` · `CreateFile` …)은 A 판이다. W 클래스로 만든 창의 프로시저가 `DefWindowProcA` 로 끝나 제목이 "S" 한 글자였다(`WindowTest.TitleReachesTheOsAsUtf16`). 게이트 `CheckWin32WideCalls.py`, `IDC_*` 는 `reinterpret_cast<LPCWSTR>` 로 넘긴다. 전역 UNICODE 정의(언리얼)는 대상마다 정의가 빠지면 말없이 A 로 돌아가 택하지 않았다.
- **A 판도 부르지 않는다 — 실행 파일 매니페스트에 기대지 않는다** — 모든 실행 파일은 `WindowsProcess.manifest` 의 `activeCodePage=UTF-8` 로 ANSI 코드 페이지가 UTF-8 이라 A 판에 UTF-8 경로를 넘겨도 지금은 통한다(한글 폴더 시험 `FileTest.DynamicLibraryLoadsFromANonAsciiFolder` · `CrashReportTest.ReportFolderWithANonAsciiNameReceivesTheFiles` 가 고치기 전에도 통과). 매니페스트가 없는 호스트(남의 프로세스에 올라간 Engine.dll · 1903 이전 Windows)에서는 깨지므로 `CheckWin32WideCalls` 가 A 판 호출도 막는다(예외 `OutputDebugStringA`). 크래시 경로는 힙 없이 스택 버퍼 + `MultiByteToWideChar` 로 바꾼다.
- **X11 창 제목은 `_NET_WM_NAME`(UTF8_STRING)까지 적는다** — `XStoreName` 은 Latin-1 이라 한글 제목이 깨진다. 확인 시험은 없다(WindowTest 는 host 스위트라 CI 가 못 돌리고, 시험 실행 파일이 libX11 을 직접 링크하지 않는다) — 리눅스 기계에서 `xprop _NET_WM_NAME` 으로 본다.
  올라온 이미지는 이름이 아니라 주소로 찾는다(`ModuleBuildId::find( &함수 )._modulePath`) — `Engine.dll` 을 글자로 찾던 시험이 리눅스(`Lib/libEngine.so`)에서
  늘 건너뛰어 "아무것도 검증하지 않은 스위트" 로 졌다.
- **명령줄 철자는 인자마다 하나다** — `ArgumentList.xxx` 의 줄에 적은 것만 키이고 열거자 이름(`WIDTH` · `COOK_SHADERS`)은 키가 아니다(`CommandLineTest.EnumeratorNameIsNotACommandLineKey`). RHI 백엔드 줄만 쿠킹 표(`CookContract.json`)의 별칭 여럿을 받는다.
- **플랫폼 · 아키텍처 · 컴파일러는 SW_ 매크로로만 묻는다** — 컴파일러 내장 매크로(`_M_X64` · `__clang__` · `_MSC_VER` …)를 읽는 곳은
  `Core/Common/TargetMacroCheck.h` 하나(CMake 판정과 대조해 `#error`), 나머지는 `CheckTargetMacros` 게이트가 막는다. 아키텍처 판정은
  `CMAKE_CXX_COMPILER_ARCHITECTURE_ID` 기준(교차 컴파일에서 `CMAKE_SYSTEM_PROCESSOR` 는 틀린다). "MSVC 확장을 쓸 수 있는가" 는 `SW_PLATFORM_WINDOWS`
  로 묻는다(clang-cl 도 그렇다 — `SW_COMPILER_MSVC` 로 물으면 clang-cl 이 다른 갈래로 간다). MinGW 를 지원하면 이 전제와 검사 헤더를 함께 바꾼다.
  ReflectionParser(libclang)는 CMake 를 거치지 않으므로 `ParserConfig::load` 가 대상 매크로를 넘긴다. Linux arm64 · macOS 갈래는 실제 빌드로 확인된 적이 없다.
- **소유하지 않는 포인터 등록부는 `Core/Container/RegistrationList<T>`**(중복 · 이름 거절, 순서, 이름 찾기, 이름 사본) — 슬롯 인덱스로 O(1) 빼기를 하는 등록부
  (Primitive · Tick · TransformHierarchy · 콜라이더)와 모양이 다른 것(TypeRegistry · GlobalVariable · 코덱 · RHIBackend)은 예외다.
- **X-매크로 목록 `.xxx` 의 정본은 `Core/Predefined/`** 이고 죽은 사본은 `CheckDataFileReferences` 가 막는다. `PredefinedNameType.xxx` 의 줄 순서가 곧 intern 인덱스다(중간 삽입
  금지, 대소문자만 다른 이름 금지). `CheckDataFileReferences` 는 참조 파일(.cpp · .h · .cmake · .py · .txt · .json · .xxx …)이 staged 됐을 때만 훅에서 돈다.
  게이트의 제외 폴더는 `kNotOurDirNames` 를 쓴다 — 게이트마다 목록을 들지 말 것(자체 목록이 `LLVM` 을 빠뜨려 그 헤더를 참조 파일로 읽고 있었다).
- **4 글자 표식은 `FourCcUtil::make( "...." )`(파일 바이트 순서) 하나** — 16 진 손글씨는 바이트 순서가 둘로 갈렸었다. 형식을 바꾸면 커밋된 데이터
  (`Test/AppTest/Golden/*.ppm.z` 압축 스트림 등)의 앞 네 바이트도 같이 고친다.
- **에셋 · 파일 글로 `hashed_string` 을 만들지 말 것.** intern 표는 줄지 않고(상한에 닿으면 그 뒤 엔진의 **모든** 새 이름이 None), 대소문자를 무시하며 영구 적재된다. 조회는
  `hashed_string::findInterned`, 해시만 필요하면 `computeHash( string_view )`. 진단은 `getInternedCount`. 같은 이름 번호는 밑 이름별로 되쓴다(`SameNameChurnKeepsInternPoolBounded`).
- **`hashed_string` 은 FName 규칙이다** — 같음 · 해시는 대소문자 무시, `c_str()` 은 적은 철자, `operator<` 없음(`HashedStringLexicalLess` · `HashedStringFastLess`), 대소문자만 바꾸는
  이름 변경은 `isEqual( …, NameCase::CaseSensitive )`. FName 과 일부러 다른 셋은 되돌리지 말 것: 해시는 실행마다 같은 FNV(저장된다), 철자 보존은 모든 구성, 숫자 꼬리 없음.
- **문자열 해시 식을 바꾸지 말 것.** `StringUtil::computeHash64`(FNV-1a, 문자를 `make_unsigned_t<CharT>` 로 넓힌다)에 쿠킹 산출물 · 셰이더 쿠킹 스탬프 · 파이썬 쿠커가 걸려 있다.
  `RuntimeStringHash` 는 프로세스 안 전용이다. `computeHash64( "리터럴", false, seed )` 는 포인터 오버로드에 묶여 키가 상수가 된다 — `string_view` 로 넘긴다.
- **`sw::unordered_map` 은 밀집 배열, `sw::map` 은 정렬 벡터다**(`SW_ENABLE_STL_CONTAINER` 꺼짐). 삽입 · 삭제가 원소를 옮기므로 표 안 원소의 포인터를 잠금 밖으로 내주지 말 것 —
  복사로 주거나 값을 `unique_ptr` 로 든다. 짧은 이름(SSO) `string` 의 `c_str()` 도 이동 뒤 빈자리를 가리킨다(프로파일러 이름은 intern 아레나에).
- **압축** — 팩 enum `PackCompressionType` 과 스트림 enum `CompressionCodecType` 은 독립된 디스크 포맷이다(`static_cast` 로 잇지 말 것). `CompressionCodecRegistry` 는 `EngineLoop` 가 소유하고
  Core 에는 슬롯만 있다. 모듈이 등록한 코덱은 그 모듈 shutdown 에서 `unregisterCodec`. zlib 은 Windows 에서 4 GB, LZ4 는 2 GB 가 한계다.
- **리눅스에서만 지은 코드를 들일 때** — 윈도우 매크로 `near` · `far` · `small` 와 겹치는 이름, DLL 이 내보내지 않은 타입(`SW_GF_API` — 리눅스 .so 는 다 내보낸다),
  같은 이름의 타입이 두 모듈에 있는 ODR 위반(`CheckDuplicateTypeNames` 가 막는다)을 먼저 본다. 병렬 본문은 컨테이너를 만지지 않고 나누기 전에 받은 포인터로만
  접근한다(`NetParallelScratch` · `engine::runParallel` — 컨테이너 `operator[]` 는 경합 검출기에 쓰기로 세어진다).
- **순서만 채널(`UnreliableSequenced`)은 메시지 종류(첫 바이트)마다 흐름이다** — 예전엔 연결 전체가 흐름 하나라 한 보내기 간격의 다른 종류 메시지가 서로 지웠다.
  같은 종류로 여러 조각(오브젝트 · 부분)을 보내는 것은 여전히 서로 지우므로 비신뢰 + 받는 쪽 틱 정렬로 보낸다(`DestructionReplication` 의 자세).
- **`quaternion::inverse()` · `conjugate()` 는 const 가 아닌 값에서 제자리 버전(void)이 골라진다** — 식 안에서는 const 참조로 받아 부를 것(`RigIkSolver::makeInverse`).
  **`quaternion::fromToRotation` 은 코사인 차 1e-6(약 0.08°) 안쪽을 단위 회전으로 버린다** — 반복 IK 의 마지막 몇 mm 가 그 안이라 CCD 가 멈춘다(`RigIkSolver::makeFromToRotation`).
- **Debug 기동은 CRT 누수 보고를 stderr 로도 낸다**(`EngineBootstrap` 의 진단 갈래가 `MemoryProfiler::enableMemoryLeakChecks` 를 부른다 — 누수 덤프가 콘솔 · CI 로그에
  나온다, `MemoryTagTest.DiagnosticBootstrapEnablesPlatformLeakChecks`).
- **Windows UDP 는 돈다**(2026-10-05, `NetworkTest.UdpTransportSendsDatagramsOverLocalhost` · `NetworkThreadTest.UdpHostsRunOnThreadsOverLocalhost`, 닫힌 포트로 보낸 뒤
  받기 포함). `SIO_UDP_CONNRESET` 끄기와 받기 고리의 "오류는 건너뛰고 다음 것" 을 둘 다 빼도 시험은 통과한다 — 루프백 ICMP 리셋을 이 시험이 재현하지 못하니 두 방어를 지우지 말 것.
- **회선 나쁨은 `NetEmulationTransport` 하나다**(N17) — 루프백 망은 보낸 순서대로 다음 `update` 에 배달만 한다. 흉내는 보내는 쪽 줄이라 호스트를 한 스레드에서
  차례로 돌리는 시험은 프레임마다 흉내를 **모두 먼저** `update` 한다(아니면 뒤에 도는 호스트가 보낸 것이 한 프레임 늦다 — `NetSimHarness` 와 시험 도우미가 그렇게 한다).
  깨짐 난수는 깨짐을 켰을 때만 뽑아 다른 조건의 수열을 바꾸지 않는다(하니스 · 파괴 시험이 바이트까지 그대로인 이유).
- **큰 신뢰 메시지는 Core 가 조각으로 나른다(64 KB, N20) — 그래도 조각마다 창 한 칸이다.** 메시지를 묶어도 창 몫(바이트 ÷ 1 KB)은 줄지 않으므로, 나누는 까닭이
  창(앞부분이라도 나가게)이면 키트의 쪼개기를 남긴다(MMO 나감 · 턴 대기 줄). 묶어서 이득은 엔티티마다의 머리 · 창 칸이 작은 것이 많을 때다(MMO 들어옴).
- **조각이 패킷을 채우면 비신뢰가 굶는다** — 1 KB 조각 뒤에는 150 B 남짓이라 600 B 스냅샷이 못 들어간다. `NetConnection::writePacket` 은 남긴 쪽부터 번갈아 싣는다.
- **대역폭 상한(N21a)은 빚 모양 토큰 버킷이다** — 몫이 0 보다 크면 꽉 찬 패킷 하나, 쌓는 몫은 보내기 간격 두 번어치. 다 써도 머리(확인 · 유지)는 가므로 타임아웃보다 오래 포화돼도
  끊기지 않는다. 상한 없이 "한 차례에 여럿" 만 넣으면 회선을 넘친다 — 둘은 같이 간다. 키트 예산은 `NetSendBudget::computeTickBudget`(설정과 상한 × 틱 × 0.5 중 작은 것).
- **신뢰 · 순서 없음으로 옮긴 메시지는 같은 채널의 다른 메시지와의 앞뒤도 잃는다(N21b).** 파괴 사건은 번호로 서로 줄 세웠지만 "청한 스냅숏보다 뒤 사건은 늦게 온다" 는
  순서 채널이 주던 것이었다 — 스냅숏을 기다리는 동안 사건을 쌓지 않으면 스냅숏이 앞서 적용한 사건을 덮어 영영 빠진다. 채널을 바꿀 때는 받는 쪽이 기대던 앞뒤를 모두 적는다.
- **신뢰 재전송 간격을 짧게 고정하면 꼬리는 줄지만 회선을 먹는다**(2026-10-05 측정). 0.1 초 고정은 250 ms · 15 % 에서 사건 최대 지연 1.0 초였지만 메시지당
  재전송 3.5 번 · 서버 올림 3 배로 파괴 시험(덩어리 오차)이 졌다. RTT + 50 ms 와 빠른 재전송(뒤 패킷 셋 확인)이 재전송 1.9 배 · 올림 +9 % 로 4.1 → 1.3 초다.
- **복제 예산은 메시지 전체를 정확히 센다**(`NetSendBudget`) — 1024 B 를 넘는 메시지는 `sendMessage` 가 버리고 확인이 안 와 기준이 그대로라, 다음 틱도 같은
  크기로 다시 버려지는 라이브락이 된다(사라진 id 400 개 = 1200 B 에서 클라이언트 틱이 멈췄다). 길이는 `writeBlob` / `readBlob( 상한 )` 하나로 — 자르는 쪽과 쓰는 쪽이 어긋났다.
- **비신뢰 입력을 "최근 N 개" 만 겹쳐 보내면 N 을 넘는 연속 손실이 영구 빈틈이 된다** — 롤백은 30 틱 끊김 뒤 두 쪽이 120 프레임에서 영원히 멈췄다. 상대가 확인한
  다음 틱부터 보낸다(GGPO) — Core `NetInputSendWindow` 가 구조로 그렇게 하고, 예산이 모자라면 **오래된 것부터** 싣는다(새 것부터 실으면 못 실은 옛 것이 같은
  빈틈이 된다). 받는 고리(`NetInputReceiveBuffer`)는 아래 · 위 창을 둘 다 둔다 — 위 창이 없으면 고리 한 바퀴 뒤의 먼 틱이 받아 둔 입력을 덮는다.
  권위 서버 입력도 같은 부품이다 — 확인은 스냅숏의 `_firstMissingInputTick`, 서버가 꺼낸 틱은 받는 창 아래로 놓아 확인이 넘어간다(`InputBurstLossLeavesNoGap`).
- **틱 · 프레임으로 찾는 고리는 `TickRingBuffer` 하나다**(예측 · 스냅숏 · 보낸 재구성 · 랙 보정 · 롤백 기록 · 락스텝 입력 · 체크섬 · 파괴 최근 해시). 키 전체를 적어
  감김 · 건너뛴 칸 비우기가 없고, 무엇이 낡았나는 쓰는 쪽이 넣기 전에 본다 — 창 너비를 고리 크기로 두면 창 안끼리 덮지 않는다(락스텝 입력 256 · 체크섬 512).
  `acquire` 는 옛 값을 비우지 않는다(버퍼를 다시 쓴다 — 처음 넣는 틱이면 쓰는 쪽이 비운다). 16 비트로 감기는 패킷 시퀀스는 `SequenceBuffer`.
- **렌더 지연 규칙은 `NetClock` 하나다 — max( 최소 지연, 표본 간격 × 2 )**(유니티 Netcode for Entities 기본 2 틱과 같은 수). 한 간격뿐이면 표본 하나를 잃을 때마다
  보간할 뒤 표본이 없어 멈춰 선다(파괴 덩어리가 미터 단위로 어긋났다). 시계는 받은 틱을 하한으로, 받은 가장 새 틱 + 지연을 상한으로 두고 렌더 틱은 되돌아가지
  않는다 — 복제 클라이언트의 옛 시계("가장 새 스냅숏 − 지연" 쪽으로 기울이다 지연 × 4 를 넘으면 뒤로도 바로 옮김)는 스냅숏이 끊긴 뒤 보간 자리가 되돌아갔고
  (`NetClientServerTest.RenderTickNeverGoesBackAcrossASnapshotGap`), 파괴 쪽 옛 시계는 상한이 없어 로컬 시계가 빠르면 받은 자세를 지나쳐 갔다. 구간 규칙(앞 이하 · 뒤 초과 · 끝이면 멈춤 — 외삽 안 함)은 `InterpolationBuffer` ·
  `NetInterpolationUtil::computeAlpha` 하나.
- **확인만 담은 패킷이 확인을 부르면 한가한 연결이 30 Hz 로 핑퐁한다**(`NetConnection` 확인 요청 비트의 이유). 요청을 끄면 거꾸로 두 쪽 유지 시각이 맞물려 한쪽은
  늘 답만 보내 RTT 표본이 0 이 된다 — 그래서 답이라도 마지막 요청에서 유지 간격이 지나면 요청한다. RTT 는 "요청 패킷이 가장 새 확인으로" 돌아올 때만 잰다(묶음으로 늦게
  확인된 것은 상대가 기다렸다 보낸 시간이 섞인다).
- **상한은 쓰는 쪽도 본다** — 받는 쪽만 상한을 보면 넘는 메시지가 보낸 쪽에선 "보냈다", 받는 쪽에선 깨짐으로 조용히 사라진다(턴 행동 900 B) · 락스텝은 보내기가 실패해도
  내 입력을 이미 예약해 나만 진행했다. 상한 상수는 메시지 구조체에 하나(`NetTurnRelayMessage::kMaxActionBytes` · `NetLockstepMessage::kMaxInputBytes`)를 두 쪽이 같이 쓴다.
- **보고의 "(sw 할당자 밖)" 은 CRT 합 − 태그 합**이라 프로파일러보다 먼저 잡힌 sw 블록도 들어간다 — MemoryProfiler 는 부트스트랩 맨 앞에서 선다. 새 스레드는 Unknown
  에서 시작하므로 띄운 쪽의 태그를 인자로 넘겨 첫 줄에서 건다. 배열은 `sw_new_array` · `make_unique<T[]>`(맨 `new` 는 `Style/RawNew` 가 막는다). 외부 라이브러리는
  공개 설정 지점으로만 sw 할당자에 잇는다(pugixml `set_memory_management_functions`, nlohmann 할당자 인자, zlib zalloc · zstd advanced · LZ4 extState, stb STBI_*).
- **메모리 태그(UE LLM 식)는 배포본이 아닌 구성 모두에 있고 추적은 Debug · 시험만 켜져 있다**(Release 는 `-gv_memoryTracking=1`). 거는 자리는 셋 — 기동 단계 표(`EngineInitStepList.xxx`)의 태그 칸, 서비스 생성의 `kServiceMemoryTag<Type>`,
  하위 시스템 진입점의 `SW_MEMORY_SCOPE`. 태스크 · 병렬 청크는 **만든 쪽의 태그를 상속**한다(`TaskNode` · `ParallelGroup` 의 패딩 자리, 크기 그대로). 분포와 sw 할당자 밖
  몫은 `-gv_profileFrames` 보고의 "memory by tag" 와 ProfilerPanel 에서 본다 — Unknown 이 커지면 진입점이 빠진 것이다. ImGui 는 `SetAllocatorFunctions` 로 sw 할당자를
  지나므로 에디터 실행의 alloc/frame 에 ImGui 할당이 들어간다. GPU 메모리는 대상이 아니다(CPU 힙만).
- **STL 구성(`SW_ENABLE_STL_CONTAINER=ON`, CI `CI-Debug-STL`)은 C++17 이라 std 해시 컨테이너에 이종 조회 · `contains` 가 없다** — sw 쪽 얇은 클래스가 메운다.
  커스텀 컨테이너 전용 시험은 그 구성에서 건너뛴다.
- **완료를 모으는 줄에 고정 용량 큐(`ConcurrentQueue`)를 쓰지 말 것** — 가득 차면 `enqueue` 가 false 를 돌려주고 그 완료는 말없이 사라진다. 에셋 스트리밍 큐가 그랬다(한 경로에 편승한
  콜백 1024 개 초과분 유실, `AssetStreamingTest.ManyCallbacksOnOnePathAreAllDelivered`). 상한 없는 잠금 + deque 로 둔다.
- **비동기 IO 의 완료 콜백은 IO 스레드가 아니라 태스크 워커에서 돈다(엔진 설정)** — 팩 해제 · CRC 가 IO 스레드를 막지 않게. 그래서 `AsyncFileIo::shutdown` 은 Task 보다 먼저(기동 단계
  `FileIo` 가 Task · ModuleImages 에 의존), 콜백 안에서 자기 큐의 잠금을 쥔 채 IO 를 걸지 말 것(내린 뒤 요청은 그 스레드에서 바로 완료된다 — `AssetStreamingQueue::issueDataRead`).
- **레이스 탐지기(Debug)** — 워커의 비-const `sw::vector::operator[]` 를 쓰기로 잡는다: `std::as_const(v).data()` 나 const 참조로 읽는다. `sw::array` 를 락-프리 버퍼에 쓰지 말 것(드물게
  Fatal). 데드락 탐지기를 피하려면 실패 기록 같은 곳은 `std::mutex`. 잠금은 `std::scoped_lock`. `compare_exchange_weak` 은 실패 순서도 명시한다(기본 `seq_cst`).
- **`SW_ASSERT` 는 Release · Shipping 에서 사라지고 `SW_LOG_ASSERT` 는 Debug 에서 `SW_DEBUG_BREAK` 까지 한다**(디버거 없는 CI 에서는 프로세스가 죽는다 — 방어 경로 시험은 Release ·
  Shipping 에서). 배포 구성의 `SW_LOG_ASSERT` 는 진행하므로 뒤가 앞에 달린 전제는 하드 단언으로.
- **크래시** — 엔진이 만드는 스레드는 시작할 때 `CrashHandler::initializeCurrentThread()`(빠뜨리면 스택 오버플로 덤프가 0 바이트). Windows 덤프는 보고 스레드가 `PssCaptureSnapshot` 으로
  쓴다(살아 있는 자기 프로세스를 `MiniDumpWriteDump` 하면 로더 락에 멈춘다 — 재현은 자식 12 개 × 400 회). 크래시 경로 로그는 `Logger::flushGlobalForCrash`(락을 못 잡으면 포기).
  시한은 `setReportDeadline`(20 초), POSIX 는 `alarm` + SIGALRM, `backtrace()` 예열. 실물 확인 `-gv_crashTest=1..5`. 크래시 보고 본문은 `CrashContext.cpp` 의 `writeCrashReport`.
  묶음 · 올리기는 다음 실행의 `CrashReportService`(Engine/Telemetry). 보고 프로세스는 `setReporterExecutable` 을 정한 호스트(App)만 띄운다 — 경로를
  `getExecutablePath` 로 잡았더니 시험 실행 파일이 `--crash-reporter=` 를 모른 채 자기를 끝없이 다시 띄웠다(실측).
- **프로세스** — 자식 상속은 Windows `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`, POSIX CLOEXEC + `close_range`(아니면 동시에 띄운 자식이 서로의 파이프를 문다). 띄우기만 할 때는 `Process::launchDetached`
  (`execute` 는 UI 스레드를 세운다, `explorer.exe /select,` 는 성공해도 1). POSIX: `isRunning` 은 `waitid(..., WNOWAIT)`, `terminate` 는 그룹째, pid 는 한 번만 읽는다(0 이면 `kill(-0)`).
- **파일** — 쓰기는 원자적이다(`writeAtomically` → 같은 폴더 임시 파일 → `replaceFile`, Windows 는 `FileRenameInfoEx` POSIX 의미). Windows 읽기는 Win32(`readRange`) — `fopen_s` 는 ANSI 라
  한글 경로가 깨진다. 실행 파일마다 `WindowsProcess.manifest`(UTF-8 코드페이지 · longPath · PerMonitorV2)를 `sw_embedProcessManifest` 로 박는다(새 exe 에 빠뜨리면 한글 경로를 못 읽는다).
  `getFileTimestamp` 는 초 단위, 셰이더 소스 캐시는 (크기, 시각) `getFileStamp`. 워처가 알림을 잃으면 빈 `_filename` 의 `Modified`(리스캔 신호) — `expandRescanEvents`.
  `std::filesystem` 은 `Core/File/Std/FileUtilStdFileSystem.cpp` 한 TU 에만 있다 — `path` 는 넓은 문자로 만들고(좁은 문자 생성자는 잘못된 UTF-8 에서 던진다), 오류는 `error_code` 판으로만.
  `std::filesystem` 을 직접 쓰지 않는다 — 게이트 `CheckStdFilesystemIsolation` · 순회는 `forEachDirectoryEntry`, 시험의 파일 시각 조작은 `setFileWriteTime`.
- **문자열** — `formatstring` 은 `string_view` 를 길이로 쓴다(`.data()` 로 풀어 넘기면 뷰 끝을 지나 읽는다 — `CheckLogViewArgument`). `%#` 은 순수 자리표, 모르는 `%…` 는 리터럴, 인자 수 불일치는
  Debug 실행 단언(정본은 `FormatString` 클래스 주석). `setlocale` 이 없다(C 로캘) — 잘못된 UTF-8 은 `escapeInvalidUtf8`. `fixed_string` 은 넘치면 글자 경계에서 자르고 경고한다.
  `StringUtil` 은 비-ASCII 바이트를 `uint8` 로 넓힌다(utf16 에 같은 치환 금지), `stristr` 은 바이트 묶음 "최적화" 금지. Win32 변환은 `utf8ToUtf16`(`ImmGetCompositionStringW` 반환은 바이트 수).
  `std::hash` 는 `string` · `wstring` · `fixed_string` 모두 `RuntimeStringHash`(프로세스 안 전용, 대소문자 구분).
- **메모리 · 컨테이너** — 과정렬 판정은 `kUsesAlignedAllocation<T>` 하나(`max_align_t` 는 MSVC 에서 8 이라 힙 계열이 갈린다). `sw::vector` 는 `is_bitwise_copyable_v` 타입을 `Memory::copy`
  한 번으로(ReflectionParser 도 쓴다). `sw_delete_array` 는 원소 소멸자를 부르지 않는다. 해시 버킷 수는 2 의 거듭제곱, 번호는 `bucketIndexOf` 하나(스무 자리가 쓴다 — 한 곳만 달라도
  원소가 사라진다). 락 없는 읽기 + 주소 안정은 `PagedArray`. `SlotHandleTable` 은 점유 + 세대를 한 워드 `_state` 에 든다(둘로 나누지 말 것 — arm64). 순서 없는 삭제는
  `VectorUtil::removeAtSwap`, 표준에 없는 함수를 `vector.h` 에 붙이지 않는다. 함수 인자 연속 뷰 이름은 `vector_reference<const T>`.
- **락을 잡은 getter 는 자기 멤버의 뷰 · 참조를 돌려주면 안 된다**(값으로). 콜백을 부르는 순회는 인덱스로 돌고 부르기 전에 델리게이트를 복사한다. `MulticastDelegate` 는 복사 · 이동 넷을 직접
  적는다. `Delegate` 의 인라인 람다를 옮기면 원본 소멸자를 부른다.
- **`EventDispatcher`** — 큐(`push`)는 아무 스레드나, 버스(`subscribe` · `publish`)는 `processEvents` 를 부르는 스레드만. 큐에 남은 이벤트는 `destroyQueuedEvents()`.
- **명령줄** — 미등록 `gv_` 키는 `_mapPendingGlobal` 에 남았다가 모듈이 변수를 올릴 때 적용된다. gv_ 선언은 읽는 파일에, `extern` 재선언 금지(`SW_EXTERN_GLOBAL_VARIABLE` 은 정의 파일 밖에서
  읽을 때만 — `CheckGlobalVariableKinds`). 시험용 전역 변수는 `SW_TEST_GLOBAL_VARIABLE`(Shipping 에서 빠진다), 배포본을 스크립트가 조종할 것만 `SW_TEST_GLOBAL_VARIABLE_SHIPPED`. bool 이 아닌 `-gv_*` 에
  값을 빠뜨리면 경고 + 기본값.
- **싱글턴을 옮기지 않는 자리**: `CrashContextStore`(시그널 핸들러가 읽는다), 등록자 헤드 · `TestRegistry`(main 이전 자기 등록), `TagID` · `hashed_string` 인터닝(프로세스 전역이어야 뜻이 있다).
  Core 에 인스턴스가 필요하면 Logger 모양 — 인스턴스는 `EngineLoop`, Core 에는 포인터 슬롯.
- **비신뢰로 실은 몫은 확인을 기다린다**(`NetPrioritizer::markSentUnconfirmed` · `resolveSend`) — 실은 순간 0 으로만 돌리면 잃은 상태가 한 차례를 통째로
  다시 기다린다. CS 는 확인한 재구성과 보낸 재구성의 그 엔티티를 견줘 받음 · 잃음을 가린다(하니스 손실 20 % 낮은 우선도 최대 96 → 89 틱; 손실 없는 포화
  회선의 최대 20 틱은 그대로 — 그 20 은 순서만 채널에 밀린 몫이 아니었다).
- **비신뢰 갱신의 "보낸 상태" 와 "확인된 상태" 를 나눈다**(MMO `_listInFlightState` · `_listSentState`) — 보낸 순간 기준을 바꾸면 잃은 갱신이 "안 바뀜" 이 되어
  가속도 없다. 클라이언트는 받은 갱신 틱을 확인(가장 새 틱 + 앞 32 틱 비트)으로 틱마다 보낸다(30 틱 끊김 뒤 먼 엔티티 13 → 4 틱).
- **스트림 전송의 연결 수명은 "걸린 일 수" 로 센다**(IOCP 는 걸린 overlapped + 콜백 중 표시, epoll 은 루프 스레드 하나가 소유) — 닫힘은 그 수가 0 이 되는 첫 순간 한 번,
  그 뒤에 자리를 세대 +1 로 돌려준다. 걸린 일이 없는 연결을 닫으면 아무 완료도 오지 않으니 정리 표를 루프에 넣는다. 열림 콜백은 받기를 걸기 **전에** — 열림이 늘 첫 콜백.
  I/O 스레드를 끝낼 때 완료 포트의 멈춤 표는 깨우기만 쓴다 — `GetQueuedCompletionStatusEx` 는 한 묶음에 표 여럿을 한 스레드에 줘서, 표 수로 끝내면 다른 스레드가 영영 기다린다.
- **멱등 기억은 연결이 아니라 주체에 묶는다** — 거래 요청이 처리된 뒤 응답 전에 끊기면 클라이언트는 새 연결에서 같은 키로 다시 보낸다. 범위가 연결이면 두 번 처리된다
  (`NetRequestServer::setPrincipal` — 로그인 키트가 붙인다). 처리 중인 키가 다시 오면 처리하지 않고 첫 응답을 같이 받게 한다. 연결이 닫혀도 처리 중 기록은 남겨 늦은 응답을 기억한다.
- **로그 문맥 꼬리표는 형식 문자열의 빈 자리다**(Core `LogContext`) — 문맥 없는 스레드(게임 · 렌더 · 에디터)의 줄은 바이트가 같다. 비동기 경계는 넘기는 쪽이
  복사해 들고 받는 쪽이 `ScopedLogContext` 로 다시 건다(`IServiceStoreWork` — 저장소가 `submit` 에서 잡는다). 지표 라벨에는 추적 id · 계정 id 를 넣지 않는다(시리즈가 끝없이 는다).
  운영 지표 · 상태 · HTTP 끝점은 엔진(`Engine/Observability`)에 둔다 — 전용 서버 실행 파일은 GameFramework DLL 을 링크하지 않는다.
- **UDP 암호화는 협상하지 않는다** — 암호화 · 토큰 결속 여부를 프로토콜 id 에 섞어 한쪽만 다르면 `SecurityMismatch` 로 거절한다(평문으로 내려가는 길이 없다).
  AEAD nonce 는 방향별 IV XOR 64 비트 패킷 번호(감기지 않는다), 재전송 창(1024)은 **복호가 통과한 뒤에** 표시한다 — 먼저 표시하면 위조 패킷이 진짜 번호를 태운다.
  서버는 주소가 확인된(상태 없는 도전을 통과한) 응답에만 X25519 를 계산하고, 토큰 결속이면 세션 비밀의 증명 태그가 맞아야 자리를 잡는다.
- **데이터 이름 `None` 은 빈 이름이다** — `hashed_string( "None" )` 은 언리얼 `FName` 처럼 `empty()` 다. 고르기 항목 · id 를 `None` 으로 지으면 "이름 없음" 으로
  읽힌다(외형 스키마는 로드 오류로 막는다). 항목은 `Off` · `Bare` 처럼 짓는다.
- **벽시계는 `WallClock` 하나**(`Core/Time`, `system_clock` 을 읽는 유일한 파일) — 서비스는 `nowMs` 매개변수로 받고 시험은 가짜 시각을 넣는다.
  여러 서비스가 함께 쓰는 비동기 창구(캐시 앞 · 버스 · 접속 상태 찾기)는 결과를 "꺼내 가기(poll)" 로 두지 않는다 — 소비자가 둘이면 서로의 결과를 가져간다.
  맡길 때 델리게이트를 받아 그 요청에만 알린다(`EphemeralStoreRouter` · `IAccountPresence`). 클라이언트 요청 id ↔ 델리게이트 표는 `sendRequest` 가 그 자리에서
  실패를 알릴 수 있어 보내기 전에 걸어 둔다(`ServiceClientCallTable::send`). 라우터보다 먼저 내려가는 델리게이트 주인은 기다리던 요청을 `cancel` 한다.
