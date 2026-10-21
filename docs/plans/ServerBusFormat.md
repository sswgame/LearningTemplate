# 서버 버스 메시지 형식 계획 (2026-10-17)

> 상태: 끝 · 선행: ReflectionFormats · 영역: network · 옛번호: F-2 · 진행: 7/7

온라인 서버끼리 주고받는 버스 메시지를 고정 배치형(B)에서 자기 기술형(A, 의미 버전 머리 + `BinarySerializer`)으로 되돌렸습니다. 단계 0~6 이 모두 main 에 병합됐습니다(`e0325da6c`, 브랜치 `wt/feat11`).
결정은 [결정 기록](../09_Decisions.md) 5-9 "서버 버스는 A 입니다" 에 있고, 이 문서는 코드 주석이 가리키는 절(3.1 · 3.3~3.6 · 6)의 규칙만 남깁니다.
서버 사이에는 핸드셰이크가 없고 배포 중에는 옛 빌드와 새 빌드가 함께 돌므로 버스는 혼합 빌드를 견뎌야 합니다. 클라이언트 메시지는 핸드셰이크가 같은 빌드를 보장하므로 B 를 유지합니다([ReflectionFormats](ReflectionFormats.md) 5절).

**근본 원인**: 버스 본문을 "같은 빌드끼리만 읽는다"는 B 의 전제 위에 두었는데 버스에는 그 전제를 보장하는 핸드셰이크가 없고, 주제와 본문 타입의 짝이 선언 하나에 묶여 있지 않았습니다.

## 구현된 곳

| 내용 | 위치 |
|---|---|
| 바이트 전송과 타입 있는 앞, 주제 정의, 혼합 빌드 코덱 | `Source/GameFramework/Base/Online/Bus/` (`ServerBus.h` · `ServerBusTopic.h` · `MixedBuildCodec.h`) |
| 컨텍스트 플래그 둘, `deserializeExactVersion` | `SerializeContext.h`, `BinarySerializer.*` |
| push 중계 | `Base/Online/Identity/OnlinePushRelay.*`, 키트 서버의 `OnlinePushRelayEntry` 등록 |
| 제재 주제 `ServiceSanctionChanged`, 캐시 읽기 | `ServiceSanction.h`, `ServiceRecordUtil.h` |
| 테스트 | `ServerBusTest.*` · `ServerBusMixedBuildTest.*` (`Test/EngineTest/GameFramework/Online/`), `ReflectionSerializationTest.*` (`TestReflectionSerialization.cpp`) |

`config.changed` 는 구독처가 없어 지웠습니다(서버는 `OnlineServerRuntime` 이 주기로 다시 읽습니다). 봉투 `[버전 u8][서버 id u64][순번 u64]`(`EphemeralServerBus`)는 바꾸지 않는 고정 형식입니다. 바꾸면 섞인 동안 모든 버스 메시지가 버려집니다.

## 3. 설계의 규칙

### 3.1 주제가 본문 타입을 가진다

- `ServerBusTopic<T>`(고정 이름)와 `ServerBusTopicFamily<T>`(접두사 + 접미사)가 이름과 본문 타입을 묶습니다. 쓰기는 `publish( ServerBus&, const T& )`, 읽기는 `read( const ServerBusMessage&, T& )`이고 둘 다 `[[nodiscard]]` 입니다(읽기는 `ServerBusReadResult`).
- 신호 주제(`liveops.changed`, `sd.changed`)도 `PROPERTY` 없는 `REFLECT` 타입을 본문으로 씁니다.
- 원시 바이트를 키트가 쓰지 못하게 나눴습니다. `IServerBusTransport` 는 호스트와 서버 조립만 가지고, 키트 의존성에는 `ServerBus` 만 넘깁니다. `ServerBusMessage` 의 필드는 숨겨져 본문은 주제 정의만 읽습니다.

### 3.3 읽기 정책

버스 읽기는 엄격 태그 루프 하나입니다. 소프트 읽기와 이관 레지스트리를 지나지 않습니다.

1. 의미 버전이 다르면 거절합니다(이관 없음). (타입, 결과, 받은 버전)마다 한 번 오류 로그, 거절 수는 `ServerBusReadLog::getRejectedCount`.
2. 타입에 없는 이름의 필드는 건너뛰고 (타입, 필드)마다 한 번 경고합니다(`warnSkippedFieldOnce`).
3. 타입에 있는 필드인데 값을 읽지 못하면(모르는 열거자 · 변환 불가) 거절합니다.
4. 스트림에 없는 필드는 타입의 기본값입니다.
5. 읽은 값이 `Min` · `Max` · `MaxCount` 를 지키는지 확인합니다. 버스 타입은 B 코덱이 반드시 있어야 합니다(없으면 `FixedLayout::write` 가 멈춥니다).

**모든 깊이에서 같은 정책.** 중첩 구조체는 언제나 엄격 `deserialize` 로 읽히므로, `SchemaOrphanPolicy` 값 하나로는 최상위 필드에만 듣습니다. 그래서 `SerializeContext` 플래그를 둘로 나눴습니다.

