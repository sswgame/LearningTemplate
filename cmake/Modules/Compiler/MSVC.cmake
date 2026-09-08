# ==============================================================================
# @file cmake/Modules/Compiler/MSVC.cmake
# @brief MSVC 컴파일러 플래그 INTERFACE
# ==============================================================================

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
	return()
endif()

# 파일 스코프 include: 멀티프로세서 컴파일 활성화
set(CMAKE_MSVC_PARALLEL_COMPILE ON)

add_library(sw_compiler_msvc INTERFACE)

# ------------------------------------------------------------------------------
# 컴파일 — 문자셋 → 코드젠 → 경고 → 서드파티 → 빌드설정(Debug/Release)
# ------------------------------------------------------------------------------
target_compile_options(sw_compiler_msvc INTERFACE

	/utf-8 # 소스 파일 및 실행 문자셋 UTF-8 인코딩 지정

	# 코드젠 / 최적화 기본 설정
	/bigobj # 대규모 템플릿/리플렉션 오브젝트용 64K+ 섹션 허용
	/EHsc # 표준 C++ 스택 언와인딩 동기식 예외 처리 모델
	/GR- # RTTI 비활성화로 바이너리 크기 및 vtable 오버헤드 축소
	/MP # MSVC cl 멀티프로세서 병렬 컴파일 활성화

	# 경고 활성화 및 특정 경고 비활성화
	/W4 # 높은 수준의 정밀 컴파일러 경고 활성화
	/wd4201 # 비표준 무명 구조체/공용체 확장 허용 (수학 벡터 컴포넌트 등)
	/wd4251 # DLL 인터페이스 클래스의 STL 멤버 DLL 내보내기 경고 억제

	# 서드파티 외부 헤더는 경고를 내지 않는다
	/external:I${CMAKE_SOURCE_DIR}/ThirdParty
	/external:W0

	$<$<CONFIG:Debug>:/Od>
	$<$<CONFIG:Debug>:/Zi>

	$<$<CONFIG:Release>:/O2>
)

# ------------------------------------------------------------------------------
# 링크 — 빌드 설정별 증분 링크 및 최적화
# ------------------------------------------------------------------------------
target_link_options(sw_compiler_msvc INTERFACE

	$<$<CONFIG:Debug>:/DEBUG:FASTLINK> # 링크 속도를 위해 PDB 를 오브젝트에 남겨 둔다
	$<$<CONFIG:Debug>:/INCREMENTAL>

	$<$<CONFIG:Release>:/INCREMENTAL:NO> # 결정론적 단일 패스 링킹
	$<$<CONFIG:Release>:/OPT:ICF> # 동일한 코드 바이트를 갖는 중복 함수 병합
	$<$<CONFIG:Release>:/OPT:REF> # 미참조 함수 및 데이터 제거
)

# ------------------------------------------------------------------------------
# 런타임 라이브러리 및 정의 매크로
# ------------------------------------------------------------------------------
set_property(TARGET sw_compiler_msvc PROPERTY
	MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)

target_compile_definitions(sw_compiler_msvc INTERFACE SW_COMPILER_MSVC)

list(APPEND sw_flag_libraries sw_compiler_msvc)
