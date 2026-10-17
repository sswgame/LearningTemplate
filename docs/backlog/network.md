# 백로그: 네트워크, 서버, 온라인 서비스

Core 네트워크, 전용 서버, 온라인 서비스 키트, 운영의 남은 일입니다. 상태 태그의 뜻, 일을 고르는 순서, 새 항목을 쓰는 법은 [백로그 안내](README.md)에 있습니다.

- `B-9` [검증대기] **패킷 압축의 남은 것**(UDP 압축을 끈 결정과 측정값은 [결정 기록](../09_Decisions.md) 5-9). WSL Release 로 `NetCompressionBenchTest` 를 한 번 돌린다(압축 시간은
  컴파일러 · libc 에 따라 다르다). 채팅 수치는 채팅 키트가 실제 기록을 내면 다시 잰다. 스트림 프레임 압축은 채팅 기록 · 거래 내역을 실을 키트가 zstd 로 켤지 정한다.

- `B-10` [검증대기] **MMO 규모 서버의 UDP 소켓 계층.** 지금은 호스트당 논블로킹 UDP 소켓 하나 + 전용 스레드 하나(`poll`/`WSAPoll`), 데이터그램마다 `recvfrom`/`sendto`, `NetHost` 잠금 하나.
  UDP 는 소켓이 하나라 IOCP · epoll 은 지렛대가 아니다. ① Windows 측정은 [검증과 측정](../08_Verification.md) 2절 "UDP 소켓 계층" 에 있다(패킷당 약 6.5 us, 대부분 `recvfrom`/`sendto`).
  리눅스(WSL Release, `ulimit -n 8192` 뒤 같은 필터)는 아직이다 — 수치를 나란히 놓는다. 그다음 일은 ②.
  ② 시스템 호출이 지배적이면 `INetTransport` 일괄 받기 · 보내기 하나의 계약 — 리눅스 `recvmmsg`/`sendmmsg`(+ GSO/GRO), Windows RIO(+ USO/URO, 없으면 `WSARecvMsg`)
  ③ 스레드 하나가 차면 포트 샤딩(두 플랫폼 같은 계약 — 로그인 토큰이 포트를 준다, 리눅스는 `SO_REUSEPORT` 를 선택) + 샤드마다 `NetHost`. 둘 다 `NetUDPBenchTest` 숫자로 전후를 측정한다.

- `C-39` [정리] **범위를 `PROPERTY` 선언과 서비스 손 규칙에 두 번 적는다.** `isValidLobbyRequest` · `registerDevice` 확인 같은 손 검사가 `PROPERTY( Min · Max )` 와 같은 범위를 따로 적고,
  `PartyLobbyService::initialize` 는 파티 크기를 `std::clamp` 로 조용히 맞춘다(폴백). 뿌리: 범위의 원본이 `PROPERTY` 하나가 아니다.
  손 규칙을 지우고 메시지 읽기의 범위 검사(`A-19`)에 맡기며, 설정 범위 밖 값은 오류로 멈춘다.

- `D7-1` [기능] **네트워크 — 파괴 · 가상 서버에서 남은 것**(2026-10-05, `GF_NetSimulation` · `GF_NetDestruction`). ① 파괴 사건은 "신뢰 · 순서 없음" 채널(N21b) — 250 ms · 손실 15 % 의 사건 지연
  평균 · 최대 전/후는 `NetSimDestructionMatrixTest` 로그(`max event lag … mean …`)로 잴 것(빌드 뒤). 남은 최대는 한 메시지가 거듭 잃는 몫이다. 덩어리 멈춤 확정(신뢰 자세)도
  받는 쪽이 틱으로 끼우므로 옮길 수 있다 — 재고 나서. ② 전용 서버 프로세스(`Server`, 타깃 Game · Server)는 섰다 — 남은 것은 그 위에 파괴 시뮬레이션을 UDP 서버로
  돌리고 Windows 클라이언트(App)가 붙어 파괴 해시가 컴파일러 · 플랫폼(WSL 리눅스 서버 ↔ Windows 클라이언트)을 넘어 같은지 보는 것(서버 쪽 `NetSimulation` 호스트 모드 ·
  클라이언트 접속 인자 · 해시 로그 비교 스크립트). 같은 입력의 해시가 구성을 넘어 같은 것은 `DestructionDamageTest.EventLogHashMatchesTheRecordedValueOnEveryBuild`(해시 다섯)가
  지킨다(Windows Debug · Release · Shipping 같은 값 확인, 리눅스는 CI 잡 — 리눅스 clang 에서 다르면 그 잡이 진다).

