# 온라인 키트 마감 계획

> 상태: 구현중 · 선행: 없음 · 영역: network · 진행: 표

온라인 키트(계정 · 채팅 · 매칭 · 경제 · 라이브옵스 · GM 도구)의 남은 일을 단위로 모았다. 단위 대부분은 코드와 테스트가 들어갔고 빌드 확인 전이다. 남은 것은 실제 제공자 등록(쓰는 게임이 생길 때), 부하 봇 측정(B2 · M1), 두 프로세스 시나리오(F5)다.

## 1. 정한 것

- **외부 계정 · 비용 연동은 코드를 넣되 자격(키 · 인증서 · 서비스 계정)은 환경 변수에서만 읽는다.** 설정이 없으면 그 제공자를 만들지 않고, 만들다 실패하면 서버 기동이 실패한다(가짜로 대신 뜨지 않는다). 제품 이름은 `Provider/<제품>/` 안에만 쓰고 데이터로 정할 수 있는 것은 데이터로 한다. 확인은 가짜 HTTPS 서버로 하고 실제 계정 등록은 쓰는 게임이 생길 때다.
- **키트끼리는 include 하지 않는다.** 두 키트를 잇는 일은 기반 `Online/` 의 인터페이스를 서버 조립 문맥의 지역 범위(`OnlineServerKitContext::_kits`)에 걸고(`exposeInterface`) 다른 키트가 `start` 에서 `get`(필수) · `find`(선택)로 읽는다. 순서는 키트 이름 의존(`getRequiredKits` · `getOptionalKits`)이 정한다.
- **부하 봇 측정이 근거인 결정(채팅 채널 샤딩, 매칭 작은 n 전수 탐색)은 측정 전에는 구현하지 않는다.**
- A5: 스팀 티켓 확인은 데이터다(`ticketQuery` — 주소 · 인자 · 결과 경로 · 성공 값). 구글 · 애플 · 카카오 client id 는 제공자 설정 파일의 `oidc` 줄(게임마다). 모바일 SDK 클라이언트는 모바일 빌드가 생길 때.

## 2. 단위

| 단계 | 내용 | 상태 |
|---|---|---|
| A1 | `BlockingHTTPClient` | 끝남(빌드 미확인) |
| A2 | `SystemExternalBrowser` | 끝남(빌드 미확인) |
| A3 | `PlatformLoginProviderSet` | 끝남(빌드 미확인) |
| A4 | 애플 토큰 철회 | 끝남(Debug 빌드 · 테스트 통과) |
| A5 | 실제 제공자 등록, 스팀 티켓 확인, 모바일 SDK | 일부(티켓 확인 제공자 `TicketQueryLoginProvider` 는 코드 끝, 빌드 전. 등록 · 모바일 SDK 는 남음) |
| B1 | 채팅 신고 | 끝남(빌드 미확인) |
| B2 | 채팅 채널 샤딩 | 남음(`ChatService.send` p99 를 잰 뒤 정한다. M1 참고) |
| C1 | GM 서버 디렉터리 점검 | 끝남(빌드 미확인) |
| D1 | 길드 채널 | 끝남(빌드 미확인) |
| D2 | 길드 레벨 | 끝남(빌드 미확인) |
| E1 | 친구 순위표 | 끝남(빌드 미확인) |
| E2 | 업적 진행률 | 끝남(빌드 미확인) |
| F1 | 매칭 접속 인증기 연결 | 끝남(빌드 미확인) |
| F2 | 백필 | 끝남(빌드 미확인) |
| F3 | 재접속 유예 | 끝남(빌드 미확인) |
| F4 | 파티 채팅 | 끝남(빌드 미확인) |
| F5 | 게임 포트 접속 인증기(접속 티켓) | 끝남(서버 쪽 빌드 미확인, 두 프로세스 시나리오는 시설이 없어 남음) |
| G1 | A/B 실험 | 끝남(빌드 미확인) |
| G2 | 우편 푸시 | 끝남(빌드 미확인) |
| G3 | 이벤트 운영 화면 | 끝남(빌드 미확인) |
| G4 | 실제 푸시 제공자 둘(FCM · APNs) | 코드 끝남(빌드 전. 실제 계정 등록은 쓰는 게임이 생길 때) |
| G5 | 이벤트 시작 푸시 | 끝남(Debug 빌드 · 테스트 통과) |
| H1 | 환불 회수 | 끝남(빌드 미확인) |
| H2 | 청약철회 | 끝남(빌드 미확인) |
| H3 | 영수증 제공자 셋(스팀 · 구글 · 애플) | 끝남(Debug 빌드 · 테스트 통과, 애플은 가짜 서버로만) |
| I1 | 인스턴스 아이템 | 끝남(빌드 미확인) |
| I2 | 새 우편 알림 | 끝남(빌드 미확인) |
| I3 | 원장 보존 검사 | 끝남(빌드 미확인) |
| I4 | 보존 기간 정리 | 끝남(Debug 빌드 · 테스트 통과) |
| I5 | 오프라인 계정 이름 찾기 | 끝남(빌드 미확인) |
| I6 | GM 전용 호스트 포트 | 끝남(Debug 빌드 · 테스트 통과. GM 도구의 GM 포트 접속은 코드만) |
| I7 | 잔액 변경 알림 | 끝남(빌드 미확인) |
| U1 | 클라이언트 UI 위젯(채팅 · 서버 고르기 · 친구 목록)과 GM 도구 설정 화면 | 코드 끝남(빌드 전) |
| M1 | 측정: 친구 접속 상태 팬아웃(친구 200 명), `ChatService.send` p99 | 남음(시나리오는 부하 검증에 연결됨 — 묶음 `onlineMeasure`, 예산 없이 보고만. 돌려 숫자를 남기는 일만) |

