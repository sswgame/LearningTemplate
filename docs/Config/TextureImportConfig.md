<!-- 생성 문서입니다. 손으로 고치지 말고 원본 코드를 고친 뒤 다시 만듭니다: py -3 Scripts/generate/GenerateConfigReference.py -->

# TextureImportConfig

[설정 색인](README.md) · [어디에 두나](../07_Configuration.md)

| | |
|---|---|
| 파일 | `Config/Editor/TextureImportConfig.json` |
| 층 | 에디터 도구 |
| 읽는 곳 | `TextureImportConfig::loadFromFile` (손으로 읽음) |
| 언제 | 텍스처 임포트(에디터 · `App --import-textures`) |
| 배포본 | 없음(임포트는 Dev 만) |
| 커밋 | 한다 |

JSON 키는 아래 테이블의 키 그대로입니다. 모르는 키는 로드 오류이고, 읽기 코드가 이 테이블(`ConfigKeyDoc`)로 검사합니다.

## 필드

파일 뿌리입니다.

원본: [`Source/Editor/Common/Asset/TextureImportConfig.cpp`](../../Source/Editor/Common/Asset/TextureImportConfig.cpp)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `presets` | `object` | — |  |  | 프리셋 이름 → 규칙(아래 키). 위에서 아래로 읽으니 부모 프리셋을 먼저 적는다 |
| `rules` | `object[]` | — |  |  | 규칙 목록(아래 키). 첫 매칭이 이긴다 — 조건 없는 규칙은 맨 끝에 |

## `kArrTextureImportRuleKeyDoc`

프리셋 · 규칙 하나입니다. 적지 않은 필드는 `inherits` 의 값, 그것도 없으면 기본값입니다.

원본: [`Source/Editor/Common/Asset/TextureImportConfig.cpp`](../../Source/Editor/Common/Asset/TextureImportConfig.cpp)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `name` | `string` | — |  |  | 규칙 이름(로그 · 경고에 나온다). 프리셋은 키가 이름이다 |
| `inherits` | `string` | — |  |  | 값을 물려받을 프리셋 이름 — 위에 정의되지 않은 이름은 로드 오류 |
| `format` | `string` | `BC7_UNORM` |  |  | DXGI 포맷 이름(BC7_UNORM · BC5_UNORM · BC6H_UF16 · B8G8R8A8_UNORM · R8G8B8A8_UNORM …) |
| `swizzle` | `string` | `RGBA` |  |  | 채널 순서: RGBA · BGRA · ARGB · RGB1 — 그 밖은 로드 오류 |
| `generate_mips` | `bool` | `true` |  |  | 밉맵을 만든다 |
| `srgb` | `bool` | `true` |  |  | sRGB 색 공간으로 읽는다(노멀 · 데이터 텍스처는 false) |
| `invert_green` | `bool` | `false` |  |  | G 채널을 뒤집는다(DirectX ↔ OpenGL 노멀 맵) |
| `include_patterns` | `string[]` | — |  |  | 맞아야 하는 와일드카드(`*` · `?`, 대소문자 무시, 파일 이름이나 리소스 경로) |
| `exclude_patterns` | `string[]` | — |  |  | 맞으면 빼는 와일드카드(포함보다 먼저 본다) |
| `include_paths` | `string[]` | — |  |  | 들어 있어야 하는 경로 조각 |
| `exclude_paths` | `string[]` | — |  |  | 들어 있으면 빼는 경로 조각 |
