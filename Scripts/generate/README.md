# Scripts/generate (자동화 및 데이터/코드 생성 스크립트)

**원본(JSON · `Constants.py` · 폴더 목록 · 리소스)에서 파일을 만들어 내는** 스크립트가 모여 있습니다 — 데이터 쿠킹, C++ 헤더 · CMake 목록 생성, 문서.
대부분은 구성 · 빌드 시점에 CMake 가 부르지만 결과를 커밋하는 것(`GenerateSpriteTextures.py`)과 문서(`GenerateDocs.py`, `SW_BUILD_DOCS` 일 때만 타깃)는 손으로 돌립니다. 부르는 쪽이 아니라 **파일을 만든다는 것**이 이 폴더의 정의입니다.

## 주요 역할 및 스크립트

| 스크립트 | 역할 | 출력/대상 |
|---|---|---|
| `GenerateShippingHostDefaults.py` | 커밋된 런타임 JSON 설정을 읽어 Shipping 및 Fallback용 C++ 헤더로 생성 | `build/.../ShippingHostDefaults.h` |
| `GenerateConfigReference.py` | 설정 참조 문서 — 설정 구조체 칸 · 키 표 · 전역 변수 · 명령줄 · CMake 옵션 · 사용자 설정을 코드에서 (손으로 돌린다, 결과를 커밋한다 · 낡으면 CheckConfigReference) | `docs/Config/*.md` · `ConfigReference.json` |
| `GenerateDocs.py` | Doxygen을 구동하여 C++ API 레퍼런스 문서 생성 | `Docs/Doxygen/html/index.html` |
| `GenerateCookContract.py` | 쿠킹 표(`Config/Engine/CookContract.json`)를 C++ X-매크로 헤더로 — RHI 백엔드 표 · 쿡 접미사 표 (구성 시점). 파이썬 도구는 같은 표를 `common/CookContract.py` 로 읽는다 | `build/.../CookContract.gen.h` |
| `GenerateCMakeConstants.py` | `Scripts/common/Constants.py` 의 상수를 CMake `set()` 목록으로 (구성 시점) | `build/.../ConfigVars.cmake` |
| `GenerateConfigureFiles.py` | 구성 시점 생성기(CMakeConstants · CookContract · ShippingHostDefaults · LintTargets 등)를 **한 프로세스로** 차례로 부른다 — configure 가 부르는 것은 이것 하나(생성기마다 파이썬을 띄우면 configure 시간의 상당 부분이 파이썬 기동이다). 각 생성기는 단독 실행도 그대로 | 위 출력들 |
| `GenerateToolchainCMake.py` | `Config/Environment/toolchain_config.json` 을 CMake `set()` 목록으로 (구성 시점) | `build/.../ToolchainVars.cmake` |
| `GenerateLintTargets.py` | `lint/gate` · `lint/selftest` 폴더를 CMake 린트 타깃 · 테스트로 (구성 시점) | `build/.../LintTargets.cmake` |
| `GenerateEngineAbiStamp.py` | Core · Engine 헤더 내용의 지문 — 핫 리로드의 엔진 ABI 스탬프 (빌드 시점) | `build/.../EngineAbiStamp.gen.h` |
| `GenerateThirdPartyNotices.py` | 서드파티 라이선스 고지 — 매니페스트(`vcpkg.json`)가 끌어오는 포트의 `share/<포트>/copyright` 와 저장소에 원문 그대로 둔 코드의 `ThirdParty/<이름>/LICENSE.md` 를 모은다(빌드 시점, `ThirdPartyNotices` 타깃) | `build/*/Bin/THIRD_PARTY_NOTICES.txt` |
| `GenerateEditorIcons.py` | 에디터 아이콘 폰트와 글리프 상수 헤더 — 그림은 `Scripts/common/EditorIconFont.py` 의 `kListIcon` (손으로 돌리고 결과를 커밋한다. 다르면 `CheckEditorIcons`) | `Resource/editor/fonts/sweditoricons.ttf` · `Source/Editor/Common/GUI/EditorIconGlyphs.h` |
| `GenerateReflectionAnnotationKeys.py` | 편집기(clangd)가 `REFLECT` · `PROPERTY` · `FUNCTION` · `ENUM` 인자 키를 완성하게 하는 선언 헤더 — 키는 `AnnotationMeta.txt`, 단위는 `ReflectUnits.h`, 내용은 `Scripts/common/ReflectionAnnotationKeys.py` (손으로 돌리고 결과를 커밋한다. 다르면 `CheckReflectionAnnotationKeys`) | `Source/Engine/Reflection/ReflectionAnnotationKeys.h` |
| `GeneratePlaceholderHorse.py` | WildWest 승마의 자리 표시 말 — 상자 스킨드 메시 · 뼈 아홉(기수 소켓 `Saddle`) · 걸음새 클립 넷 glTF (손으로 돌린다, 결과를 커밋한다. 진짜 말 모델이 오면 지운다) | `Resource/game/wildwest/models_raw/horse_placeholder.gltf` |
| `GenerateShapingTestFont.py` | 복합 문자 셰이핑 테스트용 작은 글꼴 — 아랍어 바(ب)·알리프의 연결형 모양을 GSUB `init` · `medi` · `fina` 로 갖고 글리프마다 폭이 달라 셰이퍼가 고른 모양을 번호와 전진 폭으로 안다. 내용은 `Scripts/common/ShapingTestFont.py`, 표준 라이브러리만 쓴다 (손으로 돌린다, 결과를 커밋한다 · 다르면 `ShapingTestFontTest`) | `Test/EngineTest/Text/Fonts/swshapingtest.ttf` |
| `GenerateSpriteTextures.py` | 엔진이 들고 다니는 작은 스프라이트 텍스처(DDS)와 클립 — 네 칸 테스트 텍스처 · 1 채널 R8 마스크 (손으로 돌린다, 결과를 커밋한다) | `Resource/engine/textures/test/quadrants.*` · `Resource/engine/textures/test/mask_r8.dds` · `Resource/engine/textures/missing.dds` |

