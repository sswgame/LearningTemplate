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
| A4 | 애플 client secret JWT, 인가 코드 교환, 탈퇴 때 토큰 철회(`Provider/Apple/`) | 결정(4절) | 끝남(2026-10-22 Debug 빌드 · 테스트 통과) |
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
| F5 | 전용 게임 서버 조립이 접속 인증기(`AccountConnectAuthenticator` 와 `MatchServerAgent`)를 UDP `NetHost` 에 끼우고 배정 주제를 구독 | F1, ServerDebug 의 GameNetSession | 끝남(서버 쪽 빌드 미확인, 클라이언트 티켓 제시는 2026-10-22 Debug 빌드 · 테스트 통과) |
| G1 | A/B 실험(원격 설정 출시 비율과 지표) | 없음 | 끝남(빌드 미확인) |
| G2 | 우편의 오프라인 푸시 연결(`IOfflinePushSender`) | 없음 | 끝남(빌드 미확인) |
| G5 | 라이브 이벤트 시작의 오프라인 푸시(전체 대상이라 제공자의 토픽 발송이 필요하다) | G4 | 끝남(2026-10-22 Debug 빌드 · 테스트 통과) |
| G3 | 이벤트 운영 화면(GF_Admin 패널) | C1 | 끝남(빌드 미확인) |
| G4 | 실제 푸시 제공자 둘 | 결정(4절) | 일부(FCM 끝남 — 2026-10-22 Debug 빌드 · 테스트 통과. APNs 는 HTTP/2 클라이언트 단계 H2-1(프레임 · HPACK 부호기와 허프만 없는 복호기) 코드가 들어감 — 아래 3절 G4 단계표. 확인 대기: 매니페스트 id `http2-codec-tests`) |
| H1 | 환불과 결제 취소 알림을 받아 `refund.revoke` 로 회수 | 없음 | 끝남(빌드 미확인) |
| H2 | 유상 재화 청약철회 규칙 | H1 | 끝남(빌드 미확인) |
| H3 | 영수증 제공자 셋(애플, 구글, 스팀) | 결정(4절) | 끝남(2026-10-22 Debug 빌드 · 테스트 통과. 애플은 가짜 앱 스토어 서버와 테스트용 인증서 사슬로만 확인했다 — 실제 앱 스토어 응답 확인은 iOS 빌드가 생길 때) |
| I1 | 인스턴스 아이템(내구도, 강화) | 없음 | 끝남(빌드 미확인) |
| I2 | 새 우편 알림(`mail_notice` 를 우편함 키트가 가져가 알림, 중계, 오프라인 푸시로 보냄) | 없음 | 끝남(빌드 미확인) |
| I7 | 잔액 변경 알림(GM 지급과 환불 회수처럼 클라이언트 요청 없이 움직인 잔액) | I2 | 끝남(빌드 미확인) |
| I3 | 원장 보존 검사와 만료 쓸기를 예약 작업으로, 발행과 소각 총량 지표 | 없음 | 끝남(빌드 미확인) |
| I4 | 분개, 감사, 우편, 멱등, 구매 기록의 보존 기간 정리 | I3 | 끝남(빌드 미확인) |
| I5 | 접속하지 않은 계정의 이름 찾기 | 없음 | 끝남(빌드 미확인) |
| I6 | GM 전용 호스트 포트를 서버 설정에(`_adminServicePort` · `_adminListenAddress`, 서비스의 노출 `getExposure` 로 리슨을 가른다 — 아래 3절 I6) | 없음 | 끝남(2026-10-23 Debug 빌드 경고 0 · 테스트 통과. GM 도구의 GM 포트 접속은 2026-10-24 코드로 들어감 — 빌드 전. 확인 대기: 매니페스트 id `admin-gm-port-real`) |
| U1 | 클라이언트 UI 위젯(채팅, 서버 고르기, 친구 목록) | B1, C1 | 일부(서버 고르기 화면 — `ServerListModel` · `ServerPickerScreen` · `Resource/common/ui/online/serverpicker.ui.xml`, 2026-10-24 빌드 전. 채팅 · 친구 목록은 남음 — 아래 3절 U1. 확인 대기: 매니페스트 id `server-picker-tests`) |
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
- U1: 아래 3절 U1 에 이유와 남은 조건을 적었다. I6 의 설계는 아래 3절 I6 이다.

