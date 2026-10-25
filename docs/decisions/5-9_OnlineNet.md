# 5-9. 온라인 서비스, 네트워크, 보안

> 질문: 5-9. 온라인 서비스, 네트워크, 보안 영역에서 이미 정한 것은 무엇이고 근거는 무엇인가?
> 독자: 같은 제안을 다시 하려는 사람, 정한 이유를 찾는 사람.
> 결론: 결정 기록 5 절의 한 영역이다. 번호 체계와 쓰는 법은 [결정 기록](../09_Decisions.md) 5절에 있다.

- **관심 영역 격자 하나**(2026-10-22, 통합 때 정함): `NetInterestGrid` 는 칸 크기와 두 반경, 반경 관계 검사를 격자가 갖는 API 하나(`initialize( 칸, 들어옴, 나감 )`)이고, MMO 복제기는 그 칸에 월드 셀 격자의 셀 크기를 넘기며 클라이언트-서버 키트는 정책(`InterestReplicationPolicy`)으로 거릅니다. 같은 거름을 `ReplicationServer` 안에 내장하던 `wt/wsT4` 의 구현(`setInterestArea`)은 통합 때 버렸습니다 — 반경 검사가 두 곳에 생기기 때문입니다(언리얼 Replication Graph 의 격자 노드를 정책처럼 끼우는 구조와 같습니다).
- **DB 와 캐시**(2026-10-06, 사용자 결정): 영속 저장소는 PostgreSQL(서버)과 SQLite(개발용 단독 서버, 클라이언트 로컬)입니다. 캐시는 RESP 드라이버 하나로 리눅스는 Valkey, 윈도우는 Garnet 을 씁니다.
  Redis 7.4 이상은 RSAL/SSPL 이라 "오픈소스이고 무료" 조건 밖이고, Redis 8 의 AGPLv3 선택도 그 의무 때문에 택하지 않았습니다. 전용 서버는 윈도우와 리눅스 둘 다 1 급입니다. 남은 일은 [후보 문서](../plans/Candidates-network.md) `D7-5` 에 있습니다.
- **네트워크 서비스 계층**(2026-10-06, 사용자 결정): 로그인, 채팅, 거래처럼 MMO 가 아니어도 쓰는 서비스는 상태 복제(UDP)와 따로, 순서 있는 신뢰 스트림 · 긴 연결 · 요청-응답 위의 키트로 만듭니다.
  공통 기반은 GameFramework 기반 `Online/` 입니다. 서비스 틀(등록, 라우팅, 인증 문맥, 오류 코드, 버전 협상), 요청 보호(도배 제한, 멱등 키, 크기 상한), 신원 원형(`AccountID`, 세션 토큰 검증),
  저장 계약(`IServiceStore` 영속, `IEphemeralStore` 캐시, `ILocalStore` 로컬), 마이그레이션 실행기, 감사 로그, 서버 간 버스, 예약 작업, 원격 설정과 기능 플래그, 관측(지표, 구조화 로그, 추적 id)이 여기에 있습니다.
  로컬 저장의 파일 백엔드는 바이너리 · JSON · XML 형식, 원자적 쓰기, 체크섬, 선택적 압축과 암호화를 지원합니다.
  DB 드라이버는 키트입니다: `GF_SQLStore`(SQLite), `GF_Server_SQLStore`(PostgreSQL), `GF_Server_CacheStore`(RESP, 메모리 구현은 기반 `Online/Cache`).
  기능도 키트입니다: `GF_Account`, `GF_ServerDirectory`, `GF_Economy`(원장, 지갑, 상점, 영수증 검증), `GF_Trade`(원장 위), `GF_Mailbox`, `GF_Chat`, `GF_Social`, `GF_Leaderboard`, `GF_Matchmaking`, `GF_LiveOps`, `GF_Admin`(GM 도구, 제재). 부하 테스트 봇은 테스트 도구입니다.
  제품 이름은 드라이버와 제공자 폴더에만 쓰고(`CheckProductNames`, 조립점은 `Scripts/lint/rules/CheckProductNames.toml` 의 `[exemption]` 에 이유와 함께), 서버 전용 모듈은 클라이언트 Shipping 에 넣지 않습니다.
  캐시를 잃어도 영속 데이터는 맞아야 합니다(거래의 원본은 영속 트랜잭션). DB 호출은 전용 워커, 연결 풀, 비동기 완료로 합니다.
- **네트워크 보안**(2026-10-06, 사용자 결정): 스트림(서비스)은 TLS 1.3 이고, 게임 UDP 는 연결 수립 때 키를 교환(X25519)한 뒤 패킷마다 AEAD(AES-GCM 또는 ChaCha20-Poly1305)를 겁니다.
  nonce 는 패킷 번호이고 재전송 방지 창을 둡니다. 세션 키는 로그인 키트가 발급한 토큰에 묶습니다(UDP 접속 = 토큰 제시). Valve GameNetworkingSockets, 언리얼 AESGCM PacketHandler 와 같은 구조입니다.
  암호 구현은 직접 짜지 않고, Core 의 인터페이스(`Network/Security/`) 뒤에 라이브러리 하나를 둡니다.
