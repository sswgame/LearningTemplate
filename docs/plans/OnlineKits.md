# 온라인 키트 마감 계획 (2026-10-21)

> 상태: 구현중 · 선행: 없음 · 영역: network · 옛번호: D7-5 · 진행: 표

온라인 키트의 남은 일을 한 문서에 단위로 모았습니다. 후보 문서(`Candidates-network.md`)의 아홉 절 `OnlineAccountProviders`, `OnlineChatModeration`, `OnlineServerDirectoryAdmin`, `OnlineFriendsGuild`, `OnlineLeaderboardExtras`, `OnlineMatchmakingIntegration`, `OnlineLiveOps`, `OnlineEconomyReceipts`, `OnlineEconomyItems` 를 옮겼습니다.
구성과 원칙은 [결정 기록](../09_Decisions.md) 5-9 가 원본입니다. 단위가 끝나면 상태 칸을 `끝남` 으로 바꾸고, 남는 교훈은 코드의 헤더 주석으로 옮깁니다.

## 1. 정한 것

- 외부 계정이나 비용이 드는 연동(애플, 구글, 스팀 영수증, APNs와 FCM 푸시, 실제 외부 로그인 등록)은 계약과 가짜 제공자까지만 둡니다(결정 기록 5-9, 사용자 결정). 이 문서에서는 `사용자 결정` 단위로 남깁니다.
- 키트끼리는 include 하지 않습니다. 두 키트를 잇는 일(길드 채널, 파티 채팅, 친구 순위표, 오프라인 푸시)은 기반 `Online/` 의 인터페이스를 서버 조립 문맥(`OnlineServerKitContext`)에 내놓고 다른 키트가 `start` 에서 읽는 방식으로 합니다.
- 부하 봇 측정이 근거인 결정(채팅 채널 샤딩, 매칭 작은 n 전수 탐색)은 측정 전에는 구현하지 않습니다.

## 2. 단위