## 3. 단위의 계약과 함정

### A4

애플은 탈퇴한 계정의 토큰을 철회하라고 요구합니다(App Store 심사 지침 5.1.1(v)). 철회에는 ID 토큰이 아니라 인가 코드를 교환해 받은 refresh token 이 필요합니다.
그래서 외부 로그인 티켓에 인가 코드를 싣고, 서버가 client secret JWT(`JSONWebToken::makeSigned`, ES256)로 코드를 교환해 refresh token 을 계정의 외부 연동 레코드 옆에 보관해야 합니다.
탈퇴 정리(`LoginService::purgeDueDeletions`)가 그 토큰으로 `/auth/revoke` 를 부릅니다. 모바일 빌드가 없어 클라이언트는 아직 없지만 서버 쪽은 가짜 HTTPS 애플 서버로 확인했다(2026-10-22, `wt/onlinekits`).

애플 로그인(`Provider/Apple/AppleLoginProvider`)의 인가 코드 교환과 탈퇴 때 토큰 철회. 설정 종류 `appleSignIn`(`teamId` · `signingKeyId` · `keySecretEnvironment` 의 PEM 서명 키).

- **티켓**: `ID 토큰|nonce|인가 코드`. ID 토큰은 공통 OIDC 구현이 확인하고, 코드가 있으면 서버가 만든 client secret JWT(ES256)로 `POST /auth/token` 에 교환해 `refresh_token` 을 연동 비밀로 돌려준다. 교환이 실패해도 로그인은 성공한다(비밀만 없다).
- **저장**: 로그인 서비스가 연동 비밀을 `LoginSecretVault`(AES-256-GCM, 제공자 이름이 추가 인증 데이터)로 봉인해 `login_account_secret` 에 쓴다. 키는 환경 변수(키트 옵션 `linkSecretKeySecretEnvironment`)이고, 철회하는 제공자를 등록했는데 키가 없으면 서버 기동이 실패한다.
  도구가 없는 서비스는 비밀을 버리고 오류를 남긴다(평문으로 저장하지 않는다).
- **철회**: 탈퇴 정리(`purgeAccount`)와 연동 해제(`unlinkPlatform`)가 비밀을 같은 트랜잭션에서 `login_revoke_queue` 로 옮긴다. 서비스가 `_revocationScanMs`(1 분)마다 대기열을 읽어 비밀을 풀고 제공자에 `POST /auth/revoke` 한다.
  철회했거나 이미 무효(`invalid_grant`)면 항목을 지우고, 제공자에 닿지 못했으면(5xx · 전송 실패 · 설정 문제) 남겨 다음 주기에 다시 한다. 풀리지 않는 항목(키가 바뀜)과 설정에 없는 제공자의 항목은 남긴다.
- **함정**: 탈퇴 계정 삭제 뒤에 철회를 하므로, 서버가 철회 전에 죽어도 대기열이 남아 이어서 한다. 철회 대기열 항목은 지워지기 전까지 암호화된 비밀을 들고 있다. 외부 로그인 제공자 둘 이상이 같은 이름의 철회를 요구하면 이름 규칙(`[a-z0-9_]`)이 키가 된다.

### G4 · G5

FCM HTTP v1 제공자(`Provider/Fcm/FcmPushProvider`)와 이벤트 시작의 토픽 푸시. 서비스 계정 OAuth 는 `ServiceAccountTokenSource`(기반 `Online/HTTP`)가 맡는다.

