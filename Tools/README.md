# Tools — 빌드 도구와 개발 도구

이 폴더에는 엔진 본체가 아닌 프로그램이 모여 있습니다. 두 종류가 있습니다.

- **저장소에 커밋된 도구.** 엔진 빌드에 필요한 코드 생성기, DCC 툴 애드온, 에디터 확장, 개발용 부하 테스트 봇입니다. 각 폴더에 README가 있습니다.
- **셋업 스크립트가 받아 두는 도구.** 컴파일러, 빌드 시스템, 패키지 매니저입니다. `.gitignore` 로 빠져 있어 커밋되지 않습니다.

## 커밋된 도구

| 폴더 | 무엇인가 | 문서 |
|---|---|---|
| `ReflectionParser/` | libclang으로 헤더의 `REFLECT`, `PROPERTY` 를 읽어 `*.gen.cpp` 를 만드는 코드 생성기. 엔진보다 먼저 빌드됩니다 | [README](ReflectionParser/README.md) |
| `DCC/Blender/` | Blender 애드온. 엔진 규약대로 glTF와 소켓 초안을 내보내고 임포트까지 실행합니다 | [README](DCC/Blender/README.md) |
| `launch-args/` | VS Code 확장. 전역 변수(`-gv_*`)와 명령줄 인자를 사이드바에서 골라 실행과 디버그에 넘깁니다 | [README](launch-args/README.md) |
| `OnlineLoadBot/` | 온라인 서비스 부하 테스트 봇. 개발 구성의 Game 타깃에서만 빌드되고 배포물에는 없습니다 | [README](OnlineLoadBot/README.md) |

## 받아 두는 도구

`Scripts/setup/` 의 셋업 스크립트(`SetupEnvironment.py`, `SetupLlvm.py`, `SetupVcpkg.py`)가 아래 폴더를 채웁니다.

| 폴더 | 내용 |
|---|---|
| `LLVM/` | clang-cl, clang, lld, clang-format |
| `Ninja/` | Ninja 빌드 시스템 |
| `Sccache/` | 컴파일 캐시 |
| `vcpkg/` | 패키지 매니저 |
| `_cache/`, `_deps/` | 다운로드 캐시 |
| `Tracy/` | Tracy 뷰어. 직접 받아 둡니다([Profiling README](../Source/Engine/Utility/Profiling/README.md)) |

clang-format은 PATH에 있는 다른 버전이 아니라 `LLVM/bin` 의 고정 버전을 씁니다. 버전마다 포맷 결과가 달라서, 다른 버전으로 포맷하면 바뀌지 않은 파일까지 바뀝니다.

처음 받은 저장소에서 빌드하는 순서는 [시작하기](../docs/01_GettingStarted.md)에 있습니다.
