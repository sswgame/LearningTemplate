# 기믹 회로 상태 구조 계획 (2026-10-17)

> 상태: 구현중 · 선행: ReflectionFormats · 영역: engine · 옛번호: 없음 · 진행: 4/5

[리플렉션 형식 계획](ReflectionFormats.md) 9절 단계 2(`GimmickCircuit` 을 B 로)의 설계입니다. 그 단계는 "상태 바이트가 씬 · 프리팹 · 레벨 세이브에 남는다"는 이유로 보류됐고, 이 문서는 그 보류를 푼 구조를 남깁니다.
단계 2-a~2-c(상태 구조체 · 컴포넌트 · 번들)와 2-e(문서 · 헤더 주석)는 끝났고, 남은 단위는 2-d(GPU 확인)와 아래 "남은 확인"입니다.

**정한 것(사용자 확정).**

- 씬 · 프리팹 · 레벨 세이브에 남는 컴포넌트 데이터는 A(자기 기술형, `BinarySerializer` 와 XML 텍스트)로 씁니다.
- 체크포인트 · 롤백 · 넷 스냅샷은 B(`FixedLayout`)로 씁니다. 체크포인트는 프로세스 안 값이라 직렬화 자체가 없습니다.
- 두 형식은 같은 `REFLECT` 선언 하나에서 나옵니다. 손 코덱은 남기지 않습니다.
- 구조체가 바뀌어도 세이브를 읽습니다(2.6).
- 지금 변환할 출시 데이터가 없으므로 옛 `_stateBytes` 를 읽는 변환 코드는 두지 않습니다. 기믹 XML 14 개의 빈 `<_stateBytes />` 는 지웠고 `Resource` 에 남은 것이 없습니다.

**근본 원인**: 상태 바이트의 크기를 쓰는 쪽과 읽는 쪽이 따로 손으로 계산했고, 회로 해시 하나가 상태 전체의 수용 여부를 정했습니다.

## 구현된 곳

| 내용 | 위치 |
|---|---|
| 상태 구조체(`GimmickNodeState` · `GimmickCircuitState`), 상한 상수 | `Source/GameFramework/Base/Gameplay/Gimmick/GimmickCircuitState.h` |
| 회로 `saveState` · `loadState`(`GimmickStateMatch`), 상한 검사 | `GimmickCircuit.*`, `GimmickNodeRegistry.*`(`registerKind` 의 슬롯 상한) |
| 컴포넌트의 `_state` · 체크포인트 값 · `applySavedState` | `GimmickCircuitComponent.*` |
| 번들(`REFLECT` + `FixedLayout`), 넷 조각 바이트 | `GimmickStateBundle.*`, `Kits/Feature/Network/NetClientServer/GimmickSnapshotUtil.*` |
| 테스트 | `Test/EngineTest/GameFramework/Gimmick/` 의 `TestGimmick.cpp`(`StateLimitsMatchDeclaredMaxCount` 포함) · `TestGimmickScene.cpp`(XML · 바이너리 왕복) · `TestGimmickState.cpp`(번들 · 롤백 · 조각) |

같은 이름의 `_stateBytes` 필드(`ObjectCopyBuffer`, `FractureNetworkSync` 등)는 기믹과 관계없으므로 고치지 않았습니다.

## 2. 새 구조

### 2.6 구조체가 바뀔 때

| 변경 | A(씬 · 프리팹 · 레벨 세이브) | B(롤백 · 넷) |
|---|---|---|
| 필드 추가 | 옛 데이터는 선언한 기본값으로 읽힙니다 | 스키마 해시가 바뀌어 다른 빌드의 바이트는 `SchemaMismatch` 입니다. 같은 빌드끼리만 오가므로 문제가 없습니다 |
| 필드 이름 변경 | `PROPERTY( Alias = "old" )` | 해시가 바뀝니다(리다이렉트는 해시에 넣지 않습니다, RenameRedirects 규칙 5) |
| 값 계산이 필요한 변환 | 소유 타입(`sw::GimmickCircuitComponent`)의 기록 버전을 올리고 `SW_SCHEMA_MIGRATION( id, sw::GimmickCircuitComponent, 옛 버전, fn )` 을 등록합니다. 등록이 없으면 오류로 멈춥니다 | 해당 없음 |
| 회로 데이터 변경(노드 추가 등) | 노드 id 로 짝지어 이어 읽습니다(8절) | 정확히 맞는 상태만 받습니다 |

`GimmickNodeState` 의 의미가 바뀌는 변경(예: 비트 마스크를 두 필드로 나눔)은 기본값으로 메울 수 없으므로 종류의 상태 버전(`GimmickNodeKind::_stateVersion`)을 올리거나 변환 함수가 필요합니다. 지금은 출시 데이터가 없어 변환을 등록하지 않았습니다.

## 8. 구현에서 바꾼 것(2026-10-18, `design-reviewer` 검토 반영)

처음 설계와 다르게 구현한 것만 적습니다. 커밋은 `wt/feat10` 의 2-a · 2-b · 2-c 입니다. 빌드와 테스트는 통합 때 한 번에 돌립니다.

- **노드 id 로 짝짓는 상태.** `GimmickNodeState` 가 `_id`(id 해시) · `_kind`(종류 이름 해시) · `_stateVersion` · 노드별 실수 · 정수 슬롯 목록을 갖고, 회로 전체의 `_layoutHash` 와 평평한 슬롯 목록은 없앴습니다.
  회로 해시 하나에 읽기를 걸면 노드 하나만 고쳐도 그 회로(와 그 프리팹을 쓰는 모든 씬)의 옛 세이브가 실패하기 때문입니다. `GimmickCircuit::loadState` 는 `GimmickStateMatch`(`Exact` · `NodeSetChanged` · `Mismatch`)와 받아들이는 한계를 받습니다.
  `Mismatch` 는 같은 id 인데 종류 · 상태 버전 · 슬롯 수가 다르거나 같은 id 가 둘일 때뿐입니다. 새 노드는 처음 상태, 저장에만 있는 노드는 버립니다.
