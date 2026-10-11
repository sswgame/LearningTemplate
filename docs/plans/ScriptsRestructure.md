# Scripts 개편 계획

`Scripts/` 전체(파이썬 183 파일 · 약 38,000 줄)를 구조 문제로 훑은 결과와 진행 순서다. 근거는 [백로그](../06_Backlog.md) "Scripts 정리의 남은 후보" 와
[Scripts README](../../Scripts/README.md) 의 규칙이다. 끝나면 이 문서를 지운다.

## 측정한 현재 상태 (2026-10-11, 16 스레드 PC, 다른 빌드가 도는 중)

커밋 훅을 임시 인덱스로 흉내 내 `REFLECT` 헤더 하나(`Source/Engine/Audio/AudioMixerDesc.h`)를 바꾼 커밋으로 쟀다. 훅 전체 4.2 s 중 파일 단위 게이트(이 프로세스) 합은 0.7 s 이고,
나머지는 하위 프로세스로 함께 도는 트리 전체 게이트 중 가장 긴 것이 정한다.

| 트리 전체 게이트(훅 안, 동시 실행) | 시간 | 단독 실행 |
|---|---|---|
| `CheckConfigReference` | 3.2 s | 2.4 ~ 2.6 s |
| `CheckDuplicateTypeNames` | 3.1 s | 2.0 s |
| `CheckEngineLayers` | 2.8 s | — |
| `CheckModuleTargets` | 2.3 s | — |
| `CheckDataFileReferences` | 1.6 s | — |
| `CheckGlobalVariableKinds` | 1.4 s | — |

**바닥은 게이트 하나가 아니라 넷이 비슷한 높이로 만든다.** 백로그가 적은 "`CheckConfigReference` 를 파일 단위로 나누기" 만 하면 훅은 거의 줄지 않는다
(다음 게이트가 3.1 s). 프로파일을 보면 넷 모두 일이 아니라 도구 비용이 크다.

- `CheckEngineLayers`: `common.normalizePath` 가 include 마다 `Path` 객체를 만든다(12 만 번, 프로파일 기준 누적 2.5 s).
- `CheckConfigReference`: `Source/` 를 두 번 걷고(`rglob` 넷) 헤더를 두 번 읽는다. 전역 변수 매치마다 파일 전체를 다시 가린다. `Path.relative_to` 가 0.5 s.
- `CheckDuplicateTypeNames`: 줄마다 네임스페이스 스택 전체를 `all()` 로 훑는다(100 만 번).
- `CheckModuleTargets`: include 마다 모듈 76 개를 `startswith` 로 차례로 훑는다(177 만 번).

## 찾은 구조 문제와 우선순위

| 순위 | 문제 | 효과 | 비용 | 처리 |
|---|---|---|---|---|
| 1 | 트리 전체 게이트 넷의 도구 비용(위 표) | 훅 바닥 | 작음(게이트마다 수십 줄) | 1 단계 |
| 2 | 훅이 게이트별 시간을 찍지 않는다 — 바닥을 재려면 따로 도구를 짜야 했다 | 다음 측정 | 아주 작음 | 1 단계 |
| 3 | `CheckOutParameterNames` 가 `CheckCodeConventions` 의 `Naming/OutParameter` 와 같은 주제의 게이트 하나 | 게이트 · CTest 항목 하나 감소 | 작음 | 2 단계 |
| 4 | 끝난 폴더 재배치의 이동기 둘(Engine · GameFramework). 티어 표 맞추기(`--sync-tier`)만 아직 쓰인다 | 죽은 스크립트 870 줄 | 작음 | 3 단계 — 티어 맞추기는 `RunEngineLayerGraph --apply` 로 옮긴다(그래프 계산이 이미 거기 있다) |
| 5 | `CheckFunctionVocabulary` · `CheckOutParameterNames` 가 주석 가리기를 `common.CodeText` 대신 손으로 한다 | 중복 도우미 | 작음 | 2 단계에서 하나는 사라진다, 남은 하나는 후보 |
| 6 | `CheckConfigReference` 를 파일 단위로(바뀐 구조체만) | 1 단계 뒤 다시 잰다 | 큼(생성 문서가 페이지 단위라 구조체 → 페이지 역색인이 필요) | 1 단계 결과로 판단 |
| 7 | `dev/MoveEditorState.py` — 옛 자리의 에디터 로컬 상태를 옮기는 PC 마다 한 번짜리 이동기 | 죽은 스크립트 후보 | 작음 | 보류 — 다른 PC 의 로컬 상태(추적 안 되는 파일)를 옮기므로 모든 PC 가 돌렸는지 모른다 |

훑은 것 중 문제가 아니었던 것: `dev/` · `report/` 의 참조가 README 하나뿐인 도구(사람이 가끔 돌리는 자리라 정상), 진입점(`CheckScriptEntryPoints` 가 지킨다),
폴더 · 앞머리 규칙(`CheckScriptLayout` 이 지킨다).

## 단계

### 1. 훅 바닥 — 트리 전체 게이트의 도구 비용 (끝남)
`normalizePath` 의 문자열 빠른 길, `ConfigReference` 의 한 번 걷기 · 한 번 읽기, `CheckDuplicateTypeNames` 의 스택 판정, `CheckModuleTargets` 의 소속 캐시.
훅이 끝에 게이트 시간 한 줄을 찍는다.

결과: 생성 문서는 기준과 sha256 이 같고(17 개, 위반 0), 게이트 요약 줄(파일 · 타입 수)도 같다. 기준 사본과 번갈아 5 회씩 잰 중앙값(부하가 내려간 뒤):

| 대상 | 전 | 후 |
|---|---|---|
| `CheckConfigReference` 단독 | 1.49 s | 0.99 s |
| `CheckDuplicateTypeNames` 단독 | 1.35 s | 1.03 s |
| `CheckEngineLayers` 단독 | 2.00 s | 1.09 s |
| `CheckModuleTargets` 단독 | 1.44 s | 1.21 s |
| 커밋 훅(`REFLECT` 헤더 하나) | 2.31 s | 1.92 s |

남은 바닥은 게이트 여섯이 각자 `Source/` 를 읽는 값(1~1.4 s)이다. 이 때문에 6 순위(`CheckConfigReference` 파일 단위)는 하지 않는다 — 그것만 0 으로 만들어도
나머지 다섯이 같은 높이다. 더 내리려면 트리 전체 게이트들이 파일 읽기를 나누는 구조(한 프로세스가 읽고 여럿이 훑기)가 필요하고, 그건 GIL 때문에 동시 실행을 잃는다.

### 2. `CheckOutParameterNames` 를 `CheckCodeConventions` 로
맨이름 `out` 검사를 파일 범위 규칙(`Naming/BareOutParameter`, 헤더만)으로 옮기고 게이트 파일을 지운다. CTest 항목은 폴더를 훑어 만들므로 따라 사라진다.
자기 시험 조각은 규칙이 든다(`badSample`). 별칭 · 옛 이름은 두지 않는다.

### 3. 폴더 이동기 둘 지우기
티어 표 맞추기를 `lint/report/RunEngineLayerGraph.py --apply` 로 옮기고(묶음 채우기 계산 포함) 이동기 둘을 지운다. 이동 표는 커밋 기록에 남는다.