| 플래그 | 켜면 | 끄면 |
|---|---|---|
| `setSkipUnknownProperties` | 타입에 없는 이름은 건너뛰고 한 번 경고 | 실패 |
| `setSkipUnreadableProperties` | 읽지 못한 아는 필드는 건너뛰고 한 번 경고 | 실패 |

버스와 캐시 레코드의 컨텍스트(`MixedBuildCodec::getReadContext`)는 앞의 것만 켭니다. 오브젝트 상태처럼 `setAllowUnknownProperties( true )` 를 쓰는 곳은 두 플래그를 함께 켜는 축약으로 그대로입니다.
버전 붙은 바이너리 읽기의 orphan 정책은 두 플래그가 모두 켜졌을 때만 `Ignore` 이고, 앞의 것만 켠 컨텍스트는 `deserializeExactVersion` 을 씁니다. 텍스트 직렬화기는 이 플래그를 읽지 않습니다.
소프트 경로의 숨은 폴백도 지웠습니다. 기록 타입이 다르고 변환이 실패하면 예전에는 읽힌 만큼 받았습니다(`int32` 0 을 `vector<int32>` 로 읽어 빈 목록으로 성공). 지금은 orphan 으로 남깁니다.

**위험(형식으로 막지 못함).** 새 필드가 다른 필드의 뜻을 바꾸는 변경은 옛 빌드가 알아채지 못합니다. 의미 버전을 높이는 것을 사람이 기억해야 합니다(B 의 `kBaseVersion` 과 같은 틈).

### 3.4 의미 버전

혼합 빌드를 견디려면 스키마 해시를 쓸 수 없어서 `serializeVersioned` 의 버전 머리를 의미 버전으로 씁니다. 타입 안의 `static constexpr uint32 kBusVersion` 이고, `ServerBusTopic` 이 읽으며 없으면 `static_assert` 로 멈춥니다.
다르면 이관 레지스트리를 보지 않고 거절하므로 레코드 버전 수열(`kChatRecordVersion` 등)과 묶이지 않습니다. A 의 인코딩 버전(스트림 머리 위 8 비트)과는 다릅니다.
`BinarySerializer` 의 인코딩 규칙을 바꾸면 섞인 동안 모든 버스 메시지와 캐시 레코드를 읽지 못하므로, 인코딩 버전을 높일 때는 한 배포 동안 옛 버전도 읽어야 합니다.

### 3.5 push 중계

예외는 push 중계입니다. 옛 봉투는 보낸 서버 빌드의 알림 번호와 B 를 실어 받는 서버의 빌드와 어긋났습니다.
봉투는 `OnlinePushRelay`(`push.<서버 id>`)의 A 이고 계정 id · 알림 열거형 FQN · 열거자 이름 · 본문 FQN · 본문 A 를 싣습니다. 받는 서버는 키트 서버 `.cpp` 가 등록한 `OnlinePushRelayEntry`(등록 목록 `ServiceFactoryList`)로 (열거형, 열거자 이름)을 찾아 본문 FQN 을 확인하고, 본문 A 를 읽어 **자기 빌드의 B** 로 다시 써서 클라이언트에 보냅니다.
항목이 없거나 본문 타입이 다르거나 읽지 못하면 (열거형, 열거자)마다 한 번 오류 로그와 카운터를 남기고 버립니다. 알림 열거자 이름을 바꾸면 섞인 동안 옛 빌드가 보낸 중계는 거절됩니다. 그래서 클라이언트는 언제나 접속한 서버 빌드의 B 를 받습니다.

### 3.6 필드 변경의 호환 규칙

"옛 → 새"는 옛 빌드가 보낸 것을 새 빌드가 읽는 경우, "새 → 옛"은 그 반대이고 배포 중에는 둘 다 일어납니다.

| 변경 | 규칙 |
|---|---|
| 필드 추가 | 허용(옛 → 새는 기본값, 새 → 옛은 버림). 기본값이 "값 없음"을 뜻해야 하고, 받는 쪽이 못 다루면 의미 버전을 높입니다 |
| 필드 삭제 | 옛 빌드가 기본값을 받아도 되는 필드만 한 배포로. 아니면 먼저 읽지 않게 배포하고 다음 배포에서 지웁니다 |
| 필드 이름 변경 | 한 배포로 하지 않습니다. 새 이름 필드를 더해 둘 다 쓰고 다음 배포에서 옛 필드를 지우거나 의미 버전을 높입니다 |
| 필드 타입 변경 | 하지 않습니다. 새 이름의 필드를 더합니다 |
| 열거자 추가 · 이름 변경, 범위(`MaxCount` 등) 변경 | 옛 빌드가 거절합니다(3.3 의 3 · 5). 버려도 되는 주제가 아니면 두 배포로 나눕니다 |
| 중첩 타입의 FQN 변경 | 하지 않습니다(기록 타입 해시가 달라 거절) |
| 뜻만 바뀜 | 의미 버전을 높입니다. 섞인 동안 두 방향 모두 거절됩니다 |