- **종류의 상태 버전.** `GimmickNodeKind::_stateVersion`(기본 1). 슬롯 수만 대조하면 같은 수 안에서 뜻이 바뀐 변경을 잡지 못합니다.
- **상한.** 노드 수 1,024(`GimmickCircuitState::kMaxNodeCount`, `populate` 가 오류), 종류마다 슬롯 32(`GimmickNodeState::kMaxSlotCount`, `registerKind` 가 거절). 회로 전체 슬롯 상한은 Delay 하나가 정수 20 개라 근거가 없어 두지 않았습니다.
  `PropertyInfo` 에 `MaxCount` 메타데이터가 없어 C++ 상수를 함께 두고, 상한에서 B 쓰기가 성공하고 하나 넘으면 실패하는지를 `GimmickTest.StateLimitsMatchDeclaredMaxCount` 가 대조합니다.
- **저장 상태 적용은 한 함수.** `GimmickCircuitComponent::applySavedState` 를 `onPostLoad` · `onBeginPlay` · `restoreCheckpoint` 가 지납니다. `NodeSetChanged` 는 경고 뒤 이어 가고, `Mismatch` 는 오류를 남기고 회로를 비워 돌지 않습니다(조용한 처음 상태 시작을 없앰).
  롤백 · 넷은 `restoreState`(`Exact` 만, 아니면 바꾸지 않고 false)이고 `GimmickStateBundle::apply` 는 건너뛴 수를 돌려 의미를 부르는 쪽이 정합니다. 체크포인트는 값입니다.
- **저작 저장 문맥(검토 ⓕ)은 옛 `C-53`.** 애노테이션 · 파서 · `SerializeContext` · 씬 저장 경로를 함께 고쳐야 하고 리플렉션 작업과 파일이 겹쳐 이 단계에서 하지 않았습니다. 플레이 중에만 의미 있는 필드는 `PROPERTY( RuntimeState )` 로 표시해 저작 데이터에서 뺍니다.

## 남은 확인

- **2-d GPU 확인.** `gpu-verifier` 가 쇼케이스 씬(`Resource/game/empty/maps/gimmickshowcase.scene.xml`)을 열고 캡처해 문 · 엘리베이터 · 무버가 움직이고 오류 로그가 없는지 봅니다. 실행 기록을 찾지 못했습니다.
- **테스트 실행.** 위 테스트 파일과 `-L nogpu` 의 통과 기록을 찾지 못했습니다(커밋 메시지는 빌드 · 테스트를 통합 때로 미뤘습니다). CTest 로 한 번 돌려 확인합니다. 확인할 것: 노드 하나를 더한 회로가 옛 세이브를 이어 읽음, 같은 id 의 슬롯 수 변경이 거절됨, 플레이 중 저장 · 로드 뒤 N 스텝 상태 해시가 끊지 않은 쪽과 같음(XML 은 `float32` 가 비트까지 같은지: 텍스트 쓰기가 9 자리를 지켜야 합니다. 모자라면 직렬화 쪽 결함이라 범위 밖으로 보고).
- **이전 · 이후 비교.** 같은 입력으로 옛 코드와 새 코드의 스텝마다 출력 비트 · 액추에이터 값이 같은지(바이트가 달라 해시 비교는 쓰지 않음). 옛 코드의 기록 테스트를 먼저 커밋한 기록은 찾지 못했습니다. 비교가 부담이면 `computeStateHash` 를 쓰는 결정성 테스트(`runA` · `runB`)가 통과하는 것으로 갈음할지 사용자에게 묻습니다.
- **측정(통합 때, Release).** 쇼케이스 씬 회로 5 개와 프리팹 13 개를 0 · 600 · 36,000 스텝 돌린 뒤 `FixedLayout::write( state )` 바이트 수와 조각 수(`kChunkPayloadBytes` 로 나눔)를 기록하고, 가장 큰 값이 `kMaxCircuitBytes`(3,920 바이트) 안인지 봅니다(Delay 의 절대 스텝 번호가 varuint 길이를 늘립니다). 노드 하나가 대략 10~18 바이트라 노드 200 개 안팎에서 조각 상한에 닿고, 넘는 회로는 `appendEntities` 가 "싣지 못한 회로" 로 셉니다.
  varuint 길이가 상태에 따라 달라 조각 수가 바뀌면 그 회로의 엔티티가 모두 다시 보내집니다. 기능은 맞고 대역폭만 늘어나므로 측정 결과를 보고 판단합니다. `storeState` 는 스텝을 돈 틱마다 부르므로 같은 판에서 `saveState` 의 시간 · 할당을 3~5 회 중앙값으로 잽니다(목록 용량을 이어 쓰므로 첫 틱 뒤 할당 0 이 기대값). 결과는 [검증 문서](../08_Verification.md)에 적습니다.
- **XML 읽기의 모르는 요소.** 모르는 요소를 만나면 건너뛰는지 오류인지 확인하지 못했습니다. 쿠킹 산출물이나 다른 게임 폴더에 옛 `_stateBytes` 가 남아 있으면 드러납니다.
- **같은 결함이 다른 곳에.** `ElementGrid.cpp:380` 의 `cellBytes` 가 읽는 쪽에서 크기를 손으로 계산하는 같은 구조입니다. 이 계획의 범위 밖이라 따로 조사합니다.
