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
# MSVC=1 이면 clang-cl (MSVC 호환 드라이버) → /imsvc
# ------------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
	set(CMAKE_INCLUDE_SYSTEM_FLAG_C "/imsvc ")
	set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "/imsvc ")
else()
	set(CMAKE_INCLUDE_SYSTEM_FLAG_C "-isystem ")
	set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "-isystem ")
endif()

# ------------------------------------------------------------------------------
# 컴파일 — 드라이버 공통 → clang-cl(MSVC 드라이버) → GNU 드라이버
# MSVC 는 **설정 시점**에 값이 정해지는 변수다. 예전엔 플래그마다 $<BOOL:${MSVC}> 로 감쌌는데
# (32곳), 제너레이터 표현식은 설정 시점에 모르는 것(CONFIG/COMPILE_LANGUAGE)에나 쓸 값이다.
# 그렇게 감싸 두면 같은 개념을 드라이버마다 한 줄씩 적게 되고("디버그 최적화 끄기" 와
# "clang-cl 디버그 최적화 끄기"), 어느 쪽이 실제로 걸리는지 읽어서는 알 수 없다.
# if(MSVC) 로 가르면 각 블록이 그냥 플래그 목록이 된다.
# ------------------------------------------------------------------------------
target_compile_options(sw_compiler_clang INTERFACE
	# 경고 활성화
	-Wall
	-Wextra

	# 경고 비활성화 (C++17+ 표준 및 엔진 아키텍처 지원, 알파벳 정렬)
	-Wno-c++98-compat # C++17+ 타깃 프로젝트이므로 C++98 하위 호환성 경고 억제
	-Wno-c++98-compat-pedantic # C++17+ 타깃 프로젝트이므로 C++98 pedantic 경고 억제
	-Wno-cast-function-type-strict # C-ABI 동적 심볼(GetProcAddress/vkGetInstanceProcAddr 등) 함수 포인터 캐스팅 허용
	-Wno-covered-switch-default # 모든 enum 케이스를 다루더라도 방어적 default: 레이블을 항상 작성할 수 있도록 허용 (-Wswitch-default 충돌 방지)
	-Wno-exit-time-destructors # 정적 전역 레지스트라(SW_TEST_CASE, 리플렉션 등록 등)의 종료 소멸자 허용
	-Wno-float-equal # SW_EXPECT_EQUAL 등 테스트 매크로의 정확한 값 비교(반올림 없는 왕복 검증)를 위해 부동소수점 == 허용
	-Wno-global-constructors # 정적 전역 생성자(SW_GLOBAL_VARIABLE, 테스트 등록 등) 허용
	-Wno-invalid-offsetof # 다형성/비-표준 레이아웃 클래스 대상 리플렉션 프로퍼티 오프셋 연산 허용
	-Wno-padded # 64비트 정렬(alignas)에 따른 자연스러운 구조체 패딩 허용
	-Wno-unknown-warning-option # 다양한 Clang 버전 간 신규/미지원 경고 옵션 억제 경고 방지
	-Wno-unsafe-buffer-usage # RHI/그래픽스/SIMD 등 네이티브 포인터 버퍼 연산 허용

	# sccache 캐시 적중률 — PCH 타임스탬프 검사를 끈다
	-Xclang
	-fno-pch-timestamp
)

