# 서버 버스 메시지 형식 계획 (2026-10-17)

온라인 서버끼리 주고받는 버스 메시지를 고정 배치형(B)에서 자기 기술형(A, 버전 머리 + `BinarySerializer`)으로 되돌리는 설계입니다.
백로그 `F-2` 의 결정을 구현하는 명세이며, 다음 세션 작업자가 이 문서만 읽고 단계 0 을 시작할 수 있게 씁니다.

**결정(사용자 확정, 2026-10-17)**: 서버 사이에는 핸드셰이크가 없고, 배포 중에는 옛 빌드와 새 빌드의 서버가 함께 돕니다. 그래서 버스 메시지는 혼합 빌드를 견디는 A 로 씁니다.
클라이언트와 서버 사이의 메시지는 핸드셰이크가 같은 빌드를 보장하므로 B 를 유지합니다([리플렉션 직렬화 형식 계획](ReflectionFormats.md) 5절, [결정 기록](../09_Decisions.md) 5-9).

근거는 실제로 읽은 코드만 `파일:줄` 로 적고, 읽지 않은 것은 "미확인"으로 적습니다. 줄 번호는 main `275666246` 기준입니다.
단계가 끝나면 그 단계를 지우고, 결정은 결정 기록으로 옮깁니다.

## 1. 지금 구조와 문제

### 1.1 버스의 구조

- 버스 API 는 `IServerBus` 입니다(`Source/GameFramework/Base/Online/Bus/ServerBus.h`). `publish( topic, pData, size )` 가 바이트를 그대로 받고, 배달은 최대 한 번입니다.
  헤더 주석은 "놓치면 안 되는 것은 영속 저장 + 다시 읽기, 버스는 빨리 알기 용"이라고 적고 있습니다. 메시지 크기 상한은 `kMaxMessageSize = 60 * 1024` 입니다.
- 서버 여럿을 잇는 구현은 `EphemeralServerBus` 입니다. 캐시 채널 `bus:<주제>` 위에 `[봉투 버전 1][서버 id 8][순번 8][본문]` 봉투를 붙입니다(`EphemeralServerBus.cpp:19-20`).
  봉투 버전이 다르면 받는 쪽은 `_droppedCount` 만 늘리고 버립니다(`EphemeralServerBus.cpp:141-147`). 봉투는 B 가 아닌 고정된 작은 형식이므로 이 계획에서 바꾸지 않습니다.
- 서비스는 `pollMessages` 를 직접 부르지 않습니다. 호스트가 비우고 `subscribeServerBus` 한 서비스의 `onServerBusMessage` 로 나눠 줍니다(`OnlineServiceHost.cpp:473`).

### 1.2 무엇이 잘못되는가

본문은 대부분 키트의 `XxxProtocol::MessageTypes::write` 로 쓴 B 입니다. B 는 머리에 타입 스키마 해시 64 비트를 싣고, 읽는 쪽 해시와 다르면 `SchemaMismatch` 로 거절합니다(`OnlineProtocolRegistry.h:18-19`).
배포 중에 메시지 타입의 필드 하나라도 바뀐 빌드가 섞이면, 받는 서버는 그 주제의 메시지를 모두 버립니다. 로그를 남기는 곳은 계정 해지 알림 하나뿐입니다(`AccountServer.cpp:270-274`). 나머지는 조용히 `return` 합니다.

버리는 메시지의 결과는 주제마다 다릅니다.

- 계정 해지 알림을 놓치면, 해지한 계정의 세션이 다른 빌드의 서버에 그대로 남습니다. 보안에 영향이 있습니다.
- 매치메이킹 티켓을 놓치면 티켓이 시한으로 끝날 때까지 매칭되지 않습니다.
- 채팅을 놓치면 다른 빌드 서버에 접속한 사람에게 말이 보이지 않습니다.

남은 손 코덱도 같은 문제를 가집니다. 제재 버스는 varuint 계정 id 하나를 손으로 씁니다(`AdminService.cpp:229-231`, `ChatService.cpp:614-619`).
접속 상태 전달(push 중계) 봉투는 `[varuint 계정][varuint 종류][blob 본문]` 을 손으로 쓰고, 안쪽 본문은 보낸 서버 빌드의 B 입니다(`OnlinePresence.cpp:359-363`, `:148-163`). 3.5 에서 따로 다룹니다.

**뿌리**: 버스 본문의 형식을 "같은 빌드끼리만 읽는다"는 B 의 전제 위에 두었는데, 버스에는 그 전제를 보장하는 핸드셰이크가 없습니다. 그리고 주제와 본문 타입의 짝이 선언 하나에 묶여 있지 않아서, 서비스마다 바이트를 손으로 쓰거나 B 로 쓰는 방식이 갈립니다.

## 2. 버스 메시지 전수 목록

`Source/GameFramework` 에서 `publish(` 와 `onServerBusMessage` · `handleBusMessage` 를 찾아 만든 목록입니다.
빈도는 측정값이 아니라 코드에서 발행하는 조건을 보고 추정한 것입니다. 낮음은 운영자 조작이나 드문 사건, 중간은 플레이어 행동 하나에 한 번, 높음은 플레이어가 반복하는 행동마다입니다.

