# 🚀 Getting Started (시작하기)

SW Engine 프로젝트를 로컬 환경에 구성하고 첫 번째 빌드를 수행하는 방법을 안내합니다.

---

## 1. 사전 요구 사항 (Prerequisites)

엔진을 빌드하려면 다음 도구들이 시스템에 설치되어 있어야 합니다:

**공통**
- **Python 3.10+** (스크립트 실행 및 vcpkg 설정용 — 최소 판은 `Scripts/lint/gate/CheckPythonMinimumVersion.py` 의 `kMinimumVersion`)
- **Git**

**Windows** (기본 개발 환경)
- **Windows 10/11**
- **Visual Studio 2022** (C++ 데스크톱 개발 워크로드 — Windows SDK 와 MSVC 헤더가 필요하다.
  컴파일러 자체는 `SetupEnvironment.py` 가 받는 clang-cl 을 쓴다)

**Linux / WSL** (`WSL-*` 프리셋, CI 의 ubuntu 잡과 같은 환경)
- `py -3 Scripts/setup/SetupLinuxDevEnvironment.py` 가 필요한 패키지를 알려 준다(X11·Wayland·
  Vulkan 개발 헤더, LLVM). WSL 에서는 **리눅스 파일시스템(`~`)에 클론해야 한다** — `/mnt/...`(DrvFs)
  에서는 `configure_file` 이 `Operation not permitted` 로 죽는다.

## 2. 환경 구성 및 의존성 설치

