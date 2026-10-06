# SW Engine (게임 및 에디터 엔진 템플릿)

[![CI (main)](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml?query=branch%3Amain)
[![Windows](https://img.shields.io/badge/Windows-clang--cl%20(Debug%20%7C%20Shipping)-0078D6?logo=windows&logoColor=white)](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml)
[![Linux](https://img.shields.io/badge/Linux-Clang%20(Debug%20%7C%20ASan%20%7C%20TSan%20%7C%20Shipping)-FCC624?logo=linux&logoColor=black)](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml)
[![C++17](https://img.shields.io/badge/Language-C%2B%2B17-00599C?logo=cplusplus&logoColor=white)](https://isocpp.org/)

C++17 게임 엔진 · 에디터 템플릿입니다. 개발(Dev) 빌드는 엔진이 DLL 이고 게임 · 에디터 · 장르 키트 · RHI 백엔드가 실행 중에 다시 읽히는
모듈(핫리로드)이며, 배포(Shipping) 빌드는 에디터를 빼고 전부를 실행 파일 하나에 정적 링크합니다. 렌더링은 DirectX 11 · DirectX 12 ·
Vulkan · OpenGL 네 백엔드이고, 여러 장르의 시험 게임이 같은 엔진 위에 있습니다([Source/Games](Source/Games/README.md)).
CI 구성(빌드 · 새니타이저 · 시험 집합)의 정본은 `.github/workflows/ci.yml` 입니다.

지원 플랫폼은 Windows(clang-cl)와 Linux(Clang) 둘입니다. macOS 는 지원하지 않습니다 — Apple 호스트에서는 CMake 구성이 멈춥니다.

## 빌드

```powershell
py -3 Scripts/setup/SetupEnvironment.py       # 툴체인(LLVM · Ninja · sccache) 받기
py -3 Scripts/setup/SetupVcpkg.py --install   # vcpkg 매니페스트 복원
py -3 Scripts/setup/InstallGitHooks.py        # 커밋 훅(규칙 · 포맷 검사)
cmake --preset Ninja-Debug
cmake --build --preset Ninja-Debug
```

필요한 도구 · 프리셋 · Linux/WSL 은 [시작하기](docs/01_GettingStarted.md)에 있습니다.

## 처음 돌리기

```powershell
./build/Ninja-Debug/Bin/App.exe                   # 게임만(기본 백엔드 DirectX12)
./build/Ninja-Debug/Bin/App.exe -EnableEditor     # 에디터까지
./build/Ninja-Debug/Bin/App.exe -vk -EnableEditor # 백엔드 고르기: -dx11 · -dx12 · -vk · -gl
ctest --test-dir build/Ninja-Debug -L nogpu --output-on-failure   # CI 와 같은 시험 집합
```

실행 인자 · 셰이더 쿠킹 · 진단 도구는 [시작하기](docs/01_GettingStarted.md), 시험은 [Test/README.md](Test/README.md)에 있습니다.
CI 는 `nogpu` 시험만 돌립니다 — GPU · 창 · DXC 가 필요한 `hostgpu` 시험은 GPU 가 있는 기계에서 직접 돌립니다.

## 폴더

| 폴더 | 무엇 |
| :--- | :--- |
| `Source/Core` | 토대 — 로그 · 메모리 · 문자열 · 컨테이너 · 태스크 · 파일 · 네트워크 공통 계층. Engine 이 흡수한다 |
| `Source/Engine` | 엔진 — 기동 단계 표 · RHI · 렌더러 · 오브젝트 · 씬 · 리플렉션 · 직렬화 · 물리 · 오디오 · 입력 |
| `Source/RuntimeAPI` | App ↔ 모듈의 C-ABI 계약(헤더만) |
| `Source/App` | 얇은 실행 파일 — 프레임 순서 · 모듈 호스트 · 핫리로드 · 백엔드 교체 |
| `Source/Server` | 전용 서버 실행 파일 — 창 · GPU 없이 게임 모듈을 고정 틱으로 돌린다(빌드 타깃 Game · Server) |
| `Source/Editor` | Dev 전용 ImGui 에디터 모듈 |
| `Source/GameFramework` | 장르 공통 기반(`Base/` — 온라인 기반 포함)과 장르 키트(`GF_*` · 서버 키트 `GF_Server_*`) |
| `Source/Games` | 게임 — `SW_ACTIVE_GAME`(게임별 프리셋 `Ninja-Debug-<게임>`)이 하나를 고른다 |
| `Resource/` | 런타임 에셋(`engine/` · `common/` · `game/<게임>/`, 이름은 전부 소문자) |
| `Config/` | 엔진 · 에디터 · 게임 · 서버 설정과 쿠킹 표([설정 문서](docs/07_Configuration.md)) |
| `Scripts/` · `cmake/` · `Tools/` | 파이썬 도구와 린트 · CMake 모듈 · ReflectionParser · 온라인 부하 시험 봇과 내려받는 툴체인 자리 |
| `Test/` | 시험 실행 파일과 자체 시험 프레임워크 |

## 문서

- [문서 지도](docs/02_DocumentMap.md) — 어떤 문서가 무엇의 정본인지, 모듈 README 전체 목록.
- [ARCHITECTURE.md](ARCHITECTURE.md) — 타깃 그래프 · 모듈 경계 · 엔진 층 · 주의사항.
- [AGENTS.md](AGENTS.md)(규칙 정본) · [코딩 규칙 예시](docs/04_CodingGuidelines.md) — 이름 · include · 선언 순서 규칙(린트가 강제한다).
- [설정](docs/07_Configuration.md) — 설정 값을 어디에 두는가, 칸 표는 생성 문서 [docs/Config](docs/Config/README.md).
- [백로그](docs/06_Backlog.md) — 남은 일. 작업을 시작하기 전에 읽는다.