| 주제 | 본문 타입 | 지금 형식 | 송신 위치 | 수신 위치 | 빈도 |
|---|---|---|---|---|---|
| `account.revoke` | `AccountRevokeNotice` | B | `AccountServer.cpp:459-462` | `AccountServer.cpp:258-279` | 낮음(해지 때) |
| `push.<서버 id>` | 손 봉투 + 안쪽 B | 손 | `OnlinePresence.cpp:359-363` | `OnlinePresence.cpp:148-163` | 중간 |
| `chat.channel.<채널>` | `ChatMessage` | B | `ChatService.cpp:581-588` | `ChatService.cpp:611-633` | 높음 |
| 서버 주제(`ChatProtocol::makeServerTopic`, 귓속말) | `ChatMessage` | B | `ChatService.cpp:576` | `ChatService.cpp:624-630` | 중간 |
| `sanction.changed` | varuint 계정 id | 손 | `AdminService.cpp:227-232` | `ChatService.cpp:614-619` | 낮음 |
| `social.links` | `SocialLinkChanged` | B | `SocialService.cpp:389-394` | `SocialService.cpp:522-529` | 낮음 |
| `social.presence` | `SocialPresence` | B | `SocialService.cpp:571-581` | `SocialService.cpp:531-535` | 중간(접속 · 상태 바뀜) |
| `mm.queue.<모드>` | `MatchTicket` | B | `MatchQueueService.cpp:341-342` | `MatchQueueService.cpp:538-546` | 중간 |
| `mm.cancel.<모드>` | `MatchTicketCancel` | B | `MatchQueueService.cpp:529` | `MatchQueueService.cpp:547-555` | 중간 |
| 결과 주제(`makeResultTopic( 서버 )`) | `MatchAssignment` | B | `MatchQueueService.cpp:497-498` | `MatchQueueService.cpp:556-561` | 중간 |
| `mm.assign.<서버 id>` | `MatchFormed` | B | `MatchQueueService.cpp:415-416`, `:467-468` | `MatchServerAgent.cpp:31-33` | 낮음에서 중간 |
| `liveops.changed` | 없음 | 빈 본문 | `LiveOpsService.cpp:372` | `LiveOpsServer.cpp:136-144` | 낮음 |
| `sd.changed` | 없음 | 빈 본문 | `ServerDirectoryService.cpp:520` | `ServerDirectoryServer.cpp:119` | 낮음 |
| `config.changed` | 키 문자열 바이트 | 손 | `RemoteConfig.cpp:371` | 없음(`Source` 에서 구독하는 곳을 찾지 못함) | 낮음 |

push 중계를 부르는 곳은 네 곳입니다. `LeaderboardServer.cpp:143`, `MatchmakingServer.cpp:331`, `SocialServer.cpp:262`, `TradeServer.cpp:224` 가 `OnlinePresence::sendRemotePush` 에 B 로 쓴 `BitWriter` 를 넘깁니다.

본문 타입 가운데 클라이언트 메시지와 같은 타입을 쓰는 것이 있습니다. `ChatMessage` 는 알림과 기록 레코드도 같이 씁니다(`ChatProtocol.h:8-9`). `SocialPresence` 는 접속 상태 캐시 레코드(`SocialProtocol.cpp:19-24`)와 응답 목록(`SocialProtocol.h:132`)이 같이 씁니다.
`AccountRevokeNotice`, `SocialLinkChanged`, `MatchTicket`, `MatchTicketCancel` 는 버스에만 쓰는 것으로 보입니다. `MatchAssignment` 와 `MatchFormed` 를 클라이언트에도 보내는지는 미확인입니다(단계 0).

## 3. 설계

### 3.1 주제가 본문 타입을 가진다

바이트를 받는 `publish( topic, pData, size )` 를 키트가 부를 수 없게 하고, 주제 정의 하나가 이름, 본문 타입, 의미 버전을 함께 가지게 합니다.
`OnlineMessageTypes` 가 목록에 없는 타입을 컴파일에서 막듯이, 주제와 맞지 않는 타입을 쓰거나 읽는 코드를 형식이 막습니다.

- `ServerBusTopic<T>` 는 고정 이름 주제입니다(`account.revoke` 같은 것). 이름과 의미 버전(3.4)을 가집니다.
- `ServerBusTopicFamily<T>` 는 접미사가 붙는 주제입니다(`chat.channel.<채널>`, `mm.queue.<모드>`, `push.<서버 id>`). 접두사와 의미 버전을 가지고, 접미사를 받아 주제 이름을 만듭니다.
  받은 메시지의 주제가 이 접두사로 시작하는지 확인하는 함수도 같은 정의에 둡니다. 지금 `MatchQueueServiceInternal::hasPrefix`(`MatchQueueService.cpp:26`)처럼 서비스마다 손으로 쓰는 비교가 없어집니다.
- 쓰기는 `topic.publish( IServerBus&, const T& )`, 읽기는 `topic.read( const ServerBusMessage&, T& )` 하나입니다. 읽기는 `[[nodiscard]]` 결과 열거형을 돌려줍니다(3.3).
- 본문이 없는 신호 주제(`liveops.changed`, `sd.changed`)도 같은 구조로, `PROPERTY` 가 없는 `REFLECT` 타입 하나를 본문으로 씁니다. A 로 8 바이트(버전 머리와 스트림 머리)가 늘지만 경로가 하나로 줄어듭니다.
- 제재 버스의 손 varuint 본문은 `REFLECT` 타입 하나(예: `ServiceSanctionChanged`, 필드는 계정 id 하나)로 바꿉니다. 정의는 지금 주제 이름이 있는 `ServiceSanction.h:69-75` 옆에 둡니다.
- 원시 `publish` 는 `IServerBus` 에 남지만 키트가 부를 수 없게 접근을 막습니다. 주제 정의 쪽 코덱만 부르게 하는 방법(보호 멤버와 friend, 또는 코덱 전용 토큰 인자)은 단계 2 작업자가 정합니다.

