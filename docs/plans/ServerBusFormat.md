# 서버 버스 메시지 형식 계획 (2026-10-17)

온라인 서버끼리 주고받는 버스 메시지를 고정 배치형(B)에서 자기 기술형(A, 버전 머리 + `BinarySerializer`)으로 되돌리는 설계입니다.
백로그 `F-2` 의 결정을 구현하는 명세입니다. `design-reviewer` 검토(ⓐ~ⓖ, [인계 문서](NEXT.md) 2절 0번)를 반영해 고쳤습니다.

**결정(사용자 확정, 2026-10-17)**: 서버 사이에는 핸드셰이크가 없고, 배포 중에는 옛 빌드와 새 빌드의 서버가 함께 돕니다. 그래서 버스 메시지는 혼합 빌드를 견디는 A 로 씁니다.
클라이언트와 서버 사이의 메시지는 핸드셰이크가 같은 빌드를 보장하므로 B 를 유지합니다([리플렉션 직렬화 형식 계획](ReflectionFormats.md) 5절, [결정 기록](../09_Decisions.md) 5-9).

근거는 실제로 읽은 코드만 `파일:줄` 로 적고, 읽지 않은 것은 "미확인"으로 적습니다. 줄 번호는 main `780a38ced` 기준입니다.

**구현 상태(2026-10-17)**: 단계 0 의 확인을 2절에 반영했습니다. 단계 1 부터 브랜치 `wt/feat11` 에서 구현합니다.

## 1. 지금 구조와 문제

### 1.1 버스의 구조

- 버스 API 는 `IServerBus` 입니다(`Source/GameFramework/Base/Online/Bus/ServerBus.h`). `publish( topic, pData, size )` 가 바이트를 그대로 받고, 배달은 최대 한 번입니다.
  헤더 주석은 "놓치면 안 되는 것은 영속 저장 + 다시 읽기, 버스는 빨리 알기 용"이라고 적고 있습니다. 메시지 크기 상한은 `kMaxMessageSize = 60 * 1024` 입니다.
- 서버 여럿을 잇는 구현은 `EphemeralServerBus` 입니다. 캐시 채널 `bus:<주제>` 위에 `[봉투 버전 1][서버 id 8][순번 8][본문]` 봉투를 붙입니다(`EphemeralServerBus.cpp:19-20`).
  봉투 버전이 다르면 받는 쪽은 `_droppedCount` 만 늘리고 버립니다(`EphemeralServerBus.cpp:141-147`). 봉투는 B 가 아닌 고정된 작은 형식이므로 이 계획에서 바꾸지 않습니다.
- 서비스는 `pollMessages` 를 직접 부르지 않습니다. 호스트가 비우고 `subscribeServerBus` 한 서비스의 `onServerBusMessage` 로 나눠 줍니다(`OnlineServiceHost.cpp:473`).
- 받는 쪽 `ServerBusMessage::_bytes` 가 공개 필드라(`ServerBus.h:18-24`) 서비스는 본문을 `BitReader` 로 직접 읽습니다.

### 1.2 무엇이 잘못되는가

본문은 대부분 키트의 `XxxProtocol::MessageTypes::write` 로 쓴 B 입니다. B 는 머리에 타입 스키마 해시 64 비트를 싣고, 읽는 쪽 해시와 다르면 `SchemaMismatch` 로 거절합니다(`OnlineProtocolRegistry.h:18-19`).
배포 중에 메시지 타입의 필드 하나라도 바뀐 빌드가 섞이면, 받는 서버는 그 주제의 메시지를 모두 버립니다. 로그를 남기는 곳은 계정 해지 알림 하나뿐입니다(`AccountServer.cpp:270-274`). 나머지는 조용히 `return` 합니다.

버리는 메시지의 결과는 주제마다 다릅니다.

- 계정 해지 알림을 놓치면, 해지한 계정의 세션이 다른 빌드의 서버에 그대로 남습니다. 보안에 영향이 있습니다.
- 매치메이킹 티켓을 놓치면 티켓이 시한으로 끝날 때까지 매칭되지 않습니다.
- 채팅을 놓치면 다른 빌드 서버에 접속한 사람에게 말이 보이지 않습니다.

남은 손 코덱도 같은 문제를 가집니다. 제재 버스는 varuint 계정 id 하나를 손으로 씁니다(`AdminService.cpp:229-231`, `ChatService.cpp:622-627`).
접속 상태 전달(push 중계) 봉투는 `[varuint 계정][varuint 종류][blob 본문]` 을 손으로 쓰고, 안쪽 본문은 보낸 서버 빌드의 B 입니다(`OnlinePresence.cpp:359-363`, `:148-163`). 3.5 에서 따로 다룹니다.

**뿌리**: 버스 본문의 형식을 "같은 빌드끼리만 읽는다"는 B 의 전제 위에 두었는데, 버스에는 그 전제를 보장하는 핸드셰이크가 없습니다. 그리고 주제와 본문 타입의 짝이 선언 하나에 묶여 있지 않아서, 서비스마다 바이트를 손으로 쓰거나 B 로 쓰는 방식이 갈립니다.

## 2. 버스 메시지 전수 목록

`Source/GameFramework` 에서 `publish(` 와 `onServerBusMessage` · `handleBusMessage` 를 찾아 만든 목록입니다(단계 0 확인 결과 포함).
빈도는 측정값이 아니라 코드에서 발행하는 조건을 보고 추정한 것입니다. 낮음은 운영자 조작이나 드문 사건, 중간은 플레이어 행동 하나에 한 번, 높음은 플레이어가 반복하는 행동마다입니다.