첫 일: 컴파일 · 테스트 확인. 그다음 `py -3 -m Scripts load suite onlineMeasure` 로 M1 을 재고 B2 를 정한다.

## 3. 단위의 계약과 함정

### A4 — 애플 토큰 철회

탈퇴한 계정의 토큰 철회는 애플 심사 지침 5.1.1(v)의 요구다. 설정 종류 `appleSignIn`(`teamId` · `signingKeyId` · `keySecretEnvironment`). 모바일 클라이언트는 아직 없고 서버는 가짜 서버로 확인했다.

- **티켓** `ID 토큰|nonce|인가 코드`: 코드가 있으면 client secret JWT(ES256)로 `/auth/token` 에 교환해 refresh token 을 연동 비밀로 받는다. 교환이 실패해도 로그인은 성공한다(비밀만 없음).
- **저장**: `LoginSecretVault`(AES-256-GCM)로 봉인해 `login_account_secret` 에 쓴다. 키는 `linkSecretKeySecretEnvironment`이며 철회 제공자를 등록했는데 키가 없으면 기동 실패, 도구가 없으면 비밀을 버린다(평문 저장 안 함).
- **철회**: `purgeAccount` · `unlinkPlatform` 이 비밀을 같은 트랜잭션에서 `login_revoke_queue` 로 옮기고 서비스가 1 분마다 `POST /auth/revoke` 한다. 철회했거나 `invalid_grant` 면 지우고, 닿지 못했거나 설정에 없는 제공자의 항목은 남긴다. 서버가 죽어도 대기열이 이어진다.

### G4 · G5 — 푸시 제공자

FCM HTTP v1(`Provider/Fcm/FcmPushProvider`, 서비스 계정 OAuth 는 `ServiceAccountTokenSource`)과 APNs(`Provider/Apns/ApnsPushProvider`), 이벤트 시작 토픽 푸시.

