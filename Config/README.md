# Config (호스트 / 개발 설정)

`Config/` 는 **개발·툴·런처 호스트** 설정입니다. 게임에 실려 나가는 기본 콘텐츠는 `Resource/<pack>/data/` 입니다.

## 두 층의 구분

| 위치 | 역할 | Shipping |
|------|------|----------|
| `Config/Engine/EngineConfig.json` | 런타임 창/RHI/`enginedata` 포인터 (C++ `EngineConfig`) | 베이크되어 exe에 포함, 디스크 불필요 |
| `Config/Game/GameConfig.json` | 팩 루트·gamedata 파일명 | 베이크 |
| `Config/App/AppConfig.json` | Dev 게임킷 모듈 목록 | 미포함 (정적 링크) |
| `Config/Editor/` | `EditorConfig.json` + `editordata.xml` + 유저 레이아웃 | 미포함 |
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