- **자격**: 서비스 계정 JSON 을 환경 변수에서 읽는다(`PushProviderSet`, 키트 옵션 `pushProvidersFile` · `pushTrustFile`). 설정 파일은 `{"providers":[{"kind":"fcm","serviceAccountEnvironment":"<변수>","tokenUrl":"…","sendUrl":"…"}]}` 이고 `tokenUrl` · `sendUrl` 은 가짜 서버를 가리킬 때만 쓴다.
  변수가 없거나 서비스 계정이 틀리면 서버 기동이 실패한다. 설정 파일이 없으면 실제 제공자를 만들지 않는다.
- **상태 대응**: 200 Delivered, 404 · `UNREGISTERED` InvalidToken, 429 RateLimited(`Retry-After`), 5xx · 전송 실패 Transient, 401 · 403 Rejected(액세스 토큰을 버려 다음 배달이 새로 받는다), 그 밖의 4xx Rejected. 토큰 요청이 5xx · 전송 실패면 기다리던 배달이 Transient, 4xx 면 Rejected 다.
- **문구**: 현지화 키(`title_loc_key` · `loc-key` 와 인자)라 기기가 자기 언어로 푼다. `send` 의 `locale` 은 쓰지 않는다. 문구가 없으면 데이터 메시지만 보낸다.
- **제공자 계약**: `send` 는 서비스 시각(`nowMs`)을 받고, 제공자는 `update( nowMs )`(HTTP 전송 돌리기)와 `supportsTopics` · `sendToTopic` 을 가진다. 발송기는 토픽 발송을 토픽을 아는 제공자에게만 맡기고(`notifyTopic`), 토픽 배달의 `InvalidToken` 은 해지할 기기가 없어 버린다.
- **G5 이벤트 시작 푸시**: 이벤트 매개변수 `push.title` · `push.body`(현지화 키, `@키` 가능) · `push.link` 가 있으면 방금 열린 창(`LiveOpsPush::kStartGraceMs` 10 분 안)을 `liveops.<지역>` 토픽(지역 제한이 없으면 `liveops.all`)으로 한 번 보낸다.
  언어 토픽은 두지 않는다(문구가 현지화 키라 기기가 푼다 — 4절의 "지역 · 언어 토픽"을 이것으로 바꿨다). 같은 창을 서버 여럿이 보내지 않도록 `liveops_push` 표를 "없어야 한다" 조건으로 써서(`notifyTopicOnce`) 쓴 서버만 보낸다.
  앱은 자기 지역의 토픽을 구독한다(`LiveOpsPush::makeRegionTopic`). 서버가 다시 떠서 이미 열려 있던 이벤트를 새로 알리지 않는다(열린 지 10 분이 지난 창은 알리지 않는다).
- **함정**: `liveops_push` 표는 창마다 한 줄이고 지우지 않는다(보존 기간 정리 규칙이 없다 — 하루 한 번 반복하는 이벤트는 한 해에 365 줄). 서버 하나의 `Retry` 가 없다 — 표를 쓴 서버가 제공자 호출 전에 죽으면 그 알림은 나가지 않는다(중복보다 놓침을 택했다).
  APNs 는 HTTP/2 가 필수라 공통 HTTP 클라이언트(HTTP/1.1)와 따로 HTTP/2 클라이언트를 단계로 쌓은 뒤 `Provider/Apns/` 에 더한다. 오픈 소스 라이브러리를 들이지 않는 이유: 이 저장소의 HTTP 는 자체 TLS 계층 위에 있어 라이브러리의 소켓 · TLS 와 맞지 않는다.
  - **H2-1(2026-10-24, 빌드 전)**: `Base/Online/HTTP/HTTP2Frame`(연결 서문 · 프레임 머리 · SETTINGS · WINDOW_UPDATE · PING · HEADERS · DATA · RST_STREAM · GOAWAY 바이트)와 `Hpack`(정적 표 · 동적 표 · 정수 · 글자 그대로의 부호기와 복호기). 복호기는 허프만 문자열을 만나면 오류로 멈춘다. 테스트 `HTTP2Test.*`.
  - **H2-2(남음)**: 허프만 표(RFC 7541 부록 B 의 257 부호)를 넣어 복호기가 서버 응답 머리를 읽게 한다. 표를 외워 옮기지 말고 RFC 본문에서 기계적으로 옮겨 RFC C.4 · C.6 의 허프만 예 바이트로 테스트한다.
  - **H2-3(남음)**: TLS 계층의 ALPN(`h2`) 협상(`ITLSContext`/`ITLSSession`, OpenSSL 구현). 지금 TLS 계층에 ALPN 이 없다.
  - **H2-4(남음)**: `HTTP2Connection`(스트림 상태 · 연결/스트림 흐름 제어 창 · SETTINGS 교환 · GOAWAY 처리)과 `IHTTPClient` 어댑터. 가짜 HTTP/2 서버(`FakeIssuerHandler` 모양)로 테스트.
  - **H2-5(남음)**: `Provider/Apns/ApnsPushProvider`(제공자 서명 JWT ES256 — `JSONWebToken::makeSigned`, `apns-topic` · `apns-push-type`, 상태 대응 200/410/429/5xx/403).

