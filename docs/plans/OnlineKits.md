# 온라인 키트 마감 계획 (2026-10-21)

> 상태: 구현중 · 선행: 없음 · 영역: network · 옛번호: D7-5 · 진행: 표

온라인 키트의 남은 일을 단위로 모았습니다(후보 `Candidates-network.md` 의 `Online*` 아홉 절을 옮김). 구성과 원칙은 [결정 기록](../09_Decisions.md) 5-9 가 원본이고, 단위가 끝나면 교훈은 코드 헤더 주석으로 옮깁니다.

## 1. 정한 것

- 외부 계정 · 비용 연동은 4절 결정대로 코드를 넣되 자격은 환경 변수에서만 읽습니다.
- 키트끼리는 include 하지 않습니다. 두 키트를 잇는 일은 기반 `Online/` 의 인터페이스를 서버 조립 문맥의 지역 범위(`OnlineServerKitContext::_kits`)에 걸고(`exposeInterface`) 다른 키트가 `start` 에서 `get`(필수) · `find`(선택)로 읽습니다. 순서는 키트 이름 의존(`getRequiredKits` · `getOptionalKits`)이 정합니다.
- 부하 봇 측정이 근거인 결정(채팅 채널 샤딩, 매칭 작은 n 전수 탐색)은 측정 전에는 구현하지 않습니다.

## 2. 단위

**끝남(대부분 빌드 미확인, 코드와 테스트는 있음)**: A1 `BlockingHTTPClient` · A2 `SystemExternalBrowser` · A3 `PlatformLoginProviderSet` · B1 채팅 신고 · C1 GM 서버 디렉터리 점검 · D1 길드 채널 · D2 길드 레벨 · E1 친구 순위표 · E2 업적 진행률 · F1 매칭 접속 인증기 연결 · F2 백필 · F3 재접속 유예 · F4 파티 채팅 · G1 A/B 실험 · G2 우편 푸시 · G3 이벤트 운영 화면 · H1 환불 회수 · H2 청약철회 · I1 인스턴스 아이템 · I2 새 우편 알림 · I3 원장 보존 검사 · I5 오프라인 계정 이름 찾기.
**끝남(Debug 빌드 · 테스트 통과, 2026-10-22)**: A4 애플 토큰 철회 · G5 이벤트 시작 푸시 · H3 영수증 제공자 셋(애플은 가짜 서버로만 확인) · I4 보존 기간 정리 · I7 잔액 변경 알림(빌드 미확인) · F5 접속 인증기(서버 쪽 빌드 미확인).

| 단계 | 내용 | 선행 | 상태 |
|---|---|---|---|
| A5 | 실제 제공자 등록(client id), 스팀 티켓 확인, 모바일 SDK 클라이언트 | 결정(4절) | 일부(티켓 확인 API 제공자 `TicketQueryLoginProvider` — 스팀은 데이터 한 줄, 빌드 미확인. 등록 · 모바일 SDK 는 남음) |
| B2 | 채팅 채널 샤딩 | 부하 봇 측정 | 남음(`chat_send_p99` 를 잰 뒤 정한다) |
| G4 | 실제 푸시 제공자 둘 | 결정(4절) | 일부(FCM 끝남. APNs 는 H2-2 까지 코드 — 3절 G4. 확인 대기: 매니페스트 id `http2-codec-tests`) |
| I6 | GM 전용 호스트 포트(`_adminServicePort` · `_adminListenAddress`, 3절 I6) | 없음 | 끝남(Debug 빌드 · 테스트 통과. GM 도구의 GM 포트 접속은 코드만. 확인 대기: 매니페스트 id `admin-gm-port-real`) |
| U1 | 클라이언트 UI 위젯(채팅, 서버 고르기, 친구 목록) | B1, C1 | 일부(세 화면 코드만 들어감, 빌드 전. GM 도구 설정 화면은 남음 — 3절 U1. 확인 대기: 매니페스트 id `server-picker-tests`) |
| M1 | 측정: 친구 접속 상태 팬아웃(친구 200 명), `ChatService.send` p99 | 부하 봇 | 남음(시나리오 `friend_presence_fanout` · `chat_send_p99` 가 있다 — 돌려 숫자를 남기는 일만) |

## 4. 결정 (2026-10-22, 권고안 — 사용자 지시로 되묻지 않고 정했다)