- **UDP 패킷 압축은 끕니다**(2026-10-06, 사용자 결정). 켜는 규칙은 "15 % 이상 줄고, 압축 + 해제가 패킷당 2 us 이하" 입니다. 코덱 틀은 Core `Compression`(코덱 id 레지스트리)이고 LZ4 · zstd · zlib 은 Engine 이 등록하며, Core 네트워크는 id 로만 씁니다.
  `NetCompressionBenchTest`(Release, 3 회 가운데 값, 패킷마다 봉투 3 B, 줄지 않으면 원문)에서 레지스트리 코덱은 어느 흐름에서도 규칙을 넘지 못했습니다.
  스냅숏(평균 1009 B): LZ4 1 % · 1.1 us, zstd1 15 % · 11.7 us, zstd1 + 사전 16 KB 34 % · 6.0 us. 파괴(302 B): LZ4 3 % · 0.37 us, zstd1 4 % · 6.4 us, 사전 35 % · 1.6 us.
  채팅(43 B, 합성): LZ4 1 %, zstd 0 %, 사전 27 % · 0.74 us. 규칙을 넘는 것은 학습 사전뿐(파괴, 채팅)이라, 사전 배포(버전 · id 협상, 사전 갱신)를 하게 되면 다시 봅니다.
  스트림 프레임 압축은 있지만 기본으로 꺼져 있습니다(`StreamEndpointConfig::_compression`, 봉투 `NetCompressionUtil`). 받는 쪽은 푼 크기를 본문 상한으로 보고 풀기 전에 거절합니다.
- **암호 라이브러리는 OpenSSL(vcpkg, Apache-2.0)입니다**(2026-10-06). Windows 에서는 지금 트리플릿대로 DLL 로 링크합니다. 비밀번호 해시는 보안 제공자의 Argon2id 이고, 기본값은 OWASP 권장(19 MiB, 반복 2)입니다.
- **인증기 없는 UDP 암호화와 자체 서명 개발용 인증서는 개발 빌드에만 있습니다**(2026-10-06). Shipping 서버는 설정 파일의 인증서와 키 경로가 필수입니다.
- **계정**(2026-10-06): 같은 계정으로 다시 로그인하면 새 로그인이 옛 세션을 밀어냅니다. 게스트를 정식 계정에 연동할 때 이미 쓰인 이름이나 플랫폼 계정은 거절합니다(자동 합치기 없음).
  탈퇴는 30 일 유예 뒤 지우고, 원장과 감사 기록에는 계정 id 만 남깁니다. PC 외부 로그인은 공개 클라이언트와 PKCE(S256)이고, 서명은 RS256 과 ES256 만 받습니다.
- **로컬 저장의 봉인은 장치 키입니다**(2026-10-06). 실수나 가벼운 변조를 막을 뿐이고, 경쟁 데이터의 원본은 서버입니다.
- **경제**(2026-10-06): 거래는 맡김 없이 양쪽이 확정하는 순간 원장 분개 하나로 정산합니다. 경매와 우편 첨부는 맡김 방식입니다. 유상과 무상 재화의 차감 순서는 데이터이고, 기본은 무상 먼저입니다.
  환불 회수에만 음수 잔액(빚)을 허용하고, 빚이 있는 동안 그 재화로 살 수 없습니다(PlayFab 의 Subtract 와 같은 자리). 원장, 우편 넣기, 제재처럼 여러 키트가 한 트랜잭션에 넣는 쓰기는 기반 `Online/` 에 둡니다.
- **우편 만료**(2026-10-06): 운영 우편과 보상 우편은 첨부가 사라지고, 플레이어 우편은 보낸 사람에게 돌아갑니다. 보관 기간은 30 일입니다.
- **응답 형식은 하나입니다**(2026-10-06). 업무 결과는 응답 본문의 첫 값이고, 오류 코드는 공통 `OnlineError` 만 씁니다(gRPC 의 status 와 본문 구분과 같습니다). 키트 클라이언트는 요청마다 완료 델리게이트를 받습니다.
- **운영 HTTP 엔드포인트는 기본으로 꺼져 있고(`_opsPort` 0), 켜면 `127.0.0.1` 에 바인딩합니다**(2026-10-06, Nakama 기본값과 같음). 운영 지표와 상태 엔드포인트는 엔진(`Engine/Observability`)에 있어 전용 서버가 GameFramework 없이 씁니다.
- **매칭 권한은 모드마다 캐시 임대(10 초) 하나입니다**(2026-10-06). 순위표 점수는 서버만 제출하고, 표마다 허용한 경우(`_bClientSubmit`)에만 클라이언트가 제출합니다. 길드는 소셜 키트(`GF_Server_Social`) 안에 있습니다.
- **외부 계정이나 비용이 드는 연동은 계약과 가짜 제공자까지만 둡니다**(2026-10-06). APNs, FCM 푸시와 Apple, Google, Steam 영수증 검증이 여기에 해당합니다. 금칙어 목록은 테스트용 낱말만 저장소에 둡니다.
  **2026-10-22 바꿈**: 실제 제공자 코드를 넣되 자격(키 · 인증서 · 서비스 계정)은 운영 비밀(환경 변수)에서만 읽고, 설정이 없으면 그 제공자를 만들지 않으며 만들다 실패하면 서버 기동이 실패합니다(가짜로 대신 뜨지 않는다).
  확인은 가짜 HTTPS 서버로 합니다. 푸시는 FCM(`Provider/Fcm`), 영수증은 스팀 · 구글 · 애플(`Provider/Steam` · `Provider/Google` · `Provider/Apple`), 로그인은 애플(`Provider/Apple`)이 들어갔고 APNs(HTTP/2 필요)만 남았습니다([온라인 키트 마감 계획](../plans/OnlineKits.md) 4 절).
- **서비스 계정 토큰과 JWT 는 기반 계층 하나입니다**(2026-10-22, OnlineKits G4 · H3). FCM 과 구글 영수증이 같은 서비스 계정 OAuth(JWT 베어러 교환)를 쓰므로 토큰 받기 · 캐시 · 만료 전 갱신은 `ServiceAccountTokenSource`(Base `Online/HTTP`)가,
  제공자마다 쓰는 HTTP 클라이언트와 TLS 컨텍스트 묶음은 `OwnedHTTPClient` 가, JWT 서명 · 확인(`JSONWebToken`)은 Base `Online/Security` 가 갖습니다. 키트끼리 include 하지 않으므로 제품 폴더가 서로를 베끼는 대신 기반에서 나눕니다.
