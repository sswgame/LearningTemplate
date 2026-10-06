# Scripts/setup (환경 구성 스크립트)

LLVM(컴파일러), Ninja(빌드 도구), vcpkg(패키지 매니저) 등 엔진을 빌드하기 위해 필요한 **외부 도구(툴체인)들을 탐색하고 자동으로 다운로드/설치**하는 스크립트 모음입니다.

정본(JSON · `Constants.py`)에서 CMake 파일이나 헤더를 **만들어 내는** 스크립트는 여기 두지 않는다 — `Scripts/generate/` 에 있다
(`GenerateToolchainCMake.py` 는 이 폴더가 쓴 toolchain_config.json 을 읽지만, 하는 일이 생성이라 그쪽이다).

## ⚠️ 실행 시점
- 이 스크립트들은 주로 **CMake 구성(Configure)이 시작되기 직전에 실행**됩니다.
- 최상위에서 `python Scripts/setup/SetupEnvironment.py`를 실행하면 이 안의 스크립트들이 연쇄적으로 동작하며, 개발자의 PC에 부족한 툴을 스스로 찾아내고 세팅을 마친 뒤 `Config/Environment/toolchain_config.json`에 그 경로를 기록해 둡니다.

## 함정 · 계약

- **에디터 로컬 상태는 `Saved/Editor/` 에만 있다 — 다른 PC 의 체크아웃은 한 번 `py -3 Scripts/dev/MoveEditorState.py`**(옛 `Config/Editor/` 의 imgui.ini · 레이아웃 ·
  팩 안 gv 프리셋을 옮긴다). git 이 무시하는 파일이라 pull 로 옮겨지지 않고, 엔진은 옛 자리를 읽지 않아 안 돌리면 그 PC 의 레이아웃이 기본값으로 돌아간다.
- **리눅스 Vulkan 검증 레이어는 시스템 패키지(`vulkan-validationlayers`)다 — vcpkg 포트는 Windows 만**(`"platform": "!linux"`). Windows 만 레이어를 Bin 옆에
  복사하고 `VK_LAYER_PATH` 를 건다(`RuntimeDependencies.cmake` · `VulkanRHIDevice.cpp`). 리눅스 CI(ubuntu-22.04 · clang 14)에서 그 포트가 configure 에서 져 잡 넷이 섰다.
  레이어가 없으면 경고 한 줄 뒤 검증 없이 돈다.
- **리눅스 빌드는 WSL 안의 클론(`~/LearningTemplate`)에서 한다.** 그 클론의 변경은 사용자 것이라 현재 상태를 덮어 빌드해도 된다. 가져올 때는 그 클론에서
  `git fetch /mnt/d/Projects/Personal/LearningTemplate main`. **`WSL-*` 프리셋을 Windows 체크아웃(`/mnt/d`)에서 돌리지 말 것** — 같은 `build/vcpkg_installed` 를
  리눅스 트리플릿이 덮어 Windows 트리가 통째로 선다(복구 27 분). DrvFs 에서는 configure 자체가 `Operation not permitted` 로 죽는다.
- **`wsl.exe -- bash -c "cd X && …"` 의 `cd` 실패는 조용하다** — 뒤의 `cmake -B` 가 저장소 루트에서 돌아 `toolchain_config.json` 과 공유 vcpkg 를 덮는다. `set -e` 나
  절대 `-S`/`-B`. 중단 뒤 `waiting to take filesystem lock…` 이 끝없으면: 남은 vcpkg · cmake · ninja 프로세스 종료 → `vcpkg-running.lock` 셋 삭제
  (`build/vcpkg_installed/vcpkg/`, `Tools/vcpkg/buildtrees/`, `Tools/vcpkg/packages/`) → `toolchain_config.json` 복원 → Windows 프리셋 재구성.
- **sccache 서버 포트(4226)를 Windows 와 WSL 이 나눠 쓴다.** 동시에 빌드하면 모든 컴파일이 `failed to fill whole buffer` 로 깨진다(코드 오류처럼 보인다). WSL 은
  `export SCCACHE_SERVER_PORT=4227`, 아니면 상대 서버를 `sccache --stop-server`.
- **WSL 에는 GPU 가 없다**(Vulkan 은 `llvmpipe` 하나, GL 은 `ARB_gl_spirv` 가 없어 빠진다) — API 오용은 잡지만 드라이버 거동은 못 본다. `libwayland-dev` 가 필요하다.
  **gdb 가 없다** — `/proc/<pid>/task/*/stat` 의 utime · stime 을 두 번 떠 사용자/커널을 가르고, SIGUSR1 처리기(`backtrace_symbols_fd`)를 임시로 넣어 `tgkill` 로 스레드마다
  스택을 뜬다. 리눅스 파일 하나는 `clang++ -fsyntax-only -DSW_PLATFORM_LINUX … -include Source/Core/pch.h <file>` 로 검사할 수 있다.
- **Ubuntu 26.04 는 `libxml2.so.2` 가 없어 번들 `ld.lld` 가 뜨지 못한다** — 시스템 lld 를 `--ld-path=/usr/bin/ld.lld` 로 EXE · SHARED · MODULE 세 링커 플래그 모두에
  (`SetupLinuxDevEnvironment.py` 가 안내한다). 리눅스 LLVM 은 `/usr/lib/llvm-*` glob 자연순 내림차순으로 찾는다(손목록은 새 배포판을 비켜간다).
- **WSL 의 sccache 적중은 빈 의존 파일(.d)을 남길 수 있다** — 적중한 오브젝트의 `ninja -t deps` 가 `#deps 0` 이면 그 TU 의 소스 · 헤더를 고쳐도 `ninja: no work to do`
  다(유니티 TU 에서 봤다). 낡은 빌드가 의심되면 그 오브젝트를 지우거나 `SCCACHE_RECACHE=1` 로 다시 짓는다.
- **LLVM 을 다시 깔면 PCH 가 전부 낡는다**(`… has been modified since the precompiled header was built`). `.pch` 와 짝 `cmake_pch.cxx.obj` 를 같이 지운다(`SetupLlvm.py` 가 한다).
- **오랜만에 쓰는 WSL 클론은 많이 뒤처져 있을 수 있습니다.** 실패가 이번 변경 탓인지 보려면 패치 없는 HEAD 로 기준선을 먼저 잽니다. 클론은 `git fetch … main` 뒤 `git reset --hard FETCH_HEAD` 로 맞추고 stash 를 쌓지 않습니다.
  리눅스 전용 파일은 Windows 빌드가 컴파일하지 않으므로 고쳤으면 반드시 WSL 에서 빌드합니다.