### H3

스팀 · 구글 · 애플 영수증 제공자. 자격은 환경 변수에서만 읽고(`ReceiptProviderSet`, 키트 옵션 `receiptProvidersFile` · `receiptTrustFile`), 없으면 서버 기동이 실패한다.
설정 파일은 `{"providers":[{"kind":"steam","appId":"480","keySecretEnvironment":"<변수>","sandbox":false,"reportPollSeconds":3600},{"kind":"google","packageName":"<패키지>","serviceAccountEnvironment":"<변수>","voidedPollSeconds":3600},{"kind":"apple","issuerId":"<발급자 id>","keyId":"<키 id>","bundleId":"<번들>","signingKeySecretEnvironment":"<변수>","rootCertificateFile":"<Apple Root CA - G3 PEM>","historyPollSeconds":3600}]}` 이다.

- **스팀**(`Provider/Steam`): 영수증은 주문 번호(십진 숫자)다. `ISteamMicroTxn/QueryTxn` 의 `Succeeded` 는 지급, 환불류 상태(`Refunded` · `PartialRefund` · `Chargedback` · 의심 둘 · `RefundedFraudulent`)는 환불, `Init` · `Approved` 는 다시 시도, 그 밖은 거절.
  키 오류(401 · 403)는 플레이어 탓이 아니라 다시 시도로 끝내고 오류를 남긴다. 상품 id 는 첫 항목의 `itemid` 라 상점 카탈로그의 상품 id 를 그 숫자로 적는다. 환불은 `GetReport` 를 주기 조회해 한 번씩 알린다.
  `sandbox` 면 `ISteamMicroTxnSandbox` 를 쓰고 결과가 샌드박스 결제로 표시된다(서비스가 스테이징 서버가 아니면 거절한다).
- **구글**(`Provider/Google`): 영수증은 `<상품 id>|<구매 토큰>` 이다. 서비스 계정 OAuth 토큰으로 `purchases.products.get` 을 확인한다(`purchaseState` 0 지급 · 1 환불 · 2 다시 시도, `purchaseType` 0 은 테스트 구매).
  유효한 구매는 확인 상태가 0 이면 `:acknowledge` 를 한 번 보낸다(3 일 안에 확인하지 않으면 구글이 환불한다). 환불은 RTDN 대신 `voidedpurchases` 를 조회한다(4절의 "RTDN 구독"을 이것으로 바꿨다 — Pub/Sub 구독 시설이 따로 필요하다).