| 주제 | 본문 타입 | 옛 형식 | 송신 위치 | 수신 위치 | 클라이언트에도 보내는가 | 빈도 |
|---|---|---|---|---|---|---|
| `account.revoke` | `AccountRevokeNotice` | B | `AccountServer.cpp:459-465` | `AccountServer.cpp:258-279` | 아니오 | 낮음(해지 때) |
| `push.<서버 id>` | 손 봉투 + 안쪽 B | 손 | `OnlinePresence.cpp:359-363` | `OnlinePresence.cpp:148-163` | 안쪽 본문은 예 | 중간 |
| `chat.channel.<채널>` | `ChatMessage` | B | `ChatService.cpp:590-597` | `ChatService.cpp:620-639` | 예(알림) | 높음 |
| `chat.server.<서버 id>`(귓속말) | `ChatMessage` | B | `ChatService.cpp:585` | `ChatService.cpp:630-635` | 예(알림) | 중간 |
| `sanction.changed` | varuint 계정 id | 손 | `AdminService.cpp:227-232` | `ChatService.cpp:623-627` | 아니오 | 낮음 |
| `social.links` | `SocialLinkChanged` | B | `SocialService.cpp:390-395` | `SocialService.cpp:524-535` | 아니오 | 낮음 |
| `social.presence` | `SocialPresence` | B | `SocialService.cpp:580-589` | `SocialService.cpp:537-541` | 예(응답 목록) | 중간(접속 · 상태 바뀜) |
| `mm.queue.<모드>` | `MatchTicket` | B | `MatchQueueService.cpp:338-342` | `MatchQueueService.cpp:538-546` | 아니오 | 중간 |
| `mm.cancel.<모드>` | `MatchTicketCancel` | B | `MatchQueueService.cpp:526-530` | `MatchQueueService.cpp:547-555` | 아니오 | 중간 |
| `mm.result.<서버 id>` | `MatchAssignment` | B | `MatchQueueService.cpp:491-498` | `MatchQueueService.cpp:556-561` | 예(알림 `kPushMatch`) | 중간 |
| `mm.assign.<서버 id>` | `MatchFormed` | B | `MatchQueueService.cpp:412-416`, `:464-468` | `MatchServerAgent.cpp:29-47` | 아니오(게임 서버 안에서만) | 낮음에서 중간 |
| `liveops.changed` | 없음 | 빈 본문 | `LiveOpsService.cpp:375-376` | `LiveOpsServer.cpp:136-144` | 아니오 | 낮음 |
| `sd.changed` | 없음 | 빈 본문 | `ServerDirectoryService.cpp:527-528` | `ServerDirectoryServer.cpp:119-127` | 아니오 | 낮음 |
| `config.changed` | 키 문자열 바이트 | 손 | `RemoteConfig.cpp:371` | 없음 | 아니오 | 낮음 |

push 중계를 부르는 곳은 네 곳입니다. `LeaderboardServer.cpp:143`, `MatchmakingServer.cpp:331`, `SocialServer.cpp:262`, `TradeServer.cpp:224` 가 `IAccountPresence::sendRemotePush` 에 B 로 쓴 `BitWriter` 를 넘깁니다.

`config.changed` 는 `Source` 안에 구독하는 곳이 없고, `submitSet` 에 버스를 넘기는 제품 코드도 없습니다(테스트만 넘김). 서버의 원격 설정은 `OnlineServerRuntime` 이 주기로 다시 읽습니다(`OnlineServerRuntime.cpp:389-393`).

버스 바이트를 직접 만들거나 읽는 테스트는 `TestServerBus.cpp`(글 바이트), `TestOnlineServiceHost.cpp:809-816`(`sd.changed` 에 1 바이트), `TestChatService.cpp:157-160` · `TestChatStream.cpp:215-218`(제재 varuint), `TestRemoteConfig.cpp:152-170`(`config.changed` 키 바이트),
`TestMatchQueue.cpp:109` · `:150`, `TestChatService.cpp:120`, `TestSocialService.cpp:109`(`message._bytes` 를 서비스에 넘김)입니다.
버스 전용 타입을 B 목록으로 왕복시키는 테스트는 `TestLoginService.cpp:1010`, `TestMatchQueue.cpp:455-456` · `:469`, `TestSocialService.cpp:416-417` 입니다.
온라인 스키마 해시 값을 고정한 테스트는 찾지 못했습니다.

## 3. 설계

### 3.1 주제가 본문 타입을 가진다

주제 정의 하나가 이름과 본문 타입을 함께 가지고, 의미 버전은 본문 타입이 가집니다(3.4). 주제와 맞지 않는 타입을 쓰거나 읽는 코드는 형식이 막습니다.

- `ServerBusTopic<T>` 는 고정 이름 주제입니다(`account.revoke` 같은 것). `static constexpr` 객체로 선언합니다.
- `ServerBusTopicFamily<T>` 는 접미사가 붙는 주제입니다(`chat.channel.<채널>`, `mm.queue.<모드>`, `push.<서버 id>`). 접두사를 가지고, 접미사를 받아 주제 이름을 만듭니다.
  받은 메시지의 주제가 이 접두사로 시작하는지 확인하는 함수와 접미사를 꺼내는 함수도 같은 정의에 둡니다. 서비스마다 손으로 쓰던 비교(`MatchQueueServiceInternal::hasPrefix`)가 없어집니다.
