# MMO 갱신 확인을 NetConnection 전달 통지로

> 상태: 끝 · 선행: 없음 · 영역: network · 옛번호: G-8 · 진행: 4/4

MMO 키트는 클라이언트가 받은 갱신 틱을 확인 메시지(`kUpdateAck`, 가장 새 틱 + 32 틱 비트)로 돌려보내고 서버가 그것으로 잃은 갱신을 가렸다. 연결 계층은 이미 패킷마다 확인(ack + 32 비트)을 주고받으므로 같은 정보를 키트가 한 번 더 보낸 것이다. 언리얼은 이 자리를 패킷 ACK · NAK 통지(`PacketNotify`)로 푼다.

## 1. 설계

- **연결(`NetConnection`)**: `sendNotifiedMessage( pData, size, tag )` 가 비신뢰 메시지에 값 하나를 단다. 보낸 패킷 기록(`SentPacket`)이 실은 통지의 값과 종류(첫 바이트)를 패킷당 8 개(`kMaxNotifiedPerPacket`)까지 기억한다. 판정은 메시지마다 한 번 — 패킷이 확인되면 `Delivered`, 빠른 재전송이 잃은 것으로 보거나 확인 없이 패킷 창 밖으로 밀려나거나 싣지 못하고 줄에서 나이 상한을 넘기면 `Lost` 다. 잃은 것으로 본 뒤 늦게 온 확인은 판정을 바꾸지 않는다. `drainDeliveryNotices` 로 꺼낸다.
- **호스트(`INetPeers`)**: `sendNotifiedMessage( connectionID, … )` 가 창구에 들고, `drainInbound` 가 연결 ID 를 채운 통지를 `NetInbound::_listDeliveryNotice` 로 넘긴다(샤드 묶음은 연결 ID 를 접는다).
- **라우터**: 처리기가 `getNotifiedKindMask()` 로 "이 종류는 내가 통지를 달아 보낸다" 를 등록하고(받는 종류 마스크와 따로 겹침을 거절), `pump` 가 사건 뒤 · 메시지 앞에 `onDeliveryNotice` 로 나눠 준다. 보낸 쪽이 등록하지 않은 통지는 세고(`getUnclaimedNoticeCount`) 첫 번째는 오류 로그를 남긴다(조용히 버리지 않는다).
- **MMO**: 서버는 갱신을 서버 틱을 값으로 단 통지 메시지로 보내고, 판정을 관찰자의 판정 테이블(`_updateOutcome`, 64 틱 링)에 적는다. `kUpdateAck` 와 클라이언트의 `sendAck` 는 지웠다. 와이어가 바뀌므로 `NetKitWireVersion::kMMO` 를 4 로 올렸다.

## 2. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| A1 | Core 전달 통지 — `NetConnection` · `INetPeers` · `NetHost` · `NetHostShardSet` · `NetInbound` · 라우터. `NetworkTest.NotifiedMessagesReportDeliveredOrLostExactlyOnce` | 끝남 |
| A2 | MMO 가 통지로 판정 — `kUpdateAck` · `MMOClientView::sendAck` 제거, `kMMO` 4 | 끝남 |
| A3 | MMO 테스트 옮기기 — 직접 호스트 테스트는 `drainInbound` 의 통지를 넘기고, `LostUpdateIsResentSoonAfterTheLossNotice` 로 이름을 바꿨다 | 끝남 |
| A4 | Debug `full-build` 뒤 `CoreTest` 의 `NetworkTest.*` 45 개와 `EngineTest` 의 `NetMMOTest.*` 가 통과했다(2026-10-23). 손실 뒤 다시 보기까지 3 틱(10 회 모두 같음, 옛 확인 방식은 4 틱)이라 테스트 상한을 6 틱으로 낮췄다 | 끝남 |

## 3. 주의

- 블랙아웃이 패킷 32 개를 넘으면 그 앞 패킷은 확인 비트 밖이라 빠른 재전송이 아니라 창 밖 밀림(256 패킷) 또는 MMO 의 64 틱 상한으로 잃음이 정해진다. 테스트의 30 틱 블랙아웃은 패킷 32 개 안이라 이 경로를 타지 않는다고 짐작한다(확인 안 함). 더 긴 블랙아웃에서 늦으면 연결 쪽 "확인 비트 밖으로 밀린 미확인 패킷은 잃음" 규칙을 더하는 것을 먼저 본다.
- 같은 모양: 다른 키트가 "받았다" 메시지를 따로 보내는지는 보지 않았다(클라이언트-서버 키트의 처리한 입력 틱은 의미가 달라 대상이 아니다 — 서버가 시뮬레이션한 틱이다).