**중계 필드 유실.** 메시지가 여러 서버를 거치면 새 필드는 옛 빌드 홉을 한 번이라도 지나면 기본값이 됩니다(예: `mm.queue` → 옛 매처 → `MatchFormed`). 기본값 규칙은 그 값이 거쳐 가는 모든 홉의 끝에 적용됩니다.
기본값 폴백이 폴백 금지 원칙과 부딪히므로 `PROPERTY( Required )` 는 두지 않습니다(새 필드에 붙이면 의미 버전을 높이는 것과 같습니다).

## 4. 같은 결함이 다른 곳에 있는가

- 캐시 레코드(`readCache`)는 배포 중 다른 빌드가 읽으므로 버스와 같은 읽기(`MixedBuildCodec::decode`)로 바꿨습니다.
- 영속 레코드(`readDurable`)는 모르는 필드를 보관했다가 다시 쓰는 구조가 먼저입니다. 그 전까지 모르는 필드를 거절합니다(조용히 지우지 않고 실패로 드러냄). 구조는 들어갔고(ServiceRecordUtil::readDurableKeepingUnknown · encodeKeepingUnknown) 호출부 옮기기는 [PersistentRecordUnknownFields](PersistentRecordUnknownFields.md)(옛 F-3)에 있습니다.

## 6. 테스트

혼합 빌드는 필드 이름이 겹치는 두 타입(`BusTestNoticeOld` · `BusTestNoticeNew`, `ServerBusTestTypes.h`)에 쓴 바이트를 다른 쪽으로 읽어 흉내 냅니다(A 스트림에는 타입 FQN 이 없고 필드를 이름 해시로 짝짓기 때문).
중첩 타입이 바뀌는 경우는 같은 이름의 두 `TypeInfo` 를 서로 다른 `TypeRegistry` 에 등록해 `SerializeContext::setTypeRegistry` 로 나눕니다(`ReflectionTest`).
확인하는 것: 필드 추가의 두 방향, 중첩 원소의 필드 추가 · 모르는 열거자(혼합 빌드 컨텍스트와 엄격 읽기의 차이), 모르는 열거자(같은 열거형 바이트에서 열거자 이름 해시를 덮어써 실제 경로를 탐), 의미 버전 다름, 범위 밖, 잘린 바이트(모든 접두에서 거절), push 중계, 소프트 폴백 제거.
크기 테스트는 `MatchFormed` 를 실제 상한(팀 16 × 16 명, 티켓 합계 256 명)으로 채운 A 크기가 `kMaxMessageSize` 이하임을 단언합니다. `MaxCount` 를 모두 곱한 값은 실제 상한이 아닙니다(B 로도 60 KiB 초과). 실제 상한은 `MatchmakingLimit` 이 정합니다.

## 남은 확인

- **테스트 실행**: 통합(integ6)에서 Debug 빌드는 세웠지만 위 테스트(`ReflectionSerializationTest.*` · `ServerBusTest.*` · `ServerBusMixedBuildTest.*` · `OnlineServiceHostTest.*` · `RemoteConfigTest.*`, 키트 테스트, `-L nogpu` · `-L lint`)의 통과 기록은 찾지 못했습니다. 한 번 돌려 확인합니다.
- **해지 알림(보안) 서버 둘 테스트**: 같은 경로를 `ServerBusMixedBuildTest.AnAddedFieldIsSkippedByTheOldBuild` 가 타입 하나로 보고 있고, 계정 서버 둘 고정물로 다시 확인하는 테스트는 쓰지 않았습니다.
- **측정(Release, 3~5 회 중앙값을 [검증 문서](../08_Verification.md) 에 적음)**: 판단 기준은 코덱 비용이 버스 전송 왕복(`LocalServerBusHub`)의 5 % 아래면 최적화하지 않는 것입니다. 측정할 것은 ① `ChatMessage` 짧은 글(10 자)과 512 자의 A 대 B 쓰기 + 읽기 시간과 바이트, ② 모르는 필드가 있을 때와 없을 때 읽기 차이, ③ 서버 둘(`LocalServerBusHub`, 가능하면 RESP 위 `EphemeralServerBus`)에서 초당 1k · 10k 채팅일 때 서비스 틱 p50 / p99 와 코덱 비율, ④ 버스 타입마다 실제 상한으로 채운 A 바이트. 결과가 나쁘면 고칠 곳은 A 읽기 경로이고 형식을 B 로 돌리지 않습니다.
- **알려진 위험**: 로그 폭주는 (타입, 필드) · (타입, 결과, 버전)마다 한 번으로 막았습니다. 버스 전용 타입은 `MessageTypes` 에서 빠져 기동 때의 코덱 확인을 지나지 않으므로 타입마다 주제 왕복 테스트가 그 확인을 맡습니다. 이 변경으로 온라인 스키마 해시가 한 번 바뀌어 옛 클라이언트는 한 번 업데이트를 받습니다.
