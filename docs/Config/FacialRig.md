<!-- 생성 문서입니다. 손으로 고치지 말고 원본 코드를 고친 뒤 다시 만듭니다: py -3 Scripts/generate/GenerateConfigReference.py -->

# FacialRig

[설정 색인](README.md) · [어디에 두나](../07_Configuration.md)

| | |
|---|---|
| 파일 | `Resource/game/*/models/*.facial.json` |
| 층 | 팩 데이터 |
| 읽는 곳 | `FacialRig::loadFromResource` (`FacialAnimationComponent::_facialRigPath`) |
| 언제 | 컴포넌트 시작 · 경로를 바꿀 때 |
| 배포본 | 게임 팩에 실림 |
| 커밋 | 한다 |
| 참고 | 메시에 없는 모프 타깃 · 스켈레톤에 없는 눈 본은 바인딩할 때 오류(`FacialRig::validate`) |

JSON 키는 아래 필드 이름 그대로입니다(앞의 `_` 포함). 적지 않은 필드는 기본값입니다. 모르는 키나 읽지 못하는 값은 로드 오류입니다.

## 필드

얼굴 리그 파일(`<얼굴>.facial.json`) 하나입니다. 비즘 이름은 립싱크 표(`engine/animation/lipsync.json`)의 것과 맞춥니다. 파일은 리플렉션 구조체로 읽는다(`ConfigManager::readConfigJSON` — 모르는 키 · 읽지 못한 값 · 범위 밖 숫자는 오류). 칸 표는 PROPERTY 가 정본이다. `_blink` · `_gaze` 를 적지 않으면 기본값이고, 타깃 · 눈 본이 비면 그 기능을 쓰지 않는다. 읽은 뒤 `validateFile` 이 이름 중복 · 간격 순서를 본다.

원본: [`Source/Engine/Animation/Facial/FacialRig.h`](../../Source/Engine/Animation/Facial/FacialRig.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_listExpression` | `vector<FacialPose>` | — |  |  | Expressions: named morph target weight sets, driven by the curve of the same name |
| `_listViseme` | `vector<FacialPose>` | — |  |  | Visemes: morph target weight sets the lip sync drives by viseme name |
| `_blink` | `FacialBlinkDef` | — |  |  | Eye blink |
| `_gaze` | `FacialGazeDef` | — |  |  | Eye gaze and saccades |

## `FacialPose`

이름 붙은 얼굴 포즈(표정, 비즘)입니다. 타깃 가중치 묶음이며, 포즈 가중치를 곱해 더합니다.

원본: [`Source/Engine/Animation/Facial/FacialRig.h`](../../Source/Engine/Animation/Facial/FacialRig.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_name` | `hashed_string` | — |  |  | Pose name (expression curve name, or a viseme name of the lip sync table); unique in its list |
| `_listTarget` | `vector<FacialTargetWeight>` | — |  |  | Morph target weights of this pose (empty: the pose moves nothing) |

## `FacialBlinkDef`

눈 깜빡임입니다. 타깃(눈꺼풀)과 간격, 길이(초)를 담습니다. 타깃이 없으면 깜빡이지 않습니다.

원본: [`Source/Engine/Animation/Facial/FacialRig.h`](../../Source/Engine/Animation/Facial/FacialRig.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_listTarget` | `vector<hashed_string>` | — |  |  | Eyelid morph targets (empty: no blinking) |
| `_minInterval` | `float32` | `2.0` | 0.0 ~ - | s | Shortest time between blinks (greater than 0) |
| `_maxInterval` | `float32` | `5.0` | 0.0 ~ - | s | Longest time between blinks (not below the shortest) |
| `_duration` | `float32` | `0.15` | 0.0 ~ - | s | Length of one blink (greater than 0) |

## `FacialGazeDef`

시선입니다. 눈 본들이 목표를 봅니다(앞 축과 최대 각). 사카드(작은 튐)는 간격과 크기로 정합니다. 눈 본이 없으면 보지 않습니다.

원본: [`Source/Engine/Animation/Facial/FacialRig.h`](../../Source/Engine/Animation/Facial/FacialRig.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_listEyeBone` | `vector<hashed_string>` | — |  |  | Eye bones of the skeleton (empty: no gaze) |
| `_forwardAxis` | `float3` | `0.0, 0.0, 1.0` |  |  | Eye forward axis in bone space (normalized on load, must not be zero) |
| `_maxAngle` | `float32` | `0.5235988` | 0.0 ~ - | rad | Largest angle the eyes turn from the forward axis |
| `_saccadeMinInterval` | `float32` | `0.4` | 0.0 ~ - | s | Shortest time between saccades (greater than 0) |
| `_saccadeMaxInterval` | `float32` | `1.6` | 0.0 ~ - | s | Longest time between saccades (not below the shortest) |
| `_saccadeAmplitude` | `float32` | `0.0523599` | 0.0 ~ - | rad | Size of one saccade |

## `FacialTargetWeight`

모프 타깃 하나의 가중치입니다.

원본: [`Source/Engine/Animation/Facial/FacialRig.h`](../../Source/Engine/Animation/Facial/FacialRig.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_target` | `hashed_string` | — |  |  | Morph target name of the mesh |
| `_weight` | `float32` | `0.0` |  |  | Target weight at pose weight 1 |
