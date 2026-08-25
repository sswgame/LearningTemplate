# 🚀 Getting Started (시작하기)

SW Engine 프로젝트를 로컬 환경에 구성하고 첫 번째 빌드를 수행하는 방법을 안내합니다.

---

## 1. 사전 요구 사항 (Prerequisites)

엔진을 빌드하려면 다음 도구들이 시스템에 설치되어 있어야 합니다:
- **Windows 10/11**
- **Python 3.8+** (스크립트 실행 및 vcpkg 설정용)
- **Git**
- **Visual Studio 2022** (C++ 데스크톱 개발 워크로드)

## 2. 환경 구성 및 의존성 설치

SW Engine은 [vcpkg](https://vcpkg.io/)를 활용하여 C++ 서드파티 라이브러리를 통합 관리합니다.
모든 복잡한 툴체인(LLVM Clang, Ninja, Sccache 등)과 라이브러리 설정은 파이썬 스크립트로 자동화되어 있습니다.

PowerShell을 열고 저장소 루트 디렉터리에서 다음 명령어들을 순서대로 실행하세요:

```powershell
# 1. 컴파일러(LLVM), 빌드 시스템(Ninja), 캐시(Sccache) 등을 다운로드 및 설정
py -3 Scripts/setup/SetupEnvironment.py

# 2. vcpkg를 초기화하고 vcpkg.json에 명시된 모든 패키지를 설치
py -3 Scripts/setup/SetupVcpkg.py --install
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
./build/Ninja-Debug/Bin/App.exe
```

### 자동화 테스트 실행 (CTest)
엔진 코어나 리플렉션 시스템이 정상적으로 동작하는지 확인하려면 다음 명령어를 사용하세요:
```powershell
cd build/Ninja-Debug
ctest -C Debug --output-on-failure
```

---
[🏠 위키 홈으로 돌아가기](../README.md) | [▶ 다음: 서브시스템 개요](02_EngineSubsystems.md)
