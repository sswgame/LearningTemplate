# 클라이언트 예측 · 롤백과 관심 영역 복제

> 상태: 구현중 · 선행: 없음 · 영역: network · 옛번호: D7-8 · 진행: 표

상태 동기화와 파괴 동기화는 있지만 입력 예측 · 서버 보정 · 롤백, 그리고 관심 영역(relevancy)으로 복제 대상을 거르는 층이 없다. 설계는 결정 기록 5-12 `D7-8` 행으로 정했다(서버 권위 + 내 폰만 재시뮬레이션 + 허용 오차 비교, 64인 이하 30 Hz, 첫 게임 Shooter3D). 선행 잠복 결함 셋(`NetOwnerState`, `PredictionReconcileResult`, `RollbackConfig` 범위 거절)은 고쳤다.

## 1. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| R1 | `MMOReplicator` 의 관심 영역 격자를 Core 부품으로 빼기 — `NetInterestGrid`(`Core/Network/Replication`, 들어오는 · 나가는 반경 히스테리시스와 반경 관계 거절을 한 곳에) | 끝남(빌드 · 테스트 전, `NetInterestGridTest`) |
| R2 | 클라이언트-서버 키트(`ReplicationServer`)가 `NetInterestGrid` 로 연결마다 복제 대상을 거른다 | 남음 |
| R3 | Shooter3D 의 내 폰을 `ClientPrediction` + `findOwnerState` 로 예측한다 | 남음 |
| R4 | 보정 오차를 부드럽게 녹이는 시각 보정(언리얼 smoothing) | 남음 |

## 남은 것

- R2: `ReplicationServer` 의 스냅샷은 지금 모든 엔티티를 연결마다 싣는다. 연결의 관찰 중심(내 폰)을 정하는 창구와, 보이지 않게 된 엔티티를 클라이언트에 알리는 메시지(MMO 의 `kLeave` 자리)가 함께 필요하다 — 와이어가 바뀌므로 `NetKitWireVersion::kClientServer` 를 올린다.
- R3 · R4 는 결정 기록 5-12 `D7-8` 행의 설계대로.

함정: 월드 전체 롤백은 `NetworkExtensionIdeas`([후보](Candidates-network.md)) 쪽이다. 규모 방향은 `UdpSocketLayerScaling` 이다.
