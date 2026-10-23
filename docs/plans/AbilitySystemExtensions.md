# 어빌리티 시스템의 다음 조각

> 상태: 측정대기 · 선행: 없음 · 영역: gameplay · 옛번호: D4-11 · 진행: 5/5

**남은 확인(통합에서 확인):** `AbilitySystemTest` 필터와 AbilityArena 피격음(아래 "남은 것"). 확인이 끝나면 통합 담당이 이 문서를 닫는다.

언리얼 GAS 에 있고 여기 없던 조각입니다. 에디터의 런타임 상태 패널(`AbilityDebugPanel`)은 먼저 들어갔습니다. 무적 API 는 따로 두지 않습니다(옛 D3-4).
다섯 단위의 코드와 테스트가 들어갔고 빌드 · 테스트 확인만 남았습니다(확인되면 이 문서를 닫는다).

## 1. 결정

- 걸린 이펙트가 대상에 주는 것(부여 태그 · 부여 어빌리티 · 모디파이어 집계)은 `engageActiveEffect` / `disengageActiveEffect` 짝 하나로만 붙고 뗀다. 걸 때 · 해제할 때 · 걸린 동안의 태그 조건이 바뀔 때가 모두 이 짝을 지나므로, 주는 것이 하나 늘어도 빠뜨릴 곳이 없다.
- 태그 변화의 알림은 `_ownedTagCounts` 하나에서 나온다(이펙트 · 어빌리티 · 이펙트 없는 태그 모두). 걸린 동안의 조건과 태그로 발동하는 어빌리티는 그 알림 하나에 건다(컴포넌트 생성자).
- 큐 태그 → 연출은 데이터(카탈로그의 `<GameplayCue>`)가 정하고, 큐를 내는 길(`dispatchCues` → `playCueDef`)은 하나다. 가장 가까운 조상 태그의 정의가 맡는다. 보여 줄 것이 없는 정의는 로드 오류이고, 클라이언트 코드가 없는 빌드(서버)는 연출을 내지 않는다.

## 2. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| A1 | 이펙트가 주는 어빌리티(`GameplayEffectDef::_listGrantedAbility`, XML `<GrantAbility id=…/>`) — 걸린 동안만, 해제 · 멈춤에 거둔다. 즉시 이펙트에 적으면 로드 오류 | 코드 · 테스트(`EffectGrantsAbilitiesWhileActive`), 빌드 미확인 |
| A2 | 걸린 동안의 태그 조건(`_ongoingRequiredTags`, XML `<OngoingRequiredTag>`) — 못 맞추면 남되 멈춤(모디파이어 · 부여 태그 · 부여 어빌리티 · 주기 실행이 빠짐) | 코드 · 테스트(`OngoingTagRequirementsInhibitAndResumeAnEffect`), 빌드 미확인 |
| A3 | 태그가 생길 때 발동(`GameplayAbilityConfig::_ownedTagTriggerTags`, XML `<OwnedTagTrigger>`) | 코드 · 테스트(`OwnedTagAddedTriggersAnAbility`), 빌드 미확인 |
| A4 | 어트리뷰트 base 값을 세이브에 싣는 도우미(`AbilityAttributeSaveUtil`, `이름=값` 바이트, 없는 어트리뷰트는 오류) | 코드 · 테스트(`AttributeBaseValuesRoundTripThroughSaveBytes`), 빌드 미확인 |
| A5 | 큐 표(`GameplayCueDef` — 프리팹 · 오디오 이벤트 · 오프셋 · 해제 때도 · 2D 여부, XML `<GameplayCue>`). AbilityArena 의 피격음을 코드(`ArenaProjectileComponent`)에서 표(`GameplayCue.Hit` → `ProjectileHit`)로 옮겼다 | 코드 · 테스트(`GameplayCueTableResolvesTheNearestAncestor`), 빌드 · 소리 미확인 |

## 남은 것

- 확인: 빌드 뒤 `AbilitySystemTest` 필터, AbilityArena 를 띄워 피격음이 그대로 나는지(무적으로 막힌 피격은 이제 소리가 없다 — 큐가 나가지 않는다).