- `D7-2` [기능] **전용 서버 타깃에서 남은 것.** (게임별 기동 확인은 ServerTest `EveryBuiltGameStartsAsADedicatedServer` — Debug 9 게임 77 s.)
  ④ 서버 빌드에서 렌더러 코드를 아예 빼기(지금은 컴파일만 되고 돌지 않는다 — 크기 · 링크 시간을 재고). 렌더러 · RHI 를 Engine CMake 에서 타깃별로 가르는 일이라 Engine 소유 작업으로 남겼다.
  ⑤ 리눅스 서버(`WSL-*-Server`) 빌드 · `readelf -d` 로 libX11 없음 확인 · `ServerTest` — Windows 만 확인했다. 로컬 WSL 빌드는 하지 않기로 해서(사용자 결정) CI 리눅스 잡 몫이다. 서버용 크래시 보고 전송 동의(운영 설정)는 `H-3`.

- `D7-3` [기능] **서버 디버깅 — 계획은 [docs/plans/ServerDebug.md](../plans/ServerDebug.md).** S3 · S6 · S7 의 프로세스 쪽은 들어갔고 다음은 에디터 쪽(Inspector · Sync 탭)이다. 방향 결정은 [결정 기록](../09_Decisions.md) 5-10. 단위를 끝내면 계획 문서에서 지운다. 운영 단위 O1 ~ O7 은 `H-3` 참고.
  S1 에서 남은 것: ① 사용자 프리셋 저장("서버 + 클라이언트 2" 를 이름 붙여 에디터 상태에 — 지금은 마지막 구성만 세션이 들고 있고 에디터를 다시 띄우면 Standalone) ② 리슨 호스트(App)의 줄에 틱 번호(전용 서버 줄에는 `[tick=N]` 이 붙는다 — 리슨 호스트는 고정 틱 루프가 없어 프레임 번호로 정해야 한다) ③ Stop 뒤 합친 트레이스를 Profiler 창 Timeline 탭에 저절로 열기(지금은 경로를 로그 · Session 탭에 남기고 Load Trace 로 연다) ④ 게임 모듈 핫 리로드를 넘어 세션 유지 — 게임 인스턴스가 다시 서면 `GameNetSession` 도 닫고 다시 열어 클라이언트가 끊긴다(리로드가 새 인스턴스를 먼저 만들면 포트가 아직 잡혀 있어 다시 열지 못한다; 세션을 엔진 쪽 수명으로 옮긴다 — 언리얼 넷 드라이버는 월드 소유) ⑤ 게임 포트 샤딩 — 전용 서버는 이제 서버 설정의 게임 포트를 늘 열지만(결정 기록 5-10) 소켓 하나뿐이라 `ServerConfig::_gamePortShardCount` 2 이상은 오류로 열지 않는다(샤드마다 NetHost · 로그인 토큰의 샤드 포트). ④ 는 이제 인자 없이 뜬 Dev 전용 서버에도 해당한다 ⑥ 클라이언트 창 배치(여러 App 창이 겹쳐 뜬다 — 언리얼은 창 위치를 나란히 둔다).
  ①③⑥ 은 에디터(`Source/Editor`), ②④⑤ 는 App · `GameNetSession`(`GameFramework/Base/Foundation/Framework/Net`) · 엔진 몫이라 2026-10-17 네트워크 작업(서버 · Core 네트워크 · 온라인 파일 소유)에서 남겼다.