1 절의 "외부 계정 · 비용 연동은 계약과 가짜까지만"을 바꾼다. **실제 제공자 코드를 넣되 자격(키 · 인증서 · 서비스 계정)은 환경 변수에서만 읽고, 설정이 없으면 그 제공자를 만들지 않는다**(만들다 실패하면 서버 기동 실패 — 가짜로 대신 뜨지 않는다). 제품 이름은 `Provider/<제품>/` 안에만 쓰고 데이터로 정할 수 있는 것은 데이터로 한다. 확인은 가짜 HTTPS 서버(테스트의 `FakeIssuerHandler` 모양)로 하고 실제 계정 등록은 쓰는 게임이 생길 때다.

- A5: 스팀 티켓 확인은 데이터다(`ticketQuery`, 주소 · 인자 · 결과 경로 · 성공 값이 설정). 구글 · 애플 · 카카오 client id 는 제공자 설정 파일의 `oidc` 줄(게임마다). 모바일 SDK 클라이언트는 모바일 빌드가 생길 때.
- APNs 는 HTTP/2 가 필수라 공통 HTTP 클라이언트에 HTTP/2 가 들어온 뒤다. B2 · M1 은 숫자를 잰 뒤 샤딩을 정한다.

## 3. 단위의 계약과 함정

### A4

탈퇴한 계정의 토큰 철회는 애플 심사 지침 5.1.1(v)의 요구입니다. 설정 종류 `appleSignIn`(`teamId` · `signingKeyId` · `keySecretEnvironment`). 모바일 클라이언트는 아직 없고 서버는 가짜 서버로 확인했습니다.

- **티켓** `ID 토큰|nonce|인가 코드`: 코드가 있으면 client secret JWT(ES256)로 `/auth/token` 에 교환해 refresh token 을 연동 비밀로 받습니다. 교환이 실패해도 로그인은 성공합니다(비밀만 없음).
- **저장**: `LoginSecretVault`(AES-256-GCM)로 봉인해 `login_account_secret` 에 씁니다. 키는 `linkSecretKeySecretEnvironment`이며 철회 제공자를 등록했는데 키가 없으면 기동 실패, 도구가 없으면 비밀을 버립니다(평문 저장 안 함).
- **철회**: `purgeAccount` · `unlinkPlatform` 이 비밀을 같은 트랜잭션에서 `login_revoke_queue` 로 옮기고 서비스가 1 분마다 `POST /auth/revoke` 합니다. 철회했거나 `invalid_grant` 면 지우고, 닿지 못했거나 풀리지 않거나 설정에 없는 제공자의 항목은 남깁니다. 서버가 죽어도 대기열이 남아 이어집니다.

### G4 · G5

FCM HTTP v1 제공자(`Provider/Fcm/FcmPushProvider`)와 이벤트 시작 토픽 푸시. 서비스 계정 OAuth 는 `ServiceAccountTokenSource`(`Online/HTTP`).

- **자격**: 서비스 계정 JSON 을 환경 변수에서 읽습니다(`PushProviderSet`, 옵션 `pushProvidersFile` · `pushTrustFile`, 설정 `{"providers":[{"kind":"fcm","serviceAccountEnvironment":…}]}`, `tokenUrl` · `sendUrl` 은 가짜 서버용). 변수가 없거나 틀리면 기동 실패, 설정 파일이 없으면 제공자를 만들지 않습니다.
- **상태 대응**: 200 Delivered, 404 · `UNREGISTERED` InvalidToken, 429 RateLimited(`Retry-After`), 5xx · 전송 실패 Transient, 401 · 403 Rejected(액세스 토큰을 버려 다음 배달이 새로 받음), 그 밖 4xx Rejected. 문구는 현지화 키(기기가 풂)이고 없으면 데이터 메시지만 보냅니다.
- **G5**: 이벤트 매개변수 `push.title` · `push.body` · `push.link` 가 있으면 방금 열린 창(`LiveOpsPush::kStartGraceMs` 10 분 안)을 `liveops.<지역>` 토픽(제한 없으면 `liveops.all`)으로 한 번 보냅니다. 서버 여럿 중 한 곳만 보내도록 `liveops_push` 표를 "없어야 한다" 조건으로 씁니다(`notifyTopicOnce`). 표를 쓴 서버가 제공자 호출 전에 죽으면 그 알림은 나가지 않습니다(중복보다 놓침을 택함). 표는 지우지 않습니다(보존 규칙 없음).
- **APNs(HTTP/2 단계)**: 공통 HTTP 클라이언트는 HTTP/1.1 이라 자체 TLS 계층 위에 HTTP/2 클라이언트를 쌓습니다(오픈 소스 라이브러리는 TLS 계층과 맞지 않아 쓰지 않음).
  - **H2-1 · H2-2(빌드 전)**: `Base/Online/HTTP/HTTP2Frame` 과 `Hpack`(허프만 포함 부호기 · 복호기). 허프만 · 정적 표는 `GenerateHpackHuffman.py` · `GenerateHpackStatic.py` 가 RFC 7541 에서 읽어 씁니다. 테스트 `HTTP2Test.*`.
  - **H2-3(남음)**: TLS 계층의 ALPN(`h2`) 협상(`ITLSContext`/`ITLSSession`, OpenSSL).
  - **H2-4(남음)**: `HTTP2Connection`(스트림 상태 · 흐름 제어 · SETTINGS · GOAWAY)과 `IHTTPClient` 어댑터, 가짜 HTTP/2 서버 테스트.
  - **H2-5(남음)**: `Provider/Apns/ApnsPushProvider`(JWT ES256, `apns-topic` · `apns-push-type`, 상태 대응 200/410/429/5xx/403).