- 쓰기는 `topic.publish( ServerBus&, const T& )`(가족은 접미사를 더 받음), 읽기는 `topic.read( const ServerBusMessage&, T& )` 하나입니다. 둘 다 `[[nodiscard]]` 이고, 읽기는 결과 열거형 `ServerBusReadResult` 를 돌려줍니다.
- 본문이 없는 신호 주제(`liveops.changed`, `sd.changed`)도 같은 구조로, `PROPERTY` 가 없는 `REFLECT` 타입(`LiveOpsChanged`, `ServerDirectoryChanged`)을 본문으로 씁니다. A 로 8 바이트가 늘지만 경로가 하나로 줄고, 받는 쪽도 의미 버전을 확인합니다.
- 제재 버스의 손 varuint 본문은 `REFLECT` 타입 `ServiceSanctionChanged`(필드는 계정 id 하나)로 바꿉니다. 주제 정의는 지금 주제 이름이 있는 `ServiceSanctionBus` 에 둡니다.
- `config.changed` 는 지웁니다. 구독처가 없고 서버는 주기로 다시 읽으므로, 발행과 `RemoteConfig::submitSet` 의 버스 인자와 `RemoteConfig.h:3` 주석을 함께 지웁니다.

**원시 바이트를 키트가 쓰지 못하게 하는 구조(ⓕ).** 바이트 전송 인터페이스와 타입 있는 앞을 나눕니다.

- `IServerBusTransport` 는 바이트를 보내고 받는 인터페이스입니다(`publish( topic, pData, size )`, `subscribe`, `unsubscribe`, `pollMessages`, `getServerID`). `LocalServerBus` 와 `EphemeralServerBus` 가 구현합니다.
  이 인터페이스는 호스트(`OnlineServiceHostConfig::_pServerBusTransport`)와 서버 조립(`OnlineServerRuntime`)만 가집니다.
- `ServerBus` 는 타입 있는 앞입니다. 전송 하나를 빌려 쓰고, 공개 함수는 `getServerID` 하나입니다. 바이트를 보내는 `publishBytes` 는 private 이고, 주제 정의 두 템플릿만 friend 입니다.
  키트 의존성(`XxxDependencies::_pBus`, `OnlineServerKitContext::_pBus`, `OnlineServiceHost::getServerBus`)은 모두 `ServerBus*` 입니다.
- `ServerBusMessage` 는 필드를 숨긴 클래스입니다. 주제 · 보낸 서버 · 순번은 접근자로 읽고, 본문 바이트는 주제 정의 두 템플릿만 읽습니다. 전송 구현과 테스트는 생성자로 메시지를 만듭니다.
  그래서 키트가 `BitReader` 로 본문을 직접 읽는 코드는 컴파일되지 않습니다.

### 3.2 와이어 형태

봉투는 그대로 두고, 본문만 `BinarySerializer::serializeVersioned` 의 출력으로 바꿉니다.

```
[봉투 버전 u8 = 1][보낸 서버 id u64][순번 u64]            EphemeralServerBus, 바꾸지 않음
[의미 버전 u32]                                          serializeVersioned 의 버전 머리 = T::kBusVersion
[A 스트림 머리 u32: 위 8 비트 인코딩 버전, 아래 24 비트 프로퍼티 수]
[프로퍼티마다: 이름 해시 u32][기록 타입 해시 u32][크기 u32][값]
```

A 스트림의 프로퍼티 머리 구성은 `BinarySerializer.h` 5절 주석("프로퍼티마다 12바이트 태그 헤더")과 [리플렉션 직렬화 형식 계획](ReflectionFormats.md) 1.1 을 따랐습니다.
A 스트림 머리에는 타입 FQN 이 없습니다. 필드는 이름 해시로 짝짓고, 필드 타입은 정규화한 타입 이름의 해시로 확인합니다.

B 스키마 해시는 버스 본문에 싣지 않습니다. 버스의 호환 판단은 필드 이름 짝짓기와 의미 버전이 맡습니다.

**숫자 필드 ID 를 버스에만 선택 키로 두는 안은 하지 않습니다(ⓓ).** 크기 이득은 번호가 아니라 인코딩(고정 u32 세 개)에서 나오는데, 인코딩을 바꾸면 혼합 빌드에서 모든 캐시와 버스가 읽히지 않습니다(8절 위험 4).
정체성 체계가 둘이 되면 `ChatMessage` 처럼 레코드와 버스가 함께 쓰는 타입이 갈립니다. 이 설계는 protobuf(번호)가 아니라 Avro 스키마 해석과 거의 같습니다.
Avro 1.11 명세 "Schema Resolution" 은 이름(+ alias)으로 필드를 짝짓고, 쓴 쪽에만 있는 필드는 무시하며, 읽는 쪽에만 있는 필드는 기본값이 없으면 오류이고, 모르는 열거자는 default 가 없으면 오류입니다.

### 3.3 읽기 정책

버스 읽기는 **엄격 태그 루프 하나**입니다. 소프트 읽기(orphan 수집)와 이관 레지스트리를 지나지 않습니다.

1. **의미 버전이 다르면 거절합니다.** 이관 레지스트리를 보지 않습니다. 거절은 (타입, 결과, 받은 버전)마다 한 번 오류 로그를 남기고, 버린 수를 카운터(`ServerBusReadLog::getRejectedCount`)로 셉니다.
2. **타입에 없는 이름의 필드는 건너뜁니다.** (타입, 필드 이름 해시)마다 한 번만 경고합니다(`BinarySerializer.cpp` `warnSkippedFieldOnce`, 이미 있음).
3. **타입에 있는 필드인데 값을 읽지 못하면 거절합니다.** 모르는 열거자, 변환할 수 없는 타입 변경이 해당합니다. 1 과 같이 로그와 카운터를 남깁니다.
4. **스트림에 없는 필드는 타입의 기본값입니다.** A 의 지금 동작입니다(`BinarySerializer.cpp:257-269`). 이 기본값이 뜻하는 바는 3.6 의 규칙이 정합니다.
5. **읽은 값이 선언된 범위(`Min` · `Max` · `MaxCount`)를 지키는지 확인합니다.** A 는 `MaxCount` 를 보지 않으므로 B 쓰기 검사로 다시 봅니다. 버스 타입은 B 코덱이 반드시 있어야 하고, 없으면 `FixedLayout::write` 가 이유와 함께 프로세스를 멈춥니다.