- **애플**(`Provider/Apple`): 영수증은 StoreKit 2 거래 id(십진 숫자)다. App Store Server API 에 서버가 만든 ES256 JWT(`iss` 발급자 id, `aud` `appstoreconnect-v1`, `bid` 번들, 수명 20 분)로 `GET /inApps/v1/transactions/{id}` 를 불러
  `signedTransactionInfo`(JWS)를 받고, 머리의 `x5c` 사슬을 설정의 애플 루트(`rootCertificateFile`)까지 이어 확인한다(`INetSecurityProvider::verifyCertificateChain` — 리프와 그 발급 인증서의 애플 확장 표지 OID 포함)한 뒤 리프 키로 서명을 확인한다.
  번들 id · 거래 id 가 요청과 같아야 하고, `revocationDate` 가 있으면 환불, `environment` 가 Sandbox 면 샌드박스 결제다. 환불은 서버 알림(웹훅) 대신 `POST /inApps/v1/notification/history`(종류 `REFUND`)를 주기 조회해 쪽(`hasMore` · `paginationToken`)을 열 쪽까지 읽는다.
  보안 제공자에 사슬 확인(`verifyCertificateChain`)과 테스트용 사슬 만들기(`createTestCertificateChain`)를 더했다(기본 구현은 false — 지원하지 않음). `JSONWebToken` 은 머리의 `x5c` 와 중첩 문자열 클레임을 읽는다.
- **애플 함정**: 응답의 모양(`signedTransactionInfo`, `notificationHistory`, `data.signedTransactionInfo`)과 애플 확장 표지 OID(`1.2.840.113635.100.6.11.1` 리프, `1.2.840.113635.100.6.2.1` 발급 인증서)는 애플 문서를 보고 쓴 것이고 실제 앱 스토어로 확인하지 못했다 —
  iOS 빌드가 생기면 샌드박스 서버(`sandbox: true`)로 한 번 돌려 본다. 샌드박스 결제를 운영 서버에 물으면 404 라 `sandbox` 설정으로 서버를 가른다(운영에서 404 이면 샌드박스로 다시 묻는 길은 두지 않았다). 알림 기록 조회 기간은 애플 상한(180 일)보다 짧은 이틀이다.
- **함정**: 스팀 · 구글의 환불 조회는 지난 성공 시각보다 이틀 앞에서 읽고(겹친 주문은 한 번만 알림) 한 번에 1000 건까지 읽는다 — 한 주기에 1000 건을 넘는 환불은 경고를 남기고 겹친 다음 조회에서만 닿을 수 있다. 환불 알림을 재시작 뒤 다시 받아도 환불 회수가 거래 id 로 한 번만 처리한다.
  스팀은 주문 번호만으로 영수증이 되므로 주문을 연 계정과 올린 계정이 같은지 이 제공자는 모른다(주문 번호는 게임 서버가 `InitTxn` 때 만든 비밀에 가까운 값이고, 지급은 거래 id 로 한 번뿐이다).

### B2

판단 근거는 부하 봇 첫 측정입니다(`OperationsAndDeployment`). 거르개 비용은 `ChatService.send` p99 로 봅니다.

### F5

2026-10-22(`wt/planleftA`, 빌드 미확인 — netcand 의 포트 샤딩 `3e430c317` 위). 게임 포트는 게임 인스턴스가 설 때 열리고 인증기를 만드는 온라인 키트는 그 뒤에 선다 — 그 순서 문제를 자리 하나로 풀었다.

- 서버 설정 `ServerConfig::_bGameConnectTicket` 을 켜면 `GameNetSession::makeServerParams` 가 게임 포트를 암호화하고 인증기 자리에 `GameConnectSecurity`(Engine/Net)를 끼운다. 모든 샤드가 같은 자리를 쓴다.
  암호 제공자(OpenSSL)는 GameFramework 모듈이 설 때 꺼내는 함수로 넣는다(`NetSecurity.cpp`) — 없으면 게임 포트를 열지 않는다.