- `D7-5` [기능] **네트워크 서비스 계층(로그인 · 채팅 · 거래 키트)의 남은 것.** 구성과 원칙(공통 기반 · 드라이버 키트 · 기능 키트, DB 와 캐시)은 [결정 기록](../09_Decisions.md) 5-9, 들어간 기반 · 키트 · 서버 조립
  (`OnlineServerRuntime`, 키트마다 `<키트>OnlineServerKit.cpp`, 서버 설정 옆 `<이름>.online.json`)은 Online README. 운영 쪽(관측 출력 · 오케스트레이터 · 비밀 배포 · 부하 기준선)은 `H-3`.
  - **할 일**
    - 기반: 클라이언트 접속 인자 `-gv_netConnectAddress` 의 이름(`GameNetSession` — 같은 함수로 바꾸면 된다, 이 작업의 파일 소유 밖이라 남김).
    - 계정: 실제 외부 로그인 제공자 설정(구글 · 애플 · 카카오 · 네이버 · 스팀 — 앱 등록 · client id 는 쓰는 게임이 생기면), 애플 client secret JWT · 탈퇴 때 토큰 철회(`Provider/Apple/`), 모바일 SDK 클라이언트,
      OS 브라우저 열기(`IExternalBrowser` 구현 — 게임 몫), 텔레메트리의 `IHTTPClient`(Engine, 막는 창구 · 기본 Null)를 이 HTTP 클라이언트로 연결하는 게임 어댑터(Engine 은 GameFramework 를 모른다).
    - 채팅: 신고(메시지 스냅숏 → GM 도구), 채널 샤딩(world 채널 수천 명 — 부하 봇 수치로 정함, 거르개 비용은 `ChatService.send` p99), 클라이언트 UI 위젯.
    - 서버 디렉터리: GM 도구 패널에 점검 · 공지 바꾸기 연결, 서버 고르기 UI 위젯.
    - 친구 · 길드: 길드 채널(`GuildService::setEventDelegate` → 채팅 키트), 길드 레벨 · 공개 길드 가입 신청, 친구 목록 UI 위젯, 부하 봇의 친구 접속 상태 팬아웃 측정(친구 200 명 × 캐시 읽기).
    - 순위표: 친구 순위표(친구 목록 × 점수 — 게임 조립), 업적 진행률 표시 · 숨김 업적 목록.
    - 매칭: 실력 공급 `IMatchRatingSource` · 게임 서버의 `MatchServerAgent` 를 UDP 접속 인증기에 연결, 백필(경기 중 빈자리), 재접속 유예 중 파티 · 로비 유지(지금은 끊기면 바로 뺀다), 파티 채팅 채널 연결(게임 조립 — 파티 알림 → Chat).
    - 라이브 운영: 실제 푸시 제공자 둘(`Push/Provider/<제품>/` — 공통 HTTPS 클라이언트 위 HTTP/2 · 토큰 인증; 외부 계정 · 비용이라 사용자 결정 전에는 하지 않는다), A/B 실험(원격 설정 출시 비율 + 지표), 이벤트 운영 화면(GF_Admin 패널), 우편 · 이벤트에서 "오프라인이면 푸시" 연결(게임 조립).
    - 경제 · 우편함 · GM: 영수증 제공자 셋(`Receipt/Provider/<제품>/` — Apple App Store Server API 의 서명된 거래 JWS x5c 사슬 검증 + 거래 조회, Google Play Developer API `purchases.products` — 서비스 계정 JWT → OAuth 토큰, Steam `ISteamMicroTxn` InitTxn → FinalizeTxn;
      외부 스토어 계정 · 비용이 들어 계약 + 가짜까지만 — 사용자 결정), 환불 · 결제 취소 알림 받기(Apple 서버 알림 v2 · Google RTDN · Steam 환불 → GM 회수와 같은 `refund.revoke` 빚 이동) · 유상 재화 청약철회(7 일 · 미사용분) 규칙,
      인스턴스 아이템(내구도 · 강화 — 원장은 개수만, `item.<정의>#<id>` 개수 1 + 상태 레코드; 거래 다리도 지금 가산 자산만 받고 인벤토리 인스턴스 상태 칸은 거절), 잔액 · 새 우편 알림(서버 → 클라이언트 `sendPush`),
      원장 보존 검사 · 만료 쓸기를 예약 작업(`ServiceScheduler`)으로(지금 쓸기는 우편함 서비스 틱 주기), 분개 · 감사 보존 기간 정리, `mail_sent` · 멱등 기록 · `econ_purchase` 정리,
      접속하지 않은 계정 이름 찾기(계정 키트 저장 부분을 기반 계약으로), GM 전용 호스트 포트를 서버 설정에, 발행 · 소각 총량 지표(보존 검사가 `economy_issued_total{asset}`).
    - 서버 여럿: 실제 지연(루프백이 아닌 망)에서의 버스 · 캐시 왕복 측정.
  - **시작 전에 알아야 할 함정**: 다른 서버에서 깨진 파티 표는 낸 서버에 Cancelled 를 버스로 알리지만, 권한 매처에서는 모드를 몰라 모든 모드의 빼기로 뺀다.
    팀 나누기 · 채우기는 그리디라 나눌 수 없는 조합은 그 닻을 건너뛴다 — 작은 n 전수 탐색은 부하 봇의 대기 시간 p99 를 보고 정한다. 정산은 이제 페이지 일(600 명씩)로 나뉘고 결과가 있는 사람을 건너뛰는 멱등으로 이어 간다.
  - **확인 방법**: 리눅스 epoll(`EpollStreamTransport`) — `WSL-Debug` · `WSL-Shipping` 에서 `StreamTransportTest` 를 `--test_repeat=20` 과 변이(`setReceivePaused( false )` 의 할 일 빼기 → 백프레셔 테스트가 진다)로. io_uring 은 측정 뒤.
    PostgreSQL · RESP 계약 테스트를 실제 서버로 한 번(`SW_TEST_POSTGRES_URL` · `SW_TEST_RESP_URL` — Valkey(WSL) · Garnet(Windows) 각각, 이 PC 에는 서버가 없다) — 계약 테스트(`ServiceStoreContract.h`)를 SQL 구현에도 같이 돌린다.
  상용 비교: 언리얼은 Online Subsystem/EOS 등 외부 백엔드에 맡기고, 자체 MMO 서버는 IOCP/epoll 서비스 서버를 따로 둔다.