| 단계 | 내용 | 선행 | 상태 |
|---|---|---|---|
| A1 | 엔진 `IHTTPClient` 를 GameFramework `HTTPClient` 로 보내는 어댑터 `BlockingHTTPClient` | 없음 | 끝남(빌드 미확인) |
| A2 | `IExternalBrowser` 의 OS 구현 `SystemExternalBrowser` | 없음 | 끝남(빌드 미확인) |
| A3 | 계정 키트 서버 조립이 제공자 설정 파일과 신뢰 파일로 제공자를 만들어 등록(`PlatformLoginProviderSet`) | 없음 | 끝남(빌드 미확인) |
| A4 | 애플 client secret JWT, 인가 코드 교환, 탈퇴 때 토큰 철회(`Provider/Apple/`) | 결정(4절) | 남음(설계 4절 — 갱신 토큰 보관 칸이 먼저) |
| A5 | 실제 제공자 등록(client id), 스팀 티켓 확인, 모바일 SDK 클라이언트 | 결정(4절) | 일부(티켓 확인 API 제공자 `TicketQueryLoginProvider` — 스팀은 데이터 한 줄, 빌드 미확인. 등록 · 모바일 SDK 는 남음) |
| B1 | 채팅 신고: 메시지 스냅숏을 영속 테이블에 남기고 GM 도구가 목록을 읽고 처리 | 없음 | 끝남(빌드 미확인) |
| B2 | 채팅 채널 샤딩 | 부하 봇 측정 | 남음(측정 시나리오 `chat_send_p99` 들어감 — 숫자를 잰 뒤 정한다) |
| C1 | GM 도구에서 서버 디렉터리 점검과 공지 바꾸기 | 없음 | 끝남(빌드 미확인) |
| D1 | 길드 채널: 길드 가입과 탈퇴를 채팅 멤버로 연결 | 없음 | 끝남(빌드 미확인) |
| D2 | 길드 레벨과 공개 길드 가입 신청 | 없음 | 끝남(빌드 미확인) |
| E1 | 친구 순위표 | 없음 | 끝남(빌드 미확인) |
| E2 | 업적 진행률과 숨김 업적 목록 | 없음 | 끝남(빌드 미확인) |
| F1 | 매칭 실력 공급과 게임 서버 에이전트를 UDP 접속 인증기에 연결 | 없음 | 끝남(빌드 미확인) |
| F2 | 백필(경기 중 빈자리) | F1 | 끝남(빌드 미확인) |
| F3 | 재접속 유예 중 파티와 로비 유지 | 없음 | 끝남(빌드 미확인) |
| F4 | 파티 채팅 채널 | D1 | 끝남(빌드 미확인) |
| F5 | 전용 게임 서버 조립이 접속 인증기(`AccountConnectAuthenticator` 와 `MatchServerAgent`)를 UDP `NetHost` 에 끼우고 배정 주제를 구독 | F1, ServerDebug 의 GameNetSession | 끝남(빌드 미확인) |
| G1 | A/B 실험(원격 설정 출시 비율과 지표) | 없음 | 끝남(빌드 미확인) |
| G2 | 우편의 오프라인 푸시 연결(`IOfflinePushSender`) | 없음 | 끝남(빌드 미확인) |
| G5 | 라이브 이벤트 시작의 오프라인 푸시(전체 대상이라 제공자의 토픽 발송이 필요하다) | G4 | 남음(설계 4절 — 제공자 계약에 토픽 발송) |
| G3 | 이벤트 운영 화면(GF_Admin 패널) | C1 | 끝남(빌드 미확인) |
| G4 | 실제 푸시 제공자 둘 | 결정(4절) | 남음(설계 4절 — FCM 먼저, APNs 는 HTTP/2 클라이언트가 먼저) |
| H1 | 환불과 결제 취소 알림을 받아 `refund.revoke` 로 회수 | 없음 | 끝남(빌드 미확인) |
| H2 | 유상 재화 청약철회 규칙 | H1 | 끝남(빌드 미확인) |
| H3 | 영수증 제공자 셋(애플, 구글, 스팀) | 결정(4절) | 남음(설계 4절 — 스팀 → 구글 → 애플 순서) |
| I1 | 인스턴스 아이템(내구도, 강화) | 없음 | 끝남(빌드 미확인) |
| I2 | 새 우편 알림(`mail_notice` 를 우편함 키트가 가져가 알림, 중계, 오프라인 푸시로 보냄) | 없음 | 끝남(빌드 미확인) |
| I7 | 잔액 변경 알림(GM 지급과 환불 회수처럼 클라이언트 요청 없이 움직인 잔액) | I2 | 끝남(빌드 미확인) |
| I3 | 원장 보존 검사와 만료 쓸기를 예약 작업으로, 발행과 소각 총량 지표 | 없음 | 끝남(빌드 미확인) |
| I4 | 분개, 감사, 우편, 멱등, 구매 기록의 보존 기간 정리 | I3 | 끝남(빌드 미확인) |
| I5 | 접속하지 않은 계정의 이름 찾기 | 없음 | 끝남(빌드 미확인) |
| I6 | GM 전용 호스트 포트를 서버 설정에 | 없음 | 남음(포트 샤딩 `3e430c317` 이 들어와 막힘은 풀렸다 — 2026-10-22 작업에서 손대지 못했다) |
| U1 | 클라이언트 UI 위젯(채팅, 서버 고르기, 친구 목록) | B1, C1 | 남음(2026-10-22 작업에서 손대지 못했다 — 화면 확인 없이 코드만 넣는 것도 위젯 단계와 묶는다) |
| M1 | 측정: 친구 접속 상태 팬아웃(친구 200 명), `ChatService.send` p99 | 부하 봇 | 남음(시나리오 `friend_presence_fanout` · `chat_send_p99` 들어감 — 돌려 숫자를 남기는 일만 남았다) |

## 4. 결정 (2026-10-22, 권고안 — 사용자 지시로 되묻지 않고 정했다)

1 절의 "외부 계정 · 비용 연동은 계약과 가짜까지만"을 바꾼다. **실제 제공자 코드를 넣되, 자격(키 · 인증서 · 서비스 계정)은 운영 비밀(환경 변수)에서만 읽고, 설정이 없으면 그 제공자를 만들지 않는다**(만들다 실패하면 서버 기동 실패 — 가짜로 대신 뜨지 않는다).
제품 이름은 지금처럼 `Provider/<제품>/` 안에만 쓰고, 데이터로 정의할 수 있는 것은 데이터로 한다. 확인은 가짜 HTTPS 서버(테스트의 `FakeIssuerHandler` 모양)로 한다 — 실제 계정 등록은 쓰는 게임이 생길 때다.

- A5: 스팀 티켓 확인은 데이터다 — 새 방식 `ticketQuery`(`TicketQueryLoginProvider`, 주소 · 인자 · 결과 경로 · 성공 값이 설정)로 제품 코드 없이 붙는다(들어감). 구글 · 애플 · 카카오의 client id 는 제공자 설정 파일의 `oidc` 줄이며 게임마다 넣는다. 모바일 SDK 클라이언트는 모바일 빌드가 생길 때.
- A4(설계): `Provider/Apple/AppleSignInClient` — client secret 은 `JSONWebToken::makeSigned`(ES256, `kid` = 키 id, `iss` = 팀 id, `sub` = client id, `aud` = `https://appleid.apple.com`, 수명 ≤ 6 개월),
  인가 코드 교환(`POST /auth/token`, form)으로 받은 refresh token 을 외부 연동 레코드 옆 칸에 보관, 탈퇴 정리(`LoginService::purgeDueDeletions`)가 `POST /auth/revoke` 를 부른다. 먼저 할 일은 연동 레코드의 비밀 칸(암호화 저장)이다.