- 계정 키트의 새 조립 `gameConnect`(옵션 `ticketKeySecretEnvironment` · `serverName` 필수)가 로그인 서버와 같은 주 키로 티켓 확인기만 세우고 `AccountConnectAuthenticator` 를 자리에 묶는다(`start`), `stop` 이 푼다. 키 읽기는 `LoginTicketKey` 하나로 모았다.
- 매칭 키트의 `gameServer` 옵션이 `MatchServerAgent` 를 세워 받아들이기 정책을 문맥(`OnlineServerKitContext::_pConnectAdmission`)에 내놓고, 배정 주제 구독 · 시한은 `MatchmakingServer` 가 돈다. 없으면 티켓이 맞는 모든 계정을 받는다(`AdmitEveryAccount`).
- 묶이기 전에 온 접속은 거절한다. 서비스가 선 뒤에도 묶이지 않았으면 `ServerApp` 이 기동을 멈춘다(폴백 없음).
- 클라이언트 쪽(2026-10-22, `wt/onlinekits`): `GameNetSession::makeTicketClientParams( 서버 주소, 티켓, 인자 )` 가 암호화 · 토큰 결속 클라이언트 인자를 만들고, 그 인자로 `initialize` 한 세션이 연결 시도 · 시간 초과 재시도 · `reconnect` 에 티켓을 내민다.
  주소는 서버 배정(`ServerDescriptor::pickPort`), 티켓은 계정 키트(`AccountClient::makeConnectCredentials`)가 주므로 실행 인자가 아니라 로그인 뒤 게임 코드가 이 인자로 연다. `-gv_netConnectAddress` 는 자격 없이 붙는 길로 남아 티켓 서버에는 `SecurityMismatch` 로 거절된다.
  거절된 티켓은 세션이 지워 새 티켓 없이 `reconnect` 하지 않는다. 암호 제공자(GameFramework)가 없으면 인자를 만들지 않는다(자격 없는 연결로 대신하지 않는다).
- 남은 것: 두 프로세스 시나리오 확인(서버 `_bGameConnectTicket` + 로그인 서버, 클라이언트가 로그인해 티켓을 받아 붙는 흐름 — 로그인 서버와 게임 서버를 함께 띄우는 시나리오 시설이 필요하다).

### I6

GM 전용 호스트 포트를 서버 설정에 — 2026-10-23 들어감. 계약은 `OnlineServiceHost.h` 머리 주석과 `ServerConfig::_adminServicePort` 가 원본이다.

**설계** — 호스트는 하나로 두고 리슨만 둘로 나눈다. 버린 길: 호스트 둘(서비스가 `attachHost` 로 호스트 하나에 묶이고, 계정 바인딩 · 로그인 이벤트가 둘로 쪼개진다), 전송 셋(IOCP · epoll · 루프백)에 다중 리슨(리눅스 epoll 을 이 PC 에서 빌드하지 못한다), 한 포트에서 원격 주소로 거르기(포트 분리가 아니다).
호스트 안의 묶음 전송(`OnlineServiceHost::ListenerTransport`, `IStreamTransport` 구현)이 리슨마다 전송 하나(플레이어 · GM)를 묶고, 연결 핸들의 슬롯 번호에 리슨 번호를 섞어(`슬롯 × 리슨 수 + 리슨`) 두 전송의 핸들이 겹치지 않게 한다. 엔드포인트 · 요청 서버 · 계정 표 · 버스 · 서비스는 그대로 하나다 — GM 이 GM 포트에서 로그인해도 같은 계정 표에 붙는다.
노출 면은 서비스의 형식이다: `IOnlineHostService::getExposure()`(`Player` 기본 · `Admin` · `PlayerAndAdmin`)를 GM 서비스가 `Admin`, 계정 서비스가 `PlayerAndAdmin`(GM 도 로그인한다)으로 답한다. GM 리슨이 있으면 연결이 들어온 리슨과 서비스의 노출이 맞지 않는 요청은 "없는 메서드" 와 같은 `kInvalidRequest` 로 답한다(영역이 있는지 드러내지 않는다). Hello · Ping 은 두 리슨 모두.
서버 설정은 `_adminServicePort`(0 = 나누지 않음 — GM 서비스도 서비스 포트에서, 지금까지와 같고 "서버 하나 더" 운영도 그대로 된다)와 `_adminListenAddress`(기본 `127.0.0.1`, 운영 엔드포인트와 같은 모양 — 사설 주소를 준다)다. TLS 는 같은 엔드포인트라 두 리슨에 같이 걸린다. 연결 상한(`_maxConnections`)은 리슨마다 따로 센다.