- `D7-6` [기능] **네트워크 보안의 남은 것**(방향은 [결정 기록](../09_Decisions.md) 5-9). Core 창구(`Network/Security/`) · OpenSSL 3.6 구현 · 스트림 TLS 1.3 · UDP 보안 · 서버 설정의 TLS 인증서 배선은 있다.
  남은 것: 암호화 켠 하니스로 서버 틱 시간을 재어 [결정 기록](../09_Decisions.md) 3절에 숫자 한 줄(N18a 벤치에 `_security` 를 켠 판).

- `D7-7` [기능] **넷 채널 등록 API 로 B 메시지 타입의 스키마 해시를 핸드셰이크 버전에 저절로 섞는다.** 계획은 [리플렉션 형식](../plans/ReflectionFormats.md) 3.3 · 3.10 · 5절 "게임 넷 연결"이다.
  Mech · Horror 스냅샷은 B 로 옮겼다(단계 4-a). 지금은 메시지마다 스트림 머리의 64 비트 해시가 실려, 배치가 다른 빌드의 바이트는 읽기가 `SchemaMismatch` 로 거절한다.
  남은 것: 채널 객체에 `registerMessage<T>( kind )` 를 두고, 등록한 타입의 해시가 `NetHostConfig::_wireVersion` 에 섞이게 해 핸드셰이크가 `VersionMismatch` 로 먼저 거절하게 한다.
  등록이 버전을 만들므로 섞는 일을 따로 부를 자리가 없어야 한다. 확인을 마친 채널 안에서만 본문만 쓰는 경로(3.10)를 만들어 메시지마다 싣는 해시 8 바이트를 뺀다.
  시작 조건: 이 스냅샷을 채널에 싣는 게임이 생길 때. 지금은 호출부가 테스트뿐이라 범용 API 를 미리 두지 않았다.

