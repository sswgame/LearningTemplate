<!-- 생성 문서입니다. 손으로 고치지 말고 원본 코드를 고친 뒤 다시 만듭니다: py -3 Scripts/generate/GenerateConfigReference.py -->

# LiveLinkFaceLayout

[설정 색인](README.md) · [어디에 두나](../07_Configuration.md)

| | |
|---|---|
| 파일 | `Resource/engine/animation/livelinkface.xml` |
| 층 | 엔진 기본값 |
| 읽는 곳 | `LiveLinkFaceLayout::loadFromResource` (연결 표 `*.livelinkface.xml` 의 `_layoutPath`) |
| 언제 | `LiveLinkFaceComponent` 시작 |
| 배포본 | 엔진 팩에 실림 |
| 커밋 | 한다 |
| 참고 | Live Link Face 패킷의 판 번호와 채널 순서 — 앱 판이 바뀌면 이 파일을 고친다 |

XML 속성(값 하나)과 자식 요소(목록, 구조체)의 이름은 아래 필드 이름 그대로입니다. 모르는 이름은 로드 오류입니다.

## 필드

패킷의 판 번호와 채널 순서입니다(데이터, 엔진 기본은 `engine/animation/livelinkface.xml` — 연결 표의 `_layoutPath` 가 가리킨다).

원본: [`Source/Engine/Animation/Facial/LiveLinkFace.h`](../../Source/Engine/Animation/Facial/LiveLinkFace.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_packetVersion` | `int32` | `0` | 0 ~ 255 |  | Packet version byte this layout reads (other versions are rejected) |
| `_listChannel` | `vector<hashed_string>` | — |  |  | Channel names in packet order (ARKit blend shapes, then head and eye rotations) |