### 3.2 와이어 형태

봉투는 그대로 두고, 본문만 `BinarySerializer::serializeVersioned` 의 출력으로 바꿉니다.

```
[봉투 버전 u8 = 1][보낸 서버 id u64][순번 u64]            EphemeralServerBus, 바꾸지 않음
[의미 버전 u32]                                          serializeVersioned 의 버전 머리
[A 스트림 머리 u32: 위 8 비트 인코딩 버전, 아래 24 비트 프로퍼티 수]
[프로퍼티마다: 이름 해시 u32][기록 타입 해시 u32][크기 u32][값]
```

A 스트림의 프로퍼티 머리 구성은 `BinarySerializer.h` 5절 주석("프로퍼티마다 12바이트 태그 헤더")과 [리플렉션 직렬화 형식 계획](ReflectionFormats.md) 1.1 을 따랐습니다.
A 스트림 머리에는 타입 FQN 이 없습니다. 필드는 이름 해시로 짝짓고, 필드 타입은 정규화한 타입 이름의 해시로 확인합니다.

B 스키마 해시는 버스 본문에 싣지 않습니다. 버스의 호환 판단은 필드 이름 짝짓기와 의미 버전이 맡습니다.

### 3.3 읽기 정책

새 빌드가 보낸 필드를 옛 빌드가 무시하려면 "모르는 필드(orphan)를 버리는" 정책을 켜야 합니다. 지금 있는 장치와 그대로 쓸 때의 문제는 아래와 같습니다.

- `SerializeContext::setAllowUnknownProperties( true )` 를 켜면 버전 붙은 바이너리 읽기의 orphan 정책이 `SchemaOrphanPolicy::Ignore` 가 됩니다(`BinarySerializer.cpp:589`, `SerializeContext.h:100-106`). 버전이 다르면 켜도 거절합니다.
- 버전 붙은 읽기는 소프트 읽기 위에서 돕니다(`BinarySerializer.cpp:590-598`). 소프트 읽기는 **타입에 없는 필드**와 **타입에 있지만 값을 읽지 못한 필드**를 똑같이 orphan 으로 모읍니다(`BinarySerializer.cpp:139-140` 주석).
  그래서 `Ignore` 를 그대로 켜면, 옛 빌드가 모르는 열거자 값(`readEnumBinary` 가 false, `SerializerUtil.cpp:254-255`)이나 바뀐 필드 타입을 만나도 그 필드를 버리고 기본값으로 성공합니다.
  `AccountRevokeNotice::_reason` 에 새 열거자가 오면 옛 빌드는 `LoginRevokeReason::None` 으로 읽습니다. 폴백 금지 원칙과 맞지 않습니다.
- 버려진 orphan 의 경고(`SchemaMigrate.cpp:289-323`)는 읽을 때마다 한 줄입니다. 파일 로드에는 맞지만, 배포 중의 채팅 중계에서는 메시지마다 경고가 나옵니다.

그래서 버스 읽기에는 정책을 하나 더 둡니다.

1. **의미 버전이 다르면 거절합니다.** 등록된 이관(`SW_SCHEMA_MIGRATION`)이 있으면 그것을 씁니다. 지금 `runSchemaMigrateStep`(`SchemaMigrate.cpp:683-718`) 동작 그대로입니다.
   거절은 (타입, 받은 버전)마다 한 번 오류 로그를 남기고, 버린 수를 카운터로 셉니다.
2. **타입에 없는 이름의 필드는 버립니다.** (타입, 필드 이름 해시)마다 한 번만 경고합니다.
3. **타입에 있는 필드인데 값을 읽지 못하면 거절합니다.** 모르는 열거자, 변환할 수 없는 타입 변경이 해당합니다. 1 과 같이 로그와 카운터를 남깁니다.
4. **스트림에 없는 필드는 타입의 기본값입니다.** A 의 지금 동작입니다(`BinarySerializer.cpp:271-283`, 계획 1.1). 이 기본값이 뜻하는 바는 3.6 의 규칙이 정합니다.
5. **읽은 값이 선언된 범위(`Min` · `Max` · `MaxCount`)를 지키는지 확인합니다.** A 는 `MaxCount` 를 보지 않으므로 `ServiceRecordUtil::read` 처럼 B 쓰기 검사로 다시 봅니다(`ServiceRecordUtil.h:67-74`).
   이 검사는 레코드 읽기와 버스 읽기가 같은 함수 하나를 쓰게 뽑아냅니다.

2 와 3 을 가르는 장치는 이미 있습니다. `warnDroppedOrphans` 가 쓰는 `isOrphanNameKnownTo`(`SchemaMigrate.cpp:297-298`)가 orphan 의 이름이 타입에 있는지 봅니다.
그래서 `SchemaOrphanPolicy` 에 "모르는 이름만 버리고, 아는 이름의 orphan 은 거절" 값을 하나 더하고, `SerializeContext` 가 그 값을 고르게 하는 것이 가장 작은 변경입니다.
경고를 한 번만 내는 것도 같은 컨텍스트 옵션으로 고릅니다. 파일 로드의 경고 빈도는 바꾸지 않습니다.