- `D7-8` [기능] **클라이언트 예측 · 롤백과 관심 영역 기반 복제가 없다.** 상태 동기화와 파괴 동기화(`D7-1`)는 있지만 입력 예측 · 서버 보정 · 롤백, 그리고 관심 영역(relevancy)으로 복제 대상을 거르는 층을 찾지 못했다. `B-10`(MMO 규모 UDP 계층)이 이 방향이다.
  먼저 설계 문서를 쓴다(`design-reviewer` 검토): 언리얼 CharacterMovement 예측과 Network Prediction, 유니티 Netcode for GameObjects · Fish-Net, Overwatch 식 입력 버퍼링과 비교. 결정 기록 5-9 · 5-10 의 넷 스냅샷 · 의도 기록(`D4-13`)과의 접점을 정한다.

- `D7-9` [기능] **네트워크 확장 후보.** 물리 객체 예측(`D7-8` 5단계), 음성 채팅, 치팅 방어 체계(서버 검증 규칙 목록 · 이상 탐지), 서버 오케스트레이션(매칭 뒤 인스턴스 할당 · 확장). 쓰는 게임이 생기면 설계한다.

- `F-1` [기능] **지형 가중치 에셋의 에디터 쪽 마무리** — 가중치는 `.layerweights` 데이터 에셋이 됐다([결정 기록](../09_Decisions.md) 5-10, 서버도 레이어 질의에 답한다).
  남은 것(에디터 작업자와 겹쳐 미룸): ① 이름 `TerrainComponent::_splatTexturePath` · `get/setSplatTexturePath` 를 `_layerWeightsPath` 류로 바꾸고 `TerrainBrushTool` · 씬 XML 을 함께 고친다 ·
  ② 에디터 에셋 종류(`EditorAssetType`)에 `.layerweights` 를 더해 콘텐츠 브라우저 · 속성 칸이 고르게 한다 · ③ 바깥 도구가 만든 스플랫 PNG 를 `.layerweights` 로 가져오는 임포트(높이장 임포터와 같은 길).

- `F-2` [결정대기] **서버 버스 메시지의 빌드 확인.** 서버끼리 주고받는 버스 메시지(`ChatService` 의 채널 전달, `SocialService` 의 관계 바뀜, `AccountServer` 의 해지 알림)는
  고정 배치형(B)이라 머리 해시가 다르면 받는 쪽이 `SchemaMismatch` 로 버린다. 클라이언트와 달리 서버 사이에는 핸드셰이크가 없어, 배포 중에 빌드가 섞이면 메시지가 조용히 사라진다.
  "서버 클러스터는 한 빌드로만 돈다" 를 정책으로 정할지, 버스 구독 때 빌드 해시를 확인하고 다르면 오류로 멈출지 사용자가 정한다([결정 기록](../09_Decisions.md) 5-9 열린 질문).