- **자격**: 환경 변수에서만 읽는다(`PushProviderSet`, 옵션 `pushProvidersFile` · `pushTrustFile`, 설정 `{"providers":[{"kind":"fcm","serviceAccountEnvironment":…}]}`, `tokenUrl` · `sendUrl` 은 가짜 서버용). 변수가 틀리면 기동 실패, 설정 파일이 없으면 제공자를 만들지 않는다.
- **FCM 상태 대응**: 200 Delivered, 404 · `UNREGISTERED` InvalidToken, 429 RateLimited, 5xx · 전송 실패 Transient, 401 · 403 Rejected(액세스 토큰을 버려 다음 배달이 새로 받음), 그 밖 4xx Rejected. 문구는 현지화 키(기기가 풂)이고 없으면 데이터 메시지만 보낸다.
- **G5**: 이벤트 매개변수 `push.title` · `push.body` · `push.link` 가 있으면 방금 열린 창(10 분 안)을 `liveops.<지역>` 토픽(제한 없으면 `liveops.all`)으로 한 번 보낸다. 서버 여럿 중 한 곳만 보내도록 `liveops_push` 표를 "없어야 한다" 조건으로 쓴다. 표를 쓴 서버가 제공자 호출 전에 죽으면 그 알림은 나가지 않는다(중복보다 놓침을 택함).
- **APNs 는 HTTP/2 가 필수**라 자체 TLS 계층 위에 HTTP/2 클라이언트를 쌓았다(오픈 소스 라이브러리는 TLS 계층과 맞지 않아 기각). 다섯 조각 모두 빌드 전이다.
  - 프레임 · HPACK(허프만 포함, 표는 RFC 7541 에서 생성 스크립트가 읽어 씀): `HTTP2Frame` · `Hpack`.
  - TLS 계층 ALPN 협상: `TLSContextConfig::_alpnProtocol`, `ITLSSession::getNegotiatedProtocol()`.
  - `HTTP2Connection`(소켓을 모르는 상태 기계 — 서문 · SETTINGS · 스트림 · 흐름 제어 · PING · GOAWAY · RST_STREAM)과 `HTTP2Client`(호스트:포트마다 ALPN `h2` 연결 하나를 오래 열어 요청을 다중화), `OwnedHTTP2Client`. 막는 `IHTTPClient` 어댑터는 쓰는 곳이 푸시 제공자 하나라 만들지 않았다. 한계: 헤더 블록이 상대의 프레임 상한을 넘으면 실패(CONTINUATION 쓰기 없음), 연결 유지용 PING 없음.
  - `ApnsPushProvider`: 제공자 토큰은 `iss`(팀 id) · `iat` 의 JWT ES256 을 로컬에서 서명해 40 분마다 새로 서명한다(APNs 는 20 분보다 자주 거절, 60 분 넘기면 403). 요청은 `POST /3/device/<16진 토큰>` + `apns-topic` · `apns-push-type`(문구가 있으면 `alert`, 없으면 `background`). 상태: 200 Delivered, 410 InvalidToken, 429 RateLimited, 5xx Transient, 403 Rejected(토큰을 버려 다음 배달이 새로 서명), 그 밖 4xx Rejected(400 `BadDeviceToken` 은 환경이 어긋난 토큰일 수 있어 기기를 지우지 않는다). 16진이 아닌 토큰은 InvalidToken 이고 토픽 발송은 없다. 키(.p8)는 환경 변수에서만 읽고 ES256 서명이 안 되면 기동 실패.
- **꺼내기 표**: 소비형 큐를 여럿이 꺼내는 결함을 `DrainGate`(`DrainLease`)로 막았다(Online 클라이언트와 Strategy 키트의 `claim*`). 소유자가 하나인 나머지 `drain*` · `take*` 는 표가 없다. 남은 의심(추측): `CameraComponent::consumeCut` 은 소비자가 둘(`RenderViewCollector` · `CameraManagerComponent`)이라 타깃 카메라가 자기 뷰로도 그려지면 TAA 기록이 안 끊길 수 있다 — 읽기(`peekCut`)와 지우기를 나누는 후속.

### H3 — 영수증 제공자

스팀 · 구글 · 애플. 자격은 환경 변수에서만(`ReceiptProviderSet`, 옵션 `receiptProvidersFile` · `receiptTrustFile`), 없으면 기동 실패.

- **스팀**: 영수증은 주문 번호. `QueryTxn` 의 `Succeeded` 지급, 환불류 환불, `Init` · `Approved` 다시 시도, 그 밖 거절. 키 오류(401 · 403)는 다시 시도로 끝내고 오류를 남긴다. 환불은 `GetReport` 주기 조회. `sandbox` 면 `ISteamMicroTxnSandbox`.
- **구글**: 영수증은 `<상품 id>|<구매 토큰>`. `purchases.products.get` 의 `purchaseState` 0 지급 · 1 환불 · 2 다시 시도. 확인 상태가 0 이면 `:acknowledge` 를 한 번 보낸다(3 일 안에 안 하면 구글이 환불). 환불은 RTDN 대신 `voidedpurchases` 조회(RTDN 은 Pub/Sub 시설이 따로 필요).
- **애플**: 영수증은 StoreKit 2 거래 id. 서버 ES256 JWT 로 `GET /inApps/v1/transactions/{id}` → `signedTransactionInfo`(JWS)의 `x5c` 사슬을 애플 루트까지 확인한 뒤 서명 확인. `revocationDate` 가 있으면 환불, Sandbox 면 샌드박스 결제. 환불은 `notification/history`(`REFUND`) 주기 조회.
- **함정**: 애플 응답 모양과 확장 OID 는 문서만 보고 썼고 실제 앱 스토어로 확인하지 못했다. iOS 빌드가 생기면 `sandbox: true` 로 한 번 돌린다(샌드박스 결제를 운영에 물으면 404 라 설정으로 서버를 가름). 스팀 · 구글 환불 조회는 지난 성공 시각보다 이틀 앞에서 1000 건까지 읽고(넘으면 경고), 재시작 뒤 다시 받아도 환불 회수가 거래 id 로 한 번만 처리한다. 스팀은 주문을 연 계정과 올린 계정이 같은지 모른다.

