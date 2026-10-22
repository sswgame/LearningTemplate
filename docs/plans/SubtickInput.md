# CS2 식 서브틱 입력

> 상태: 끝 · 선행: 없음 · 영역: network · 옛번호: G-2 · 진행: 6/6

입력이 프레임 단위로만 보여 클릭한 순간의 시선이 아니라 프레임 끝의 시선으로 쏘고, 넷 입력의 보던 틱이 메시지마다 하나라 다시 보낸 옛 입력도 최신 틱으로 랙 보정했다. `RawInputEvent` · `NativeWindowEvent` 에 시각이 없었다(`MSG::time` 을 버림).

**함정**: 롤백 · 락스텝 키트에는 넣지 않는다 — 프레임 단위 결정성이 계약이다(`RollbackSession::kInputFormat` 은 스탬프 없음 그대로).

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| B1 | 사건 시각 — `RawInputEvent::_timeMicroseconds`(큐에 넣는 순간, `InputManager::postRawEvent`) · `_frameFraction`. 입력 리플레이 판 5(이벤트 배치가 바뀜) | 끝남(Debug 테스트 확인) |
| B2 | 프레임 안 자리와 그 순간까지의 시선 누적 — `beginFrame` 이 창 [지금 − 델타, 지금] 으로 OS 이벤트의 자리를 채우고(가상 이벤트는 녹화된 자리 그대로, 가상 커서는 1), 누름마다 `InputPressInstant`(자리 · 그 앞까지의 마우스 원시 이동량 · 시각)를 적는다. 시계 주입은 `InputManager::setClock` | 끝남(Debug 테스트 확인) |
| B3 | `InputMap::tryGetActionPressInstant` — 바인딩이 완성된 순간(조합 키는 늦은 슬롯), 바인딩 여럿이면 가장 이른 것. 누름 이벤트가 없는 바인딩(축 임계)만이면 false(지어내지 않는다). 폴링 장치는 프레임 끝(1). 테스트 `InputMapTest.ActionPressInstantCarriesFramePlaceAndLookBeforeIt` | 끝남(Debug 테스트 확인) |
| B4 | 클릭 순간의 시선으로 발사 — `ControlIntent::_bTriggerLook` · `_triggerYaw` · `_triggerPitch`(선택 필드, 없으면 비트 하나. `write` · `read` · `quantize` · `==` 에 포함), 폰의 `_triggerLookButton`(Shooter3D 는 `Fire`)이 어느 버튼의 순간인지 정하고, `PlayerControllerComponent::produceIntent` 가 프레임 시작의 조종 회전 + 시선 액션 값 × `InputPressInstant::computeLookFraction` 으로 채운다(누름 순간을 모르는 바인딩은 프레임 끝의 조종 회전). `ShooterPlayerComponent` 는 막 누른 틱에만 그 방향으로 쏜다. `ControlIntentHistory::kVersion` 2 · `NetKitWireVersion::kClientServer` 6. 테스트 `ControlTest.TriggerLookCarriesTheControlRotationAtTheClick` · `IntentRoundTripsThroughBits` · `InputMapTest.ActionPressInstantCarriesFramePlaceAndLookBeforeIt` | 끝남(Debug 테스트 확인) |
| B5 | 넷 입력 항목마다 보던 틱 스탬프 — `NetClientServerMessage::kInputFormat` 스탬프 켬, 메시지 머리의 보던 틱 제거, `popInput` 이 그 입력의 스탬프로 `getClientViewTick` 을 정하고 서버가 [꺼내는 틱 − `_maxViewLagTicks`, 꺼내는 틱] 으로 자른다. `NetKitWireVersion::kClientServer` 5. 테스트 `NetClientServerTest.ViewTickTravelsWithEachInputAndIsClampedByTheServer` | 끝남(Debug 테스트 확인) |
| B6 | (선택) Win32 입력 스레드 · `GetMessageTime` — 해상도를 측정했더니(`GetTickCount64` 를 1.5 초 훑음) 변화 간격이 15 ~ 31 ms(평균 15.8 ms)로 60 Hz 프레임 하나(16.7 ms)와 같아 프레임 안 위치를 가리지 못한다. 지금 시각(큐에 넣는 순간의 단조 시계)이 더 세밀하므로 하지 않는다([결정 기록](../09_Decisions.md) 5-6) | 끝남(하지 않기로 함) |

## 확인한 것 · 못 한 것

- 확인함(Debug): `EngineTest --test_filter=ControlTest.*:InputMapTest.*:InputManagerTest.*:VirtualInputTest.*:NetClientServerTest.*` — 조종 의도의 발동 시선이 비트로 왕복하고, 클릭 앞의 이동만 담기며, 프레임 끝 조종 회전과 다르다.
- 확인함(Debug DX12): Shooter3D 시나리오 `firelook`(클릭이 사격으로 이어짐 — 발동 시선 분기를 지나가도 쏜다) · `weaponswitch` · `autoplay` · `fieldofview` 통과.
- 확인 못 함: 사격이 실제로 클릭한 순간의 방향으로 나가는지는 시나리오로 보지 못했다(방향 계산은 `ControlTest` 가 보고, 게임 쪽 분기는 `firelook` 이 발사만 본다). `ShooterPlayerComponent::onTick` 은 게임 모듈이라 `EngineTest` 가 링크하지 않는다.
- 하지 않음: B6(근거는 위 표와 [결정 기록](../09_Decisions.md) 5-6).
