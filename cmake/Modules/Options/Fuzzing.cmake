# ==============================================================================
# @file cmake/Modules/Options/Fuzzing.cmake
# @brief 커버리지 안내 퍼징 계측 (SW_ENABLE_FUZZING) — 모든 타깃에 `-fsanitize=fuzzer-no-link`, 퍼저 실행 파일만 `-fsanitize=fuzzer` 로 링크한다
# ==============================================================================

if(NOT SW_ENABLE_FUZZING)
	return()
endif()

# libFuzzer 런타임(clang_rt.fuzzer)이 Windows 에서는 정적 CRT(/MT) 빌드뿐이라 동적 CRT 엔진과 링크되지 않는다(LNK2038).
# 계측 콜백(8 비트 카운터 · 비교 추적)의 기본 구현은 ASan 런타임이 갖고 있어, 퍼저가 아닌 실행 파일(시험 · 도구)도 그대로 링크된다.
if(WIN32 OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
	message(FATAL_ERROR "[Fuzzing] SW_ENABLE_FUZZING needs Linux clang (preset CI-Fuzz) - Windows clang_rt.fuzzer is /MT only")
endif()
if(NOT SW_ENABLE_SANITIZER OR NOT SW_SANITIZER_KIND STREQUAL "address")
	message(FATAL_ERROR "[Fuzzing] SW_ENABLE_FUZZING needs SW_ENABLE_SANITIZER=ON with SW_SANITIZER_KIND=address")
endif()

add_library(sw_fuzzing INTERFACE)
target_compile_options(sw_fuzzing INTERFACE -fsanitize=fuzzer-no-link)
target_compile_definitions(sw_fuzzing INTERFACE SW_FUZZING=1)
list(APPEND sw_flag_libraries sw_fuzzing)
message(STATUS "[Fuzzing] libFuzzer coverage instrumentation (fuzzer-no-link)")
