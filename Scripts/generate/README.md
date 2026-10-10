# Scripts/generate (자동화 및 데이터/코드 생성 스크립트)

**정본(JSON · `Constants.py` · 폴더 목록 · 리소스)에서 파일을 만들어 내는** 스크립트가 모여 있습니다 — 데이터 쿠킹, C++ 헤더 · CMake 목록 생성,
문서. 대부분은 구성 · 빌드 시점에 CMake 가 부르지만, 결과를 커밋하는 것(`GenerateSpriteTextures.py`)과 문서(`GenerateDocs.py`, `SW_BUILD_DOCS` 일 때만
타깃)는 손으로 돌립니다. 부르는 쪽이 아니라 **파일을 만든다는 것**이 이 폴더의 정의입니다.

## 주요 역할 및 스크립트

| 스크립트 | 역할 | 출력/대상 |
|---|---|---|
| `CookAssets.py` | 씬 · 프리팹을 `App --cook-scenes` 로 쿠킹하게 하고(SCN1 · PFB2 — 리플렉션이 엔진 안에 있어서 엔진이 쿠킹한다), Resource 도메인을 `.pack` 으로 패킹(SWPK, 4KB 섹터 정렬). 쿠킹된 씬 · 프리팹은 소스 옆이 아니라 스테이징 폴더에 쓰고 팩에 같은 상대 경로로 넣는다. `--build-target`(Game · Client · Server)이 빼는 에셋 종류(쿠킹 표 `target_excluded_asset_kinds` — 서버: 텍스처 · 셰이더 바이너리 · 오디오)는 넣지 않는다 | `--cooked-dir`(기본 `build/*/Bin/Cooked`), `build/*/Bin/Packs/*.pack` |
| `GenerateShippingHostDefaults.py` | 커밋된 런타임 JSON 설정을 읽어 Shipping 및 Fallback용 C++ 헤더로 생성 | `build/.../ShippingHostDefaults.h` |
| `GenerateConfigReference.py` | 설정 참조 문서 — 설정 구조체 칸 · 키 표 · 전역 변수 · 명령줄 · CMake 옵션 · 사용자 설정을 코드에서 (손으로 돌린다, 결과를 커밋한다 · 낡으면 CheckConfigReference) | `docs/Config/*.md` · `ConfigReference.json` |
| `GenerateDocs.py` | Doxygen을 구동하여 C++ API 레퍼런스 문서 생성 | `Docs/Doxygen/html/index.html` |
| `GeneratePackFormat.py` | 팩 바이너리 계약(`Config/Engine/PackFormat.json`)을 C++ 헤더로 | `build/.../PackFormat.gen.h` |
| `GenerateCookContract.py` | 쿠킹 표(`Config/Engine/CookContract.json`)를 C++ X-매크로 헤더로 — RHI 백엔드 표 · 쿡 접미사 표 (구성 시점). 쿠커는 같은 표를 `common/CookContract.py` 로 읽는다 | `build/.../CookContract.gen.h` |
| `GenerateCMakeConstants.py` | `Scripts/common/Constants.py` 의 상수를 CMake `set()` 목록으로 (구성 시점) | `build/.../ConfigVars.cmake` |
| `GenerateConfigureFiles.py` | 구성 시점 생성기 다섯(CMakeConstants · PackFormat · CookContract · ShippingHostDefaults · LintTargets)을 **한 프로세스로** 차례로 부른다 — configure 가 부르는 것은 이것 하나(생성기마다 파이썬을 띄우면 configure 의 3 할이 파이썬이었다). 각 생성기는 단독 실행도 그대로 | 위 다섯의 출력 |
| `GenerateToolchainCMake.py` | `Config/Environment/toolchain_config.json` 을 CMake `set()` 목록으로 (구성 시점) | `build/.../ToolchainVars.cmake` |
| `GenerateLintTargets.py` | `lint/gate` · `lint/selftest` 폴더를 CMake 린트 타깃 · 테스트로 (구성 시점) | `build/.../LintTargets.cmake` |
| `GenerateEngineAbiStamp.py` | Core · Engine 헤더 내용의 지문 — 핫 리로드의 엔진 ABI 도장 (빌드 시점) | `build/.../EngineAbiStamp.gen.h` |
| `GenerateThirdPartyNotices.py` | 서드파티 라이선스 고지 — 매니페스트(`vcpkg.json`)가 끌어오는 포트의 `share/<포트>/copyright` 와 저장소에 원문 그대로 둔 코드의 `ThirdParty/<이름>/LICENSE.md` 를 모은다(빌드 시점, `ThirdPartyNotices` 타깃) | `build/*/Bin/THIRD_PARTY_NOTICES.txt` |
| `GenerateEditorIcons.py` | 에디터 아이콘 폰트와 글리프 상수 헤더 — 그림은 `Scripts/common/EditorIconFont.py` 의 `kListIcon` (손으로 돌리고 결과를 커밋한다. 다르면 `CheckEditorIcons`) | `Resource/editor/fonts/sweditoricons.ttf` · `Source/Editor/Common/GUI/EditorIconGlyphs.h` |
| `GenerateReflectionAnnotationKeys.py` | 편집기(clangd)가 `REFLECT` · `PROPERTY` · `FUNCTION` · `ENUM` 인자 키를 완성하게 하는 선언 헤더 — 키는 `AnnotationMeta.txt`, 단위는 `ReflectUnits.h`, 내용은 `Scripts/common/ReflectionAnnotationKeys.py` (손으로 돌리고 결과를 커밋한다. 다르면 `CheckReflectionAnnotationKeys`) | `Source/Engine/Reflection/ReflectionAnnotationKeys.h` |
| `GenerateSpriteTextures.py` | 엔진이 들고 다니는 작은 스프라이트 텍스처(DDS)와 클립 — 네 칸 시험 텍스처 (손으로 돌린다, 결과를 커밋한다) | `Resource/engine/textures/test/quadrants.*` · `Resource/engine/textures/missing.dds` |