- **푸시 문구는 현지화 키이고 토픽은 지역마다입니다**(2026-10-22, G5). 라이브 이벤트 시작 알림은 `liveops.<지역>`(지역 제한이 없으면 `liveops.all`) 토픽으로 한 번 보냅니다. 문구가 현지화 키(`title_loc_key` · `loc-key`)라 기기가 자기 언어로 풀어서 언어 토픽은 두지 않습니다
  (결정 4 절의 "지역 · 언어 토픽"을 이것으로 바꿨습니다). 서버 여럿이 같은 창을 알리지 않도록 `liveops_push` 표를 "없어야 한다" 조건으로 써 쓴 서버만 보냅니다. 표는 창마다 하나뿐이라 지우지 않습니다.
- **구글 환불은 RTDN 대신 무효 구매 목록(`voidedpurchases`) 조회입니다**(2026-10-22, H3). RTDN 은 Pub/Sub 구독 시설이 따로 있어야 하는데 같은 정보(환불 · 취소된 주문)를 목록 조회로 받을 수 있습니다. 스팀 환불은 `GetReport` 조회입니다.
  두 조회는 지난 성공 시각보다 이틀 앞에서 읽고 겹친 주문은 한 번만 알립니다(재시작 뒤 겹친 알림은 환불 회수가 거래 id 로 한 번만 처리합니다).
- **애플 영수증은 서명된 거래(JWS)의 `x5c` 사슬을 애플 루트까지 확인합니다**(2026-10-22, H3). 보안 제공자(`INetSecurityProvider`)에 `verifyCertificateChain`(시각 · 신뢰 루트 · 리프와 발급 인증서의 확장 표지 OID)을 더했고, 사슬을 만드는 테스트가 쓰도록 `createTestCertificateChain` 도 뒀습니다.
  애플 서버 알림(웹훅)은 바깥에서 받는 시설이 필요해 쓰지 않고 알림 기록 조회로 환불을 받습니다.
- **애플 로그인의 연동 비밀은 암호화해서만 저장하고, 탈퇴한 계정의 토큰은 철회 대기열로 옮긴 뒤 철회합니다**(2026-10-22, A4). 철회에는 인가 코드를 교환해 받은 refresh token 이 필요합니다. 비밀은 `LoginSecretVault`(AES-256-GCM, 제공자 이름이 추가 인증 데이터)로 봉인해
  `login_account_secret` 에 쓰고, 탈퇴 정리 · 연동 해제가 `login_revoke_queue` 로 옮기며, 서비스가 주기마다 읽어 철회합니다. 철회에 실패하면(제공자에 닿지 못함) 항목이 남아 다시 합니다. 키가 없는 서버는 비밀을 버립니다(평문 저장 없음).
- **접속 티켓을 내미는 클라이언트는 실행 인자가 아니라 로그인 뒤 코드가 엽니다**(2026-10-22, F5). `-gv_netConnectAddress` 는 자격 없이 붙는 길로 남고(티켓 서버는 거절), 티켓이 있는 클라이언트는 `GameNetSession::makeTicketClientParams` 의 인자로 `initialize` 합니다.
  서버 주소는 서버 배정이, 티켓은 계정 키트가 주기 때문입니다. 거절된 티켓은 세션이 지워 같은 티켓으로 다시 붙지 않습니다.
- **부하 테스트 봇(`Tools/OnlineLoadBot`)은 Dev 구성에서만 빌드합니다**(2026-10-06).
- **시간 · 바이트 단위와 온라인 · 스트림 상한은 정본 한 곳에 모읍니다**(2026-10-15). 시간 · 바이트 단위는 `Defines.h` 의 `constant` 가, 온라인 저장소 상한은 `OnlineLimit.h` 가 갖습니다(서비스마다 같은 값을 따로 적어 한쪽만 바뀌는 일을 막으려는 것). 재시도 횟수 `kMaxConflictRetry = 4` 는 길드 · 소셜 · 리더보드가 각자 정하던 값이 우연히 같았던 것을 한 정책으로 묶은 것이며, 서비스별로 다르게 하려면 다시 나눕니다. 스트림 큐 기본값은 보내기 줄(`kDefaultMaxQueuedSendBytes`)과 받기 적체(`kDefaultMaxPendingReceiveBytes`)가 독립인 설정이라 상수를 둘로 둡니다.
- **중단점에 멈춘 쪽은 패킷 비트로 알리고 상대가 시간 초과를 늘립니다**(2026-10-11, 서버 디버깅 S2). 시간 초과를 끄는 전역 스위치(언리얼 `-NoTimeouts`)는 양쪽을 미리 맞춰 띄워야 하고 진짜 끊김도 못 보게 해서, 디버거가 붙은 쪽만 알리는 연결 단위 신호(`NetDebugHold`, 대기 시한 600초)와 멈췄던 쪽의 가상 시계를 골랐습니다.
  Shipping 은 알리지도 믿지도 않습니다. 디버거 없이 멈추는 길은 Dev 서버 콘솔 `pause` · `step N` · `resume` 이며, 네트워크 스레드는 계속 돌아 연결이 삽니다.
