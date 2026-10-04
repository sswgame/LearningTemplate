# Config (호스트 / 개발 설정)

`Config/` 는 **개발·툴·런처 호스트** 설정입니다. 게임에 실려 나가는 기본 콘텐츠는 `Resource/<pack>/data/` 입니다.

## 두 층의 구분

| 위치 | 역할 | Shipping |
|------|------|----------|
| `Config/Engine/EngineConfig.json` | 런타임 창/RHI/`enginedefaultassets` 포인터 (C++ `EngineConfig`) | 생성되어 exe에 포함, 디스크 불필요 |
| `Config/Engine/CookContract.json` | RHI 백엔드 표(이름 · 셰이더 폴더 · 명령줄 별칭 · 기본 백엔드)와 쿠킹 확장자 표 — C++(configure 때 `CookContract.gen.h`)와 Python 쿠커가 같이 읽는 단일 출처 | 빌드에 굳어 들어감 |
| `Config/Engine/PackConfig.json` · `PackFormat.json` | 리소스 팩 쿠킹 설정(코덱 · 제외 폴더 — `textures_raw` 등)과 `.pack` 바이너리 포맷의 단일 출처(C++ 는 `PackFormat.gen.h`, Python 쿠커는 JSON 을 직접 읽는다) | 빌드 · 쿠킹 전용, 미포함 |
| `Config/Game/GameConfig.json` | 팩 루트·gamesettings 파일명 | 생성 |
| `Config/App/AppConfig.json` | Dev 게임킷 모듈 목록 | 미포함 (정적 링크) |
| `Config/Editor/` | `EditorConfig.json` + `editortooldefaults.json` + `TextureImportConfig.json`(텍스처 임포트 규칙: 포맷 · sRGB · 밉) + 유저 레이아웃 | 미포함 |
| `Config/Environment/` | 머신 로컬 툴체인·파서 | **절대 미포함** |

## Environment (툴체인)

| 파일 | 설명 |
|------|------|
| `toolchain_config.json` | SetupEnvironment가 채운 LLVM/vcpkg/SDK **절대 경로 캐시** (Git 무시) |
| `search_paths.json` / `*.defaults.json` | 도구 탐색 후보 |
| `parser_config.json` / `*.defaults.json` | ReflectionParser 플래그 |

`.defaults.json` 을 시드로 커밋하고, 로컬 `*.json` 은 생성·Git 무시합니다.

## 명칭 주의

- **`EngineConfig.json`**: 런타임 엔진 호스트 (창, RHI). C++ `sw::EngineConfig`.
- **`toolchain_config.json`**: 개발 PC 컴파일러/SDK 경로. 런타임 `EngineConfig.json`과 혼동하지 말 것.
