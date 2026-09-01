# ==============================================================================
# @file cmake/Config/BuildOptions.cmake
# @brief SW Engine 전역 기능 빌드 옵션(SW_*) 및 워크스페이스 메타데이터 정의
#
# [CMake 네이밍 및 개발 규칙]:
# - sw_camelCase      : CMake function() / macro() 헬퍼 함수 (예: sw_configurePch, sw_addGameModule)
# - sw_snake_case     : 프로젝트 내부 변수 및 INTERFACE 타겟명 (예: sw_flag_libraries)
# - SW_UPPER_SNAKE    : option() / CACHE 빌드 옵션 및 C++ 컴파일 매크로 (예: SW_ENABLE_PCH, SW_SHIPPING_BUILD)
# - camelCase         : 함수/매크로 내부 로컬 변수
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 워크스페이스 메타데이터 · C++ 표준 · 바이너리 출력 루트
# ------------------------------------------------------------------------------
set(sw_workspace_name "Workspace")
set(sw_project_version "1.0.0")

# 최소 C++17 표준 요구. 필요 시 -Dsw_cpp_standard=20 (또는 23)으로 오버라이드 가능.
if(NOT DEFINED sw_cpp_standard)
	set(sw_cpp_standard 17)
endif()
set(sw_output_directory "${CMAKE_BINARY_DIR}")

# ------------------------------------------------------------------------------
# 2) 핵심 빌드 및 아키텍처 기능 옵션 (SW_* / option) — 논리 그룹별 알파벳 정렬
# ------------------------------------------------------------------------------
# 2-1) 빌드 모드 및 엔진 기능 옵션
option(SW_BUILD_DOCS "Doxygen 코드 문서화 생성 타겟 추가" OFF)
option(SW_BUILD_GAME "GameFramework 및 게임 모듈(SWGame DLL/정적 링크) 빌드" ON)
option(SW_BUILD_GAMEFRAMEWORK "Source/GameFramework 및 게임 장르별 키트 라이브러리 빌드" ON)
option(SW_ENABLE_PCH "빌드 속도 단축을 위한 프리컴파일드 헤더(PCH) 사용" ON)

function(sw_configurePch targetName headerPath)
	if(SW_ENABLE_PCH)
		target_precompile_headers(${targetName} PRIVATE "${headerPath}")
	endif()
endfunction()
option(SW_ENABLE_SANITIZER "Address/UB Sanitizer 컴파일러 플래그 모듈 활성화" OFF)
option(SW_ENABLE_TESTING "단위/통합 테스트 프로젝트 빌드 및 CTest 등록" ON)
option(SW_ENABLE_UNITY_BUILD "대형 라이브러리 타겟에 CMake UNITY_BUILD(소스 묶음 컴파일) 사용" OFF)
option(SW_GLOB_CONFIGURE_DEPENDS "소스 파일 추가/삭제를 빌드 시스템이 자동 감지하도록 CONFIGURE_DEPENDS 활성화 (Ninja 권장)" ON)
option(SW_REQUIRE_REFLECTION "Engine/SWGame 등 리플렉션 타겟에 ReflectionParser 및 libclang 필수 요구" ON)
option(SW_RHI_AS_MODULES "RHI 그래픽스 백엔드(DX11/DX12/GL/Vulkan)를 MODULE DLL 플러그인으로 분리 빌드" ON)
option(SW_SHIPPING_BUILD "배포용 단일 실행 파일 정적 링크 빌드 (Editor 모듈 제외 및 최고 성능 최적화)" OFF)
option(SW_USE_SCCACHE "사용 가능 시 sccache 컴파일러 캐시를 활성화하여 빌드 가속" ON)

# 2-2) 도구 및 vcpkg 부트스트랩 옵션
option(SW_LLVM_AUTO_BOOTSTRAP "LLVM이 없을 때 SetupLlvm.py를 통해 Tools/LLVM에 최소 clang-cl+libclang 키트 자동 다운로드 허용" ON)
option(SW_USE_VCPKG "vcpkg 패키지 매니저 연동 및 툴체인 사용" ON)
option(SW_VCPKG_AUTO_BOOTSTRAP "vcpkg가 없을 때 Scripts/setup/SetupVcpkg.py를 통해 Tools/vcpkg로 자동 git clone 허용" ON)
option(SW_VCPKG_FORCE_INSTALL "설치 트리 및 스탬프가 일치해도 vcpkg manifest install 강제 실행" OFF)

# ------------------------------------------------------------------------------
# 3) 디버깅 및 진단 도구 옵션
# ------------------------------------------------------------------------------
option(SW_ENABLE_DEADLOCK_DETECTION "sw::Mutex 잠금 순서를 실시간 추적하여 데드락 사이클 탐지 (성능 저하 주의)" OFF)
option(SW_ENABLE_LTO "Shipping 배포 빌드 시 ThinLTO(링크 타임 최적화) 활성화" ON)
option(SW_ENABLE_STL_CONTAINER "엔진 커스텀 할당자 대신 std::allocator를 사용하도록 설정" OFF)
option(SW_ENABLE_TIME_TRACE "Clang 컴파일 시간 프로파일링(-ftime-trace JSON 출력)" OFF)

# 활성화할 대상 게임 팩 선택 (Source/Games/ 하위 디렉터리 이름)
set(SW_ACTIVE_GAME "Empty" CACHE STRING "활성화할 Source/Games 게임 팩 (Empty)")
set_property(CACHE SW_ACTIVE_GAME PROPERTY STRINGS Empty)

# ------------------------------------------------------------------------------
# 4) 컴파일 PDB · compile_commands.json · 플래그 INTERFACE 초기화
# ------------------------------------------------------------------------------
set(CMAKE_COMPILE_PDB_NAME "compile")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

unset(CMAKE_BUILD_PARALLEL_LEVEL CACHE)
set(sw_flag_libraries "")

# ------------------------------------------------------------------------------
# 5) Ninja 빌드 작업 풀(Job Pools) — 컴파일(CPU 풀가동) vs 링크(메모리/디스크 제한) 분리
# ------------------------------------------------------------------------------
if(CMAKE_GENERATOR MATCHES "Ninja")
	cmake_host_system_information(RESULT cpuCount QUERY NUMBER_OF_LOGICAL_CORES)
	if(NOT cpuCount OR cpuCount LESS 2)
		set(cpuCount 4)
	endif()

	# 링크 동시 실행 개수는 코어 수의 1/4 (최소 2, 최대 4)로 제한하여 RAM 부족/페이징 스래싱 방지
	math(EXPR linkJobs "${cpuCount} / 4")
	if(linkJobs LESS 2)
		set(linkJobs 2)
	elseif(linkJobs GREATER 4)
		set(linkJobs 4)
	endif()

	set_property(GLOBAL PROPERTY JOB_POOLS compile_job_pool=${cpuCount} link_job_pool=${linkJobs})
	set(CMAKE_JOB_POOL_COMPILE compile_job_pool)
	set(CMAKE_JOB_POOL_LINK link_job_pool)
endif()