**모든 깊이에서 같은 정책(ⓐ ⓑ).** 앞 판의 이 절은 `SchemaOrphanPolicy` 에 값 하나를 더하자고 했지만, 그 정책은 최상위 필드에만 듣습니다.
중첩 구조체는 언제나 엄격 `deserialize` 로 읽고(`SerializerUtil.cpp:674-684`), 엄격 경로는 `allowsUnknownProperties` 하나로 "모르는 이름"과 "읽지 못한 아는 필드"를 함께 정했습니다(`BinarySerializer.cpp:190`, `:246-249`).
그래서 `MatchFormed::_listTicket`(`MatchmakingTypes.h:125`) 원소에 필드 하나만 더해도 플래그를 끄면 메시지 전체가 거절되고, 켜면 중첩 안의 모르는 열거자가 기본값으로 조용히 통과했습니다.

그래서 `SerializeContext` 의 플래그를 둘로 나눕니다.

| 플래그 | 켜면 | 끄면 |
|---|---|---|
| `setSkipUnknownProperties` | 타입에 없는 이름은 건너뛰고 (타입, 이름)마다 한 번 경고 | 실패 |
| `setSkipUnreadableProperties` | 읽지 못한 아는 필드는 건너뛰고 (타입, 필드)마다 한 번 경고 | 실패 |

같은 컨텍스트가 중첩 구조체 · 컨테이너 원소 · 맵 키까지 내려가므로 모든 깊이에서 정책이 같습니다.
버스와 캐시 레코드의 읽기 컨텍스트는 앞의 것만 켭니다(`MixedBuildCodec::getReadContext`). 오브젝트 상태처럼 지금 `setAllowUnknownProperties( true )` 를 쓰는 곳은 두 플래그를 함께 켜는 축약으로 동작이 그대로입니다.
버전 붙은 바이너리 읽기(`deserializeVersioned`)의 orphan 정책은 두 플래그가 모두 켜졌을 때만 `Ignore` 입니다. 앞의 것만 켠 컨텍스트는 그 경로를 쓰지 않고 `BinarySerializer::deserializeExactVersion` 을 씁니다.
텍스트 직렬화기(XML, JSON)는 이 플래그를 읽지 않습니다(`allowsUnknownProperties` 의 사용처는 `BinarySerializer.cpp` 와 `SerializeContext.cpp:258` 뿐이었습니다).

**소프트 경로의 숨은 폴백 제거(ⓒ).** 소프트 읽기는 기록 타입이 다르고 변환(`tryCoerceBinaryPayload`)이 실패해도 제 타입으로 "읽힌 만큼은" 받았습니다(`BinarySerializer.cpp:226-228`, 끝까지 읽었는지 보지 않음).
예를 들어 `int32` 0 으로 기록된 필드를 `vector<int32>` 로 바꾸면 그 4 바이트를 원소 수 0 으로 읽어 빈 목록으로 성공했습니다. 이 분기를 지우고, 변환이 안 되면 orphan 으로 남깁니다.
버전 붙은 바이너리 읽기는 orphan 을 이관 없이 거절하므로 실패가 드러납니다.

**이 정책의 위험.** 모르는 필드를 버리면, 새 필드가 다른 필드의 뜻을 바꾸는 변경(예: "이 필드가 있으면 `_x` 는 초 단위가 아니라 밀리초")을 옛 빌드가 알아채지 못합니다.
이 경우는 3.4 의 의미 버전을 높여야 하고, 형식으로는 막지 못합니다. B 에서 `kBaseVersion` 이 맡는 틈과 같은 종류입니다(결정 기록 5-9).
버스는 서버끼리만 쓰므로 악의적인 입력은 고려하지 않습니다. 깨진 바이트는 A 의 크기 확인이 거절합니다.

### 3.4 해시 대신 의미 버전

B 는 배치가 바뀌면 해시가 바뀌어 거절합니다. 혼합 빌드를 견디려면 필드를 더하거나 빼는 일로는 거절하지 않아야 하므로, 버스는 스키마 해시를 쓸 수 없습니다.
대신 `serializeVersioned` 의 버전 머리를 **의미 버전**으로 씁니다. 필드 이름 짝짓기로 견딜 수 없는 변경(3.6 의 "버전을 높임" 줄)에서만 높입니다.

이 버전은 손 상수입니다. [리플렉션 직렬화 형식 계획](ReflectionFormats.md) 1.4 는 "손 버전 상수"를 결함으로 꼽았지만, 그것은 배치가 바뀔 때 높이는 것을 잊으면 잘못 읽는 B 의 경우입니다.
A 는 배치 변경을 이름으로 견디므로, 손 버전이 맡는 것은 해시가 볼 수 없는 의미 변경뿐입니다. B 의 `kBaseVersion` 과 같은 역할입니다.

**의미 버전은 타입 안의 `static constexpr uint32 kBusVersion` 입니다(ⓔ).** 생성기를 바꾸지 않습니다.
`ServerBusTopic<T>` 와 `ServerBusTopicFamily<T>` 가 그 값을 읽고, 없거나 `uint32` 가 아니면 `static_assert` 로 컴파일이 멈춥니다. 같은 타입의 주제 둘(`chat.channel.` 과 `chat.server.`)이 값을 따로 적는 일이 없습니다.

