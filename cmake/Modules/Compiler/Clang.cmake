# ==============================================================================
# @file cmake/Modules/Compiler/Clang.cmake
# @brief Clang / AppleClang 컴파일러 플래그 INTERFACE
# ==============================================================================

if(
	NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
	AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang"
)
	return()
endif()

# sccache/ccache는 DetectToolchain.cmake(SW_USE_SCCACHE)에서만 설정한다.

add_library(sw_compiler_clang INTERFACE)

# ------------------------------------------------------------------------------
# 1) SYSTEM include 플래그 — target_include_directories(... SYSTEM ...)용
#    MSVC=1 이면 clang-cl (MSVC 호환 드라이버) → /imsvc
# ------------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
	set(CMAKE_INCLUDE_SYSTEM_FLAG_C "/imsvc ")
	set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "/imsvc ")
else()
	set(CMAKE_INCLUDE_SYSTEM_FLAG_C "-isystem ")
	set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "-isystem ")
endif()

# ------------------------------------------------------------------------------
# 2) 컴파일 — 문자셋 → 코드젠 → 경고 → 서드파티 → 빌드설정(Debug/Release)
# ------------------------------------------------------------------------------
target_compile_options(sw_compiler_clang INTERFACE
	# 문자셋 (Clang GNU vs Clang-cl MSVC)
	$<$<NOT:$<BOOL:${MSVC}>>:-finput-charset=UTF-8> # 소스 파일 UTF-8 입력 인코딩 지정
	$<$<NOT:$<BOOL:${MSVC}>>:-fexec-charset=UTF-8>  # 바이너리 실행 문자셋 UTF-8 인코딩 지정
	$<$<AND:$<CXX_COMPILER_ID:Clang>,$<BOOL:${MSVC}>>:/utf-8> # clang-cl 소스 및 실행 문자셋 UTF-8 지정

	# 코드젠 / 최적화 기본 설정
	$<$<BOOL:${MSVC}>:/bigobj>                      # 대규모 템플릿/리플렉션 오브젝트용 64K+ 섹션 허용
	$<$<BOOL:${MSVC}>:/GR->                        # RTTI 비활성화로 바이너리 크기 및 vtable 오버헤드 축소
	$<$<BOOL:${MSVC}>:/Gw>                         # 전체 프로그램 데이터/가상함수 최적화 지원
	$<$<BOOL:${MSVC}>:/Zc:inline>                  # 미사용 인라인 함수 제거로 컴파일 및 링크 가속
	$<$<BOOL:${MSVC}>:-clang:-fdelayed-template-parsing> # 템플릿 정의 인스턴스화 시점 지연 파싱
	$<$<BOOL:${MSVC}>:-clang:-fmerge-all-constants>      # 중복된 상수 문자열 및 데이터 병합
	$<$<BOOL:${MSVC}>:-clang:-fno-spell-checking>        # 오타 교정 기능 비활성화로 컴파일 속도 향상
	-Xclang
	-fno-pch-timestamp                             # sccache 캐시 적중률 향상을 위한 PCH 타임스탬프 검사 비활성화
	$<$<AND:$<NOT:$<BOOL:${MSVC}>>,$<COMPILE_LANGUAGE:CXX>>:-fno-rtti> # GNU 드라이버 RTTI 비활성화

	# 경고 활성화
	-Wall
	-Wextra

	# 경고 비활성화 (C++17+ 표준 및 엔진 아키텍처 지원, 알파벳 정렬)
	-Wno-c++98-compat          # C++17+ 타깃 프로젝트이므로 C++98 하위 호환성 경고 억제
	-Wno-c++98-compat-pedantic # C++17+ 타깃 프로젝트이므로 C++98 pedantic 경고 억제
	-Wno-cast-function-type-strict # C-ABI 동적 심볼(GetProcAddress/vkGetInstanceProcAddr 등) 함수 포인터 캐스팅 허용
	-Wno-covered-switch-default# 모든 enum 케이스를 다루더라도 방어적 default: 레이블을 항상 작성할 수 있도록 허용 (-Wswitch-default 충돌 방지)
	-Wno-exit-time-destructors # 정적 전역 레지스트라(SW_TEST_CASE, 리플렉션 등록 등)의 종료 소멸자 허용
	-Wno-global-constructors   # 정적 전역 생성자(SW_GLOBAL_VARIABLE, 테스트 등록 등) 허용
	-Wno-invalid-offsetof      # 다형성/비-표준 레이아웃 클래스 대상 리플렉션 프로퍼티 오프셋 연산 허용
	-Wno-padded                # 64비트 정렬(alignas)에 따른 자연스러운 구조체 패딩 허용
	-Wno-unknown-warning-option# 다양한 Clang 버전 간 신규/미지원 경고 옵션 억제 경고 방지
	-Wno-unsafe-buffer-usage   # RHI/그래픽스/SIMD 등 네이티브 포인터 버퍼 연산 허용
	$<$<BOOL:${MSVC}>:-Wno-language-extension-token> # Windows SDK 헤더 및 MSVC 전용 키워드(__declspec, __FUNCSIG__, __forceinline 등) 사용 허용

	# 서드파티 외부 헤더 경고 억제 (clang-cl /external; SYSTEM은 위의 /imsvc 사용)
	$<$<BOOL:${MSVC}>:/external:I${CMAKE_SOURCE_DIR}/ThirdParty> # ThirdParty 디렉터리를 외부 헤더로 지정
	$<$<BOOL:${MSVC}>:/external:W0>                              # 외부 헤더에 대한 모든 컴파일러 경고 비활성화

	# 빌드 설정별 최적화 및 디버그 심볼
	# Debug: 디버그 심볼 & 최적화 끄기
	$<$<CONFIG:Debug>:-g>                          # DWARF/디버그 정보 생성 (GNU 드라이버)
	$<$<AND:$<NOT:$<BOOL:${MSVC}>>,$<CONFIG:Debug>>:-O0>        # 디버그 최적화 비활성화
	$<$<AND:$<NOT:$<BOOL:${MSVC}>>,$<CONFIG:Debug>>:-gz=zlib>   # 디버그 정보 zlib 압축
	$<$<AND:$<BOOL:${MSVC}>,$<CONFIG:Debug>>:/Od>               # clang-cl 디버그 최적화 끄기
	$<$<AND:$<BOOL:${MSVC}>,$<CONFIG:Debug>>:/Z7>               # clang-cl CodeView 디버그 심볼 생성

	# Release: 최고 최적화 & AVX2 & 부동소수점 벡터화 가속
	$<$<AND:$<NOT:$<BOOL:${MSVC}>>,$<CONFIG:Release>>:-O3>                # GNU 드라이버 최고 수준 최적화
	$<$<AND:$<NOT:$<BOOL:${MSVC}>>,$<CONFIG:Release>>:-mavx2>             # AVX2 256비트 SIMD 벡터화 명령어 활성화
	$<$<AND:$<NOT:$<BOOL:${MSVC}>>,$<CONFIG:Release>>:-fno-math-errno>    # 수학 함수 호출 시 errno 설정 오버헤드 제거
	$<$<AND:$<NOT:$<BOOL:${MSVC}>>,$<CONFIG:Release>>:-fno-trapping-math> # 부동 소수점 예외 트랩 비활성화
	$<$<AND:$<BOOL:${MSVC}>,$<CONFIG:Release>>:/O2>                       # clang-cl 속도 우선 최적화
	$<$<AND:$<BOOL:${MSVC}>,$<CONFIG:Release>>:/arch:AVX2>                # clang-cl AVX2 SIMD 확장 세트 사용
	$<$<AND:$<BOOL:${MSVC}>,$<CONFIG:Release>>:-clang:-fno-math-errno>    # clang-cl 수학 함수 errno 비활성화
	$<$<AND:$<BOOL:${MSVC}>,$<CONFIG:Release>>:-clang:-fno-trapping-math> # clang-cl 부동 소수점 트랩 비활성화

	# Clang -ftime-trace 컴파일 타임 프로파일러 (SW_ENABLE_TIME_TRACE=ON)
	$<$<AND:$<BOOL:${SW_ENABLE_TIME_TRACE}>,$<NOT:$<BOOL:${MSVC}>>>:-ftime-trace>
	$<$<AND:$<BOOL:${SW_ENABLE_TIME_TRACE}>,$<BOOL:${MSVC}>>:-clang:-ftime-trace>
)

