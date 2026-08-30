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
# 1) 컴파일 — 문자셋 → 코드젠 → 경고 → 서드파티 → 빌드설정(Debug/Release)
# ------------------------------------------------------------------------------
target_compile_options(sw_compiler_msvc INTERFACE
	# 문자셋
	/utf-8                             # 소스 파일 및 실행 문자셋 UTF-8 인코딩 지정

	# 코드젠 / 최적화 기본 설정
	/bigobj                            # 대규모 템플릿/리플렉션 오브젝트용 64K+ 섹션 허용
	/EHsc                              # 표준 C++ 스택 언와인딩 동기식 예외 처리 모델
	/GR-                               # RTTI 비활성화로 바이너리 크기 및 vtable 오버헤드 축소
	/MP                                # MSVC cl 멀티프로세서 병렬 컴파일 활성화

	# 경고 활성화 및 특정 경고 비활성화
	/W4                                # 높은 수준의 정밀 컴파일러 경고 활성화
	/wd4201                            # 비표준 무명 구조체/공용체 확장 허용 (수학 벡터 컴포넌트 등)
	/wd4251                            # DLL 인터페이스 클래스의 STL 멤버 DLL 내보내기 경고 억제

	# 서드파티 외부 헤더 경고 억제 (/external)
	/external:I${CMAKE_SOURCE_DIR}/ThirdParty # ThirdParty 디렉터리를 외부 헤더로 지정
	/external:W0                              # 외부 헤더에 대한 모든 컴파일러 경고 비활성화

	# 빌드 설정별 최적화 및 디버그 심볼
	# Debug: 디버그 심볼 & 최적화 끄기
	$<$<CONFIG:Debug>:/Od>             # 디버그 최적화 비활성화
	$<$<CONFIG:Debug>:/Zi>             # PDB 디버그 심볼 생성

	# Release: 최고 최적화
	$<$<CONFIG:Release>:/O2>            # 속도 우선 최고 수준 최적화
)

# ------------------------------------------------------------------------------
# 2) 링크 — 빌드 설정별 증분 링크 및 최적화
# ------------------------------------------------------------------------------
target_link_options(sw_compiler_msvc INTERFACE
	# Debug: FastLink 디버그 정보
	$<$<CONFIG:Debug>:/DEBUG:FASTLINK> # 디버그 링킹 속도 향상을 위한 FastLink 사용
	$<$<CONFIG:Debug>:/INCREMENTAL>    # 디버그 증분 링크 활성화

	# Release: 참조 제거 및 중복 함수 병합(ICF)
	$<$<CONFIG:Release>:/INCREMENTAL:NO> # 릴리즈 빌드 결정론적 단일 패스 링킹
	$<$<CONFIG:Release>:/OPT:ICF>        # 동일한 코드 바이트를 갖는 중복 함수 병합
	$<$<CONFIG:Release>:/OPT:REF>        # 미참조 함수 및 데이터 제거
)

# ------------------------------------------------------------------------------
# 3) 런타임 라이브러리 및 정의 매크로
# ------------------------------------------------------------------------------
set_property(TARGET sw_compiler_msvc PROPERTY
	MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)

target_compile_definitions(sw_compiler_msvc INTERFACE SW_COMPILER_MSVC)

list(APPEND sw_flag_libraries sw_compiler_msvc)