if(MSVC)
	# clang-cl — MSVC 호환 드라이버
	target_compile_options(sw_compiler_clang INTERFACE
		/utf-8 # 소스 및 실행 문자셋 UTF-8
		/bigobj # 대규모 템플릿/리플렉션 오브젝트용 64K+ 섹션 허용
		/GR- # RTTI 비활성화로 바이너리 크기 및 vtable 오버헤드 축소
		/Gw # 전체 프로그램 데이터/가상함수 최적화 지원
		/Zc:inline # 미사용 인라인 함수 제거로 컴파일 및 링크 가속
		-clang:-fdelayed-template-parsing # 템플릿 정의 인스턴스화 시점 지연 파싱
		-clang:-fmerge-all-constants # 중복된 상수 문자열 및 데이터 병합
		-clang:-fno-spell-checking # 오타 교정 기능 비활성화로 컴파일 속도 향상
		-Wno-language-extension-token # Windows SDK 및 MSVC 전용 키워드(__declspec, __FUNCSIG__ 등) 허용

		# 서드파티 외부 헤더는 경고를 내지 않는다 (SYSTEM include 는 위의 /imsvc)
		/external:I${CMAKE_SOURCE_DIR}/ThirdParty
		/external:W0

		$<$<CONFIG:Debug>:/Od>
		$<$<CONFIG:Debug>:/Z7> # CodeView 심볼을 오브젝트에 직접 (sccache 친화)
		# 원래 -g 는 드라이버 구분 없이 걸려 있었다(주석엔 "GNU 드라이버" 라고 적혀 있었지만
		# 코드는 clang-cl 에도 걸었다). 동작을 바꾸지 않으려 그대로 둔다.
		$<$<CONFIG:Debug>:-g>

		$<$<CONFIG:Release>:/O2>
		$<$<CONFIG:Release>:/arch:AVX2> # AVX2 256비트 SIMD
		$<$<CONFIG:Release>:-clang:-fno-math-errno> # 수학 함수의 errno 설정 오버헤드 제거
		$<$<CONFIG:Release>:-clang:-fno-trapping-math> # 부동소수점 예외 트랩 비활성화
	)

	if(SW_ENABLE_TIME_TRACE)
		target_compile_options(sw_compiler_clang INTERFACE -clang:-ftime-trace)
	endif()
else()
	# GNU 드라이버
	target_compile_options(sw_compiler_clang INTERFACE
		-finput-charset=UTF-8
		-fexec-charset=UTF-8
		$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti> # C 소스에는 걸 수 없는 플래그다

		$<$<CONFIG:Debug>:-g>
		$<$<CONFIG:Debug>:-O0>
		$<$<CONFIG:Debug>:-gz=zlib> # 디버그 정보 압축

		$<$<CONFIG:Release>:-O3>
		$<$<CONFIG:Release>:-mavx2> # AVX2 256비트 SIMD
		$<$<CONFIG:Release>:-fno-math-errno> # 수학 함수의 errno 설정 오버헤드 제거
		$<$<CONFIG:Release>:-fno-trapping-math> # 부동소수점 예외 트랩 비활성화
	)

	if(SW_ENABLE_TIME_TRACE)
		target_compile_options(sw_compiler_clang INTERFACE -ftime-trace)
	endif()
endif()

# ------------------------------------------------------------------------------
# 링크 — LLD가 있으면 사용, clang-cl은 MSVC 스타일 링크 옵션
# ------------------------------------------------------------------------------
include("${CMAKE_CURRENT_LIST_DIR}/../Toolchain/FindLlvmBin.cmake")
sw_findLlvmBin(llvmBin)
find_program(SW_LLD_LINK_EXE NAMES lld-link lld HINTS "${llvmBin}")

if(SW_LLD_LINK_EXE)
	message(STATUS "[Clang.cmake] LLD Fast Linker detected: ${SW_LLD_LINK_EXE}")
endif()

if(MSVC)
	target_link_options(sw_compiler_clang INTERFACE
		$<$<CONFIG:Debug>:/INCREMENTAL:NO> # 단일 패스 결정론적 빠른 링킹
		$<$<CONFIG:Release>:/OPT:REF> # 미참조 함수 및 데이터 제거
		$<$<CONFIG:Release>:/OPT:ICF> # 동일한 코드의 중복 함수 병합
	)
else()
	target_link_options(sw_compiler_clang INTERFACE -fuse-ld=lld)
endif()

target_compile_definitions(sw_compiler_clang INTERFACE SW_COMPILER_CLANG)

list(APPEND sw_flag_libraries sw_compiler_clang)
