# 넷 채널 등록 API 로 스키마 해시를 핸드셰이크 버전에 섞기

> 상태: 측정대기 · 선행: 없음 · 영역: network · 옛번호: D7-7 · 진행: 2/2

고정 배치형(B) 메시지는 메시지마다 스트림 머리의 64 비트 스키마 해시를 실어, 배치가 다른 빌드의 바이트는 읽기가 `SchemaMismatch` 로 거절했다. 계획은 [리플렉션 형식](ReflectionFormats.md) 3.3 · 3.10 · 5절 "게임 넷 연결".

## 들어간 것

- `NetMessageChannel`(`Source/Engine/Net`)이 종류 바이트마다 B 타입을 등록하고(`registerMessage<T>`), 봉인(`seal`)이 (종류, 스키마 해시)를 종류 순으로 기반 와이어 버전에 섞은 호스트 설정을 내준다. 배치가 다른 빌드는 핸드셰이크에서 `VersionMismatch` 로 먼저 거절된다.
  본문만 쓰고 읽는 길(`writeMessage` · `readMessage`)은 봉인한 채널에만 열리고, 머리 없는 바이트를 만드는 `FixedLayout::writeBody` · `readBody` 는 이 클래스만 부른다(friend) — 섞는 일을 따로 부를 자리가 없다.
- 첫 사용처: Mech · Horror 스냅샷은 `registerSnapshotMessage( channel, kind )` · `writeSnapshotMessage( channel, kind, writer )` 로 본문만 싣는다(머리 있는 `writeState( BitWriter& )` 는 지웠다).

## 남은 확인(통합에서 확인)

- 매니페스트 id `net-message-channel-test`(`NetMessageChannelTest.*` · `MechArenaTest.SnapshotRoundTripsAndRefusesOutOfRangeValues` · `AsymmetricHorrorTest.SnapshotRoundTripsAndRefusesOutOfRangeValues` 필터 — 케이스가 0개면 실패해 테스트가 있다는 것까지 증명한다). 전체 실행에 든 것은 개별 통과의 증거가 아니라 되돌렸다(2026-10-24 판정). 게임 두 프로세스 사이의 핸드셰이크 거절은 `NetMessageChannelTest` 가 배치가 다른 두 채널의 봉인 버전이 다름을 단언한다. 확인이 끝나면 통합 담당이 이 문서를 닫는다.

함정(교훈은 `NetMessageChannel.h` 머리 주석에 있다): 봉인한 설정이 아닌 설정으로 호스트를 연 상대와는 본문만 주고받으면 안 된다 — 채널은 연결 하나(그 설정으로 연 호스트)에 하나다.