**이 정책의 위험.** 모르는 필드를 버리면, 새 필드가 다른 필드의 뜻을 바꾸는 변경(예: "이 필드가 있으면 `_x` 는 초 단위가 아니라 밀리초")을 옛 빌드가 알아채지 못합니다.
이 경우는 3.4 의 의미 버전을 높여야 하고, 형식으로는 막지 못합니다. B 에서 `kBaseVersion` 이 맡는 틈과 같은 종류입니다(결정 기록 5-9).
버스는 서버끼리만 쓰므로 악의적인 입력은 고려하지 않습니다. 깨진 바이트는 A 의 크기 확인이 거절합니다.

### 3.4 해시 대신 의미 버전

B 는 배치가 바뀌면 해시가 바뀌어 거절합니다. 혼합 빌드를 견디려면 필드를 더하거나 빼는 일로는 거절하지 않아야 하므로, 버스는 스키마 해시를 쓸 수 없습니다.
대신 `serializeVersioned` 의 버전 머리를 **의미 버전**으로 씁니다. 필드 이름 짝짓기로 견딜 수 없는 변경(3.6 의 "버전을 높임" 줄)에서만 높입니다.

이 버전은 손 상수입니다. [리플렉션 직렬화 형식 계획](ReflectionFormats.md) 1.4 는 "손 버전 상수"를 결함으로 꼽았지만, 그것은 배치가 바뀔 때 높이는 것을 잊으면 잘못 읽는 B 의 경우입니다.
A 는 배치 변경을 이름으로 견디므로, 손 버전이 맡는 것은 해시가 볼 수 없는 의미 변경뿐입니다. B 의 `kBaseVersion` 과 같은 역할입니다.

의미 버전은 **타입마다 하나**입니다. 주제마다 두지 않습니다.

- 이관 레지스트리는 (타입, 기록된 버전)으로 찾습니다(결정 기록 5-4). `ChatMessage` 는 기록 레코드가 `kChatRecordVersion = 1`(`ChatProtocol.cpp:12`)로 쓰고, `SocialPresence` 는 캐시 레코드가 `kSocialPresenceRecordVersion` 으로 씁니다.
  버스가 다른 버전 수열을 쓰면 같은 (타입, 버전) 키가 레코드의 이관과 버스의 이관을 함께 가리킵니다.
- 그래서 버스 주제 정의는 그 타입의 레코드 버전 상수를 그대로 받습니다. 레코드가 없는 타입은 주제 정의 옆에 상수 하나를 둡니다.
  상수를 타입 선언 옆 한 곳으로 모으는 구조(예: `REFLECT` 키)는 생성기 변경이라 이 계획에서 하지 않습니다(열린 질문 6).

A 의 인코딩 버전(스트림 머리 위 8 비트)은 이 버전과 다릅니다. `BinarySerializer` 는 현재 인코딩 버전 하나만 읽으므로(계획 1.1), 인코딩 규칙을 바꾸면 혼합 빌드에서 모든 버스 메시지와 캐시 레코드를 읽지 못합니다. 8절 위험 4 에 적습니다.

### 3.5 메시지 종류 번호

버스에는 메서드 번호가 없습니다. 주제 이름이 메시지 종류이고, 3.1 의 주제 정의가 그 이름과 본문 타입을 묶습니다. 주제 이름은 문자열이므로 빌드 사이에 번호가 어긋날 일이 없습니다.

예외는 push 중계입니다. 봉투의 `kind` 는 보낸 서버 빌드의 클라이언트 알림 번호이고(메서드 테이블의 열거자 순서, 결정 기록 5-9), 안쪽 본문은 보낸 서버 빌드의 B 입니다.
받는 서버는 그 바이트를 그대로 자기 클라이언트에 보냅니다(`OnlinePresence.cpp:158-161`). 클라이언트는 받는 서버와 같은 빌드이므로, 보낸 서버의 빌드가 다르면 두 가지가 깨집니다.

- 알림 번호가 다른 알림을 가리킬 수 있습니다. 메서드 테이블에 알림을 하나 더하면 뒤 번호가 밀립니다.
- 본문 해시가 달라 클라이언트가 `SchemaMismatch` 로 버립니다.

push 중계는 아래처럼 바꿉니다.

- 봉투를 `REFLECT` 타입 하나(예: `OnlinePushRelay`)의 A 로 씁니다. 필드는 계정 id, 알림 열거형 FQN, 알림 열거자 이름, 본문 타입 FQN, 본문의 A 바이트입니다.
- 받는 서버는 열거자 이름으로 **자기 빌드의** 알림 번호를 찾고, 타입 FQN 으로 `TypeRegistry` 에서 타입을 찾아 본문 A 를 임시 인스턴스(`createScratchInstance`)에 읽은 뒤, 자기 빌드의 B 로 다시 써서 클라이언트에 보냅니다.
  그러면 클라이언트는 언제나 접속한 서버 빌드의 B 를 받으므로 5-a 의 보장이 그대로 맞습니다.
- 이름이나 타입을 찾지 못하면 오류 로그와 카운터를 남기고 버립니다.
- `sendRemotePush` 는 `BitWriter` 대신 타입 있는 본문을 받는 템플릿이 됩니다. 부르는 곳 네 곳(2절)이 바뀝니다.
- `TypeInfo` 로 B 를 쓰는 함수가 필요합니다. `FixedLayout::findCodec( const TypeInfo&, string& )` 는 공개이지만 `writeWithCodec` 는 private 입니다(`FixedLayout.h:339`, `:357`, `:370`). 공개 진입점 하나를 더합니다.