- **꺼내기 표**: 소비형 큐를 여럿이 꺼내는 결함을 `Core/Container/DrainGate.h`(`DrainGate` · `DrainLease`)로 막았습니다(Online 클라이언트와 Strategy 키트의 `claim*`, 테스트 `OnlineDrainGateTest` · `EventBufferTest`). 소유자가 하나인 나머지 `drain*` · `take*` 는 표가 없습니다. 남은 의심: `CameraComponent::consumeCut` 은 소비자가 둘(`RenderViewCollector` · `CameraManagerComponent`)이라 타깃 카메라가 자기 뷰로도 그려지면 TAA 기록이 안 끊길 수 있습니다 — 읽기(`peekCut`)와 지우기를 나누는 후속(추측, 읽음 수준).

### H3

스팀 · 구글 · 애플 영수증 제공자. 자격은 환경 변수에서만(`ReceiptProviderSet`, 옵션 `receiptProvidersFile` · `receiptTrustFile`), 없으면 기동 실패.

- **스팀**: 영수증은 주문 번호. `ISteamMicroTxn/QueryTxn` 의 `Succeeded` 지급, 환불류 환불, `Init` · `Approved` 다시 시도, 그 밖 거절. 키 오류(401 · 403)는 다시 시도로 끝내고 오류를 남깁니다. 환불은 `GetReport` 주기 조회. `sandbox` 면 `ISteamMicroTxnSandbox`.
- **구글**: 영수증은 `<상품 id>|<구매 토큰>`. `purchases.products.get`(`purchaseState` 0 지급 · 1 환불 · 2 다시 시도). 확인 상태가 0 이면 `:acknowledge` 를 한 번 보냅니다(3 일 안에 안 하면 구글이 환불). 환불은 RTDN 대신 `voidedpurchases` 를 조회합니다(RTDN 은 Pub/Sub 시설이 따로 필요).
- **애플**: 영수증은 StoreKit 2 거래 id. 서버 ES256 JWT 로 `GET /inApps/v1/transactions/{id}` → `signedTransactionInfo`(JWS)의 `x5c` 사슬을 설정의 애플 루트까지 확인(`INetSecurityProvider::verifyCertificateChain`)한 뒤 서명 확인. `revocationDate` 가 있으면 환불, Sandbox 면 샌드박스 결제. 환불은 `notification/history`(`REFUND`)를 주기 조회합니다.
- **함정**: 애플 응답 모양과 확장 OID 는 문서만 보고 썼고 실제 앱 스토어로 확인하지 못했습니다 — iOS 빌드가 생기면 `sandbox: true` 로 한 번 돌립니다(샌드박스 결제를 운영에 물으면 404 라 설정으로 서버를 가름). 스팀 · 구글 환불 조회는 지난 성공 시각보다 이틀 앞에서 한 번에 1000 건까지 읽고(넘으면 경고), 재시작 뒤 다시 받아도 환불 회수가 거래 id 로 한 번만 처리합니다. 스팀은 주문을 연 계정과 올린 계정이 같은지 모릅니다.

