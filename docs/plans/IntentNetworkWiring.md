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
| I4 | 재생 UI(되감기 막대) · 킬캠 카메라 · 되감기 — 들어간 것: 되감기 키 상태(`IntentRewindBuffer` · `IntentRewindConfig`, 3절). **남은 것**: 되감기 실행(키로 되돌린 뒤 목표 틱까지 빨리 돌리기), 재생 UI, 킬캠 프리셋 | 일부 코드 · 테스트(`IntentRewindTest.KeySnapshotsCoverTheRewindWindow`) |

## 3. 되감기 설계

- **키 상태**: 재생하는 동안 `IntentRewindConfig::_intervalTicks`(기본 60 틱 = 60 Hz 에서 1 초)마다 `GameInstanceBase::captureSnapshot( 게임플레이 목적 )` 의 바이트를 `IntentRewindBuffer` 에 넣는다.
  메모리 상한(`_maxMegabytes`, 기본 64)을 넘으면 오래된 키부터 버린다 — 되감을 수 있는 범위는 남은 키가 덮는 틱이다(언리얼 Replay 의 체크포인트 간격 · 보존 한도와 같은 두 값).
- **되감기(목표 틱 T)**: `findAtOrBefore( T )` 의 키로 `restoreSnapshot` 하고, 그 키의 틱부터 T 까지 기록된 의도를 같은 고정 프레임으로 다시 돈다(재생이 결정적이라 처음과 같은 궤적).
  되돌린 뒤 거기서 다시 돌면 미래가 갈라지므로 먼저 `discardAfter( T )` 한다. 다시 도는 틱 수는 간격 이하이고, 한 프레임에 다 돌지 못하면 여러 프레임에 나눠 돌며 화면은 멈춘 그림을 둔다.
- **리플레이 씬**: 되감기와 킬캠은 플레이 씬이 아니라 그 스냅샷으로 띄운 별도 씬에서 돈다 — 플레이 중인 상태 · 네트워크 세션을 건드리지 않는다. 킬캠은 그 씬의 카메라 디렉터 프리셋 하나(맞은 쪽을 보는 3인칭)다.
- **남은 선행**: 고정 프레임 N 틱을 그 자리에서 빨리 돌리는 엔진 진입점(시나리오 실행기의 고정 프레임 진행과 같은 것)과, 스냅샷으로 별도 씬을 띄우는 진입점.

## 남은 것

- I4 남은 것(위 3절의 실행 · UI · 킬캠). 폰을 쓰는 넷 테스트 게임이 생기면 I2 를 그 게임의 서버 루프에 잇는다(지금 0).
