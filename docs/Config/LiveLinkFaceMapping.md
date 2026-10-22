<!-- 생성 문서입니다. 손으로 고치지 말고 원본 코드를 고친 뒤 다시 만듭니다: py -3 Scripts/generate/GenerateConfigReference.py -->

# LiveLinkFaceMapping

[설정 색인](README.md) · [어디에 두나](../07_Configuration.md)

| | |
|---|---|
| 파일 | `Resource/game/*/models/*.livelinkface.xml` |
| 층 | 팩 데이터 |
| 읽는 곳 | `LiveLinkFaceMapping::loadFromResource` (`LiveLinkFaceComponent::_mappingPath`) |
| 언제 | 컴포넌트 시작 |
| 배포본 | 게임 팩에 실림 |
| 커밋 | 한다 |
| 참고 | 패킷 채널 → 얼굴 리그 표정 연결. 배치 표(`livelinkface.xml`)에 없는 채널은 오류 |

XML 속성(값 하나)과 자식 요소(목록, 구조체)의 이름은 아래 필드 이름 그대로입니다. 모르는 이름은 로드 오류입니다.

## 필드

패킷 채널에서 얼굴 리그 표정으로 가는 연결 표입니다(머리마다 하나, `<머리>.livelinkface.xml`). 읽을 패킷 배치 표(`_layoutPath`)를 데이터가 고릅니다.

원본: [`Source/Engine/Animation/Facial/LiveLinkFace.h`](../../Source/Engine/Animation/Facial/LiveLinkFace.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_layoutPath` | `string` | — |  |  | Packet layout this mapping reads (LiveLinkFaceLayout, e.g. engine/animation/livelinkface.xml) - required |
| `_listExpression` | `vector<LiveLinkFaceExpression>` | — |  |  | Expressions and the packet channels that drive them |

## `LiveLinkFaceExpression`

얼굴 리그 표정 하나와, 그 값을 이루는 패킷 채널들입니다(값 = 채널 × 배율의 합).

원본: [`Source/Engine/Animation/Facial/LiveLinkFace.h`](../../Source/Engine/Animation/Facial/LiveLinkFace.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_expression` | `hashed_string` | — |  |  | Facial rig expression name the channels drive |
| `_listChannel` | `vector<LiveLinkFaceChannelWeight>` | — |  |  | Channels summed (value x scale) into the expression |

## `LiveLinkFaceChannelWeight`

표정 하나에 더하는 패킷 채널 하나와 그 배율입니다.

원본: [`Source/Engine/Animation/Facial/LiveLinkFace.h`](../../Source/Engine/Animation/Facial/LiveLinkFace.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_channel` | `hashed_string` | — |  |  | Packet channel name (LiveLinkFaceLayout::_listChannel) |
| `_scale` | `float32` | `1.0` |  |  | Scale of the channel value added to the expression |