SW Engine은 [vcpkg](https://vcpkg.io/)를 활용하여 C++ 서드파티 라이브러리를 통합 관리합니다.
모든 복잡한 툴체인(LLVM Clang, Ninja, Sccache 등)과 라이브러리 설정은 파이썬 스크립트로 자동화되어 있습니다.

PowerShell을 열고 저장소 루트 디렉터리에서 다음 명령어들을 순서대로 실행하세요:

```powershell
# 1. 컴파일러(LLVM), 빌드 시스템(Ninja), 캐시(Sccache) 등을 다운로드 및 설정
py -3 Scripts/setup/SetupEnvironment.py

# 2. vcpkg를 초기화하고 vcpkg.json에 명시된 모든 패키지를 설치
py -3 Scripts/setup/SetupVcpkg.py --install

# 3. 커밋 전 컨벤션·포맷 검사를 걸어 두는 git 훅 설치 (권장)
py -3 Scripts/setup/InstallGitHooks.py
```
이 과정은 최초 1회만 수행하면 되며, 필요한 경우 시간이 다소 소요될 수 있습니다.

## 3. CMake 구성 및 빌드

환경 구성이 완료되었다면 CMake를 사용해 프로젝트를 빌드합니다.
SW Engine은 초고속 빌드를 위해 **Ninja** 생성기를 기본으로 사용합니다.

```powershell
# CMake 구성 (Debug 프리셋 사용)
cmake --preset Ninja-Debug

# 실제 빌드 수행
cmake --build --preset Ninja-Debug
```
> **Tip:** 코어 수가 많다면 `sccache` 덕분에 첫 빌드 이후의 증분 빌드(Incremental Build) 속도가 비약적으로 상승합니다.

## 4. 첫 번째 실행 및 테스트

빌드가 성공적으로 완료되면 바이너리들은 `build/Ninja-Debug/Bin/` 디렉터리에 생성됩니다.

### 엔진 데모 실행
```powershell
./build/Ninja-Debug/Bin/App.exe                  # 게임만 (기본 백엔드 DirectX12)
./build/Ninja-Debug/Bin/App.exe -EnableEditor    # 에디터까지 — 주지 않으면 에디터 모듈을 올리지 않는다
./build/Ninja-Debug/Bin/App.exe -vk -EnableEditor # 백엔드 고르기: -dx11 · -dx12 · -vk · -gl
```
실행 인자 전체는 [README 4절](../README.md#실행-인자)(정본: `Source/Core/Predefined/ArgumentList.xxx`)에 있습니다.

> **셰이더 · 텍스처는 빌드가 쿠킹 · 임포트하지 않습니다.** HLSL 을 고쳤으면 `App.exe --cook-shaders`, `Resource/**/textures_raw/` 의 원본 이미지를
> 고쳤으면 `App.exe --import-textures`(Dev 빌드)로 쿠킹 · 임포트하고, 결과(셰이더 바이너리 + `cook.stamp` · DDS + `import.stamp`)를 같이 커밋합니다.

### 자동화 테스트 실행 (CTest)
엔진 코어나 리플렉션 시스템이 정상적으로 동작하는지 확인하려면 다음 명령어를 사용하세요:
```powershell
# GPU 없이 도는 집합 (CI 와 같다) — 평소엔 이걸 쓴다
ctest --test-dir build/Ninja-Debug -L nogpu --output-on-failure

# 컨벤션·include 순서 등 Python 정적 검사만
ctest --preset Ninja-Debug-lint

# 전체 (GPU 가 있어야 하는 렌더링 검증까지)
ctest --test-dir build/Ninja-Debug --output-on-failure

# CI 가 못 돌리는 GPU · 창 · DXC 시험 — 작업을 끝내기 전에 GPU 있는 기계에서 배포 구성으로
ctest --test-dir build/Ninja-Shipping -L hostgpu --output-on-failure
```
> Ninja 는 단일 구성(single-config) 생성기라 `ctest -C Debug` 의 `-C` 는 아무 일도 하지 않습니다.
> 구성은 프리셋(=빌드 디렉터리)이 정합니다.

설정 파일 · 전역 변수 · 명령줄 인자의 전체 표는 [docs/Config](Config/README.md), 어디에 두나는 [07_Configuration.md](07_Configuration.md).

## 5. 무엇이 일어났는지 보기 (진단 도구)

"왜 이렇게 됐나" 를 디버거 없이 묻는 길입니다. 실행 플래그는 `App.exe` 뒤에 붙이고, 스스로 끝나게 `-gv_profileFrames=3` 을 같이 줍니다.
`-gv_dump*` 는 Debug · Release 빌드에만 있습니다(Shipping 에서는 빠집니다).

| 묻고 싶은 것 | 보는 법 |
|---|---|
| 이 타입 · enum 이 실행 중에 어떻게 등록됐나(부모 사슬 · 실제 자리 · 범위 · 플래그 · 열거자) | `-gv_dumpReflection=CameraComponent,CameraRole` |
| 리플렉션 파서가 이 헤더에서 무엇을 뽑았나 | `ReflectionParser --dump …` (예시 명령은 `Tools/ReflectionParser/README.md`) |
| 렌더 패스가 어떤 순서 · 레벨로 도나, 무엇이 컬링됐나 | `-gv_dumpRenderGraph=1` |
| 렌더 그래프가 순환해서 그려지지 않는다 | 로그의 `'A' waits on 'B'` 줄들 |
| 씬 · 프리팹 · XML · JSON 이 왜 안 읽히나 | 로그의 `경로:줄:열: 이유` (코드에서는 `XmlDocument` · `JsonDocument::getLastError()`) — 없는 파일만 `not found` 라고 한다 |
| 에셋의 enum 값이 왜 안 먹나 | 경고 `'Bogus' is not a value of enum sw::X - the field keeps its current value` |
| 짧은 이름이 같은 타입 둘 | 경고 `Reflected type name 'X' now means a::X and no longer b::X` |
| 디버거 없이 `SW_ASSERT` 가 멈춘 자리 | stderr 의 `[SW_ASSERT] 식 / at 파일:줄 / in 함수`, 크래시면 `Saved/Logs/crash_*` 의 스택 |
| 로그에 이상한 바이트가 섞였다 | 잘못된 UTF-8 바이트만 `\xNN` 으로 남고 나머지 글은 그대로다 |
| 화면에 무엇이 나갔나 | `-gv_screenshot=out.ppm` (`-gv_screenshotFrame=N`, 연속이면 `-gv_screenshotCount=N -gv_screenshotInterval=K`) |

시험을 쓸 때의 도우미(`Test/TestFramework/TestFramework.h`):

- `test::ScopedLogCollector` — 스코프 동안의 Warning · Error 를 모아 `countContaining( "…" )` 로 "그 경고가 나왔나" 를 묻는다. 실패 메시지에는 `joined()`.
- `test::ScopedDefensiveTestLog` — 일부러 내는 오류 · 경고를 `[Expected Defensive Test]` 로 표시한다(실패로 읽히지 않게).
- `test::makeTempPath( "이름" )` — 케이스마다 따로 지워지는 임시 경로.
- `SW_ASSERT_TRUE_MSG( 조건, 메시지 )` — 실패하면 메시지(대개 실제로 받은 글)를 남기고 그 케이스를 멈춘다.

---
[🏠 위키 홈으로 돌아가기](../README.md) | [▶ 다음: 서브시스템 개요](02_EngineSubsystems.md)