**의미 버전이 다르면 이관 레지스트리를 보지 않고 거절합니다.** 그래서 버스의 버전 수열은 레코드 버전 수열과 묶일 이유가 없습니다.
`ChatMessage` 의 기록 레코드는 `kChatRecordVersion`(`ChatProtocol.cpp:12`), 버스는 `ChatMessage::kBusVersion` 을 따로 씁니다. 앞 판의 열린 질문 6(버전 위치)과 위험 7(이관 키 충돌)은 함께 풀렸습니다.
혼합 배포 중 소프트 경로의 메시지마다 orphan 복사와 전역 잠금 아래의 이관 테이블 선형 탐색도 없어집니다.

A 의 인코딩 버전(스트림 머리 위 8 비트)은 이 버전과 다릅니다. `BinarySerializer` 는 현재 인코딩 버전 하나만 읽으므로(계획 1.1), 인코딩 규칙을 바꾸면 혼합 빌드에서 모든 버스 메시지와 캐시 레코드를 읽지 못합니다. 8절 위험 4 에 적습니다.

### 3.5 push 중계

버스에는 메서드 번호가 없습니다. 주제 이름이 메시지 종류이고, 3.1 의 주제 정의가 그 이름과 본문 타입을 묶습니다.

예외는 push 중계입니다. 옛 봉투의 `kind` 는 보낸 서버 빌드의 클라이언트 알림 번호이고(메서드 테이블의 열거자 순서, 결정 기록 5-9), 안쪽 본문은 보낸 서버 빌드의 B 입니다.
받는 서버는 그 바이트를 그대로 자기 클라이언트에 보냈습니다(`OnlinePresence.cpp:158-161`). 클라이언트는 받는 서버와 같은 빌드이므로, 보낸 서버의 빌드가 다르면 알림 번호가 다른 알림을 가리키거나 본문 해시가 달라 버려집니다.

push 중계는 아래처럼 바꿉니다. 받는 서버는 **"알림 이름 → 기대하는 본문 타입" 테이블**로 바꿀 수 있는 알림만 받습니다(ⓔ).

- 봉투는 `REFLECT` 타입 `OnlinePushRelay` 의 A 입니다(`push.<서버 id>` 주제, `Base/Online/Identity/OnlinePushRelay.h`). 필드는 계정 id, 알림 열거형 FQN, 알림 열거자 이름, 본문 타입 FQN, 본문의 A 바이트(본문 타입의 `kBusVersion` 머리)입니다.
- 중계할 수 있는 알림은 키트 서버 `.cpp` 가 이름 공간 범위의 `OnlinePushRelayEntry` 로 등록합니다. 항목은 (메서드 테이블, 알림 열거자, 키트 메시지 목록, 본문 타입)을 템플릿으로 받아
  자기 빌드의 알림 번호, 본문 FQN, "A 를 읽어 자기 빌드의 B 로 쓰는" 함수를 가집니다. 등록 목록은 `ServiceFactoryList` 입니다(모듈 로드 · 언로드 · 핫 리로드와 같은 규칙).
- 보내는 쪽은 `IAccountPresence::sendRemotePush( accountID, 알림 열거자, 본문 )` 템플릿을 부릅니다. 템플릿이 봉투를 만들고 가상 함수 `relayRemotePush( 봉투 )` 로 넘깁니다.
- 받는 서버는 봉투의 (열거형 FQN, 열거자 이름)으로 항목을 찾고, 본문 FQN 이 항목과 같은지 확인한 뒤 본문 A 를 혼합 빌드 읽기로 읽어 **자기 빌드의 B** 로 다시 써서 클라이언트에 보냅니다.
  보낸 계정이 그사이 이 서버로 옮겨 왔을 때(`finishPush` 의 같은 서버 분기)도 같은 함수를 지납니다.
- 항목이 없거나, 본문 타입이 다르거나, 본문을 읽지 못하면 (열거형, 열거자)마다 한 번 오류 로그와 카운터를 남기고 버립니다.
- 그러면 클라이언트는 언제나 접속한 서버 빌드의 B 를 받으므로 5-a 의 보장이 그대로 맞습니다. 앞 판의 `TypeRegistry` + `createScratchInstance` + `FixedLayout` 의 `TypeInfo` 쓰기 진입점은 필요 없어졌습니다(본문 타입을 컴파일 때 압니다).

중계하는 알림은 소셜 `Notification`, 순위표 업적 알림, 거래 세 알림, 매치메이킹 네 알림입니다(본문 타입은 각 항목에 적습니다).

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
| 뜻만 바뀜(단위 같은 것) | 알아채지 못함 | 알아채지 못함 | 의미 버전을 높입니다. 섞인 동안 두 방향 모두 거절됩니다(이관은 없습니다) |

**중계 필드 유실(ⓖ).** 메시지가 여러 서버를 거치면, 새 필드는 **옛 빌드 홉을 한 번이라도 지나면 기본값**이 됩니다.
예를 들어 `mm.queue` 로 새 빌드가 보낸 `MatchTicket` 의 새 필드는 옛 빌드 매처가 버리고, 그 매처가 만든 `MatchFormed`(`MatchMaker.cpp:202`)를 받은 새 빌드 게임 서버는 기본값을 봅니다.
그래서 "필드 추가" 줄의 기본값 규칙은 직접 받는 쪽만이 아니라 **그 값이 거쳐 가는 모든 홉의 끝**에 적용됩니다. 기본값을 처리할 수 없는 필드는 의미 버전을 높입니다.

기본값 규칙(첫 줄)은 폴백 금지 원칙과 부딪힐 수 있습니다. "필드가 없다"를 받는 쪽이 오류로 다뤄야 하는 필드라면 기본값으로 계속하면 안 됩니다.
필드에 "없으면 거절" 표시를 두는 키(`PROPERTY( Required )`)는 두지 않습니다(ⓔ). 새 필드에 Required 를 붙이면 옛 빌드가 보낸 메시지를 새 빌드가 모두 거절하므로, 의미 버전을 높이는 것과 같습니다.

