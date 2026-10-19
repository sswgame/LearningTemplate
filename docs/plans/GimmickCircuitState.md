# 기믹 회로 상태 구조 계획 (2026-10-17)

> 상태: 구현중 · 선행: ReflectionFormats · 영역: engine · 옛번호: 없음 · 진행: 4/5

[리플렉션 형식 계획](ReflectionFormats.md) 9절 단계 2(`GimmickCircuit` 을 B 로)의 설계입니다. 그 단계는 "상태 바이트가 씬 · 프리팹 · 레벨 세이브에 남는다"는 이유로 보류됐고, 이 문서는 그 보류를 푼 구조를 남깁니다.
단계 2-a~2-c(상태 구조체 · 컴포넌트 · 번들)와 2-e(문서 · 헤더 주석)는 끝났고, 2026-10-20 에 Debug 테스트와 Release 측정도 끝냈습니다(아래 "검증 기록"). 남은 단위는 2-d(GPU 확인)뿐입니다.

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
- **같은 결함이 다른 곳에.** `ElementGrid.cpp:380` 의 `cellBytes` 가 읽는 쪽에서 크기를 손으로 계산하는 같은 구조입니다. 이 계획의 범위 밖이라 따로 조사합니다.

## 검증 기록 (2026-10-20, `wt/gimmick-verify`)

- **테스트(Debug).** main 에서 갈라진 이 브랜치에서 `full-build`(경고 0) 뒤 기믹 · 원소 규칙 45 케이스, `ReflectionTest`, `EngineTest_NoGPU_Shard1~5` 가 통과했습니다.
  노드를 더한 회로가 옛 세이브를 이음(`GimmickTest.StateMatchesNodesByID`), 같은 id 의 종류 · 상태 버전 · 슬롯 수 변경 거절(같은 케이스와 `StateSaveLoadResetRoundTrip` 의 `shortSlot`),
  저장 · 로드 뒤 N 스텝 해시가 같음(바이너리 `StateSurvivesBinaryObjectStateAndContinues`, XML 은 이번에 더한 `StateSurvivesXMLObjectStateAndContinues` — 타이머 누적 시간 `float32` 가 9 자리로 쓰여 120 스텝 뒤에도 같은 해시).
  XML 은 따로 결함이 없었습니다(직렬화 쪽 범위 밖 보고 없음).
- **이전 · 이후 비교는 결정성 테스트로 갈음했습니다.** 옛 코드의 기록 테스트가 없어 스텝마다 출력 비트를 옛 코드와 견줄 수 없고, 이 구조 변경은 상태를 저장 · 복원하는 길만 바꿨습니다.
  같은 입력을 두 번 돌려 해시를 견주는 `GimmickTest.SameInputsSameStateHash`(`runA` · `runB`)와 위의 N 스텝 왕복 해시 일치가 통과해, 저장 · 복원이 스텝 결과를 바꾸지 않음은 확인했습니다. 옛 코드 대비 바이트 동일성은 보증하지 않습니다.
- **측정(Release).** 쇼케이스 회로 5 개와 프리팹 13 개 모두 한 조각, 가장 큰 244 바이트(상한 3,920 의 6 %), `saveState` ≤ 60 ns, 첫 호출 뒤 할당 0. 표와 방법은 [검증 문서](../08_Verification.md)의 기믹 회로 상태 항목.
  조각 수가 스텝에 따라 바뀌지 않아 "varuint 길이로 엔티티가 다시 보내짐" 걱정은 이 데이터에서 실현되지 않습니다. 측정 케이스는 `Test/EngineTest/GameFramework/Gimmick/TestGimmickStateBench.cpp`(`GimmickStateBenchTest`, 호스트 스위트).
- **XML 읽기의 모르는 요소는 오류가 아니라 건너뜁니다**(코드 읽기로 확인, 실행 테스트는 따로 없음). 오브젝트 · 컴포넌트 읽기는 `setAllowUnknownProperties( true )` 로 모르는 요소를 건너뛰고 `runSchemaMigrateStep` 이 버린 이름을 로드마다 한 번 경고합니다
  (`ObjectStateSerializer.cpp`, 정책 `SchemaOrphanPolicy::Ignore`, 고정 테스트 `ReflectionSerializationTest.OrphanOnlyPolicyDiffersByFormat`). 그래서 옛 `_stateBytes` 가 남은 쿠킹 산출물이나 게임 폴더도 오류 없이 읽히고 그 요소만 버려집니다. 바이너리 오브젝트 상태도 같은 규칙입니다.
  엄격한 `XMLSerializer::deserialize` 는 모르는 요소가 있으면 실패하지만 오브젝트 읽기는 그 경로가 아닙니다.