### B2

근거는 부하 봇 첫 측정입니다(`OperationsAndDeployment`). 거르개 비용은 `ChatService.send` p99 로 봅니다.

### F5

게임 포트는 게임 인스턴스가 설 때 열리고 인증기를 만드는 온라인 키트는 그 뒤에 서므로 자리 하나로 풀었습니다.

- `ServerConfig::_bGameConnectTicket` 을 켜면 `GameNetSession::makeServerParams` 가 게임 포트를 암호화하고 인증기 자리에 `GameConnectSecurity`(Engine/Net)를 끼웁니다. 암호 제공자(OpenSSL)가 없으면 게임 포트를 열지 않습니다.
- 계정 키트의 조립 `gameConnect`(옵션 `ticketKeySecretEnvironment` · `serverName` 필수)가 로그인 서버와 같은 키로 티켓 확인기를 세워 `AccountConnectAuthenticator` 를 자리에 묶습니다. 매칭 키트의 `gameServer` 옵션이 `MatchServerAgent` 를 세워 받아들이기 정책(`IConnectAdmission`)을 문맥의 키트 범위에 내놓고 없으면 티켓이 맞는 모든 계정을 받습니다.
- 묶이기 전에 온 접속은 거절하고 서비스가 선 뒤에도 안 묶였으면 `ServerApp` 이 기동을 멈춥니다(폴백 없음).
- 클라이언트: `GameNetSession::makeTicketClientParams( 주소, 티켓, 인자 )`(주소 `ServerDescriptor::pickPort`, 티켓 `AccountClient::makeConnectCredentials`). `-gv_netConnectAddress` 는 자격 없는 길이라 티켓 서버에는 `SecurityMismatch` 로 거절됩니다. 거절된 티켓은 지워 새 티켓 없이 `reconnect` 하지 않습니다.
- **남은 것**: 두 프로세스 시나리오(서버 `_bGameConnectTicket` + 로그인 서버, 클라이언트가 로그인해 티켓을 받아 붙는 흐름) — 로그인 서버와 게임 서버를 함께 띄우는 시나리오 시설이 필요합니다.

### I6

GM 전용 호스트 포트. 계약은 `OnlineServiceHost.h` 머리 주석과 `ServerConfig::_adminServicePort` 가 원본입니다.

**설계** — 호스트는 하나로 두고 리슨만 둘로 나눕니다. 버린 길: 호스트 둘(계정 바인딩 · 로그인 이벤트가 쪼개짐), 전송 셋에 다중 리슨(epoll 을 이 PC 에서 빌드 못 함), 한 포트에서 원격 주소로 거르기(포트 분리가 아님). 노출 면은 서비스의 형식입니다: `IOnlineHostService::getExposure()`(`Player` 기본 · `Admin` · `PlayerAndAdmin`). 리슨과 노출이 맞지 않는 요청은 "없는 메서드" 와 같은 `kInvalidRequest` 로 답합니다(영역이 있는지 숨김). `_adminServicePort` 0 이면 GM 리슨이 없고 GM 키트(`Admin` 노출 서비스)를 켠 서버는 기동하지 못합니다(플레이어 포트로 받는 폴백 없음, 2026-10-27 U9). `_adminListenAddress` 기본은 `127.0.0.1` 입니다.

- 확인: `OnlineServiceHostTest.AdminListenerSplitsServicesByExposure`(루프백). 실제 IOCP 두 리슨과 전용 서버 기동은 확인하지 않았습니다.
- GM 도구 접속(빌드 전): `AdminToolConnection` 이 `host:port` 하나로 겁니다(포트 없는 주소는 거절). 에디터 확장이 계정 키트를 include 할 수 없어(층 검사) 로그인은 부르는 쪽이 `getConnection().bindLoginHook` 에 `AccountGmLogin`(계정 키트)을 끼워야 하고, 없으면 `Ready` 에서 멈춰 서버가 `NotLoggedIn` 으로 거절합니다. 이 조립을 하는 도구 실행 파일은 아직 없고 에디터 안에서는 이름 · 비밀번호 UI 가 필요합니다(U1). 한 계정은 연결 하나에만 붙으므로 GM 계정을 따로 씁니다.

### U1