- **키트 서버 조립의 옵션 · 메서드는 표 하나씩입니다**(2026-10-15, C-26 ①). 옵션은 키트가 (이름, 읽어 넣을 칸) 표로 `OnlineServerKitEntry::readOptions` 를 한 번 부르고, 모르는 키 · 형식 오류는 거기서 키트 이름과 함께 거절합니다.
  메서드는 `ENUM( Count = Count )` 메서드 이름 열거형 하나가 와이어 번호(`OnlineMethodTable`, 영역 + 1 부터)와 지표 라벨(snake_case)을 함께 정합니다(언리얼 `UPROPERTY(Config)` 방향, 기억에 의존).
  `_bStarted` 류 플래그는 키트에 남깁니다 — 지키는 대상이 키트마다 다릅니다(경제 · 계정은 `initialize` 단계 서비스, 나머지는 `start` 단계 서비스).
  옵션을 리플렉션 구조체로 옮기는 안은 기각했습니다. 키트 옵션은 같은 키를 여러 번 적는 목록이 있고 키트마다 타입이 달라, 설정 파일 하나의 고정 스키마로 읽을 수 없습니다.
- **저장 드라이버는 키트 `.cpp` 의 정적 묶기 객체(`OnlineStoreDriverBinding`)로 서버 조립에 붙습니다**(2026-10-17, D7-5). 묶기 함수만 있고 부르는 곳이 없어 SQLite · PostgreSQL · RESP 를 고른 서버가 늘 기동을 멈췄고, 언로드 정리(`ServiceStoreFactory::shutdown`)도 불리지 않았습니다.
  모듈 수명과 같은 정적 객체가 묶고 풀어서 "실렸는데 안 묶임" · "풀지 않은 함수" 를 쓸 수 없게 했습니다(키트 조립의 `SW_REGISTER_ONLINE_SERVER_KIT` 과 같은 모양).
- **키트가 기반 표에 올리는 것(로컬 저장 바닥, SQL 드라이버)은 정적 등록 객체가 모듈 로드 때 올리고, 표는 모듈 언로드 리스너로서 내려가는 이미지의 항목을 지웁니다**(2026-10-18, [엔진 분할 계획](../plans/EnginePartition.md) E6). 등록 함수는 비공개이고 창구는 등록 객체(`LocalStoreBackendRegistration`, `SQLDriverRegistration`) 하나라 "실렸는데 안 올림"(제품에서 "sqlite" 로컬 저장이 한 번도 오르지 않았다)과 "처음 쓸 때 올림"(PostgreSQL)을 쓸 수 없습니다.
  핫 리로드는 옛 이미지의 언로드(정적 소멸자)를 미루므로 소멸자만으로 내리면 옛 항목이 남거나 새 이미지의 같은 이름을 지웁니다. 그래서 표가 `IModuleUnloadListener` 로 이미지 범위의 항목을 지우고, 등록 객체의 소멸자는 자기 항목만(함수 · 객체 주소로) 지웁니다. 같은 이름의 두 번째 등록은 단언과 오류입니다.
  표를 서브시스템으로 두는 안은 기각했습니다. Shipping 은 정적 링크라 등록이 `main` 전 정적 초기화에서 돌고, 그때는 범위가 없습니다. 남는 정적 변수를 막는 게이트는 두지 않았습니다 — 두 표는 등록 창구가 형식으로 하나라 남은 틈이 없습니다.
- **호스트 이름은 Core 의 막는 해석(`PlatformSocketUtil::resolveAddress`, `getaddrinfo` IPv4)으로 기동 때 한 번 풉니다**(2026-10-17, D7-5). `localhost` 만 손으로 바꾸던 자리(캐시 엔드포인트)를 이 함수로 바꿨고, 풀리지 않으면 기동 오류입니다.
  틱 · 요청 경로는 DNS 왕복만큼 막히므로 그대로 부르지 않습니다 — HTTP 클라이언트는 전용 해석 스레드(처음 이름을 받을 때 띄움)에 맡기고 `update` 가 연결을 겁니다. TaskManager 워커는 막는 일을 받지 않아 쓰지 않습니다.
- **온라인 접속은 Hello 첫 필드의 온라인 스키마 해시 하나로 거절합니다**(2026-10-17, [고정 배치형 코덱 계약](../../Source/Engine/Serialization/Fixed/README.md) 단계 5-a). 키트마다 (영역, 판) 목록을 대조하던 협상을 없앴습니다.
  해시는 빌드에 올라온 `OnlineProtocolEntry` 전부(영역 순)와 `kBaseVersion` 을 섞은 값이고, 키트 서비스가 `getProtocol` 로 그 항목을 돌려줘야 하므로 키트가 따로 목록을 적어 빠뜨릴 곳이 없습니다.
  클라이언트가 올린 키트만 섞으면 키트 일부만 쓰는 클라이언트가 서버와 어긋나므로 빌드 전체를 섞습니다(서버와 클라이언트는 같은 빌드로 배포). 다르면 `[kVersionMismatch][서버 해시 u64]` 로 답하고 연결을 닫으며, 클라이언트는 다시 접속하지 않고 계정 키트는 `LoginResult::UpdateRequired` 를 냅니다.
  다른 키트는 키트 표보다 먼저 `OnlineReplyUtil::markError` 가 결과를 `UpdateRequired` 로 바꿉니다(키트 표의 기본값 `Unavailable` 은 재시도를 부르므로). Hello 에 다른 답이 오면 클라이언트는 `HandshakeFailed` 로 멈춥니다.
  한쪽만 키트 모듈을 끄거나(`EditorModuleOverrides`) 핫 리로드로 키트 프로토콜을 바꾸면 그쪽 해시만 달라져 접속이 거절됩니다. 같은 빌드끼리만 잇는다는 규칙대로의 동작이라 따로 맞추지 않습니다.
  언리얼 `FNetworkVersion::GetLocalNetworkVersion`, Unity Netcode `ProtocolVersion` 과 같은 자리입니다(기억에 의존). 옛 클라이언트를 받는 탈출구는 열지 않았습니다.
