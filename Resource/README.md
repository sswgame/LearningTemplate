# Resource

엔진이 로드하는 에셋의 최상위 폴더입니다. `Resource/` 자체는 표시·감시용이며, 파일은 아래 팩 아래에만 둡니다.

| 팩 | 경로 | 내용 |
| --- | --- | --- |
| Engine | `engine/` | 파이프라인, 코어 셰이더, 폴백 머티리얼, 내장 텍스처, 셸 InputMap, `enginedefaultassets.xml` |
| Common | `common/` | 게임 팩이 공유하는 셰이더(아웃라인, 샘플 컴퓨트 등) |
| Game | `game/<pack>/` | 해당 게임 콘텐츠(맵, 프리팹, 텍스처, 오디오, `gamesettings.xml`) |
| Editor | `editor/` | 에디터가 배포물에서 읽는 에셋(스플래시 텍스처 등). **`editortooldefaults.json` 은 여기가 아니라 `Config/Editor/` 에 있다** — 에디터 도구 시드는 배포되지 않는다 |

## 경로 규칙

- 코드에서 드라이브 절대경로는 쓰지 않습니다.
- 검색 시 경로는 소문자로 정규화됩니다. 그래서 `Resource/` 아래 파일 · 폴더 이름은 전부 소문자(`[a-z0-9_.-]+`)여야 하고(이 `README.md` 만 예외),
  `CheckResourceCasing.py` 와 커밋 훅이 강제합니다.
- 전역 ID: `engine/pipeline/forwardpipeline.xml`, `common/shaders/samplecompute.hlsl`, `game/<pack>/maps/editortest.scene.xml`, `editor/textures/splash.dds`
- 팩 상대 키: `pipeline/forwardpipeline.xml` → `game/<pack>/` → `common/` → `engine/` → `editor/` 순으로 검색
- 셸 InputMap: `engine/input/default.input.xml` (폴백). 게임플레이: `game/<pack>/input/default.input.xml`
- 사용자 설정(옵션 메뉴) 스키마: `engine/settings/engine.settings.xml` + 게임 덧붙이기 `game/<pack>/data/*.settings.xml`(게임 프리셋의 `_userSettingsSchema`) — `Source/Engine/UserSettings/README.md`
- 데이터는 **지금 형식으로만** 읽습니다 — 옛 판을 짐작해 읽지 않고, 이름을 바꿀 때 별칭(`Alias` · `ValueAlias`)도 두지 않습니다. 이름이나 형식을 바꾸면 이 폴더의 데이터를
  새 모양으로 다시 씁니다. `ResourceDataSchemaTest` 가 이 폴더 데이터 전부가 모르는 이름 없이 읽히는지 단언합니다.

## 텍스처

- 런타임은 **DDS 만** 읽습니다(BC 압축 · 밉이 이미 된 것). `textures/` 에는 `.dds` 와 데이터(`.sprite.json` · `.meta`)만 둡니다.
- 원본 이미지(PNG · JPG · TGA …)는 같은 도메인의 `textures_raw/` 에 같은 상대 경로로 둡니다(`editor/textures_raw/splash.jpg` →
  `editor/textures/splash.dds`). 규칙(포맷 · sRGB · 밉)은 `Config/Editor/TextureImportConfig.json` 이 정합니다. 쿠킹은 `textures_raw/` 를 팩에서 뺍니다.
- 원본을 고치면 `build/Ninja-Debug/Bin/App.exe --import-textures` 로 임포트하고 DDS 와 `textures_raw/import.stamp` 를 함께 커밋합니다(에디터가 떠
  있으면 핫 리로드가 임포트합니다). `App.exe --check-textures` · `TextureImportStampTest` 가 원본 · 규칙 · DDS 의 어긋남을 내용 해시로 잡습니다.
  BC7 은 Debug 에서 느리므로 큰 원본은 Release App 으로 임포트합니다. `.hdr` 은 아직 임포트하지 못합니다(8비트 디코더).
- 참조하는 곳이 없는 원본은 옮기지 말고 지웁니다. `CheckTextureFolders.py` 게이트가 폴더 규칙을 지킵니다.
