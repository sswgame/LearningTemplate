# CS2 식 서브틱 입력

> 상태: 구현중 · 선행: 없음 · 영역: network · 옛번호: G-2 · 진행: 4/6

입력이 프레임 단위로만 보여 클릭한 순간의 시선이 아니라 프레임 끝의 시선으로 쏘고, 넷 입력의 보던 틱이 메시지마다 하나라 다시 보낸 옛 입력도 최신 틱으로 랙 보정했다. `RawInputEvent` · `NativeWindowEvent` 에 시각이 없었다(`MSG::time` 을 버림).

**함정**: 롤백 · 락스텝 키트에는 넣지 않는다 — 프레임 단위 결정성이 계약이다(`RollbackSession::kInputFormat` 은 스탬프 없음 그대로).

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| B1 | 사건 시각 — `RawInputEvent::_timeMicroseconds`(큐에 넣는 순간, `InputManager::postRawEvent`) · `_frameFraction`. 입력 리플레이 판 5(이벤트 배치가 바뀜) | 끝남(빌드 미확인) |
| B2 | 프레임 안 자리와 그 순간까지의 시선 누적 — `beginFrame` 이 창 [지금 − 델타, 지금] 으로 OS 이벤트의 자리를 채우고(가상 이벤트는 녹화된 자리 그대로, 가상 커서는 1), 누름마다 `InputPressInstant`(자리 · 그 앞까지의 마우스 원시 이동량 · 시각)를 적는다. 시계 주입은 `InputManager::setClock` | 끝남(빌드 미확인) |
| B3 | `InputMap::tryGetActionPressInstant` — 바인딩이 완성된 순간(조합 키는 늦은 슬롯), 바인딩 여럿이면 가장 이른 것. 누름 이벤트가 없는 바인딩(축 임계)만이면 false(지어내지 않는다). 폴링 장치는 프레임 끝(1). 테스트 `InputMapTest.ActionPressInstantCarriesFramePlaceAndLookBeforeIt` | 끝남(빌드 미확인) |
| B4 | 클릭 순간의 시선으로 발사 | 남음 |
| B5 | 넷 입력 항목마다 보던 틱 스탬프 — `NetClientServerMessage::kInputFormat` 스탬프 켬, 메시지 머리의 보던 틱 제거, `popInput` 이 그 입력의 스탬프로 `getClientViewTick` 을 정하고 서버가 [꺼내는 틱 − `_maxViewLagTicks`, 꺼내는 틱] 으로 자른다. `NetKitWireVersion::kClientServer` 5. 테스트 `NetClientServerTest.ViewTickTravelsWithEachInputAndIsClampedByTheServer` | 끝남(빌드 미확인) |
| B6 | (선택) Win32 입력 스레드 · `GetMessageTime` | 남음 |

## 남은 것

- **B4**: 조종 의도(`ControlIntent`)에 "이번 틱 첫 누름 순간의 조종 회전" 칸을 두고, 플레이어 조종자가 프레임 시작 회전 + 감도 × `InputPressInstant::_lookDeltaBefore` 로 채운다. Shooter3D `ShooterPlayerComponent::fireWeapon` 이 카메라 시선 대신 그 칸을 쓴다. 의도의 직렬화(`ControlIntent::write`, `kMaxSerializedBytes`)와 기록 형식이 함께 바뀐다. 조종자 쪽 파일(`IntentDevCommands.cpp` 등)이 다른 작업에서 바뀌는 중이라 이번에 손대지 않았다.
- **B6**: 먼저 `GetMessageTime` 해상도를 잰다(지금 시각은 창 스레드가 큐에 넣는 순간이다). 입력 스레드로 옮기면 에디터 · 콘솔이 먼저 소비하는 판정(`shouldDropNativeEvent`, 에디터 입력 캡처)을 우회하지 않게 한다.
- 빌드 · `EngineTest --test_filter=InputMapTest.*:InputManagerTest.*:VirtualInputTest.*:NetClientServerTest.*` 실행.
