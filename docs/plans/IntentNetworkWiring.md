# 의도 기록 · 원격 조종자의 남은 연결

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-13 · 진행: 1/3

입력 의도를 기록 · 재생하고 원격 폰을 조종하는 길의 서버 쪽 연결입니다. 재생 진입점(`IntentPlayback::start` · `startFromFile`)은 있습니다.

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| I1 | 입력 바이트의 형식을 의도 하나로 고정 — 클라이언트 `ReplicationClient::sendIntent`(`ControlIntent::write` → `sendInput`), 서버 `ReplicationServer::popIntent`(`popInput` → `ControlIntent::read`, 되풀이 틱은 발동 비트를 지움). 게임이 바이트를 직접 짓지 않으므로 양쪽 형식이 어긋날 수 없다 | 코드 · 테스트(`NetClientServerTest.ControlIntentRoundTripsAsInput`), 빌드 미확인 |
| I2 | 서버 폰을 `RemoteControllerComponent` 로 몰기 | 남음 — 아래 |
| I3 | 의도 기록을 켜고 `.swintent` 를 쓰는 명령줄 · 에디터 단추, 재생 UI · 킬캠 카메라 · 되감기 | 남음 — 호출하는 쪽의 일 |

## 남은 것

- I2 설계 메모: `RemoteControllerComponent` 는 `NetInputReceiveBuffer` 를 빌려 틱으로 읽는데, 서버의 받는 버퍼는 `ReplicationServer::ClientState`(`vector` 원소)라 연결이 늘면 주소가 바뀐다 — 그 포인터를 빌려 주면 안 된다. 서버 게임 루프가 틱마다 `popIntent` 로 꺼낸 의도를 조종자에 넣는 길(밀어 넣기)로 바꾸고 버퍼 빌리기를 지우는 쪽이 한 길이다. 폰을 쓰는 넷 테스트 게임이 생기면(지금 0) 그 게임과 함께 한다.