- `G-2` [조건부] **CS2 식 서브틱 입력(2026-10-05 제안 — 넷 게임이 생기면; 지금 넷 키트를 쓰는 테스트 게임은 0).** `RawInputEvent` · `NativeWindowEvent` 에 시각이 없다(`MSG::time` 을 버림).
  B1 사건 시각 + 입력 리플레이 판 4 · B2 프레임 안 자리와 그 순간까지의 시선 누적(시계 주입) · B3 `InputMap` 이 행동별 "누른 순간" 을 준다 · B4 클릭 순간의 시선으로 발사
  (쿨다운 남은 몫은 이미 고침) · B5 넷 입력 항목마다 스탬프 · `viewTick`(지금은 메시지마다 하나라 다시 보낸 옛 입력도 최신으로 판정 — 자리는 있다:
  `NetInputFormat::_bStamped` · `NetInputSendWindow::push( …, stamp )` · `NetInputEntry::_stamp`, 켜면 `kClientServer` 판을 올린다) · 서버 클램프(랙 보정
  `LagCompensationHistory::raycastAt(float)` 는 있다) · B6(선택) Win32 입력 스레드 — 먼저 `GetMessageTime` 해상도를 재고, 에디터 · 콘솔이 먼저 소비하는 판정을 우회하지 않게.
  롤백 · 락스텝 키트에는 넣지 않는다(프레임 단위 결정성이 계약).

- `G-8` [조건부] **MMO 갱신 확인을 `NetConnection` 전달 통지로**: 패킷 확인 → 메시지 전달 통지가 Core 에 생기면 키트 확인 메시지(`kUpdateAck`)를 지우고 그 통지로 판정한다(언리얼 NAK 자리).

- `G-15` [조건부] **스토어 제공자(Steam · Epic) 연동이 없다.** 업적 · 클라우드 세이브 · 오버레이 · 친구. 온라인 서비스의 제공자 추상(`D7-5`)에 붙일 자리는 있다. 출시 플랫폼이 정해지면 한다.

- `G-16` [조건부] **패치 · 델타 업데이트 배포 구조가 없다.** 팩 단위 차분 배포 · 버전 관리. 출시 일정이 잡히면 한다(크래시 컨텍스트와 텔레메트리 코드는 있다).

- `H-3` [보류] **(보류 — 사용자 결정 2026-10-13) 운영과 배포.** 서버 운영 단위 O1 ~ O7([서버 디버깅 계획](../plans/ServerDebug.md) 운영 단위: 사고 기록 · 로그 · 종료 드레인 · 관리 채널 · 부하 기준선 · 컨테이너 · 쿠버네티스 · Agones · 운영 사고 재생)과
  `D7-2` 의 운영 쪽(크래시 묶기 · 텔레메트리 전송)은 "아직 할 예정이 없다"로 정했다. 폴리싱(결함 · 성능 증거 · 검증 자동화 · 다중 월드 · 에디터)이 먼저다. 풀 때까지 하지 않는다.
  다른 항목에서 여기로 모은 조각(그 항목에는 `H-3` 참고만 둔다): 서버용 크래시 보고 전송 동의 = 운영 설정(O1, 크래시 묶기 자체는 들어갔다), 서버 로그 JSON lines 출력 · OpenTelemetry 내보내기 · 감사 줄의 추적 id 칸(O2),
  부하 봇(`Tools/OnlineLoadBot`) 첫 측정 — Release · 서버 한 대 · 봇 1000/5000 으로 `chat_and_match` · `login_storm` 의 동작별 p99 와 서버 틱 p99, 전용 서버에 `--server` 로 같은 시나리오(O5 — 채팅 채널 샤딩 · 정산 길이 · 매처 대기 p99 판단의 근거,
  Debug 참고값은 [검증과 측정](../08_Verification.md) 2절 "온라인 부하 봇"), 오케스트레이터(Agones · 쿠버네티스)와 서버 등록 · 매칭 할당 · 서버 띄우기 연결, 게임 UDP 접속 표 서명 키 · 계정 티켓 주 키의 배포 · 교체 절차, 실제 금칙어 목록(O6 · 운영).

---

[◀ 백로그 안내](README.md)
