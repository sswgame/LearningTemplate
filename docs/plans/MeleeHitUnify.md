# 근접 판정 한 길로

> 상태: 측정대기 · 선행: 없음 · 영역: gameplay · 옛번호: 없음 · 진행: 3/3

근접 타격 판정이 둘로 갈라져 있었습니다. 애니메이션 알림 `HitWindow` 는 칼날을 쓸어(처음 닿은 하나만) `onHitReceived` 로 알렸고, ActionCombat 킷의 `MeleeHitboxComponent` 는 2D 콜라이더의 겹침 이벤트로 접촉을 따로 세고 `UnitStatsComponent::takeDamage` 를 직접 불렀습니다. "이미 맞힘" · 공격자 제외 · 피해 전달을 두 곳이 따로 들고 있었고, 어느 쪽도 클라이언트에서 피해를 막지 않았습니다.

## 1. 정한 것

- 근접 판정은 `Engine/Character/Hit/MeleeHitWindow.h` 의 `MeleeHitWindowUtil::sweep` 하나입니다. 칼날 표본점마다 지난 위치 → 지금 위치, 그리고 지금 칼날 자체를 쓸고, 토막마다 걸린 바디를 **모두** 봅니다(`CharacterHitUtil::sweepAll` — 처음 닿은 하나에서 멈추면 이미 맞힌 오브젝트가 뒤의 표적을 가립니다). 공격자와 그 아래 오브젝트, 창에서 이미 맞힌 오브젝트는 건너뜁니다.
- `HitWindow` 처리기는 소켓으로 칼날을 만들고 맞음을 기록하기만 합니다. `MeleeHitboxComponent` 는 오브젝트 기준 칼날(`setBlade` 두 점 · 두께)을 만들고 창을 열고 닫기만 하며, 물리 뒤 틱이 스윕을 틱 직후로 미룹니다. 겹침 접촉 목록(`getOverlapCount`)과 겹침 이벤트 처리는 지웠습니다.
- 맞음은 `onHitReceived` 한 길로 갑니다. ActionCombat 유닛은 `UnitStatsComponent::onHitReceived` 에서 기본 피해 × 히트 존 배율을 반올림해 `takeDamage` 합니다.
- **서버 권위.** `sweep` 은 `MeleeHitAuthority` 를 기본값 없이 받습니다. `Authority`(혼자 하는 게임 · 서버)만 맞음을 전달하고 `Predicted`(클라이언트)는 찾아서 돌려주기만 합니다. 프로세스의 권위는 `MeleeHitWindowUtil::getProcessAuthority` 한 곳이 `GameNetSession` 역할에서 읽습니다.
- 합치지 않은 것: MechArena(`MechArenaWorld::stepMelee`)는 롤백을 위해 물리 없이 자기 상태로 도는 결정적 시뮬레이션이고, AbilityArena(`ArenaMeleeStrikeAbility`)는 사거리 안의 가장 가까운 적을 고르는 어빌리티 규칙입니다. 둘 다 칼날 스윕이 아니라서 이 길에 넣지 않습니다.

## 2. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| M1 | `MeleeHitWindowUtil::sweep` · `CharacterHitUtil::sweepAll`, `HitWindow` · `MeleeHitboxComponent` 옮기기, 겹침 길 지우기, `UnitStatsComponent::onHitReceived` | 끝 |
| M2 | 서버 권위(`MeleeHitAuthority`, 클라이언트는 전달하지 않음), 테스트 `AnimationNotify2DTest.MeleeHitWindowDeliversOnlyWithAuthority` | 끝 |
| M3 | 클라이언트의 예측 맞음 주장(`MeleeHitClaim`)을 서버가 자기 스윕 기록(`MeleeHitLedger`)으로 확인하는 경로 — 3절. 테스트 `AnimationNotify2DTest.MeleeHitLedgerConfirmsClaimsAgainstServerSweeps` | 코드 · 테스트, 빌드 미확인 |

## 3. 클라이언트 맞음 주장의 서버 확인 (M3)

- **문제**: 서버는 자기 시점의 스윕으로만 맞음을 정한다. 지연된 월드를 보고 휘두른 클라이언트는 맞혔다고 보지만, 서버의 칼날이 닿는 순간 표적은 이미 비켜서 있을 수 있다. 반대로 클라이언트가 지어낸 맞음을 서버가 믿으면 안 된다.
- **구조**: `Engine/Character/Hit/MeleeHitLedger.h` — 서버가 `MeleeHitLedger::ensureFor( manager )` 로 켠 씬에서 권위 스윕(`MeleeHitWindowUtil::sweep`, `Authority`)이 칼날 표본점 줄 · 두께 · 피해를 프레임 번호로 적는다(맞지 않은 스윕도). 클라이언트와 혼자 하는 게임은 장부를 켜지 않아 기록하지 않는다.
  클라이언트는 `MeleeHitClaim`(서버 프레임 · 표적 번호)을 **입력 바이트 안에** 실어 보낸다 — 키트의 와이어 형식을 바꾸지 않는다(`MeleeHitClaim::write` · `read`). 서버의 폰 루프가 입력 틱을 그 입력을 처리한 서버 프레임으로 옮기고, 지연 보상(`LagCompensationHistory::sampleAt`)이 보던 틱으로 되감은 표적 자리를 `claimHit` 에 넘긴다.
- **판정**: 표적 구(중심 · 반지름)가 주장 프레임 안팎(`kClaimSlackFrames`)의 기록된 칼날 토막(표본점마다 지난 → 지금, 칼날 자체)에서 칼날 두께 + 반지름 + 여유(`kClaimToleranceMeters`) 안이면 `Confirmed`. 스윕 기록이 없으면 `NoSweep`(휘두르지 않은 쪽 · 너무 오래된 주장), 스윕은 있지만 닿지 않으면 `OutOfBlade`.
  인정된 주장만 기록된 피해 · 충격량으로 `onHitReceived` 에 전달하고, 창에서 이미 맞힌 오브젝트는 건너뛴다(한 창에 오브젝트 하나는 한 번).
- **남은 연결**: 근접을 네트워크로 겨루는 게임이 생기면 그 게임의 서버 루프가 위 두 호출(입력 틱 → 프레임 옮김, `claimHit`)을 잇는다. 그 게임이 아직 없어 키트 쪽 메시지는 더하지 않았다.