### 3.6 필드 변경의 호환 규칙

"옛 → 새"는 옛 빌드가 보낸 메시지를 새 빌드가 읽는 경우이고, "새 → 옛"은 그 반대입니다. 배포 중에는 두 방향이 함께 일어납니다.

| 변경 | 옛 → 새 | 새 → 옛 | 규칙 |
|---|---|---|---|
| 필드 추가 | 새 필드는 기본값 | 새 필드를 버림(경고 한 번) | 허용합니다. 단, 기본값이 "값 없음"을 뜻해야 합니다. 받는 쪽이 기본값을 처리할 수 없으면 의미 버전을 높입니다 |
| 필드 삭제 | 지운 필드를 버림 | 옛 빌드는 그 필드를 기본값으로 받음 | 옛 빌드가 기본값을 받아도 되는 필드만 한 배포로 지웁니다. 아니면 두 배포로 나눕니다. 먼저 읽지 않게 배포하고, 다음 배포에서 지웁니다 |
| 필드 이름 변경 | `PROPERTY( Alias )` 로 읽음 | 옛 빌드는 새 이름을 모름. 버림 + 기본값 | 한 배포로 하지 않습니다. 새 이름 필드를 더해 둘 다 쓰고, 다음 배포에서 옛 필드를 지웁니다. 또는 의미 버전을 높입니다 |
| 필드 타입 변경 | 변환(`tryCoerceBinaryPayload`)이 되면 읽음, 아니면 거절 | 같음 | 하지 않습니다. 새 이름의 필드를 더하는 방식으로 바꿉니다 |
| 열거자 추가 | 문제없음 | 옛 빌드는 모르는 열거자를 거절(3.3 의 3) | 새 빌드가 새 값을 보내면 섞인 동안 그 메시지는 버려집니다. 버려도 되는 주제가 아니면 두 배포로 나눕니다 |
| 열거자 이름 변경 | `ENUM( ValueAlias )` 로 읽음 | 옛 빌드는 거절 | 이름 변경과 같이 두 배포로 나눕니다 |
| `MaxCount` · 범위 넓힘 | 문제없음 | 옛 빌드는 범위 밖 값을 거절(3.3 의 5) | 열거자 추가와 같습니다 |
| `MaxCount` · 범위 좁힘 | 옛 값이 범위 밖이면 거절 | 문제없음 | 위와 같습니다 |
| 중첩 타입의 FQN 변경 | 기록 타입 해시가 달라 거절. `REFLECT( Alias )` 가 있으면 읽음 | 옛 빌드는 거절 | 하지 않습니다 |
| 뜻만 바뀜(단위 같은 것) | 알아채지 못함 | 알아채지 못함 | 의미 버전을 높입니다. 섞인 동안 거절되고, 새 빌드는 이관을 등록하면 옛 버전을 읽을 수 있습니다 |

기본값 규칙(첫 줄)은 폴백 금지 원칙과 부딪힐 수 있습니다. "필드가 없다"를 받는 쪽이 오류로 다뤄야 하는 필드라면 기본값으로 계속하면 안 됩니다.
지금 A 는 스트림에 필드가 없었는지를 호출부에 알리지 않습니다. 필드에 "없으면 거절" 표시를 두는 구조는 열린 질문 1 입니다.

### 3.7 클라이언트 핸드셰이크 해시(단계 5-a)와의 관계

- 버스에만 쓰는 타입(`AccountRevokeNotice`, `SocialLinkChanged`, `MatchTicket`, `MatchTicketCancel`, 단계 0 에서 확정)은 키트의 `MessageTypes` 목록에서 뺍니다.
  그러면 이 타입들의 스키마 해시가 온라인 스키마 해시에서 빠지고, 서버끼리만 쓰는 필드를 바꿔도 클라이언트에 업데이트를 요구하지 않습니다.
  `OnlineMessageTypes` 의 템플릿 인자 주석(`OnlineProtocolRegistry.h:44`)이 "서버 버스 본문"을 적고 있으므로 같이 고칩니다.
- 클라이언트에도 보내는 타입(`ChatMessage`, `SocialPresence` 와 단계 0 에서 확인할 것)은 목록에 남습니다. 클라이언트 경로는 B 와 해시, 버스 경로는 A 와 의미 버전으로 같은 선언에서 따로 만들어집니다.
- 목록에서 타입을 빼면 온라인 스키마 해시가 한 번 바뀝니다. 이 변경을 배포하면 옛 클라이언트는 한 번 "업데이트 필요"를 받습니다. 의도한 결과입니다.
- 주제 목록은 핸드셰이크 해시에 넣지 않습니다. 클라이언트는 주제를 모르기 때문입니다.
- push 중계를 3.5 처럼 바꾸면 클라이언트가 받는 B 는 언제나 접속한 서버가 쓴 것이므로, 5-a 의 "클라이언트와 그 서버는 같은 빌드" 보장 안에서 닫힙니다.

### 3.8 성능 영향

측정한 값은 없습니다. 아래는 구조로 따진 것이고, 판단은 단계 5 의 측정 뒤에 합니다(6절).

- **크기**: B 는 필드를 태그 없이 비트로 붙이지만, A 는 프로퍼티마다 12 바이트 머리를 붙이고 버전 머리와 스트림 머리로 8 바이트가 늘어납니다. 열거형은 열거자 이름 해시와 값을 같이 씁니다(`SerializerUtil.cpp:157-163`).
  구조체 목록(`MatchFormed::_listTicket` 같은 것)은 원소마다 그 원소의 프로퍼티 머리가 붙습니다. 가장 큰 메시지가 `kMaxMessageSize`(60 KiB)를 넘지 않는지 테스트로 확인합니다(6절).
