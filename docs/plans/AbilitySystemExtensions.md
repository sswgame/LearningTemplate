# 어빌리티 시스템의 다음 조각

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-11 · 진행: 4/5

언리얼 GAS 에 있고 여기 없던 조각입니다. 에디터의 런타임 상태 패널(`AbilityDebugPanel`)은 먼저 들어갔습니다. 무적 API 는 따로 두지 않습니다(옛 D3-4).

## 1. 결정

- 걸린 이펙트가 대상에 주는 것(부여 태그 · 부여 어빌리티 · 모디파이어 집계)은 `engageActiveEffect` / `disengageActiveEffect` 짝 하나로만 붙고 뗀다. 걸 때 · 해제할 때 · 걸린 동안의 태그 조건이 바뀔 때가 모두 이 짝을 지나므로, 주는 것이 하나 늘어도 빠뜨릴 곳이 없다.
- 태그 변화의 알림은 `_ownedTagCounts` 하나에서 나온다(이펙트 · 어빌리티 · 이펙트 없는 태그 모두). 걸린 동안의 조건과 태그로 발동하는 어빌리티는 그 알림 하나에 건다(컴포넌트 생성자).

## 2. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| A1 | 이펙트가 주는 어빌리티(`GameplayEffectDef::_listGrantedAbility`, XML `<GrantAbility id=…/>`) — 걸린 동안만, 해제 · 멈춤에 거둔다. 즉시 이펙트에 적으면 로드 오류 | 코드 · 테스트(`EffectGrantsAbilitiesWhileActive`), 빌드 미확인 |
| A2 | 걸린 동안의 태그 조건(`_ongoingRequiredTags`, XML `<OngoingRequiredTag>`) — 못 맞추면 남되 멈춤(모디파이어 · 부여 태그 · 부여 어빌리티 · 주기 실행이 빠짐) | 코드 · 테스트(`OngoingTagRequirementsInhibitAndResumeAnEffect`), 빌드 미확인 |
| A3 | 태그가 생길 때 발동(`GameplayAbilityConfig::_ownedTagTriggerTags`, XML `<OwnedTagTrigger>`) | 코드 · 테스트(`OwnedTagAddedTriggersAnAbility`), 빌드 미확인 |
| A4 | 어트리뷰트 base 값을 세이브에 싣는 도우미(`AbilityAttributeSaveUtil`, `이름=값` 바이트, 없는 어트리뷰트는 오류) | 코드 · 테스트(`AttributeBaseValuesRoundTripThroughSaveBytes`), 빌드 미확인 |
| A5 | 큐를 데이터로 잇는 큐 매니저(큐 태그 → 프리팹 · 사운드) | 남음 |

## 남은 것

- A5: "game" 채널의 `GameplayCueEvent` 를 받는 게임 서비스 하나가 큐 표(XML: 큐 태그 → 프리팹 경로 · 오디오 이벤트, 단계 Added/Executed/Removed)를 읽어 대상 위치에 스폰 · 재생한다. 표에 없는 큐 태그는 로드 검증이 알린다(폴백 없음). 쓰는 게임(AbilityArena)의 큐 목록과 함께 넣는다.
- A1~A4 확인: 빌드 뒤 `AbilitySystemTest` 필터.