- **온라인 키트 메시지는 고정 배치형(B)이고, 키트의 메시지 타입 목록 하나가 쓰기 · 읽기와 스키마 해시를 함께 정합니다**(2026-10-17, [고정 배치형 코덱 계약](../../Source/Engine/Serialization/Fixed/README.md) 단계 5-b).
  11 키트의 손 코덱과 손 `kVersion` 을 지웠습니다. 키트는 `XxxProtocol::MessageTypes = OnlineMessageTypes<...>` 하나를 두고, `write` · `read` 는 목록에 없는 타입이면 컴파일이 멈춥니다.
  같은 목록을 `OnlineProtocolEntry` 에 넘기므로 타입마다의 `FixedLayout::getSchemaHash` 가 온라인 스키마 해시에 섞이고, 해시용 목록을 따로 적어 빠뜨릴 곳이 없습니다(언리얼 Iris 와 같은 방향, 기억에 의존). 세부:
  - 메서드와 알림 번호는 키트의 메서드 표(`OnlineMethodTable`, `ENUM( Count = Count )` 열거형 둘)의 열거자 순서에서 나오고, 등록 항목이 그 이름 순서를 해시에 섞습니다.
    같은 타입을 받는 두 메서드(`TradeIDRequest` 의 Lock 과 Cancel 등)의 번호를 맞바꾸면 해시가 달라지므로, 규칙 문장 없이 형식이 막습니다.
    손으로 번호를 적던 일곱 키트를 이 표로 옮겼고, 번호가 연속이 아니던 서버 디렉터리(0 부터)와 소셜(길드가 0x10 부터)은 와이어 번호가 바뀌었습니다. 해시도 바뀌므로 옛 빌드와는 접속에서 갈립니다.
  - 메시지 머리에 그 타입의 해시(64 비트)를 싣습니다(계획 초안은 본문만 쓰기). 메서드 표는 이름과 번호만 담고 받는 타입은 담지 않아, 한 메서드가 받는 타입을 목록 안의 다른 타입으로 바꾼 빌드는 핸드셰이크 해시가 같습니다.
    그 어긋남은 머리 해시만 잡으므로(`SchemaMismatch`) 8 바이트를 남겼습니다. 없애려면 메서드마다 받는 타입을 표에 적어 해시에 섞어야 하고, 그 전에는 없앤 뒤의 안전을 테스트로 보일 수 없습니다.
  - 저장소에 남는 값(채팅 기록, 라이브 운영 이벤트 · 기기, 서버 디렉터리 점검 · 공지, 거래 레코드)과 캐시 값(파티 · 로비, 접속 상태)은 A(`ServiceRecordUtil`, 판 머리)입니다. 캐시도 배포 중에 다른 빌드의 서버가 읽기 때문입니다.
    옛 손 형식 레코드의 변환 코드는 두지 않았고, 못 읽은 레코드의 처리는 읽기 함수가 정합니다. 영속 레코드는 `ServiceRecordUtil::readDurable` 로 읽어 손상이면 오류 로그 후 그 요청이나 다시 읽기를 실패로 끝내고(건너뛰지 않음),
    다시 만들 수 있는 캐시는 `readCache` 로 읽어 없는 것으로 봅니다.
    A 는 `MaxCount` 를 보지 않으므로 두 읽기는 B 코덱이 있는 타입이면 B 쓰기 검사로 범위를 다시 보고, 없는 타입은 쓰기 때의 규칙 함수(`LiveEventRules::isValid`, 서버 디렉터리 `isValidWindow` · `isValidNotice`)를 다시 지나며, 어기면 오류로 읽기를 멈춥니다.
    A 는 필드를 이름으로 짝짓고 모르는 필드(orphan)를 거절합니다. 필드를 더하면 옛 레코드의 그 필드는 기본값이 되고, 필드를 빼면 옛 레코드 전부가 읽기 실패입니다. 필드를 빼거나 뜻을 바꿀 때는 판을 올리고 데이터를 다시 씁니다.
    이관 함수는 없습니다(5-4).
    쓰기(`ServiceRecordUtil::encode`)는 `[[nodiscard]] bool` 이고 읽기가 거절할 레코드는 쓰지 않습니다(2026-10-17, 백로그 `A-17`). 실을 수 없는 필드가 있거나 B 코덱이 있는 타입인데 범위를 벗어나면 오류 로그와 false 입니다.
    (바이트를 돌려주던 옛 모양은 실패를 Debug 단언으로만 막아 Release 에서 끊긴 레코드가 저장소에 들어갔다.) 호출부는 저장소 작업을 하지 않고 요청을 `Invalid` 로 끝냅니다(재시도로 풀리지 않으므로 `Unavailable` 이 아님).
    요청이 없는 쓰기(채팅 기록 묶음, 접속 상태 시한 연장)는 그 값만 빼고 오류 로그를 남깁니다. 서비스마다 손 규칙(`isValidLobbyRequest` 등)이 같은 범위를 먼저 보므로 호출부의 실패 분기는 공개 API 로 닿지 않아, 테스트는 쓰기 함수에서 거절을 봅니다.
  - 범위 밖 요청은 클라이언트가 보내지 않고 `kInvalidRequest` 응답으로 끝냅니다(서버가 거절한 것과 같은 결과). 쓰기와 거절은 `ServiceClientCallTable::sendMessage` 하나가 맡고,
    거절 응답은 `OnlineServiceClient::rejectRequest` 가 만들어 서버 응답과 같은 처리기를 지납니다. 약속: 0 이 아닌 요청 번호를 돌려주고, 콜백(또는 받은 응답 목록)의 번호가 그 번호와 같으며, 보내지 못한 요청의 콜백은 요청 함수 안에서 바로 옵니다.
    서버는 응답 메시지를 `OnlineServiceHost::respondMessage` 하나로 보내고, 쓰지 못하면 오류 로그와 `kInternal` 로 답합니다. 계정 키트만 계정을 연결에 묶은 뒤에 응답해야 하므로 몸을 먼저 써 두고 같은 약속으로 답합니다. 알림이 범위 밖이면 오류 로그를 남기고 보내지 않습니다.
  - 같은 상한을 상수로도 두는 곳(`LedgerConstant::kMaxAssetIDSize`, `ChatLimit`, `ServiceAuditEntry::kMaxStateSize` 등)은 `PROPERTY( MaxCount = LedgerConstant::kMaxAssetIDSize )` 처럼 상수 이름을 적습니다(2026-10-21).
    생성기가 libclang 으로 그 상수의 값을 계산해 코덱 · 정규 문자열(스키마 해시) · `PropertyMetadata::_maxCount` 에 넣으므로 정본이 상수 하나입니다([고정 배치형 코덱 계약](../../Source/Engine/Serialization/Fixed/README.md) 3.5).
    전에는 애노테이션이 정수 리터럴만 받아 키트 테스트 `DeclaredLimitsMatchTheConstants` 가 정규 문자열에서 `MaxCount` 를 읽어 상수와 대조했는데, 그 게이트를 지웠습니다.
  - **서버 버스는 A 입니다**(2026-10-17, 사용자 결정 — 서버 사이에는 핸드셰이크가 없고 배포 중에는 빌드가 섞인다).
    주제 정의(`ServerBusTopic<T>` · `ServerBusTopicFamily<T>`)가 이름과 본문 타입을 묶고, 본문은 `T::kBusVersion`(의미 버전) 머리 + `BinarySerializer` 입니다.
    읽기는 엄격 태그 루프 하나로, 모르는 이름의 필드는 건너뛰고 읽지 못한 아는 필드(모르는 열거자)와 다른 의미 버전은 거절합니다(이관 없음, 로그 한 번 + 카운터). 중첩 구조체까지 같은 정책입니다(`SerializeContext` 플래그 둘).
    Avro 스키마 해석과 같은 방향이고, 숫자 필드 ID(protobuf)는 고르지 않았습니다 — 인코딩을 바꾸면 섞인 동안 모든 버스 · 캐시가 읽히지 않고 레코드와 버스의 정체성 체계가 둘이 됩니다.
    바이트 전송(`IServerBusTransport`)과 타입 있는 앞(`ServerBus`)을 나눠 키트는 바이트를 쓰지도 읽지도 못합니다. 버스 전용 타입은 키트 `MessageTypes` 에서 뺐습니다(클라이언트 해시에 섞이지 않음).
    push 중계는 알림 열거자 이름 · 본문 A 를 싣고 받는 서버가 등록 테이블(`OnlinePushRelayEntry`)로 자기 빌드의 번호와 B 를 만듭니다. 필수 필드 표시(`Required`)는 두지 않고(의미 버전을 높이는 것과 같다),
    `config.changed` 는 구독처가 없어 지웠고(서버는 주기로 다시 읽음), 캐시 레코드는 버스와 같은 읽기, 영속 레코드의 모르는 필드 보관은 백로그 `F-3` 입니다.
    읽기 정책, 의미 버전, push 중계, 필드 변경의 호환 규칙은 아래 항목들에 있습니다.
  - 읽기 정책(`MixedBuildCodec::decode`, 버스와 캐시 레코드가 같음): 의미 버전이 다르면 거절(이관 없음, (타입, 결과, 받은 버전)마다 오류 로그 한 번과 `ServerBusReadLog::getRejectedCount`), 타입에 없는 이름의 필드는 건너뛰고 (타입, 필드)마다 한 번 경고, 아는 필드를 읽지 못하면(모르는 열거자 · 변환 불가) 거절, 스트림에 없는 필드는 기본값, 읽은 값은 `Min` · `Max` · `MaxCount` 확인.
    모든 깊이에 같은 정책이라 `SerializeContext` 플래그를 `setSkipUnknownProperties`(없는 이름 건너뜀)와 `setSkipUnreadableProperties`(읽지 못한 아는 필드 건너뜀)로 나눴고 버스는 앞의 것만 켠다(`deserializeExactVersion`). 소프트 경로의 숨은 폴백(기록 타입이 다르고 변환이 실패하면 읽힌 만큼 받음)은 지웠다.
  - 의미 버전은 타입 안의 `static constexpr uint32 kBusVersion` 이고 `ServerBusTopic` 이 읽는다(없으면 `static_assert`). 레코드 버전 수열과 묶이지 않는다. 새 필드가 다른 필드의 뜻을 바꾸는 변경은 형식으로 막지 못하므로(B 의 `kBaseVersion` 과 같은 틈) 사람이 의미 버전을 올린다. `BinarySerializer` 의 인코딩 규칙을 바꾸면 섞인 동안 모든 버스 메시지와 캐시가 읽히지 않으니 인코딩 버전을 올릴 때는 한 배포 동안 옛 버전도 읽는다.
  - 주제가 본문 타입을 가진다: `publish( ServerBus&, const T& )` 와 `read( msg, T& )` 가 모두 `[[nodiscard]]` 이고 신호 주제도 `PROPERTY` 없는 `REFLECT` 타입을 본문으로 쓴다. `IServerBusTransport` 는 호스트와 서버 조립만 갖고 키트에는 `ServerBus` 만 넘긴다. 봉투 `[버전 u8][서버 id u64][순번 u64]`(`EphemeralServerBus`)는 바꾸면 섞인 동안 모든 메시지가 버려지는 고정 형식이다.
  - push 중계(`OnlinePushRelay`, 주제 `push.<서버 id>`): 봉투가 계정 id · 알림 열거형 FQN · 열거자 이름 · 본문 FQN · 본문 A 를 싣고, 받는 서버가 `OnlinePushRelayEntry`(등록 목록 `ServiceFactoryList`)로 (열거형, 열거자 이름)을 찾아 본문 A 를 읽어 자기 빌드의 B 로 다시 써 클라이언트에 보낸다. 항목이 없거나 본문 타입이 다르거나 읽지 못하면 (열거형, 열거자)마다 한 번 오류 로그와 카운터를 남기고 버린다. 알림 열거자 이름을 바꾸면 섞인 동안 옛 빌드의 중계가 거절된다.
  - 필드 변경: 추가는 허용(옛 → 새는 기본값, 새 → 옛은 버림, 기본값이 "값 없음"이어야 하고 받는 쪽이 못 다루면 의미 버전을 올림). 삭제는 옛 빌드가 기본값을 받아도 되는 필드만 한 배포로, 아니면 먼저 읽지 않게 배포하고 다음 배포에서 지운다. 이름 변경은 새 이름 필드를 더해 둘 다 쓰고 다음 배포에서 옛 필드를 지우거나 의미 버전을 올린다. 타입 변경은 새 이름의 필드를 더한다. 열거자 추가 · 이름 변경과 `MaxCount` 변경은 옛 빌드가 거절하므로 버려도 되는 주제가 아니면 두 배포로 나눈다. 중첩 타입의 FQN 변경은 하지 않는다. 뜻만 바뀌면 의미 버전을 올린다.
    메시지가 여러 서버를 거치면 새 필드는 옛 빌드 홉을 한 번이라도 지나면 기본값이 된다(예: `mm.queue` → 옛 매처 → `MatchFormed`). 필수 필드 표시(`PROPERTY( Required )`)는 두지 않는다.
  - 테스트: 혼합 빌드는 필드 이름이 겹치는 두 타입(`BusTestNoticeOld` · `BusTestNoticeNew`)에 쓴 바이트를 다른 쪽으로 읽어 흉내 내고, 중첩 타입 변경은 같은 이름의 두 `TypeInfo` 를 서로 다른 `TypeRegistry` 에 등록해 `SerializeContext::setTypeRegistry` 로 나눈다. 크기 테스트는 `MatchFormed` 를 실제 상한(`MatchmakingLimit`)으로 채운 A 크기가 `kMaxMessageSize` 이하임을 단언한다(`MaxCount` 를 모두 곱한 값은 실제 상한이 아니다). 로그 폭주는 (타입, 필드) · (타입, 결과, 버전)마다 한 번으로 막았다.
  - 가변 길이가 두 겹인 칸(`vector<string>`, `vector<vector<T>>`)은 원소를 구조체로 감쌌습니다(`AccountLinkedProvider`, `MatchFormedTeam`). 클라이언트에 보내지 않는 값은 형식으로 막았습니다: 점검 허용 목록은 상태 응답의 타입(`MaintenanceView`)에 없고, 서버 안에서만 쓰는 칸(`AdminReply::_bRevokeSessions`, 레코드 판 `_version`)은 `PROPERTY` 가 아닙니다.
  - 의미만 바뀌고 배치는 그대로인 변경(단위 변경 같은 것)은 해시가 잡지 못하므로 `OnlineProtocolConstant::kBaseVersion` 하나를 올립니다.
  - 남은 손 코덱(기반 계층의 제재 버스, 접속 상태 전달 봉투와 캐시 값, 원장 · 우편 · 감사 · 원격 설정 레코드, 계정 · 길드 · 거래의 id 레코드)은 키트 프로토콜이 아니어서 옮기지 않았습니다.
