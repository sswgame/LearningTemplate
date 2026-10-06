<!-- 생성 문서입니다. 손으로 고치지 말고 원본 코드를 고친 뒤 다시 만듭니다: py -3 Scripts/generate/GenerateConfigReference.py -->

# ModelImportConfig

[설정 색인](README.md) · [어디에 두나](../07_Configuration.md)

| | |
|---|---|
| 파일 | `Config/Editor/ModelImportConfig.json` |
| 층 | 에디터 도구 |
| 읽는 곳 | `ModelImportConfig::loadFromFile` (손으로 읽음) |
| 언제 | 모델 임포트(에디터 · `App --import-models`) |
| 배포본 | 없음(임포트는 Dev 만) |
| 커밋 | 한다 |

JSON 키는 아래 테이블의 키 그대로입니다. 모르는 키는 로드 오류이고, 읽기 코드가 이 테이블(`ConfigKeyDoc`)로 검사합니다.

## 필드

파일 뿌리입니다.

원본: [`Source/Editor/Common/Asset/ModelImportConfig.cpp`](../../Source/Editor/Common/Asset/ModelImportConfig.cpp)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `rules` | `object[]` | — |  |  | 규칙 목록(아래 키). 첫 매칭이 이긴다. 맞는 규칙이 없는 원본은 기본값(옮기지 않음 · 애니메이션 모두 · ACL) |

## `kArrModelImportRuleKeyDoc`

규칙 하나입니다. 적용 순서는 `translation` 다음 `recenter` — 스킨드 모델에는 둘 다 쓸 수 없다(바인드 행렬이 어긋난다).

원본: [`Source/Editor/Common/Asset/ModelImportConfig.cpp`](../../Source/Editor/Common/Asset/ModelImportConfig.cpp)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `name` | `string` | — |  |  | 규칙 이름(로그에 나온다) |
| `include_patterns` | `string[]` | — |  |  | 맞아야 하는 와일드카드(`*` · `?`, 대소문자 무시) |
| `exclude_patterns` | `string[]` | — |  |  | 맞으면 빼는 와일드카드 |
| `include_paths` | `string[]` | — |  |  | 들어 있어야 하는 경로 조각 |
| `exclude_paths` | `string[]` | — |  |  | 들어 있으면 빼는 경로 조각 |
| `translation` | `number[3]` | `0, 0, 0` |  |  | 모든 노드 월드 위치에 더하는 이동(glTF 원본 공간 — 축 변환 전) |
| `recenter` | `string` | `none` |  |  | none · xz(경계 상자 XZ 중심을 원점) · bottom-center(XZ 중심 + 가장 낮은 Y 를 0) |
| `animations` | `bool` | `true` |  |  | 애니메이션을 가져온다 |
| `clips` | `string[]` | — |  |  | 가져올 클립 이름(비면 모두 — 원본에 없는 이름은 임포트 오류) |
| `animation_codec` | `string` | `acl` |  |  | 애니메이션 코덱 이름(`AnimCodecRegistry` 에 없으면 설정 오류) |
| `animation_sample_rate` | `number` | `30` |  |  | 초당 표본 수 |
| `animation_precision` | `number` | `0.0001` |  |  | 코덱 정밀도(`AnimCodecSettings`) |
| `animation_shell_distance` | `number` | `0.1` |  |  | 코덱 껍질 거리(미터) |
| `root_motion_bone` | `string` | — |  |  | 루트 모션 트랙이 될 본(비면 없음) |
| `attachments` | `bool` | `true` |  |  | 본 아래 스킨 없는 메시를 따로 임포트한다 |
| `fracture` | `object` | — |  |  | 있으면 `.mesh` 옆에 `.fracture` 를 쓴다(스킨 없는 메시만, 아래 키) |

## `kArrModelImportFractureKeyDoc`

`fracture` 객체입니다(`FractureSettings`).

원본: [`Source/Editor/Common/Asset/ModelImportConfig.cpp`](../../Source/Editor/Common/Asset/ModelImportConfig.cpp)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `pattern` | `string` | `uniform` |  |  | uniform · clustered(맞은 자리 둘레에 몰림) · slices(격자에 흔들림) |
| `volume` | `string` | `mesh` |  |  | mesh(닫힌 메시) · bounds(경계 상자) · hull(볼록 껍질) — 닫히지 않은 모델의 대리 부피 |
| `pieces` | `number` | `16` |  |  | 조각 수(uniform · clustered) |
| `seed` | `number` | `1` |  |  | 씨앗 난수 |
| `levels` | `number[]` | — |  |  | 클러스터 레벨마다 클러스터 수(위 → 아래), 비면 뿌리 하나 |
| `impact_point` | `number[3]` | `0, 0, 0` |  |  | clustered: 맞은 자리(메시 공간) |
| `cluster_radius` | `number` | `0.5` |  |  | clustered: 몰리는 반경(미터) |
| `cluster_fraction` | `number` | `0.7` |  |  | clustered: 반경 안에 놓을 씨앗 비율(0..1) |
| `slices` | `number[3]` | `4, 1, 1` |  |  | slices: 축마다 셀 수 |
| `slice_jitter` | `number` | `0.15` |  |  | slices: 셀 크기에 대한 흔들림(0..0.5) |
| `interior_color` | `number[4]` | `0.62, 0.58, 0.52, 1` |  |  | 안쪽 면 정점 색 |
| `interior_uv_scale` | `number` | `1` |  |  | 안쪽 면 UV 의 미터당 배율 |
| `max_hull_points` | `number` | `48` |  |  | 조각 껍질 점의 상한 |
