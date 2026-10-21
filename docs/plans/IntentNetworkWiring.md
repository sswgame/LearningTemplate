# 의도 기록 · 원격 조종자의 남은 연결

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-13 · 진행: 3/4

입력 의도를 기록 · 재생하고 원격 폰을 조종하는 길의 서버 쪽 연결입니다. 재생 진입점(`IntentPlayback::start` · `startFromFile`)은 있습니다.

## 1. 결정

- 넷 입력의 형식은 의도 하나다 — 클라이언트 `ReplicationClient::sendIntent`, 서버 `ReplicationServer::popIntent`. 게임이 바이트를 직접 짓지 않는다.
- 원격 조종자는 받은 의도를 **사본으로 소유**한다(`RemoteControllerComponent` 의 틱 링). 서버의 받는 버퍼는 연결 목록(`vector`) 원소라 연결이 늘면 주소가 바뀌므로 빌려 주지 않는다 — 키트(또는 게임 루프)가 틱마다 꺼낸 의도를 `receiveIntent` 로 넣는다.

## 2. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| I1 | 입력 바이트 형식 고정(`sendIntent` · `popIntent`, 되풀이 틱은 발동 비트를 지움) | 코드 · 테스트(`NetClientServerTest.ControlIntentRoundTripsAsInput`), 빌드 미확인 |
| I2 | 서버 폰을 `RemoteControllerComponent` 로 — 버퍼 빌리기(`setReceiveBuffer`)를 지우고 사본 링(`receiveIntent` · `clearReceived`)으로 바꿨다. 서버 루프는 `popIntent( 연결, 틱, 의도, 정확함 )` 가 정확할 때 `receiveIntent( 틱, 의도 )` 를 부르고, 아니면 조종자가 마지막 의도를 되풀이한다 | 코드 · 테스트(`ControlTest.RemoteControllerReadsWindowBytes` 를 새 구조로), 빌드 미확인 |
| I3 | 의도 기록을 켜고 `.swintent` 로 쓰고 재생하는 개발 명령 — `intent.record [틱 수]` · `intent.save <파일>` · `intent.play <파일>`(콘솔 · 에디터 콘솔 · 명령줄 개발 명령) | 코드, 빌드 미확인 |
| I4 | 재생 UI(되감기 막대) · 킬캠 카메라 · 되감기 | 남음 — 재생은 같은 씬을 처음부터 같은 고정 프레임으로 돌릴 때만 같은 궤적이라, 되감기는 상태 스냅샷(`GameInstanceBase::captureSnapshot`)을 틱 간격으로 찍어 두고 가까운 것에서 의도를 다시 돌리는 설계가 먼저다. 킬캠은 리플레이 씬 위의 카메라 디렉터 프리셋 하나다 |

## 남은 것

- I4 설계(스냅샷 간격 · 메모리 상한 · 리플레이 씬 분리)를 이 문서 3절로 쓰고 착수한다. 폰을 쓰는 넷 테스트 게임이 생기면 I2 를 그 게임의 서버 루프에 잇는다(지금 0).
