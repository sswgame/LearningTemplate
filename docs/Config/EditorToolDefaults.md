<!-- 생성 문서 — 손으로 고치지 않는다. 정본은 코드다. 다시 만들기: py -3 Scripts/generate/GenerateConfigReference.py -->

# EditorToolDefaults

[설정 색인](README.md) · [어디에 두나](../07_Configuration.md)

| | |
|---|---|
| 파일 | `Config/Editor/editortooldefaults.json` |
| 층 | 에디터 도구 |
| 읽는 곳 | `EditorToolDefaults::loadFromHostPath` (에디터 모듈 기동) |
| 언제 | 에디터 기동 · 에디터 핫 리로드 |
| 배포본 | 없음(에디터 없음) |
| 커밋 | 한다 |
| 참고 | 기본값과 다른 값이 있을 때만 만든다 — 없으면 기본값 |

JSON 키는 아래 칸 이름 그대로입니다(앞의 `_` 포함). 적지 않은 칸은 기본값입니다. 모르는 키 · 읽지 못하는 값은 로드 오류입니다.

## 칸

editortooldefaults.json 의 에디터 도구 시드입니다. 읽기는 `JsonSerializer` 가 PROPERTY 그래프로 합니다. 필드를 추가하면 읽기가 따라옵니다(손으로 파싱하지 않습니다).

정본: [`Source/Editor/Common/Config/EditorToolDefaults.h`](../../Source/Editor/Common/Config/EditorToolDefaults.h)

| 칸 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_defaultMap` | `string` | — |  |  | 타일맵 패널이 처음 여는 맵(리소스 경로, 비면 없음) |
| `_warpMap` | `string` | — |  |  | 타일맵 패널의 워프 대상 기본값(리소스 경로) |
| `_spriteAtlas` | `string` | — |  |  | 스프라이트 클립 패널의 기본 아틀라스(리소스 경로) |
| `_fontSize` | `float32` | `16.0` | 6.0 ~ - |  | 에디터 글꼴 크기(픽셀, DPI 배율 전) |
| `_clearColor` | `float4` | `0.12, 0.15, 0.18, 1.0` |  |  | Game View 렌더 타깃 클리어 색 |
| `_listBaseFont` | `vector<string>` | `consola.ttf, Consolas.ttf, DejaVuSansMono.ttf, … (8 개)` |  |  | 라틴 글꼴 후보 — 에디터 팩 `fonts/` → OS 글꼴 폴더 순으로 앞의 것부터 찾는다 |
| `_listKoreanFont` | `vector<string>` | `malgun.ttf, malgunsl.ttf, NanumGothic.ttf, … (8 개)` |  |  | 한글 글꼴 후보(병합) — 찾는 순서는 위와 같다 |
| `_listHotReloadExtension` | `vector<string>` | — |  |  | 실행 중에 다시 읽을 애셋 확장자입니다. **비우면 처리기가 있는 확장자 전부**를 봅니다. 무엇을 감시할지는 설정이 정하고, 다시 읽는 방법이 있는지는 코드가 정합니다(`AssetHotReload` 의 처리기 표). 처리기가 없는 확장자를 적으면 경고를 남기고 뺍니다. 감시만 하고 아무 일도 하지 않는 자리를 만들지 않습니다. 변경이 쏟아지는 폴더를 잠시 빼고 싶을 때 이 목록을 좁히면 됩니다. |
| `_ideOpenCommand` | `string` | — |  |  | 출력 로그 줄을 IDE 로 여는 명령 틀입니다. `{file}` · `{line}` 이 위치로 바뀝니다(예: `code -g "{file}:{line}"`, `rider64 --line {line} "{file}"`). 비우면 VS Code 입니다(`EditorLogCommands::getOpenCommandTemplate`). |