- **영속 레코드는 모르는 필드를 버리지 않고 보관했다가 다시 싣습니다**(2026-10-22, 영속 레코드의 모르는 필드 보관). 배포 중 옛 빌드가 새 빌드의 레코드를 읽을 때 모르는 필드를 거절하면 요청이 실패하고, 버리고 읽으면 옛 빌드가 고쳐 다시 쓸 때 새 빌드의 값이 지워지기 때문입니다. `BinarySerializer::serializeKeepingUnknown` · `serializeVersionedKeepingUnknown` 이 읽을 때 담은 모르는 칸(이름이 빈 orphan)을 (이름 해시, 기록 타입 해시, 값) 그대로 뒤에 싣습니다(언리얼 `FPropertyTag` 보관 · protobuf unknown fields 와 같은 계열). 아는 칸 · 읽지 못한 아는 칸 · 다른 와이어 버전은 고쳐 싣지 않고 거절합니다.
  영속 레코드 타입(원장 `LedgerItemState` · 신고 `ServiceReport` · 채팅 · LiveOps 둘 · 점검 창 · 공지 · 거래)은 `ServiceRecordUnknownFields _unknownFields`(PROPERTY 아님)를 두고 `readDurable` 이 담고 `encode` 가 다시 싣습니다. 칸이 없는 타입은 `static_assert` 로 막혀 호출부 옮기기가 남을 수 없습니다. 맨 바깥 칸만 담으므로 안쪽 구조체에 칸을 더할 때는 레코드 버전을 올리고, 버전이 다르면 이관 없이 거절합니다. 캐시 레코드(`readCache`)는 서버 버스와 같은 혼합 빌드 읽기입니다.
