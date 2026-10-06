<!-- 생성 문서 — 손으로 고치지 않는다. 정본은 코드다. 다시 만들기: py -3 Scripts/generate/GenerateConfigReference.py -->

# CMake 빌드 옵션 (`SW_*`)

[설정 색인](README.md)

configure 때 정한다(`cmake --preset <프리셋>` 또는 `-D<이름>=<값>`). 값을 바꾸면 다시 configure 한다. 게임을 바꾸는 것은 옵션이 아니라 프리셋이다(`Ninja-Debug-<게임>` — 빌드 폴더가 따로).

| 이름 | 타입 | 기본값 | 고를 수 있는 값 | 설명 | 정하는 프리셋 | 선언 |
|---|---|---|---|---|---|---|
| `SW_ACTIVE_GAME` | STRING | `Empty` |  | 활성화할 Source/Games 게임 팩 — 게임을 바꿀 때는 이 값이 아니라 프리셋(Ninja-Debug-<게임>)을 고른다 | Ninja-Debug-AbilityArena, Ninja-Debug-HarvestValley, Ninja-Debug-MeadowVillage 외 5 | [cmake/Config/BuildOptions.cmake](../../cmake/Config/BuildOptions.cmake) |
| `SW_BUILD_DOCS` | BOOL | `OFF` |  | Doxygen 코드 문서화 생성 타겟 추가 |  | [cmake/Config/BuildOptions.cmake](../../cmake/Config/BuildOptions.cmake) |
| `SW_BUILD_GAME` | BOOL | `ON` |  | GameFramework 및 게임 모듈(SWGame DLL/정적 링크) 빌드 |  | [cmake/Config/BuildOptions.cmake](../../cmake/Config/BuildOptions.cmake) |
| `SW_BUILD_GAMEFRAMEWORK` | BOOL | `ON` |  | Source/GameFramework 및 게임 장르별 키트 라이브러리 빌드 |  | [cmake/Config/BuildOptions.cmake](../../cmake/Config/BuildOptions.cmake) |
| `SW_CPP_STANDARD` | STRING | `17` |  | C++ 표준(17 이상 — 20 · 23) |  | [cmake/Config/BuildOptions.cmake](../../cmake/Config/BuildOptions.cmake) |
| `SW_ENABLE_DEADLOCK_DETECTION` | BOOL | `OFF` |  | sw::Mutex 잠금 순서를 실시간 추적하여 데드락 사이클 탐지 (성능 저하 주의) |  | [cmake/Config/BuildOptions.cmake](../../cmake/Config/BuildOptions.cmake) |
| `SW_ENABLE_FUZZING` | BOOL | `OFF` |  | 리눅스 clang + ASan 에서 libFuzzer 대상(LoaderFuzzer)과 엔진 커버리지 계측 | CI-Fuzz | [cmake/Config/BuildOptions.cmake](../../cmake/Config/BuildOptions.cmake) |
| `SW_ENABLE_LTO` | BOOL | `ON` |  | Shipping 배포 빌드 시 ThinLTO(링크 타임 최적화) 활성화 |  | [cmake/Config/BuildOptions.cmake](../../cmake/Config/BuildOptions.cmake) |
| `SW_ENABLE_PCH` | BOOL | `ON` |  | 빌드 속도 단축을 위한 프리컴파일드 헤더(PCH) 사용 |  | [cmake/Config/BuildOptions.cmake](../../cmake/Config/BuildOptions.cmake) |
| `SW_ENABLE_SANITIZER` | BOOL | `OFF` |  | Address/UB Sanitizer 컴파일러 플래그 모듈 활성화 | CI-Debug-ASAN, CI-Debug-TSAN, Ninja-Debug-ASAN | [cmake/Config/BuildOptions.cmake](../../cmake/Config/BuildOptions.cmake) |
| `SW_ENABLE_STL_CONTAINER` | BOOL | `OFF` |  | 엔진 커스텀 할당자 대신 std::allocator를 사용하도록 설정 | CI-Debug-STL | [cmake/Config/BuildOptions.cmake](../../cmake/Config/BuildOptions.cmake) |
| `SW_ENABLE_TESTING` | BOOL | `ON` |  | 단위/통합 테스트 프로젝트 빌드 및 CTest 등록 |  | [cmake/Config/BuildOptions.cmake](../../cmake/Config/BuildOptions.cmake) |
| `SW_ENABLE_TIME_TRACE` | BOOL | `OFF` |  | Clang 컴파일 시간 프로파일링(-ftime-trace JSON 출력) |  | [cmake/Config/BuildOptions.cmake](../../cmake/Config/BuildOptions.cmake) |
| `SW_ENABLE_TRACY` | BOOL | `${swTracyDefault}` |  | Tracy 프로파일러 클라이언트 링크(개발 빌드, Shipping 은 무시) |  | [cmake/Config/BuildOptions.cmake](../../cmake/Config/BuildOptions.cmake) |
| `SW_ENABLE_UNITY_BUILD` | BOOL | `OFF` |  | 대형 라이브러리 타겟에 CMake UNITY_BUILD(소스 묶음 컴파일) 사용 | CI-Debug, CI-Debug-ASAN, CI-Debug-TSAN 외 2 | [cmake/Config/BuildOptions.cmake](../../cmake/Config/BuildOptions.cmake) |
| `SW_LLVM_AUTO_BOOTSTRAP` | BOOL | `ON` |  | LLVM이 없을 때 SetupLlvm.py를 통해 Tools/LLVM에 최소 clang-cl+libclang 키트 자동 다운로드 허용 |  | [cmake/Config/BuildOptions.cmake](../../cmake/Config/BuildOptions.cmake) |
| `SW_RELEASE_DEBUG_INFO` | STRING | `lines` | `lines` · `full` · `none` | Release/Shipping debug info: lines \| full \| none |  | [cmake/Config/BuildOptions.cmake](../../cmake/Config/BuildOptions.cmake) |
| `SW_REQUIRE_REFLECTION` | BOOL | `ON` |  | Engine/SWGame 등 리플렉션 타겟에 ReflectionParser 및 libclang 필수 요구 |  | [cmake/Config/BuildOptions.cmake](../../cmake/Config/BuildOptions.cmake) |
| `SW_SANITIZER_KIND` | STRING | `address` | `address` · `thread` | SW_ENABLE_SANITIZER 가 켤 새니타이저: address \| thread | CI-Debug-TSAN | [cmake/Config/BuildOptions.cmake](../../cmake/Config/BuildOptions.cmake) |
| `SW_SHIPPING_BUILD` | BOOL | `OFF` |  | 배포용 단일 실행 파일 정적 링크 빌드 (Editor 모듈 제외 및 최고 성능 최적화) | CI-Shipping, Ninja-Shipping, WSL-Shipping | [cmake/Config/BuildOptions.cmake](../../cmake/Config/BuildOptions.cmake) |
| `SW_SHIPPING_RHI_BACKEND` | STRING | `DirectX12 · Vulkan` | `DirectX11` · `DirectX12` · `Vulkan` · `OpenGL` | Shipping 이 Engine 에 정적 링크할 RHI 백엔드(쿠킹 표 이름) |  | [cmake/Config/BuildOptions.cmake](../../cmake/Config/BuildOptions.cmake) |
| `SW_TARGET_TYPE` | STRING | `` | `""` · `Game` · `Client` · `Server` | 빌드 타깃 종류: Game \| Client \| Server (비우면 Shipping=Client, 그 밖=Game) | CI-Shipping, CI-Shipping-Server, Ninja-Debug-Server 외 5 | [cmake/Config/BuildOptions.cmake](../../cmake/Config/BuildOptions.cmake) |
| `SW_USE_SCCACHE` | BOOL | `ON` |  | 사용 가능 시 sccache 컴파일러 캐시를 활성화하여 빌드 가속 |  | [cmake/Config/BuildOptions.cmake](../../cmake/Config/BuildOptions.cmake) |
| `SW_USE_VCPKG` | BOOL | `ON` |  | vcpkg 패키지 매니저 연동 및 툴체인 사용 |  | [cmake/Config/BuildOptions.cmake](../../cmake/Config/BuildOptions.cmake) |
| `SW_VCPKG_AUTO_BOOTSTRAP` | BOOL | `ON` |  | vcpkg가 없을 때 Scripts/setup/SetupVcpkg.py를 통해 Tools/vcpkg로 자동 git clone 허용 |  | [cmake/Config/BuildOptions.cmake](../../cmake/Config/BuildOptions.cmake) |
| `SW_VCPKG_FORCE_INSTALL` | BOOL | `OFF` |  | 설치 트리 및 스탬프가 일치해도 vcpkg manifest install 강제 실행 | CI-Debug-TSAN | [cmake/Config/BuildOptions.cmake](../../cmake/Config/BuildOptions.cmake) |