- G4(설계): FCM HTTP v1 을 먼저 — 서비스 계정 JWT(RS256 — `makeSigned` 가 이미 서명한다)로 OAuth 토큰(`urn:ietf:params:oauth:grant-type:jwt-bearer`)을 받아 캐시하고 `messages:send` 에 보낸다.
  상태 대응: 200 Delivered, 404 · `UNREGISTERED` InvalidToken, 429 RateLimited(`Retry-After`), 5xx Transient, 401 · 403 Rejected(토큰 다시 받기). APNs 는 HTTP/2 가 필수라 공통 HTTP 클라이언트에 HTTP/2 가 들어온 뒤다.
- G5(설계): 제공자 계약(`IPushNotificationProvider`)에 `sendToTopic( topic, message )` 를 더하고(FCM `message.topic`), 라이브 이벤트 시작이 지역 · 언어 토픽으로 한 번 보낸다(계정마다 보내지 않는다). 기기는 앱이 토픽을 구독한다.
- H3(설계, 순서): 스팀(`ISteamMicroTxn/QueryTxn` — 키 쿼리, 상태 `Succeeded` · `Refunded`, 환불은 `GetReport` 주기 조회) → 구글(서비스 계정 OAuth + `purchases.products.get`, 환불은 RTDN 구독) →
  애플(App Store Server API — ES256 JWT, 응답의 JWS 를 `x5c` 사슬로 애플 루트까지 확인해야 하므로 보안 제공자에 X.509 사슬 확인이 먼저다).
- B2 · M1: 측정이 근거라는 결정(1 절)은 그대로다. 측정 도구(부하 봇 시나리오 `chat_send_p99` · `friend_presence_fanout`)를 넣었고, 숫자를 잰 뒤 샤딩을 정한다.
- U1 · I6: 결정할 것은 없다(설계가 있는 구현 일). 2026-10-22 작업에서 손대지 못했다.

## 3. 단위의 계약과 함정

### A4

애플은 탈퇴한 계정의 토큰을 철회하라고 요구합니다(App Store 심사 지침 5.1.1(v)). 철회에는 ID 토큰이 아니라 인가 코드를 교환해 받은 refresh token 이 필요합니다.
그래서 외부 로그인 티켓에 인가 코드를 싣고, 서버가 client secret JWT(`JSONWebToken::makeSigned`, ES256)로 코드를 교환해 refresh token 을 계정의 외부 연동 레코드 옆에 보관해야 합니다.
탈퇴 정리(`LoginService::purgeDueDeletions`)가 그 토큰으로 `/auth/revoke` 를 부릅니다. 모바일 빌드가 없어 애플 로그인을 쓰는 클라이언트가 없으므로 미룹니다.

### B2

판단 근거는 부하 봇 첫 측정입니다(`OperationsAndDeployment`). 거르개 비용은 `ChatService.send` p99 로 봅니다.

### F5

2026-10-22(`wt/planleftA`, 빌드 미확인 — netcand 의 포트 샤딩 `3e430c317` 위). 게임 포트는 게임 인스턴스가 설 때 열리고 인증기를 만드는 온라인 키트는 그 뒤에 선다 — 그 순서 문제를 자리 하나로 풀었다.

- 서버 설정 `ServerConfig::_bGameConnectTicket` 을 켜면 `GameNetSession::makeServerParams` 가 게임 포트를 암호화하고 인증기 자리에 `GameConnectSecurity`(Engine/Net)를 끼운다. 모든 샤드가 같은 자리를 쓴다.
  암호 제공자(OpenSSL)는 GameFramework 모듈이 설 때 꺼내는 함수로 넣는다(`NetSecurity.cpp`) — 없으면 게임 포트를 열지 않는다.
- 계정 키트의 새 조립 `gameConnect`(옵션 `ticketKeySecretEnvironment` · `serverName` 필수)가 로그인 서버와 같은 주 키로 티켓 확인기만 세우고 `AccountConnectAuthenticator` 를 자리에 묶는다(`start`), `stop` 이 푼다. 키 읽기는 `LoginTicketKey` 하나로 모았다.
- 매칭 키트의 `gameServer` 옵션이 `MatchServerAgent` 를 세워 받아들이기 정책을 문맥(`OnlineServerKitContext::_pConnectAdmission`)에 내놓고, 배정 주제 구독 · 시한은 `MatchmakingServer` 가 돈다. 없으면 티켓이 맞는 모든 계정을 받는다(`AdmitEveryAccount`).
- 묶이기 전에 온 접속은 거절한다. 서비스가 선 뒤에도 묶이지 않았으면 `ServerApp` 이 기동을 멈춘다(폴백 없음).
- 남은 것: 클라이언트 쪽 — `GameNetSession` 클라이언트가 티켓(`NetConnectCredentials`)을 내밀어 붙는 길(지금 `-gv_netConnectAddress` 는 자격 없이 붙는다), 그리고 두 프로세스 시나리오 확인.