### 3.7 클라이언트 핸드셰이크 해시(단계 5-a)와의 관계

- 버스에만 쓰는 타입(`AccountRevokeNotice`, `SocialLinkChanged`, `MatchTicket`, `MatchTicketCancel`, `MatchFormed`)은 키트의 `MessageTypes` 목록에서 뺍니다.
  그러면 이 타입들의 스키마 해시가 온라인 스키마 해시에서 빠지고, 서버끼리만 쓰는 필드를 바꿔도 클라이언트에 업데이트를 요구하지 않습니다.
  `OnlineMessageTypes` 의 템플릿 인자 주석(`OnlineProtocolRegistry.h:44`)이 "서버 버스 본문"을 적고 있으므로 같이 고칩니다.
- 클라이언트에도 보내는 타입(`ChatMessage`, `SocialPresence`, `MatchAssignment` 와 push 중계 본문)은 목록에 남습니다. 클라이언트 경로는 B 와 해시, 버스 경로는 A 와 의미 버전으로 같은 선언에서 따로 만들어집니다.
- 목록에서 타입을 빼면 온라인 스키마 해시가 한 번 바뀝니다. 이 변경을 배포하면 옛 클라이언트는 한 번 "업데이트 필요"를 받습니다. 의도한 결과입니다.
- 주제 목록은 핸드셰이크 해시에 넣지 않습니다. 클라이언트는 주제를 모르기 때문입니다.
- push 중계를 3.5 처럼 바꾸면 클라이언트가 받는 B 는 언제나 접속한 서버가 쓴 것이므로, 5-a 의 "클라이언트와 그 서버는 같은 빌드" 보장 안에서 닫힙니다.

### 3.8 성능 영향

측정한 값은 없습니다. 아래는 구조로 따진 것이고, 판단은 7절의 측정 뒤에 합니다.

- **크기**: B 는 필드를 태그 없이 비트로 붙이지만, A 는 프로퍼티마다 12 바이트 머리를 붙이고 버전 머리와 스트림 머리로 8 바이트가 늘어납니다. 열거형은 열거자 이름 해시를 씁니다(`SerializerUtil.cpp:157-163`).
  구조체 목록(`MatchFormed::_listTicket` 같은 것)은 원소마다 그 원소의 프로퍼티 머리가 붙습니다.
- **CPU**: A 읽기는 프로퍼티마다 타입의 프로퍼티 목록을 선형으로 훑어 이름 해시로 대상을 찾고, 값마다 타입 이름으로 핸들러를 찾습니다(계획 1.1, `SerializerUtil.cpp:596-616`). 읽은 뒤 범위 검사로 B 쓰기를 한 번 더 합니다.
- **채팅 중계**: 비용은 메시지 하나에 보내는 서버의 쓰기 한 번과, 그 채널을 구독한 **서버마다** 읽기 한 번입니다. 받은 서버 안의 수신자 팬아웃은 이미 읽은 객체를 쓰므로 수신자 수에 비례하지 않습니다(`ChatService.cpp:599-610`).
  그래서 비용은 "메시지 수 × 구독 서버 수"에 비례하고 플레이어 수에는 직접 비례하지 않습니다.
- 측정 결과가 문제가 되면, 고칠 곳은 A 읽기 경로(타입마다의 이름 해시 조회 캐시 같은 것)이고 형식을 B 로 돌리지 않습니다. 이 최적화는 측정 없이 하지 않습니다.

### 3.9 고려했지만 고르지 않은 안

- **B 유지 + 주제 이름에 빌드 해시 붙이기**(`chat.channel.<채널>@<해시>`): 같은 빌드끼리만 메시지가 가므로 조용히 잘못 읽지는 않습니다. 하지만 배포 중에 빌드가 다른 서버의 플레이어끼리 채팅 · 매칭 · 해지 알림이 끊깁니다. 해지 알림이 끊기는 것은 받아들일 수 없습니다.
- **B 유지 + "클러스터는 한 빌드로만 돈다" 정책**: 무중단 순차 배포를 막습니다. 사용자 결정으로 고르지 않았습니다.
- **버스 구독 때 빌드 해시를 확인하고 다르면 멈춤**: 섞인 빌드를 오류로 알리지만 견디지는 못합니다. 순차 배포 중 매번 멈춥니다.
- **`SchemaOrphanPolicy` 에 "모르는 이름만 버림" 값 더하기**(앞 판): 최상위 필드에만 듣습니다(3.3, ⓐ).
- **의미 버전이 다르면 이관으로 읽기**(앞 판): 버스 버전 수열이 레코드 수열에 묶이고, 같은 (타입, 버전) 키가 두 용도의 이관을 함께 가리킵니다. 버스는 최대 한 번 배달이고 영속 저장이 원본이므로 섞인 동안 거절로 충분합니다.
- **숫자 필드 ID**: 3.2.
- **push 중계 대신 도메인 이벤트 주제**: 받는 키트가 자기 빌드로 알림을 만드는 안입니다. 키트마다 주제와 처리기가 생겨 네 키트 아홉 알림에 같은 일을 되풀이하므로, 등록 테이블 하나로 했습니다.

## 4. 같은 결함이 다른 곳에 있는가

- **캐시 레코드(`readCache`)**: 파티 · 로비 · 접속 상태 캐시는 "배포 중에 다른 빌드의 서버가 읽기 때문에" A 입니다(결정 기록 5-9). 그런데 `ServiceRecordUtil::read` 는 orphan 을 거절했습니다(`ServiceRecordUtil.h:5-7`, `:72-88`).
  새 빌드가 필드를 더한 캐시 값을 옛 빌드가 읽으면 없는 것으로 보고, 파티 · 로비가 배포 중에 사라진 것처럼 보였습니다. 버스와 뿌리가 같으므로 **버스와 같은 읽기(`MixedBuildCodec::decode`)** 로 바꿉니다(ⓔ).
