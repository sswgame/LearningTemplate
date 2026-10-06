# Resource

엔진이 실행 중에 로드하는 에셋이 모두 이 폴더에 있습니다. 파일은 `Resource/` 바로 아래가 아니라 아래 표의 팩 폴더 안에만 둡니다.

| 팩 | 경로 | 내용 |
| --- | --- | --- |
| Engine | `engine/` | 렌더 파이프라인, 코어 셰이더, 폴백 머티리얼, 내장 텍스처, 셸 입력 맵 |
| Common | `common/` | 여러 게임 팩이 같이 쓰는 셰이더, 프리팹, 데이터, 저장소 SQL |
| Game | `game/<팩>/` | 그 게임의 맵, 프리팹, 텍스처, 오디오, `gamesettings.xml` |
| Editor | `editor/` | 에디터가 배포본에서 읽는 에셋(스플래시 텍스처 등) |

엔진의 기본 에셋 경로는 `engine/data/enginedefaultassets.xml` 이 정합니다.
에디터 도구의 기본값 파일 `editortooldefaults.json` 은 `editor/` 팩이 아니라 `Config/Editor/` 에 둡니다. 에디터 도구 설정은 배포본에 실리지 않기 때문입니다.

## 경로 규칙

리소스 경로는 팩 도메인을 포함한 전역 ID입니다. 예를 들면 다음과 같습니다.

- `engine/pipeline/forwardpipeline.xml`
- `common/shaders/samplecompute.hlsl`
- `game/<팩>/maps/editortest.scene.xml`
- `editor/textures/splash.dds`

도메인 없이 팩 상대 경로(`pipeline/forwardpipeline.xml`)로 찾으면 `game/<팩>/`, `common/`, `engine/`, `editor/` 순서로 검색합니다. 코드에는 드라이브 절대 경로를 쓰지 않습니다.

찾을 때 경로를 소문자로 정규화하기 때문에, `Resource/` 아래의 파일과 폴더 이름은 모두 소문자(`[a-z0-9_.-]+`)여야 합니다. 이 `README.md` 만 예외입니다.
`CheckResourceCasing.py` 와 커밋 훅이 이 규칙을 검사합니다.

자주 찾는 데이터의 위치는 다음과 같습니다.

- 셸 입력 맵은 `engine/input/default.input.xml` 이고, 게임플레이 입력 맵은 `game/<팩>/input/default.input.xml` 입니다.
- 옵션 메뉴의 스키마는 `engine/settings/engine.settings.xml` 이고, 게임이 덧붙이는 스키마는 `game/<팩>/data/*.settings.xml` 입니다. 게임 프리셋의 `_userSettingsSchema` 가 가리킵니다. 자세한 내용은 `Source/Engine/UserSettings/README.md` 에 있습니다.

데이터는 현재 형식으로만 읽습니다. 예전 형식을 짐작해서 읽지 않고, 이름을 바꿀 때 별칭(`Alias`, `ValueAlias`)도 두지 않습니다.
이름이나 형식을 바꾸면 이 폴더의 데이터를 새 형식으로 다시 씁니다. `ResourceDataSchemaTest` 가 이 폴더의 데이터가 모르는 이름 없이 읽히는지 검사합니다.

## 텍스처

런타임은 DDS만 읽습니다. BC 압축과 밉맵이 이미 끝난 파일입니다. 그래서 `textures/` 폴더에는 `.dds` 와 데이터 파일(`.sprite.json`, `.meta`)만 둡니다.

원본 이미지(PNG, JPG, TGA, HDR 등)는 같은 도메인의 `textures_raw/` 에 같은 상대 경로로 둡니다. 예를 들어 `editor/textures_raw/splash.jpg` 를 임포트하면 `editor/textures/splash.dds` 가 됩니다.
포맷, sRGB, 밉맵 규칙은 `Config/Editor/TextureImportConfig.json` 이 정합니다. 쿠킹할 때 `textures_raw/` 는 팩에서 뺍니다.

원본을 고쳤다면 이렇게 합니다.

1. `build/Ninja-Debug/Bin/App.exe --import-textures` 로 임포트합니다. 에디터가 떠 있으면 핫 리로드가 대신 임포트합니다.
2. 바뀐 DDS와 `textures_raw/import.stamp` 를 함께 커밋합니다.

`App.exe --check-textures` 와 `TextureImportStampTest` 가 원본, 규칙, DDS가 어긋났는지를 내용 해시로 확인합니다.
BC7 압축은 Debug 빌드에서 느리므로 큰 원본은 Release 빌드의 App으로 임포트합니다.
참조하는 곳이 없는 원본은 다른 곳으로 옮기지 말고 지웁니다. `CheckTextureFolders.py` 게이트가 폴더 규칙을 검사합니다.

## 함정과 주의

**임포트와 수집 결과물은 줄 끝 변환을 받지 않게 둡니다.** `.gitattributes` 가 `Resource/**/models_raw/**`, `Resource/**/models/**`, `Resource/**/localization/**` 를 `-text` 로 지정합니다.
스탬프는 원본과 결과를 바이트 해시로 비교합니다. `core.autocrlf=true` 로 체크아웃하면 `*.skeleton.json` 같은 텍스트 결과물이 CRLF로 바뀌고, 임포트한 워크트리가 아닌 곳에서는 "손으로 고친 파일"로 판정됩니다.
텍스트 결과물을 새로 만들면 같은 규칙에 넣습니다. 넣지 않으면 `App --check-text` 같은 검사가 CRLF 체크아웃에서 "OUT OF DATE" 를 냅니다.

**고아 에셋 검사(`orphans`)는 전체 경로와 파일 이름만 찾습니다.** 코드가 `"game/<게임>/models/" + 이름 + ".mesh"` 처럼 경로를 조립해서 쓰는 메시는 찾지 못합니다.
그런 메시는 `Config/Editor/AssetValidationRules.json` 의 `exclude_patterns` 에 적습니다. NileCity, StarSkirmish, HarvestValley의 작물 메시가 그 예입니다.
아무도 쓰지 않는 원본은 `models_raw/` 의 파일과 `import.stamp` 의 줄까지 함께 지웁니다. 기믹 프리팹(`common/prefabs/gimmicks`)은 게임이 가져다 쓰는 라이브러리라 검사에서 뺍니다.

**경로는 리소스 ID 하나로 보관합니다.** `ResourceUtil::toResourceId` 가 경로를 ID로 바꾸고, 리소스 루트 밖의 경로나 `..` 가 든 경로는 빈 문자열이 됩니다.
쓰기 경로는 `ResourceUtil::getWritePath` 하나로 얻고, 폴더는 `FileUtil::ensureParentDirectoryExists` 로 만듭니다. 이 함수는 실패하면 그 자리에서 경로와 OS 오류를 로그로 남깁니다.
맵의 키는 `normalizePath`(소문자)로 만들고, 파일을 열 경로는 `normalizeSeparators` 로 만듭니다. `collectFiles` 는 대소문자를 보존합니다.

**배포 빌드는 `.meta` 를 쓰지 않고 GUID도 만들지 않습니다.** 배포본의 GUID 테이블은 쿠커가 도메인마다 넣는 `assetregistry.txt` 입니다. `ensureMeta` 는 리소스 루트 밖의 절대 경로를 받으면 null GUID를 돌려줍니다.