- **넷 채널은 등록한 B 메시지 타입의 스키마 해시를 핸드셰이크 버전에 섞습니다**(2026-10-22, 넷 채널 스키마 해시 핸드셰이크). `NetMessageChannel`(`Source/Engine/Net`)이 종류 바이트마다 B 타입을 등록하고(`registerMessage<T>`), 봉인(`seal`)이 (종류, 스키마 해시)를 종류 순으로 기반 와이어 버전에 섞은 호스트 설정을 내줍니다. 배치가 다른 빌드는 핸드셰이크에서 `VersionMismatch` 로 먼저 거절됩니다. 본문만 쓰고 읽는 길(`writeMessage` · `readMessage`)은 봉인한 채널에만 열려 있고 머리 없는 바이트를 만드는 `FixedLayout::writeBody` · `readBody` 는 이 클래스만 부르므로(friend) 섞는 일을 빼먹을 자리가 없습니다. 채널은 그 설정으로 연 호스트(연결) 하나에 하나이며, 봉인한 설정이 아닌 설정으로 연 상대와는 본문만 주고받으면 안 됩니다. Mech · Horror 스냅샷이 첫 사용처입니다(`registerSnapshotMessage` · `writeSnapshotMessage`).
- **MMO 갱신 확인은 키트 메시지가 아니라 연결 계층의 전달 통지입니다**(2026-10-23, MMO 갱신 확인을 전달 통지로). 연결 계층은 이미 패킷마다 확인(ack + 32 비트)을 주고받으므로 키트가 같은 정보를 한 번 더 보내던 `kUpdateAck` 를 지웠습니다(언리얼 `PacketNotify` 와 같은 계열). `NetConnection::sendNotifiedMessage( pData, size, tag )` 가 비신뢰 메시지에 값 하나를 달면 보낸 패킷 기록(`SentPacket`)이 값과 종류(첫 바이트)를 패킷당 8 개(`kMaxNotifiedPerPacket`)까지 기억하고, 판정은 메시지마다 한 번입니다: 패킷이 확인되면 `Delivered`, 빠른 재전송이 잃은 것으로 보거나 확인 없이 패킷 창 밖으로 밀리거나 줄에서 나이 상한을 넘으면 `Lost` 이며 그 뒤 늦은 확인은 판정을 바꾸지 않습니다(`drainDeliveryNotices`).
  호스트는 `INetPeers::sendNotifiedMessage( connectionID, … )` 와 `NetInbound::_listDeliveryNotice` 로 넘기고, 라우터 처리기는 `getNotifiedKindMask()` 로 통지를 달아 보내는 종류를 등록해(받는 종류 마스크와 겹침을 거절) `onDeliveryNotice` 로 받습니다. 등록하지 않은 통지는 세고(`getUnclaimedNoticeCount`) 첫 번째는 오류 로그를 남깁니다. MMO 서버는 갱신을 서버 틱을 값으로 단 통지 메시지로 보내고 판정을 관찰자의 테이블(`_updateOutcome`, 64 틱 링)에 적으며 `NetKitWireVersion::kMMO` 를 4 로 올렸습니다. 손실 뒤 다시 보기까지 3 틱으로 옛 방식(4 틱)보다 빠릅니다.
  한계: 블랙아웃이 패킷 32 개를 넘으면 그 앞 패킷은 확인 비트 밖이라 잃음이 창 밖 밀림(256 패킷)이나 64 틱 상한으로 정해집니다. 더 긴 블랙아웃에서 늦으면 연결 쪽 "확인 비트 밖으로 밀린 미확인 패킷은 잃음" 규칙을 먼저 봅니다. 다른 키트가 "받았다" 메시지를 따로 보내는지는 보지 않았습니다(클라이언트-서버 키트의 처리한 입력 틱은 서버가 시뮬레이션한 틱이라 의미가 다릅니다).