- 확인: `OnlineServiceHostTest.AdminListenerSplitsServicesByExposure` · `WithoutAnAdminListenerOneListenerServesEveryService`(루프백). 실제 IOCP 두 리슨과 전용 서버 기동(`_adminServicePort` 를 준 설정 파일)은 확인하지 않았다.
- GM 도구 접속(2026-10-24, `wt/plannet`, 빌드 전): `AdminToolConnection`(에디터 확장 Admin)이 전송 · `OnlineServiceClient` · `AdminClient` 를 들고 `host:port` 하나로 건다. `AdminToolSession::connectLive` · `disconnectLive` · `updateConnection` 이 감싸고 패널의 제공자 줄에 주소 칸과 Connect 단추를 더했다. 주소에 포트가 없으면 거절한다(기본 포트로 대신 걸지 않는다).
  로그인은 에디터 확장이 계정 키트를 include 할 수 없어(층 검사) 부르는 쪽이 `getConnection().setLoginHook` 으로 끼운다 — 훅이 없으면 `Ready` 에서 멈추고 서버가 `NotLoggedIn` 으로 거절한다. 훅을 끼우는 쪽 도우미 `AccountGmLogin`(계정 키트 `Account/Shared/API`, 2026-10-24, 빌드 전)이 계정 클라이언트 등록(`attach` — 연결을 열기 전에 `getConnection().getServiceClient()` 로) · 이름과 비밀번호 로그인(`onReady` — 토큰이 있으면 재접속을 계정 클라이언트가 하므로 보내지 않는다) · 응답 읽기(`update`)를 맡고, 비밀번호는 응답을 받으면 지운다. 두 키트를 함께 아는 조립 쪽(도구 실행 파일 · 게임)이 `setLoginHook` 에 `onReady` 를 건다. 이 조립을 하는 도구 실행 파일은 아직 없다 — 에디터 안에서는 설정 화면이 이름 · 비밀번호를 받는 UI 가 필요하다(U1 과 같은 UI 일). 한 계정은 연결 하나에만 붙으므로 같은 계정으로 플레이어 포트와 GM 포트에 함께 로그인하지 못한다(GM 계정을 따로 쓴다).

### U1

클라이언트 UI 위젯(채팅, 서버 고르기, 친구 목록). 모양은 "모델 + 화면"이다 — 모델(`ServerListModel`)이 클라이언트 응답을 받아 목록 · 상태 · 판(revision)을 들고, 화면(`UIScreen` 파생)이 모델을 읽어 줄을 만든다. 화면이 요청을 직접 보내지 않으므로 응답 델리게이트가 화면 수명에 묶이지 않는다.
- **서버 고르기(2026-10-24, 빌드 전)**: `Kits/Feature/Online/ServerDirectory/Shared/ServerListModel` · `ServerPickerScreen`. 문서 경로는 게임이 `ServerPickerScreen::open( ui, 경로, 모델, 종류, 고른 뒤 델리게이트 )` 에 준다(코드에 기본 경로가 없다). 견본 문서는 `Resource/common/ui/online/serverpicker.ui.xml`(패널 `Servers`, 글 `Status`, 명령 `Refresh` · `Close`). `Open` 이 아닌 서버의 단추는 꺼진다. 목록을 못 받으면 낡은 목록을 비우고 이유를 `Status` 에 적는다. 테스트 `ServerListModelTest`.
- **시나리오는 아직 없다**: 화면을 띄울 게임 장면과 서버 목록을 채우는 가짜 클라이언트(응답 주입)가 필요하다 — 서버 디렉터리 서버 키트를 한 프로세스에 띄우는 시나리오 시설이 먼저다(F5 의 두 프로세스 시나리오와 같은 시설).
- **남음**: 채팅(`ChatClient` 의 채널 · 메시지 꺼내기 → 모델 + 입력 줄 화면), 친구 목록(`SocialClient` 의 친구 응답 → 모델 + 화면). 같은 모양으로 쓴다. 계정 이름 · 비밀번호를 받는 GM 도구 설정 화면도 이 일이다.

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
