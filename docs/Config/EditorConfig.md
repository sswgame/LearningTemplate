<!-- 생성 문서 — 손으로 고치지 않는다. 정본은 코드다. 다시 만들기: py -3 Scripts/generate/GenerateConfigReference.py -->

# EditorConfig

[설정 색인](README.md) · [어디에 두나](../07_Configuration.md)

| | |
|---|---|
| 파일 | `Saved/Editor/EditorConfig.json` |
| 층 | 에디터 도구 |
| 읽는 곳 | `EditorConfig::loadFromHost` · `saveToHost` |
| 언제 | 에디터 기동 · 테마 저장 때 **앱이 통째로 다시 쓴다** |
| 배포본 | 없음(에디터 없음) |
| 커밋 | 하지 않는다(로컬 · git 무시) |
| 참고 | 앱이 쓰는 상태(`Saved/`, git 무시) — 사람이 고치지 않는다 |

JSON 키는 아래 칸 이름 그대로입니다(앞의 `_` 포함). 적지 않은 칸은 기본값입니다. 모르는 키 · 읽지 못하는 값은 로드 오류입니다.

## 칸

에디터가 저장하는 상태입니다(Shipping 에는 없습니다). 지금은 테마뿐입니다. **필드를 늘리기 전에** 그 값을 앱이 쓰는지 사람이 쓰는지 확인하십시오. 사람이 쓰는 것은 `EditorToolDefaults` 에 둡니다.

정본: [`Source/Editor/Common/Config/EditorConfig.h`](../../Source/Editor/Common/Config/EditorConfig.h)

| 칸 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_themePreset` | `string` | `ModernDark` |  |  | 테마 프리셋 이름(테마 대화 상자의 목록) |
| `_themeAccentR` | `float32` | `0.27` | 0.0 ~ 1.0 |  | 강조색 R(0..1) |
| `_themeAccentG` | `float32` | `0.57` | 0.0 ~ 1.0 |  | 강조색 G(0..1) |
| `_themeAccentB` | `float32` | `1.0` | 0.0 ~ 1.0 |  | 강조색 B(0..1) |
| `_themeWindowRounding` | `float32` | `4.0` | 0.0 ~ - |  | 창 모서리 둥글기(픽셀) |
| `_themeFrameRounding` | `float32` | `3.0` | 0.0 ~ - |  | 입력 칸 · 버튼 모서리 둥글기(픽셀) |
| `_themeTabRounding` | `float32` | `4.0` | 0.0 ~ - |  | 탭 모서리 둥글기(픽셀) |