### F5 — 게임 포트 접속 티켓

게임 포트는 게임 인스턴스가 설 때 열리고 인증기를 만드는 온라인 키트는 그 뒤에 서므로 자리 하나로 풀었다.

- `ServerConfig::_bGameConnectTicket` 을 켜면 `GameNetSession::makeServerParams` 가 게임 포트를 암호화하고 인증기 자리에 `GameConnectSecurity` 를 끼운다. 암호 제공자(OpenSSL)가 없으면 게임 포트를 열지 않는다.
- 계정 키트의 조립 `gameConnect`(옵션 `ticketKeySecretEnvironment` · `serverName` 필수)가 로그인 서버와 같은 키로 티켓 확인기를 세워 `AccountConnectAuthenticator` 를 자리에 묶는다. 매칭 키트의 `gameServer` 옵션이 `MatchServerAgent` 를 세워 받아들이기 정책(`IConnectAdmission`)을 내놓고, 없으면 티켓이 맞는 모든 계정을 받는다.
- 묶이기 전에 온 접속은 거절하고 서비스가 선 뒤에도 안 묶였으면 `ServerApp` 이 기동을 멈춘다(폴백 없음).
- 클라이언트: `GameNetSession::makeTicketClientParams( 주소, 티켓, 인자 )`. `-gv_netConnectAddress` 는 자격 없는 길이라 티켓 서버에는 `SecurityMismatch` 로 거절된다. 거절된 티켓은 지워 새 티켓 없이 `reconnect` 하지 않는다.
- **남은 것**: 두 프로세스 시나리오. 한 프로세스 안의 같은 흐름(로그인 → 티켓 → 암호화 UDP 접속, 위조 · 남의 티켓 거절)은 `AccountStreamTest` 가 이미 확인한다. 두 프로세스에는 (1) `Server` 실행 파일이 계정 키트 조립과 로그인 서비스 포트를 명령줄 · 설정으로 띄우는 길, (2) `App` 이 로그인 서버 주소 · 계정으로 로그인해 티켓을 받아 `makeTicketClientParams` 로 초기화하는 구동기, (3) `Scripts/common/NetPairRun.py` 에 두 프로세스를 띄워 `[Error]` 없이 접속이 열리는지 보는 드라이버가 없다.

### I6 — GM 전용 호스트 포트

계약은 `OnlineServiceHost.h` 머리 주석과 `ServerConfig::_adminServicePort` 가 원본이다.

- **설계**: 호스트는 하나로 두고 리슨만 둘로 나눈다. 기각: 호스트 둘(계정 바인딩 · 로그인 이벤트가 쪼개짐), 전송 셋에 다중 리슨(epoll 을 이 PC 에서 빌드 못 함), 한 포트에서 원격 주소로 거르기(포트 분리가 아님).
- 노출 면은 서비스의 형식이다: `IOnlineHostService::getExposure()`(`Player` 기본 · `Admin` · `PlayerAndAdmin`). 리슨과 노출이 맞지 않는 요청은 "없는 메서드" 와 같은 `kInvalidRequest` 로 답한다(영역이 있는지 숨김).
- `_adminServicePort` 0 이면 GM 리슨이 없고 GM 키트를 켠 서버는 기동하지 못한다(플레이어 포트로 받는 폴백 없음). `_adminListenAddress` 기본은 `127.0.0.1`.
- 확인: `OnlineServiceHostTest.AdminListenerSplitsServicesByExposure`(루프백). 실제 IOCP 두 리슨과 전용 서버 기동은 확인하지 않았다.
- GM 도구 접속(빌드 전): `AdminToolConnection` 이 `host:port` 하나로 건다. 에디터 확장이 계정 키트를 include 할 수 없어(층 검사) 로그인은 부르는 쪽이 `bindLoginHook` 에 `AccountGmLogin` 을 끼워야 하고, 없으면 서버가 `NotLoggedIn` 으로 거절한다. 이 조립을 하는 도구 실행 파일은 아직 없다. 한 계정은 연결 하나에만 붙으므로 GM 계정을 따로 쓴다.