### F2

다른 서버에서 깨진 파티 표는 낸 서버에 Cancelled 를 버스로 알리지만, 권한 매처에서는 모드를 몰라 모든 모드의 빼기로 뺍니다.
팀 나누기와 채우기는 그리디라서 나눌 수 없는 조합은 그 닻을 건너뜁니다. 작은 n 전수 탐색은 부하 봇의 대기 시간 p99 를 보고 정합니다.
정산은 페이지 일(600 명씩)로 나뉘고, 결과가 있는 사람을 건너뛰는 멱등으로 이어 갑니다.

### I4

사용자 결정으로 권고안대로 정했습니다. 키트가 아니라 서버 런타임이 보존 기간 쓸기를 예약 작업 `retention.sweep` 으로 매일 한 번 돌립니다(`ServiceRetentionJob`, 회차는 서버 하나가 차지).

- **규칙**: 테이블, 보존 기간, 레코드에서 기록 시각을 읽는 함수(`ServiceRetentionRule`). 시각을 읽는 함수는 그 테이블 형식을 아는 쪽이 내놓습니다(`ServiceAuditLog::readTimeMs`, `Ledger::readJournalTimeMs`, `Ledger::readHistoryTimeMs`, `ServiceReportStore::readClosedTimeMs`). 시각을 읽지 못하는 레코드는 지우지 않습니다.
- **기본 기간**(서버 온라인 설정, 0 이면 지우지 않음): 감사 기록 365 일, 원장 분개와 내역 400 일, 닫힌 신고 90 일. 분개는 내역보다 하루 더 남겨 내역이 가리키는 분개가 먼저 사라지지 않게 합니다.
- **상한**: 규칙마다 한 회차에 5000 개 훑고 500 개 지웁니다. 키 순서가 시각 순서가 아니라서(감사 기록은 대상이 앞이다) 커서로 테이블을 돌고, 끝에 닿으면 처음부터 다시 돕니다. 지우는 쓰기는 읽은 버전을 조건으로 해서 그사이 바뀐 레코드는 건너뜁니다.
- **다른 기록**: 우편은 이미 만료 색인(`mail_expiry`)으로 우편함 키트가 정리합니다. 멱등 기록(`ServiceIdempotency`)은 키에 시각이 없어 저장소 구현의 만료 정리가 맡습니다. 구매 기록은 원장 분개(`shop.buy`)가 그것이라 분개 규칙에 포함되고, 계정별 구매 수(`econ_purchase`)는 한도 규칙의 상태라서 지우지 않습니다. 맡김(에스크로) 보유자의 내역 키는 `esc/<도메인>/<토큰>/<시각>/…` 이고 도메인과 토큰의 글자 규칙에 `/` 가 없어, 슬래시 셋째 뒤를 시각으로 읽어 같은 규칙으로 쓸습니다.
- **함정**: 분개 키는 클라이언트 멱등 키이기도 해서 보존 기간이 지나면 같은 키의 재시도를 가리지 못합니다. 그래서 원장 기간(400 일)을 재시도가 올 수 있는 기간보다 길게 둡니다.

### I7

기반 창 `IBalanceNotice`(`Base/Online/Ledger/`)를 경제 키트가 구현해 서버 조립 문맥 `_pBalanceNotice` 에 내놓습니다. 키트끼리 include 하지 않는 원칙은 그대로입니다.

- **알림**: 경제 알림 이름 `EconomyPushName::BalanceChanged`(`kPushBalanceChanged`), 본문 `EconomyBalanceChanged`(바뀐 자산의 잔액 절댓값 목록). 클라이언트는 받으면 캐시를 그 값으로 덮으므로 늦거나 중복되어도 결과가 같습니다.
- **전달**: 계정이 이 서버에 붙어 있으면 바로, 다른 서버에 붙어 있으면 접속 상태를 찾아 `OnlinePushRelay` 로 중계합니다(등록 항목 `Economy.BalanceChanged`). 접속하지 않은 계정에는 보내지 않습니다. 다음 로그인이 지갑 전체를 읽습니다.
- **부르는 곳**: GM 키트의 지급과 회수(`AdjustAsset`, 재시도로 되풀이된 응답은 제외), 경제 키트의 스토어 환불 회수(`revokeRefund` 가 바뀐 계정 잔액을 돌려준다), 우편함 키트의 만료 반환(`sweepExpired` 가 보낸 쪽에 돌려준 재화의 잔액을 모아 서비스가 알린다). 거래 정산은 거래 키트의 알림이 양쪽에 가므로 이 길을 쓰지 않습니다.