# ------------------------------------------------------------------------------
# 3) 링크 — LLD가 있으면 사용, clang-cl은 MSVC 스타일 링크 옵션
# ------------------------------------------------------------------------------
include("${CMAKE_CURRENT_LIST_DIR}/../Toolchain/FindLlvmBin.cmake")
sw_findLlvmBin(llvmBin)
find_program(SW_LLD_LINK_EXE NAMES lld-link lld HINTS "${llvmBin}")
if(SW_LLD_LINK_EXE)
	message(STATUS "[Clang.cmake] LLD Fast Linker detected: ${SW_LLD_LINK_EXE}")
endif()

target_link_options(sw_compiler_clang INTERFACE
	$<$<NOT:$<BOOL:${MSVC}>>:-fuse-ld=lld>          # LLVM 고속 lld 링커 사용
	$<$<AND:$<BOOL:${MSVC}>,$<CONFIG:Debug>>:/INCREMENTAL:NO> # 단일 패스 결정론적 빠른 링킹
	$<$<AND:$<BOOL:${MSVC}>,$<CONFIG:Release>>:/OPT:REF>      # 미참조 함수 및 데이터 제거
	$<$<AND:$<BOOL:${MSVC}>,$<CONFIG:Release>>:/OPT:ICF>      # 동일한 코드의 중복 함수 병합(ICF)
)

target_compile_definitions(sw_compiler_clang INTERFACE SW_COMPILER_CLANG)

list(APPEND sw_flag_libraries sw_compiler_clang)