### U1 — 클라이언트 UI 위젯

"모델 + 화면"이다. 모델이 클라이언트 응답을 받아 목록 · 상태 · 판을 들고 화면(`UIScreen` 파생)이 읽는다. 문서 경로는 게임이 `open( ui, 경로, … )` 에 주며(코드에 기본 경로 없음) 견본은 `Resource/common/ui/online/` 이다. 알림 · 메시지 버퍼를 꺼내는 모델은 `DrainGate` 로 클라이언트당 하나만 붙는다.

- **서버 고르기**(`ServerListModel` · `ServerPickerScreen`): `Open` 이 아닌 서버의 단추는 꺼지고 목록을 못 받으면 낡은 목록을 비우고 이유를 적는다.
- **친구 목록**(`FriendListModel` · `FriendListScreen`): 관계 목록 → 접속 상태 두 요청이 끝나야 `Loaded`, 한쪽이라도 실패하면 비우고 `Failed`. 접속 중인 친구가 앞.
- **채팅**(`ChatLogModel` · `ChatScreen`): 200 줄까지 쌓고 한 채널의 최근 15 줄을 보인다. 보낸 사람도 채널 알림으로 자기 글을 받으므로 보내기 성공을 따로 더하지 않는다. 채널 입장은 게임의 몫.
- **GM 도구 설정 화면**(빌드 전): `AdminToolPanel` 연결 줄의 계정 이름 · 비밀번호 칸. 입력을 로그인으로 잇는 일은 조립 쪽 몫이라 `AdminToolSession::bindCredentialsHandler` 와 `bindLoginHook` 을 함께 건다. 처리기가 있을 때만 칸을 보이고, `connectWithLogin` 이 빈 값과 처리기의 거절을 연결 전에 막으며 입력 칸의 비밀번호를 곧바로 지운다. 테스트 `admin.credentialsHandoff`.
- **시나리오 없음**: 화면을 띄울 장면과 가짜 클라이언트, 서버 키트를 한 프로세스에 띄우는 시나리오 시설(F5 의 두 프로세스와 같은 시설)이 먼저다.
- **남음**: 친구 신청 수락 · 삭제 단추와 귓속말은 쓰는 게임이 생길 때.

### F2 · I4 · I7

- **F2 매칭**: 다른 서버에서 깨진 파티 표는 낸 서버에 Cancelled 를 버스로 알리지만 권한 매처에서는 모드를 몰라 모든 모드의 빼기로 뺀다. 팀 나누기와 채우기는 그리디라 나눌 수 없는 조합은 그 닻을 건너뛴다. 작은 n 전수 탐색은 대기 시간 p99 를 보고 정한다. 정산은 페이지 일(600 명씩)로 나뉘고 결과가 있는 사람을 건너뛰는 멱등으로 이어 간다.
- **I4 보존 기간**: 키트가 아니라 서버 런타임이 예약 작업 `retention.sweep` 으로 매일 한 번 돌린다(`ServiceRetentionJob`). 규칙은 `ServiceRetentionRule`(테이블 · 보존 기간 · 기록 시각 읽기 함수, 시각을 못 읽는 레코드는 지우지 않음)이고 기본 기간은 감사 365 일 · 원장 분개와 내역 400 일 · 닫힌 신고 90 일(0 이면 안 지움)이다. 회차마다 규칙당 5000 개 훑고 500 개 지우며 읽은 버전을 조건으로 써 그사이 바뀐 레코드는 건너뛴다. 우편은 `mail_expiry`, 멱등 기록은 저장소의 만료 정리가 맡는다. **함정**: 분개 키는 클라이언트 멱등 키이기도 해서 보존 기간이 지나면 같은 키의 재시도를 가리지 못한다 — 원장 기간을 재시도가 올 수 있는 기간보다 길게 둔다.
- **I7 잔액 알림**: `IBalanceNotice` 를 경제 키트가 구현해 키트 범위에 내놓는다. 알림 `EconomyPushName::BalanceChanged`(바뀐 자산의 잔액 절댓값 목록)는 클라이언트가 캐시를 덮으므로 늦거나 중복돼도 같다. 이 서버에 붙어 있으면 바로, 다른 서버면 `OnlinePushRelay` 로 중계하고 접속하지 않은 계정에는 보내지 않는다(다음 로그인이 지갑을 읽음). 부르는 곳은 GM 지급 · 회수(재시도 응답 제외), 스토어 환불 회수, 우편 만료 반환이다. 거래 정산은 거래 키트의 알림이 간다.
