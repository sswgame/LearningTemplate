# 넷 채널 등록 API 로 스키마 해시를 핸드셰이크 버전에 섞기

> 상태: 끝 · 선행: 없음 · 영역: network · 옛번호: D7-7 · 진행: 2/2

고정 배치형(B) 메시지는 메시지마다 스트림 머리의 64 비트 스키마 해시를 실어, 배치가 다른 빌드의 바이트는 읽기가 `SchemaMismatch` 로 거절했다. 계획은 [리플렉션 형식](ReflectionFormats.md) 3.3 · 3.10 · 5절 "게임 넷 연결".

## 들어간 것

- `NetMessageChannel`(`Source/Engine/Net`)이 종류 바이트마다 B 타입을 등록하고(`registerMessage<T>`), 봉인(`seal`)이 (종류, 스키마 해시)를 종류 순으로 기반 와이어 버전에 섞은 호스트 설정을 내준다. 배치가 다른 빌드는 핸드셰이크에서 `VersionMismatch` 로 먼저 거절된다.
  본문만 쓰고 읽는 길(`writeMessage` · `readMessage`)은 봉인한 채널에만 열리고, 머리 없는 바이트를 만드는 `FixedLayout::writeBody` · `readBody` 는 이 클래스만 부른다(friend) — 섞는 일을 따로 부를 자리가 없다.
- 첫 사용처: Mech · Horror 스냅샷은 `registerSnapshotMessage( channel, kind )` · `writeSnapshotMessage( channel, kind, writer )` 로 본문만 싣는다(머리 있는 `writeState( BitWriter& )` 는 지웠다).

## 확인한 것

- 빌드 · 테스트(`NetMessageChannelTest`, Mech · Horror 스냅샷 테스트): 코드는 2026-10-22 에 들어갔고, 그 뒤 통합 파동 B · C 의 Debug `nogpu` 전체 실행(NEXT 인계 기록)이 통과했다. 이 테스트들은 `EngineTest` 안에 있어 그 실행에 든다. 개별 필터 결과는 따로 남기지 않았다.
- 남는 실행 확인은 없다. 게임 두 프로세스 사이의 핸드셰이크 거절은 `NetMessageChannelTest` 가 배치가 다른 두 채널의 봉인 버전이 다름을 단언한다.

함정(교훈은 `NetMessageChannel.h` 머리 주석에 있다): 봉인한 설정이 아닌 설정으로 호스트를 연 상대와는 본문만 주고받으면 안 된다 — 채널은 연결 하나(그 설정으로 연 호스트)에 하나다.
