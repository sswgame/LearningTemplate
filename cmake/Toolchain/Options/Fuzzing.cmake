# ==============================================================================
# @file cmake/Toolchain/Options/Fuzzing.cmake
# @brief 커버리지 안내 퍼징 계측 (SW_ENABLE_FUZZING) — 모든 타깃에 `-fsanitize=fuzzer-no-link`, 퍼저 실행 파일만 `-fsanitize=fuzzer` 로 링크한다
# ==============================================================================

if(NOT SW_ENABLE_FUZZING)
	return()
endif()

# libFuzzer 런타임(clang_rt.fuzzer)이 Windows 에서는 정적 CRT(/MT) 빌드뿐이라 동적 CRT 엔진과 링크되지 않는다(LNK2038).
# 계측 콜백(8 비트 카운터 · 비교 추적)의 기본 구현은 ASan 런타임이 갖고 있어, 퍼저가 아닌 실행 파일(테스트 · 도구)도 그대로 링크된다.
if(WIN32 OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
	message(FATAL_ERROR "[Fuzzing] SW_ENABLE_FUZZING needs Linux clang - Windows clang_rt.fuzzer is /MT only")
endif()
if(NOT SW_ENABLE_SANITIZER OR NOT SW_SANITIZER_KIND STREQUAL "address")
	message(FATAL_ERROR "[Fuzzing] SW_ENABLE_FUZZING needs SW_ENABLE_SANITIZER=ON with SW_SANITIZER_KIND=address")
endif()

# 리눅스 심볼 숨김(`--exclude-libs,ALL`, cmake/Modules/Build/BuildLayout.cmake)에는 걸리지 않는다 — 계측된 모듈 .so 가 찾는 `__sanitizer_cov_*` ·
# `__sancov_lowest_stack` 은 공유 ASan 런타임(`-shared-libsan`, Sanitizer.cmake)이 내보내고, 그 숨김은 정적 아카이브에서 온 심볼만 가린다.
# 퍼저 실행 파일을 더할 때는 걸린다: `-fsanitize=fuzzer` 의 정적 libFuzzer 가 가진 진짜 콜백이 숨겨져 .so 는 ASan 의 빈 기본 구현에 묶이고,
# 링크 · 실행은 되는데 엔진 .so 의 커버리지가 퍼저에 닿지 않는다. 그 타깃은 대상 코드를 정적으로 링크하거나 숨김을 빼고, 시작 줄 `Loaded N modules` 로 확인한다.
add_library(sw_fuzzing INTERFACE)
target_compile_options(sw_fuzzing INTERFACE -fsanitize=fuzzer-no-link)
target_compile_definitions(sw_fuzzing INTERFACE SW_FUZZING=1)
list(APPEND SW_FLAG_LIBRARIES sw_fuzzing)
message(STATUS "[Fuzzing] libFuzzer coverage instrumentation (fuzzer-no-link)")
