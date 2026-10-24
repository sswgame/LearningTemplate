# 클라이언트 예측 · 롤백과 관심 영역 복제

> 상태: 측정대기 · 선행: 없음 · 영역: network · 옛번호: D7-8 · 진행: 표

**남은 확인(통합에서 확인):** 매니페스트 id `rollback-prediction-lte-run`(아래 "남은 것"). 확인이 끝나면 통합 담당이 이 문서를 닫는다.

상태 동기화와 파괴 동기화는 있지만 입력 예측 · 서버 보정 · 롤백, 그리고 관심 영역(relevancy)으로 복제 대상을 거르는 층이 없었다. 설계는 결정 기록 5-12 `D7-8` 행으로 정했다(서버 권위 + 내 폰만 재시뮬레이션 + 허용 오차 비교, 64인 이하 30 Hz, 첫 게임 Shooter3D). 선행 잠복 결함 셋(`NetOwnerState`, `PredictionReconcileResult`, `RollbackConfig` 범위 거절)은 고쳤다.
네 단계 모두 코드가 들어갔고 빌드 · 테스트 · 실행 확인이 남았다(아래 "남은 것").

## 1. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| R1 | `MMOReplicator` 의 관심 영역 격자를 Core 부품으로 빼기 — `NetInterestGrid`(`Core/Network/Replication`, 들어오는 · 나가는 반경 히스테리시스와 반경 관계 거절을 한 곳에) | 끝남(빌드 · 테스트 전, `NetInterestGridTest`) |
| R2 | 클라이언트-서버 키트의 관심 영역 정책 `InterestReplicationPolicy` — 같은 `NetInterestGrid` 규칙, 보이는 목록은 게임 스레드가 `updateRelevancy` 로 정하고 스냅샷 워커는 읽기만, 자리 없는 엔티티는 모두에게 | 끝남(빌드 · 테스트 전, `InterestReplicationPolicyTest`) |
| R3 | Shooter3D 내 폰 예측 — 몸 이동을 순수 함수(`ShooterBodyMovementComponent::simulate`)로 빼고 `ShooterNetPawnComponent` 가 고정 틱으로 서버 권위 시뮬레이션 · `ClientPrediction` 기록 · `findOwnerState` 재조정 | 끝남(빌드 · 실행 전) |
| R4 | 보정을 화면에서 녹이는 시각 보정 — Core `PredictionSmoother`(반감기 지수 감쇠, 큰 보정은 바로), 몸 · 카메라는 `getDisplayFeetPosition` | 끝남(빌드 · 테스트 전, `PredictionSmootherTest`) |

## 남은 것

- 확인 대기: 매니페스트 id `shooter-net-jump-lte`. Shooter3D 전용 서버 + 클라이언트(`Server -game=Shooter3D` · `App -game=Shooter3D -gv_netConnectAddress=127.0.0.1:7777`)에 `-gv_netEmuPreset=lte` 를 걸고 움직여 보정 횟수(`getCorrectionCount`)와 화면이 튀지 않는지 본다.
- 서버의 조종 연결이 하나다(아레나에 플레이어 몸이 하나) — 연결마다 몸을 스폰하는 일은 Shooter3D 멀티플레이 판 설계와 함께 한다.
- 점프 눌림 쌓기는 들어갔다(2026-10-24, `wt/plannet`, 빌드 전): `ShooterNetPawnComponent::_bJumpPending` 이 틱이 없던 프레임의 눌림을 다음 입력 틱까지 쌓아 싣고 비운다. 확인: 위 `lte` 실행에서 짧게 눌러도 점프가 매번 나가는지.

함정: 월드 전체 롤백은 `NetworkExtensionIdeas`([후보](Candidates-network.md)) 쪽이다. 규모 방향은 `UdpSocketLayerScaling` 이다.