- **로그인 응답 쓰기 실패**(2026-10-17, 백로그 `A-16`): 계정 서버가 로그인 · 재접속 응답 몸을 쓰지 못하면 `kInternal` 로 답하고, 저장소 작업이 이미 만든 그 세션은 로그아웃 작업(`LoginService::logout`)으로 지웁니다.
  응답 몸을 저장소 작업 전에 쓰는 안은 기각했습니다. 몸이 싣는 세션 토큰 · 신원 · 시한은 그 작업이 만들기 때문입니다. 지금 실패를 부르는 값(원격 설정의 상점 주소)만 미리 확인하는 것은 같은 결함이 다른 필드로 다시 날 수 있어 고르지 않았습니다.
  지우는 작업의 완료는 짝이 되는 요청이 없어 버려지고, 지우기가 저장소 오류로 실패하면 세션은 시한까지 남습니다(클라이언트는 토큰을 받지 못했으므로 쓸 수 없습니다).
- **설치 빌드 번호와 패치 방식의 출처는 팩 폴더 옆 기록(`base.packbuild.xml`) 하나이고 `patching="chain|none"` 을 늘 명시합니다**(2026-10-24, 패치 · 델타 배포 P4 의 충돌 해소). 팩을 마운트하려면 설치 빌드를 먼저 알아야 하는데 팩 안의 설정은 마운트 뒤에야 읽힌다.
  언리얼은 엔진 설정(INI)을 pak 안에 넣되 pak 을 마운트하는 부트 단계의 값은 팩 밖 파일(`Build.version` · 명령줄)에서 읽고, 유니티(Addressables)는 팩(번들) 밖의 카탈로그 파일이 번들 해시 · 버전을 쥐며, 갓은 `override.cfg` 와 `--main-pack` 인자처럼 팩 밖에서 팩을 고른다 — 셋 모두 "마운트 전에 읽는 값은 팩 밖"이다.
  그래서 프로젝트 설정 필드로 옮기지 않고(옮기면 설정 속 번호와 팩이 어긋날 수 있다) 팩 옆 기록을 유일한 출처로 둔다. 패치를 쓰지 않는 빌드(스팀)는 기록이 없는 폴더로 두지 않고 `patching="none"` 을 쓴 기록을 쿠커가 남기며(`-pack-build` 를 주지 않은 쿠킹),
  `patching` 이 없는 옛 기록은 chain 으로 보지 않고 읽기가 거절한다. none 폴더에 패치 목록이 있거나 none 기록에서 패치를 만들려 하면 오류다.
