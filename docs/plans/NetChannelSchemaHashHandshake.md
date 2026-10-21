# 넷 채널 등록 API 로 스키마 해시를 핸드셰이크 버전에 섞기

> 상태: 구현중 · 선행: 없음 · 영역: network · 옛번호: D7-7 · 진행: 1/2

고정 배치형(B) 메시지는 메시지마다 스트림 머리의 64 비트 스키마 해시를 실어, 배치가 다른 빌드의 바이트는 읽기가 `SchemaMismatch` 로 거절했다. 계획은 [리플렉션 형식](ReflectionFormats.md) 3.3 · 3.10 · 5절 "게임 넷 연결".

## 들어간 것

`NetMessageChannel`(`Source/Engine/Net`)이 종류 바이트마다 B 타입을 등록하고(`registerMessage<T>`), 봉인(`seal`)이 (종류, 스키마 해시)를 종류 순으로 기반 와이어 버전에 섞은 호스트 설정을 내준다. 배치가 다른 빌드는 핸드셰이크에서 `VersionMismatch` 로 먼저 거절된다.
본문만 쓰고 읽는 길(`writeMessage` · `readMessage`)은 봉인한 채널에만 열리고, 머리 없는 바이트를 만드는 `FixedLayout::writeBody` · `readBody` 는 이 클래스만 부른다(friend) — 섞는 일을 따로 부를 자리가 없다. 테스트는 `NetMessageChannelTest`(빌드 · 실행 전).

## 남은 것

- 첫 사용처: Mech · Horror 스냅샷(`MechArenaSnapshot` · `HorrorSnapshot`, 지금은 머리 있는 `FixedLayout::write`)을 채널에 등록해 본문만 싣는다. 그 게임의 호스트 설정을 `seal` 이 내준 것으로 바꾸는 일이 함께다.

함정: 봉인한 설정이 아닌 설정으로 호스트를 연 상대와는 본문만 주고받으면 안 된다 — 채널은 연결 하나(그 설정으로 연 호스트)에 하나다.