- **CPU**: A 읽기는 프로퍼티마다 타입의 프로퍼티 목록을 선형으로 훑어 이름 해시로 대상을 찾고, 값마다 타입 이름으로 핸들러를 찾습니다(계획 1.1, `SerializerUtil.cpp:596-616`). 읽은 뒤 범위 검사로 B 쓰기를 한 번 더 합니다.
- **채팅 중계**: 비용은 메시지 하나에 보내는 서버의 쓰기 한 번과, 그 채널을 구독한 **서버마다** 읽기 한 번입니다. 받은 서버 안의 수신자 팬아웃은 이미 읽은 객체를 쓰므로 수신자 수에 비례하지 않습니다(`ChatService.cpp:590-601`).
  그래서 비용은 "메시지 수 × 구독 서버 수"에 비례하고 플레이어 수에는 직접 비례하지 않습니다.
- 측정 결과가 문제가 되면, 고칠 곳은 A 읽기 경로(타입마다의 이름 해시 조회 캐시 같은 것)이고 형식을 B 로 돌리지 않습니다. 이 최적화는 측정 없이 하지 않습니다.

### 3.9 고려했지만 고르지 않은 안

- **B 유지 + 주제 이름에 빌드 해시 붙이기**(`chat.channel.<채널>@<해시>`): 같은 빌드끼리만 메시지가 가므로 조용히 잘못 읽지는 않습니다. 하지만 배포 중에 빌드가 다른 서버의 플레이어끼리 채팅 · 매칭 · 해지 알림이 끊깁니다. 해지 알림이 끊기는 것은 받아들일 수 없습니다.
- **B 유지 + "클러스터는 한 빌드로만 돈다" 정책**: 무중단 순차 배포를 막습니다. 사용자 결정으로 고르지 않았습니다.
- **버스 구독 때 빌드 해시를 확인하고 다르면 멈춤**: 섞인 빌드를 오류로 알리지만 견디지는 못합니다. 순차 배포 중 매번 멈춥니다.

## 4. 같은 결함이 다른 곳에 있는가

- **캐시 레코드(`readCache`)**: 파티 · 로비 · 접속 상태 캐시는 "배포 중에 다른 빌드의 서버가 읽기 때문에" A 입니다(결정 기록 5-9). 그런데 `ServiceRecordUtil::read` 는 orphan 을 거절합니다(`ServiceRecordUtil.h:5-7`, `:65`).
  새 빌드가 필드를 더한 캐시 값을 옛 빌드가 읽으면 없는 것으로 보고, 파티 · 로비가 배포 중에 사라진 것처럼 보일 수 있습니다. 버스와 뿌리가 같습니다(열린 질문 5).
- **영속 레코드(`readDurable`)**: 같은 거절이 "손상" 오류가 되어 요청을 실패로 끝냅니다. 레코드는 "필드를 빼거나 뜻을 바꿀 때 버전을 높이고 데이터를 다시 쓴다"는 정책이 있지만, 필드 추가도 배포 중의 옛 빌드에는 읽기 실패입니다. 범위 밖이지만 열린 질문 5 에 함께 둡니다.
- **봉투 버전**: `EphemeralServerBus` 의 `kEnvelopeVersion` 을 바꾸면 섞인 동안 모든 버스 메시지가 버려집니다. 지금은 `_droppedCount` 만 셉니다. 바꾸지 않는 고정 형식으로 두고 헤더 주석에 그렇게 적습니다.

## 5. 구현 단계

빌드는 동시에 둘까지입니다([작업 방식](../11_Workflow.md)). 단계 3 의 키트 작업만 키트별로 나눠 병렬로 할 수 있습니다.

| 단계 | 내용 | 파일 | 담당(모델, effort) |
|---|---|---|---|
| 0 | 클라이언트에도 보내는 버스 타입 확정(`MatchAssignment`, `MatchFormed`), 결과 주제 · 서버 주제 이름, `config.changed` 구독처 유무, 버스 바이트를 직접 만드는 테스트 목록 | 읽기만 | `scout`(haiku, low) |
| 1 | A 읽기 정책 하나 더하기(3.3 의 2 · 3), 한 번만 경고 옵션 | `Engine/Serialization/Base/SchemaMigrate.h` · `.cpp`, `SerializeContext.h` · `.cpp`, `Format/BinarySerializer.cpp`, `ReflectionSerializationTest` | `worker`(opus, medium) |
| 2 | 주제 정의(`ServerBusTopic`, `ServerBusTopicFamily`)와 버스 코덱, 원시 `publish` 접근 막기, 범위 검사 함수 뽑기, 제재 · 원격 설정 주제 | `Base/Online/Bus/ServerBus.h` · `.cpp`, 새 `ServerBusTopic.h`, `Store/ServiceRecordUtil.h`, `Sanction/ServiceSanction.h`, `Config/RemoteConfig.cpp`, `Test/.../Online/TestServerBus.cpp` | `worker`(opus, medium) |
| 3 | 키트 옮기기, 버스 전용 타입을 `MessageTypes` 에서 빼기 | Account(`AccountServer.cpp`, `AccountProtocol.h`), Chat(`ChatService.cpp`, `ChatProtocol.h`), Social(`SocialService.cpp`, `SocialProtocol.h`), Matchmaking(`MatchQueueService.cpp`, `MatchServerAgent.cpp`, `MatchmakingProtocol.h`), Admin(`AdminService.cpp`), LiveOps · ServerDirectory 신호 주제 | 키트마다 `worker`(sonnet, medium). 패턴이 정해진 뒤의 기계적 변경입니다 |
| 4 | push 중계(3.5), `FixedLayout` 의 `TypeInfo` 쓰기 진입점 | `Kits/.../Account/Server/Service/OnlinePresence.h` · `.cpp`, `Engine/Serialization/Fixed/FixedLayout.h` · `.cpp`, 부르는 곳 `LeaderboardServer.cpp`, `MatchmakingServer.cpp`, `SocialServer.cpp`, `TradeServer.cpp` | `worker`(opus, medium) |
| 5 | 혼합 빌드 테스트, 크기 테스트, 측정(6절) | `Test/EngineTest/GameFramework/Online/` 새 파일, 키트 테스트 | `worker`(sonnet, medium) |
| 6 | 문서: 결정 기록 5-9 의 열린 질문을 결정으로 바꾸기, 계획 2절 테이블에 "서버 버스 = A" 줄 더하기, `ChatProtocol.h` · `OnlineProtocolRegistry.h` · `ServerBus.h` 헤더 주석, 백로그 `F-2` 지우기 | 문서와 주석 | `mech-worker`(sonnet, low) |

