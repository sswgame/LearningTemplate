# Tools (호스트 빌드 도구)

본격적인 엔진 컴파일 전에 미리 실행되어 빌드를 돕거나, 외부 패키지 매니저(vcpkg) 등이 보관되는 장소입니다.

## 주요 구성요소
- **ReflectionParser**: libclang으로 `REFLECT` / `PROPERTY` 등을 스캔해 `*.gen.cpp`를 생성합니다. 초심자용 흐름·CLI·템플릿 설명은 [ReflectionParser/README.md](ReflectionParser/README.md). 엔진 본체를 빌드하려면 이 도구가 먼저 준비되어야 합니다.
- **LLVM · Ninja · Sccache · vcpkg**: `Scripts/setup/` 의 셋업 스크립트(`SetupEnvironment.py` · `SetupLlvm.py` · `SetupVcpkg.py`)가 받아 두는 툴체인 · 패키지 매니저 자리입니다.
  clang-format 도 `LLVM/bin` 의 고정 판을 씁니다(PATH 의 다른 판이 아니라).
- **_cache**: 위 도구를 받을 때의 다운로드 캐시입니다.
- **DCC/Blender**: Blender 내보내기 애드온 — 고른 것을 엔진 규약으로 glTF(`models_raw/`) · 소켓 초안(`*.sockets.xml`)으로 내보내고 `App --import-models` 를 띄운다. [DCC/Blender/README.md](DCC/Blender/README.md).
- **launch-args**: VS Code 확장 — 소스에서 읽은 전역 변수(`-gv_*`) · 커맨드라인 인자를 사이드바에서 골라 CMake Tools 디버그 · 실행에 넘긴다(프로필로 어느 CMake 프로젝트에나). [launch-args/README.md](launch-args/README.md).
- 받아 오는 폴더는 `.gitignore` 로 빠져 있고, 이 폴더에서 커밋되는 소스는 `ReflectionParser` · `DCC` · `launch-args` 와 `CMakeLists.txt` 뿐입니다.