클라이언트 UI 위젯은 "모델 + 화면"입니다 — 모델이 클라이언트 응답을 받아 목록 · 상태 · 판을 들고 화면(`UIScreen` 파생)이 읽습니다. 문서 경로는 게임이 `open( ui, 경로, … )` 에 주며(코드에 기본 경로 없음) 견본은 `Resource/common/ui/online/` 입니다. 알림 · 메시지 버퍼를 꺼내는 모델은 `DrainGate` 표로 클라이언트당 하나만 붙습니다.

- **서버 고르기**(`ServerListModel` · `ServerPickerScreen`): `Open` 이 아닌 서버의 단추는 꺼지고 목록을 못 받으면 낡은 목록을 비우고 이유를 `Status` 에 적습니다. 테스트 `ServerListModelTest`.
- **친구 목록**(`FriendListModel` · `FriendListScreen`): 관계 목록 → 접속 상태 두 요청이 끝나야 `Loaded`, 한쪽이라도 실패하면 비우고 `Failed`. 접속 중인 친구가 앞. 테스트 `FriendListModelTest`.
- **채팅**(`ChatLogModel` · `ChatScreen`): 200 줄까지 쌓고 한 채널의 최근 15 줄을 보입니다. 보낸 사람도 채널 알림으로 자기 글을 받으므로 보내기 성공을 따로 더하지 않습니다. 채널 입장은 게임의 몫. 테스트 `ChatLogModelTest`.
- **시나리오 없음**: 화면을 띄울 장면과 서버 목록을 채우는 가짜 클라이언트가 필요하고, 서버 키트를 한 프로세스에 띄우는 시나리오 시설(F5 의 두 프로세스 시나리오와 같은 시설)이 먼저입니다.
- **남음**: 계정 이름 · 비밀번호를 받는 GM 도구 설정 화면. 친구 신청 수락 · 삭제 단추와 귓속말은 쓰는 게임이 생길 때.

### F2

다른 서버에서 깨진 파티 표는 낸 서버에 Cancelled 를 버스로 알리지만 권한 매처에서는 모드를 몰라 모든 모드의 빼기로 뺍니다. 팀 나누기와 채우기는 그리디라 나눌 수 없는 조합은 그 닻을 건너뜁니다. 작은 n 전수 탐색은 대기 시간 p99 를 보고 정합니다. 정산은 페이지 일(600 명씩)로 나뉘고 결과가 있는 사람을 건너뛰는 멱등으로 이어 갑니다.

### I4

보존 기간 쓸기는 키트가 아니라 서버 런타임이 예약 작업 `retention.sweep` 으로 매일 한 번 돌립니다(`ServiceRetentionJob`). 규칙은 `ServiceRetentionRule`(테이블 · 보존 기간 · 기록 시각 읽기 함수; 시각을 읽지 못하는 레코드는 지우지 않음)이고 기본 기간은 서버 온라인 설정(0 이면 안 지움)에서 감사 365 일 · 원장 분개와 내역 400 일 · 닫힌 신고 90 일입니다. 한 회차에 규칙마다 5000 개 훑고 500 개 지우며(커서, 읽은 버전을 조건으로 써 그사이 바뀐 레코드는 건너뜀) 우편은 `mail_expiry` 로, 멱등 기록은 저장소의 만료 정리가 맡습니다. **함정**: 분개 키는 클라이언트 멱등 키이기도 해서 보존 기간이 지나면 같은 키의 재시도를 가리지 못합니다 — 원장 기간을 재시도가 올 수 있는 기간보다 길게 둡니다.

### I7

`IBalanceNotice`(`Base/Online/Ledger/`)를 경제 키트가 구현해 서버 조립 문맥의 키트 범위(`_kits`)에 내놓습니다. 알림은 `EconomyPushName::BalanceChanged`(바뀐 자산의 잔액 절댓값 목록)이며 클라이언트가 캐시를 덮으므로 늦거나 중복돼도 같습니다. 이 서버에 붙어 있으면 바로, 다른 서버면 `OnlinePushRelay` 로 중계하고 접속하지 않은 계정에는 보내지 않습니다(다음 로그인이 지갑을 읽음). 부르는 곳은 GM 지급 · 회수(`AdjustAsset`, 재시도 응답 제외), 스토어 환불 회수, 우편 만료 반환입니다. 거래 정산은 거래 키트의 알림이 가므로 쓰지 않습니다.