병합 전에 `code-reviewer` 가 묶은 diff 를 검토합니다. 단계 2 · 3 · 4 를 한 배치로 통합한다면 `integrator`(opus, high)가 맡습니다.

**겹침 주의.**

- 단계 1 은 이름 변경 리다이렉트 작업(`wt/renameredirect`, 최근 병합)이 고친 직렬화 파일과 같은 곳입니다. 시작 전에 main 을 받습니다.
- 단계 3 의 Matchmaking 과 Social 은 단계 4 와 같은 파일(`MatchmakingServer.cpp`, `SocialServer.cpp`)을 고칩니다. 단계 4 는 단계 3 이 병합된 뒤에 하거나, 두 키트를 같은 작업자에게 맡깁니다.
- `ChatService.cpp` 는 채팅 주제와 제재 주제를 함께 읽으므로 단계 2 의 제재 타입이 먼저 있어야 합니다.
- 키트 프로토콜 헤더(`XxxProtocol.h`)의 `MessageTypes` 를 고치면 온라인 스키마 해시가 바뀝니다. 클라이언트 접속 테스트가 해시 값을 고정해 두었다면 같이 고칩니다(미확인, 단계 0).

## 6. 검증 계획

성능 측정은 Release 로 하고, 테스트는 CTest 로 돌립니다.

**혼합 빌드를 흉내 내는 방법.** 한 실행 파일 안에 같은 FQN 의 타입을 둘 둘 수는 없습니다. 하지만 A 스트림에는 타입 FQN 이 없고 필드를 이름 해시로 짝짓습니다(3.2).
그래서 테스트 전용 헤더에 필드 이름이 겹치는 두 타입(예: `BusTestNoticeOld`, `BusTestNoticeNew`)을 두고, 한쪽으로 쓴 바이트를 다른 쪽으로 읽으면 옛 빌드와 새 빌드 사이를 흉내 낼 수 있습니다.
필드 타입이 구조체이면 기록 타입 해시가 그 구조체의 정규화한 이름이므로, 두 타입이 같은 중첩 타입을 써야 합니다.

| 테스트 | 확인하는 것 |
|---|---|
| 필드 추가, 새 → 옛 | 옛 타입이 성공하고 아는 필드 값이 같음. 경고는 같은 (타입, 필드)에 한 번 |
| 필드 추가, 옛 → 새 | 새 필드가 기본값 |
| 필드 삭제 | 위와 반대 방향 둘 |
| 모르는 열거자 | 같은 이름 열거형을 쓸 수 없으므로 두 열거형의 열거자 이름 집합을 다르게 두고 확인. 거절, 오류 로그 한 번, 카운터 증가 |
| 의미 버전 다름 | 거절. 이관을 등록하면 새 쪽이 옛 버전을 읽음 |
| 범위 밖 | `MaxCount` 를 넘는 목록을 A 로 쓴 바이트를 읽으면 거절 |
| 잘린 바이트 | 바이트를 하나씩 줄인 모든 접두에서 거절, 대상 객체 그대로 |
| 크기 | 버스 타입마다 모든 목록을 `MaxCount` 로 채운 A 크기가 `kMaxMessageSize` 이하 |
| push 중계 | 다른 서버가 낸 중계를 받은 서버가 자기 빌드 번호와 B 로 클라이언트에 보내고, 클라이언트가 `Ok` 로 읽음. 모르는 열거자 이름이면 버리고 오류 로그 |
| 해지 알림(보안) | `LocalServerBusHub` 위 서버 둘에서, 필드가 하나 더 있는 테스트 타입으로 낸 해지 알림을 받은 서버가 세션을 끊음 |

기존 서버 둘 테스트(`TestOnlineMultiServer.cpp`, `TestChatService.cpp`, `TestSocialService.cpp`, `TestMatchQueue.cpp`, `TestAdminService.cpp`, `TestServerBus.cpp` 등 단계 0 에서 확정)는 동작이 바뀌지 않으므로 그대로 통과해야 합니다.
버스 바이트를 직접 만들거나 비교하는 테스트가 있으면 새 코덱으로 바꿉니다.