`setup/` 은 **외부 도구를 찾아 설치하는** 폴더이고, 원본에서 파일을 만들어 내는 일은 구성 시점이든 빌드 시점이든 여기다.

## 쿠킹은 엔진이 한다

씬 · 프리팹 · 레지스트리 · VAT · 내비메시와 리소스 팩은 모두 엔진(`App` · 전용 서버는 `Server`)이 굽습니다 — 이 폴더에 쿠커가 없습니다.
`--cook-scenes`(스테이징), `--cook-packs`(팩, `PackCooker`), `--cook-all`(둘 다)이고 폴더는 `--cooked-dir=<스테이징>` · `--pack-dir=<팩 폴더>` 입니다. CMake 의 `CookAssets` 타깃이 `--cook-all` 을 부릅니다.
쿠킹은 소스 트리(`ContentSource::SourceTree`)를 읽고 팩을 마운트하지 않습니다. 모르는 타입의 컴포넌트가 든 씬은 실패로 세고 실패가 하나라도 있으면 실행 파일이 0 이 아닌 코드로 끝나 빌드가 멈춥니다.
팩 레이아웃의 원본은 `Source/Engine/Resource/Pack/ResourcePakTypes.h`, 쓰는 곳은 `ResourcePakWriter` 하나입니다.

## 팩 압축 코덱 고르기

`Config/Engine/PackConfig.json` 의 `_compression` 이 정합니다(형식 `PackConfig`, 이름은 `PackCompressionType` 의 것).

```json
"_compression": { "_codec": "Zlib", "_level": 0 }
```

`None` · `RLE` · `Zlib` · `LZ4` · `Zstd` 모두 엔진 코덱이라 도구 설치가 필요 없고 기본값은 `Zlib` 입니다.
이 프로젝트의 실제 팩은 내용이 이미 압축된 것(DDS 텍스처 · 셰이더 바이너리)이라 코덱 차이가 거의 없고(Zlib 과 Zstd 는 0.1 % 안쪽) **LZ4 만 눈에 띄게 커집니다**(큰 팩에서 +28 %). 합성 데이터에서는 Zstd 가 훨씬 작았으므로 벤치마크 표본이 곧 결론은 아닙니다.
압축되지 않은 자산(대량의 JSON · XML · 오디오 PCM 등)이 팩에 들어오면 다시 재 보십시오.

## 실행 방법

```bash
build/Ninja-Debug/Bin/App.exe --cook-all --cooked-dir=build/Ninja-Debug/Cooked --pack-dir=build/Ninja-Debug/Bin/Packs [-dx12] [--game=<게임>] [--verify-shaders]
# CMake 의 CookAssets 타깃이 같은 호출을 한다(App 뒤에 돈다).
py -3 Scripts/generate/GenerateShippingHostDefaults.py <output_header_path>
py -3 Scripts/generate/GenerateDocs.py [--open]
```