`setup/` 은 **외부 도구를 찾아 설치하는** 폴더이고, 정본에서 파일을 만들어 내는 일은 구성 시점이든 빌드 시점이든 여기다.

> **씬 쿠킹은 소스 트리를 읽습니다.** `App --cook-scenes` 는 팩을 마운트하지 않고 소스 트리(`ContentSource::SourceTree`)를 올려, 배포 구성에서도
> 느슨한 파일 · 소스 프리팹을 읽습니다. 모르는 타입의 컴포넌트(`MissingComponent`)가 든 씬은 쿠킹하지 않고 실패로 세며, 실패가 하나라도 있으면
> App 이 0 이 아닌 코드로 끝나 쿠커가 멈춥니다.

## 팩 압축 코덱 고르기

`Config/Engine/PackConfig.json` 의 `compression` 이 정합니다. 이름은 팩 포맷 계약
(`Config/Engine/PackFormat.json` 의 `compression.codecs`)에 있는 것만 씁니다.

```json
"compression": { "codec": "Zlib", "level": 0 }
```

| 코덱 | 쿠킹에 필요한 것 | 쓰는 자리 |
|---|---|---|
| `None` · `RLE` · `Zlib` | **없음** — 파이썬 표준 라이브러리 | 기본값은 `Zlib` |
| `LZ4` | `py -3 -m pip install lz4` | 해제 속도가 로딩 시간인 자리 |
| `Zstd` | `py -3 -m pip install zstandard` | 배포물 크기를 줄이고 싶을 때 |

> **모듈이 없으면 쿠킹이 그 자리에서 멈춥니다.** 설정은 LZ4 인데 zlib 으로 쿠킹해 버리면 설정과
> 산출물이 달라지고, 그 사실은 한참 뒤 배포본에서야 드러납니다.

**실측 — 이 프로젝트의 실제 팩에서는 코덱 차이가 거의 없습니다**:

| 팩 | Zlib | LZ4 | Zstd |
|---|---|---|---|
| `engine.pack` | 254,558,515 | 327,207,121 (**+28%**) | 254,202,331 |
| `common.pack` | 87,267 | 91,826 | 87,315 |
| `game_empty.pack` | 20,812 | 20,944 | 20,851 |

내용이 이미 압축된 것(DDS 텍스처·셰이더 바이너리)이라 더 줄 여지가 없기 때문입니다. **LZ4 만
눈에 띄게 커집니다.** 합성 데이터(1MB 모사 버퍼)에서는 Zstd 가 Zlib 보다 훨씬 작았는데
(34.7% vs …), 실제 팩에서는 0.1% 차이입니다 — 벤치마크 표본이 곧 결론이 아니라는 예입니다.
압축되지 않은 자산(대량의 JSON·XML·오디오 PCM 등)이 팩에 들어오면 다시 재 보십시오.

## 실행 방법

```bash
py -3 -m Scripts cook --all
# 또는 직접 실행:
py -3 Scripts/generate/CookAssets.py [--all] [--prefabs-only] [--scenes-only] [--packs-only] [--app <App 실행 파일>] [--cooked-dir <폴더>] [--target-rhi <백엔드>] [--cook-shaders]
# 씬 쿠킹은 App --cook-scenes 라 App 이 먼저 서 있어야 한다. CMake 의 CookAssets 타겟은 App 뒤에 돌며 경로를 --app 으로 넘긴다.
py -3 Scripts/generate/GenerateShippingHostDefaults.py <output_header_path>
py -3 Scripts/generate/GenerateDocs.py [--open]
```