**측정.** 채팅 중계 메시지 하나(`ChatMessage`, 본문을 상한까지 채운 것과 짧은 것)의 쓰기 + 읽기를 지금 B 와 새 A 로 각각 Release 에서 측정합니다. 메시지당 시간과 바이트 수를 [검증 문서](../08_Verification.md) 에 적습니다.
판단 기준은 측정 뒤에 정합니다. 숫자 없이 "느리지 않다"로 끝내지 않습니다.

## 7. 정할 것과 정한 것

정한 것은 아래와 같습니다.

- 버스 본문은 A(버전 머리 + `BinarySerializer`)이고, 클라이언트 메시지는 B 입니다(사용자 결정).
- 주제 정의 하나가 이름, 본문 타입, 의미 버전을 가집니다. 키트는 바이트를 직접 발행하지 않습니다.
- 버스 읽기는 모르는 이름의 필드만 버리고, 아는 필드를 읽지 못하면 거절합니다.
- 의미 버전은 타입마다 하나이고 레코드 버전과 같은 상수를 씁니다.
- 거절은 조용히 하지 않습니다. 오류 로그 한 번과 카운터를 남깁니다.

## 8. 위험

1. **기본값 폴백**: 없는 필드가 기본값이 되는 규칙이 폴백 금지 원칙과 부딪힙니다. 3.6 의 규칙과 열린 질문 1 로 다룹니다.
2. **조용한 뜻 어긋남**: 모르는 필드를 버리므로 뜻이 바뀐 변경을 옛 빌드가 알아채지 못합니다. 의미 버전을 높이는 것을 사람이 기억해야 하는 틈이고, 형식으로 막지 못합니다.
3. **로그 폭주**: 배포 중 채팅 중계에서 경고가 메시지마다 나오지 않게 한 번만 경고합니다(3.3). 이 옵션이 빠지면 로그가 넘칩니다.
4. **A 인코딩 버전**: `BinarySerializer` 의 인코딩 규칙을 바꾸면 섞인 동안 모든 버스 메시지와 캐시 레코드를 읽지 못합니다. 인코딩 버전을 높일 때는 한 배포 동안 옛 버전도 읽어야 한다는 것을 `BinarySerializer` 헤더에 적습니다.
5. **봉투 버전**: 4절과 같습니다.
6. **크기**: A 가 B 보다 커서 큰 메시지(`MatchFormed` 같은 것)가 `kMaxMessageSize` 를 넘을 수 있습니다. 6절 크기 테스트로 잡습니다.
7. **이관 키 충돌**: 버스와 레코드가 다른 버전 수열을 쓰면 같은 (타입, 버전)의 이관이 두 용도에 함께 적용됩니다. 타입마다 버전 하나로 막습니다(3.4).
8. **채팅 중계 비용**: 측정 전에는 알 수 없습니다(3.8).
9. **온라인 스키마 해시 한 번 바뀜**: 버스 전용 타입을 목록에서 빼면 클라이언트가 한 번 업데이트를 받습니다(3.7).
10. **push 중계의 이름 찾기**: 알림 열거자 이름을 바꾸면 섞인 동안 옛 빌드가 보낸 중계를 찾지 못합니다. `ENUM( ValueAlias )` 로 새 빌드는 옛 이름을 읽지만 반대 방향은 버려집니다.

## 9. 열린 질문(사용자 결정)

1. **필수 필드 표시.** "스트림에 없으면 거절"을 필드에 표시하는 키(예: `PROPERTY( Required )`)를 둘까요. 두면 기본값 폴백을 형식으로 막지만, 생성기 · `AnnotationMeta.txt` 변경과 A 읽기가 "있었던 필드"를 세는 변경이 필요합니다.
   권하는 안은 이번에는 3.6 의 규칙으로 두고, 기본값을 처리할 수 없는 필드가 처음 생길 때 더하는 것입니다.
2. **의미 버전이 다를 때.** 섞인 동안 거절하고 버린 수만 셀까요, 아니면 새 빌드가 옛 버전과 새 버전을 함께 발행할까요. 권하는 안은 거절입니다. 버스는 최대 한 번 배달이고 영속 저장이 원본입니다.
3. **push 중계를 이번 범위에 넣을지.** 넣지 않으면 섞인 동안 다른 서버를 거친 알림(친구, 업적, 거래, 매치)이 클라이언트에서 버려지거나 다른 알림으로 읽힐 수 있습니다. 권하는 안은 넣는 것(단계 4)입니다.
4. **`config.changed`.** `Source` 안에 구독하는 곳을 찾지 못했습니다. 주제를 지울까요, 원격 설정의 다시 읽기에 쓸까요.
5. **캐시 · 영속 레코드의 orphan 정책.** 레코드도 배포 중에 다른 빌드가 읽는데 지금은 모르는 필드를 거절합니다(4절). 같은 정책(3.3)을 레코드 읽기에도 쓸까요. 이 계획의 범위 밖이며, 정하면 백로그 항목으로 둡니다.
6. **의미 버전의 위치.** 지금 레코드 버전은 `.cpp` 의 지역 상수입니다(`ChatProtocol.cpp:12`). 타입 선언 옆 한 곳(예: `REFLECT` 키)으로 옮기면 버스와 레코드가 같은 값을 쓰는 것을 형식이 보장합니다. 생성기 변경이 필요합니다.