- **영속 레코드(`readDurable`)**: 옛 빌드가 새 필드를 버리고 읽은 레코드를 고쳐 다시 쓰면 새 빌드가 쓴 값이 지워집니다. 그래서 영속 레코드는 "모르는 필드를 보관했다가 다시 쓰기"가 먼저입니다(ⓔ).
  보관 구조(레코드가 orphan 바이트를 가지고 쓰기 때 그대로 다시 싣기)는 이 계획의 범위 밖이라 백로그 `F-3` 으로 두고, 그 전까지 `readDurable` 은 지금처럼 모르는 필드를 거절합니다(조용히 지우지 않고 실패로 드러냅니다).
- **봉투 버전**: `EphemeralServerBus` 의 `kEnvelopeVersion` 을 바꾸면 섞인 동안 모든 버스 메시지가 버려집니다. 지금은 `_droppedCount` 만 셉니다. 바꾸지 않는 고정 형식으로 두고 헤더 주석에 그렇게 적습니다.

## 5. 구현 단계

빌드 없이 코드와 테스트를 단계마다 커밋하고, 통합 때 한 워크트리에서 빌드 한 번으로 검증합니다.

| 단계 | 내용 | 파일 |
|---|---|---|
| 0 | 2절의 확인(클라이언트로 보내는 타입, 주제 이름, `config.changed` 구독처, 버스 바이트를 직접 다루는 테스트) | 읽기만 |
| 1 | 컨텍스트 플래그 둘, 소프트 폴백 제거, `deserializeExactVersion` | `SerializeContext.h` · `.cpp`, `BinarySerializer.h` · `.cpp`, `SchemaMigrate.h` 주석, `TestReflectionSerialization.cpp` |
| 2 | 전송과 앞 나누기, 주제 정의, 혼합 빌드 코덱, 호스트 · 조립, 제재 주제, `config.changed` 삭제, 캐시 읽기 | `Base/Online/Bus/*`, `OnlineServiceHost.*`, `OnlineServerRuntime.cpp`, `OnlineServerKit.h`, `ServiceSanction.h`, `RemoteConfig.*`, `ServiceRecordUtil.h`, 테스트 |
| 3 | 키트 옮기기, 버스 전용 타입을 `MessageTypes` 에서 빼기 | Account, Chat, Social, Matchmaking, Admin, LiveOps, ServerDirectory 키트와 테스트 |
| 4 | push 중계 | `Base/Online/Identity/OnlinePushRelay.*`, `AccountPresence.h`, `OnlinePresence.*`, 부르는 곳 네 키트, 테스트 |
| 5 | 혼합 빌드 · 크기 테스트 | `Test/EngineTest/GameFramework/Online/TestServerBusMixedBuild.cpp`, 테스트 타입 헤더 |
| 6 | 문서 · 주석, 결정 기록, 백로그 | 이 문서, `09_Decisions.md`, `backlog/network.md`, 헤더 주석 |

## 6. 검증 계획

**통합 때 돌릴 것.** Debug 빌드(경고 0) → `ReflectionTest`(`ReflectionSerializationTest.*`), `EngineTest` 의 `ServerBusTest.*` · `ServerBusMixedBuildTest.*` · `OnlineServiceHostTest.*` · `RemoteConfigTest.*`,
키트 테스트(계정 · 채팅 · 소셜 · 매치메이킹 · 관리 · 라이브 운영 · 서버 디렉터리 · 거래 · 순위표, `OnlineMultiServer`) → `-L nogpu` → `-L lint`.
리플렉션 헤더를 새로 만들었으므로(`OnlinePushRelay.h`, 테스트 타입 헤더, 제재 · 신호 타입) 새 워크트리는 `cmake --preset` 부터 다시 구성합니다.

**혼합 빌드를 흉내 내는 방법.** 한 실행 파일 안에 같은 FQN 의 타입을 둘 둘 수는 없습니다. 하지만 A 스트림에는 타입 FQN 이 없고 필드를 이름 해시로 짝짓습니다(3.2).
그래서 테스트 전용 헤더에 필드 이름이 겹치는 두 타입(`BusTestNoticeOld`, `BusTestNoticeNew`)을 두고, 한쪽으로 쓴 바이트를 다른 쪽으로 읽으면 옛 빌드와 새 빌드 사이를 흉내 낼 수 있습니다.
중첩 타입이 바뀌는 경우는 이름이 같은 두 `TypeInfo` 를 서로 다른 `TypeRegistry` 에 손으로 등록하고 `SerializeContext::setTypeRegistry` 로 쓰는 쪽과 읽는 쪽을 나눕니다(`ReflectionTest`).

| 테스트 | 확인하는 것 |
|---|---|
| 필드 추가, 새 → 옛 | 옛 타입이 성공하고 아는 필드 값이 같음 |
| 필드 추가, 옛 → 새 | 새 필드가 기본값 |
| 중첩 원소에 필드 추가(ⓐ) | 혼합 빌드 컨텍스트는 성공, 두 플래그를 끈 엄격 읽기는 실패 |
| 중첩 원소의 모르는 열거자(ⓐ) | 혼합 빌드 컨텍스트는 실패, 두 플래그를 켠 읽기는 성공(옛 동작) |
| 모르는 열거자(버스) | 같은 열거형으로 쓴 바이트에서 열거자 이름 해시를 덮어써 실제 혼합 빌드 경로(`readEnumBinary` 가 false)를 탐. 거절, 카운터 증가 |
| 의미 버전 다름 | 거절, 카운터 증가 |
| 범위 밖 | `MaxCount` 를 넘는 글을 A 로 쓴 바이트를 읽으면 거절 |
| 잘린 바이트 | 바이트를 하나씩 줄인 모든 접두에서 거절, 대상 객체 그대로 |
| 크기 | `MatchFormed` 를 실제 상한(팀 16 × 16 명, 티켓 합계 256 명)으로 채운 A 크기가 `kMaxMessageSize` 이하 |
| push 중계 | 다른 서버가 낸 중계를 받은 서버가 자기 빌드 번호와 B 로 클라이언트에 보내고, 클라이언트가 `Ok` 로 읽음. 모르는 열거자 이름이면 버리고 카운터 증가 |
| 소프트 폴백 제거(ⓒ) | `int32` 로 기록된 필드를 `vector<int32>` 로 읽으면 orphan 이 하나 생김 |

