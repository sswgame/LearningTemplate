# 의도 기록 · 원격 조종자의 남은 연결

> 상태: 측정대기 · 선행: 없음 · 영역: gameplay · 옛번호: D4-13 · 진행: 4/4

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
| I4 | 재생 UI(되감기 막대) · 킬캠 카메라 · 되감기 — 키 상태(`IntentRewindBuffer`), 실행(`IntentRewindSession` · `ControlSceneRewindWorld`, 기록을 키 틱 번째부터 틀기 `IntentPlayback::start` 의 `firstIndex`), 막대(`IntentReplayBar` + `Resource/common/ui/replaybar.ui.xml`), 킬캠 프리셋(`shooter.cameras.xml` 의 `killcam`) | 코드 · 테스트(`IntentRewindTest.*`, `ControlTest.PlaybackStartsFromAGivenTickOfTheTrack`), 빌드 미확인 |

## 3. 되감기 설계

- **키 상태**: 재생하는 동안 `IntentRewindConfig::_intervalTicks`(기본 60 틱 = 60 Hz 에서 1 초)마다 `GameInstanceBase::captureSnapshot( Snapshot )` 의 바이트를 `IntentRewindBuffer` 에 넣는다(`IntentRewindSession::captureIfDue` — 그 틱의 키가 이미 있으면 건너뛴다).
  메모리 상한(`_maxMegabytes`, 기본 64)을 넘으면 오래된 키부터 버린다 — 되감을 수 있는 범위는 남은 키가 덮는 틱이다(언리얼 Replay 의 체크포인트 간격 · 보존 한도와 같은 두 값). 그 범위 앞으로 가는 목표는 오류로 거절한다(`findEarliestSeekableTick`).
- **되감기(목표 틱 T)**: `IntentRewindSession::beginSeek( T )` 가 `findAtOrBefore( T )` 의 키로 `restoreSnapshot` 하고(`IIntentRewindWorld::restoreKey`), 그 키의 틱부터 T 까지 기록된 의도를 같은 고정 프레임으로 다시 돈다(재생이 결정적이라 처음과 같은 궤적).
  되돌린 뒤 T 뒤의 키는 버린다(`discardAfter( T )` — 거기서 다시 돌면 미래가 갈라진다). T 가 지금 틱보다 앞이 아니면 되돌리지 않고 그대로 앞으로 돈다.
  다시 도는 틱 수는 간격 이하이고, `stepSeek( 최대 틱 수 )` 가 한 프레임에 돌 수 있는 만큼만 돌고 남은 틱 수를 돌려준다. 막대(`IntentReplayBar`)는 프레임마다 `seekTicksPerFrame` 틱씩 나눠 돌며, 그동안 `shouldHostStep` 은 거짓이라 호스트는 자기 틱을 돌리지 않고 화면은 멈춘 그림이다.
- **장면 짝**: `ControlSceneRewindWorld` 가 장면 하나를 세계로 보이게 한다 — 키는 게임 인스턴스 스냅샷, 한 틱은 `GameObjectManager::update( 고정 프레임 )` 한 번(시나리오 실행기의 고정 프레임 진행과 같다). 장면을 비울 때 조종 시스템이 떨어졌다 다시 붙으므로 되돌린 뒤 틱을 `ControlSystem::setTick( 키 틱 )` 으로 맞추고,
  기록(`ControlIntentHistory`)의 트랙을 키 틱 번째부터 다시 싣는다. 재생의 틱 번호는 **재생을 시작한 틱을 0 으로** 센 값이다.
- **리플레이 씬**: 되감기와 킬캠은 플레이 씬이 아니라 그 스냅샷으로 띄운 별도 씬에서 돈다 — 플레이 중인 상태 · 네트워크 세션을 건드리지 않는다. `ControlSceneRewindWorld` 는 게임 인스턴스와 오브젝트 관리자를 인자로 받으므로, 별도 씬의 인스턴스 · 관리자를 넘기면 같은 길로 돈다. 그 씬을 띄우는 일은 게임의 흐름(씬 관리자)이 한다.
- **킬캠**: 그 씬의 카메라 디렉터 프리셋 하나다. Shooter3D 는 `shooter.cameras.xml` 에 `killcam`(대상 정면 3인칭 · 얕은 초점 · 비네트, 들어갈 때 0.25 초 블렌드)이 있다. 대상을 맞은 쪽으로 바꾸고 이 프리셋을 켜는 것은 리플레이 씬을 띄우는 게임 코드의 몫이다.

## 남은 것

- 통합에서 확인: `EngineTest --test_filter=IntentRewindTest.*:ControlTest.*Playback*:ControlTest.ServerLoopDrivesRemotePawnFromReplicationIntents`. 막대 문서의 이름 · 명령이 코드와 맞는지도 `IntentRewindTest.ReplayBarSeeksPausesAndSteps` 가 잡는다.
- 화면 확인(GPU): 막대를 띄운 게임에서 슬라이더를 끌 때 장면이 키 틱으로 돌아가 나눠 돌고 멈춘 그림이 나오는지, `-gv_cameraPreset=killcam` 이 보이는지. 게임이 막대를 여는 연결(리플레이 씬을 띄우는 흐름)은 그 게임이 생길 때 잇는다.
- 폰을 쓰는 넷 서버 루프(I2)는 `ControlTest.ServerLoopDrivesRemotePawnFromReplicationIntents` 가 같은 모양으로 돌려 본다. 실제 게임의 서버 루프에 잇는 일은 그 게임이 생길 때 한다.