`MatchFormed` 의 `MaxCount` 를 모두 곱한 값(티켓 256 개 × 멤버 256 명)은 실제 상한이 아닙니다. 그 크기는 B 로도 60 KiB 를 넘습니다. 실제 상한은 `MatchmakingLimit`(팀 수 × 팀 인원)이 정합니다.

기존 서버 둘 테스트(`TestOnlineMultiServer.cpp`, `TestChatService.cpp`, `TestSocialService.cpp`, `TestMatchQueue.cpp`, `TestAdminService.cpp`, `TestServerBus.cpp`)는 동작이 바뀌지 않으므로 그대로 통과해야 합니다.

## 7. 측정(통합 때)

Release 로 빌드하고 같은 측정을 3~5 회 돌려 중앙값을 [검증 문서](../08_Verification.md) 에 적습니다. **판단 기준은 측정 전에 정합니다**: 코덱 비용이 버스 전송 왕복(`LocalServerBusHub` 기준)의 5 % 아래면 최적화하지 않습니다.

1. `ChatMessage` 짧은 글(10 자)과 512 자에서 A 대 B 의 쓰기 + 읽기 시간과 바이트 수. 벤치는 한 함수 안에서 같은 메시지를 10 만 번 돌립니다.
2. 혼합 배포 중의 경로: 모르는 필드가 하나 있는 바이트를 읽을 때(엄격 루프의 건너뛰기)와 없을 때의 차이.
3. 서버 둘(`LocalServerBusHub`, 가능하면 RESP 캐시 위 `EphemeralServerBus`)에서 초당 1k · 10k 채팅 메시지일 때 서비스 틱 p50 / p99 와 그중 코덱 비율(Tracy 구역 또는 `Stopwatch`).
4. 버스 타입마다 실제 상한으로 채운 A 바이트(6절 크기 테스트가 단언하는 값을 그대로 적습니다).

## 8. 위험

1. **기본값 폴백**: 없는 필드가 기본값이 되는 규칙이 폴백 금지 원칙과 부딪힙니다. 3.6 의 규칙으로 다룹니다.
2. **조용한 뜻 어긋남**: 모르는 필드를 버리므로 뜻이 바뀐 변경을 옛 빌드가 알아채지 못합니다. 의미 버전을 높이는 것을 사람이 기억해야 하는 틈이고, 형식으로 막지 못합니다.
3. **로그 폭주**: 배포 중 채팅 중계에서 경고가 메시지마다 나오지 않게 (타입, 필드)마다 한 번만 경고하고, 거절도 (타입, 결과, 버전)마다 한 번만 로그를 남깁니다.
4. **A 인코딩 버전**: `BinarySerializer` 의 인코딩 규칙을 바꾸면 섞인 동안 모든 버스 메시지와 캐시 레코드를 읽지 못합니다. 인코딩 버전을 높일 때는 한 배포 동안 옛 버전도 읽어야 한다는 것을 `BinarySerializer` 헤더에 적습니다.
5. **봉투 버전**: 4절과 같습니다.
6. **크기**: A 가 B 보다 커서 큰 메시지(`MatchFormed` 같은 것)가 `kMaxMessageSize` 를 넘을 수 있습니다. 넘으면 주제 정의의 `publish` 가 오류 로그와 false 를 돌려줍니다. 6절 크기 테스트로 잡습니다.
7. **중계 필드 유실**: 3.6 의 "중계 필드 유실" 규칙입니다.
8. **채팅 중계 비용**: 측정 전에는 알 수 없습니다(3.8, 7절).
9. **온라인 스키마 해시 한 번 바뀜**: 버스 전용 타입을 목록에서 빼면 클라이언트가 한 번 업데이트를 받습니다(3.7).
10. **push 중계의 이름 찾기**: 알림 열거자 이름을 바꾸면 섞인 동안 옛 빌드가 보낸 중계를 찾지 못합니다. 그 알림은 거절되고 카운터에 남습니다.
11. **버스 타입의 B 코덱**: 버스 전용 타입을 `MessageTypes` 에서 빼면 기동 때의 코덱 확인(`computeSchemaHash`)을 지나지 않습니다. 첫 발행에서 `FixedLayout::write` 가 멈추므로, 타입마다 주제 왕복 테스트가 그 확인을 맡습니다.

## 9. 남은 결정

없습니다. 앞 판의 열린 질문은 아래로 정했습니다(구현이 끝나면 결정 기록 5-9 로 옮깁니다).

1. 필수 필드 표시는 두지 않습니다(3.6).
2. 의미 버전이 다르면 거절합니다(3.3, 3.4).
3. push 중계는 이번 범위에 넣고 등록 테이블로 확인합니다(3.5).
4. `config.changed` 는 지웁니다(3.1).
5. 캐시 레코드는 버스와 같은 읽기, 영속 레코드는 모르는 필드 보관을 백로그 `F-3` 으로 둡니다(4절).
6. 의미 버전은 타입 안의 `kBusVersion` 입니다(3.4).
