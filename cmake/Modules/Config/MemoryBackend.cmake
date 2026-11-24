# ==============================================================================
# @file cmake/Modules/Config/MemoryBackend.cmake
# @brief 힙 백엔드(`SW_MEMORY_BACKEND`)의 auto 를 이 구성의 값(mimalloc · crt)으로 푼다 — 대상 OS 를 알아야 해 project() 뒤에 부른다(CMakeLists.txt 3 절)
# @details `cmake -P` 로도 부를 수 있게 함수만 둔다(Scripts/setup/tests/test_cmake_build_rules.py).
# ==============================================================================
include_guard(GLOBAL)

# REQUESTED: 캐시의 auto | mimalloc | crt. SYSTEM_NAME: CMAKE_SYSTEM_NAME. SANITIZER: SW_ENABLE_SANITIZER. 고른 백엔드를 OUT_VAR 에.
#   auto 는 모든 구성(Windows · Linux · Android, 개발 · Shipping · 서버)에서 mimalloc 이고, 아래 둘만 crt 다. 명시한 mimalloc 이 그 둘에 걸리면 구성을 세운다.
#   새니타이저 : 힙을 가로채야 선다 — mimalloc 은 할당을 제 힙으로 돌려 ASan · TSan 이 블록을 보지 못한다(언리얼 -EnableASan 도 시스템 할당기로 바꾼다).
#   웹        : 브라우저 wasm 힙 위에서 얻을 것이 없고 포트도 깔지 않는다(vcpkg.json).
#   언리얼은 할당기를 플랫폼마다 갈라(Windows 에디터 mimalloc, 게임 Binned2/3) 시스템 malloc 을 쓰지 않고, Godot 은 시스템 malloc 이다 — 크기 등급 · 스레드 캐시가
#   있는 언리얼 쪽을 고르되, 자체 Binned 를 유지하는 대신 개발 · 배포가 같은 할당기 하나를 쓴다.
function(sw_resolve_memory_backend REQUESTED SYSTEM_NAME SANITIZER OUT_VAR)
	if(NOT REQUESTED MATCHES "^(auto|mimalloc|crt)$")
		message(FATAL_ERROR "SW_MEMORY_BACKEND='${REQUESTED}' is not a heap backend - use auto, mimalloc or crt")
	endif()

	set(crt_reason "")
	if(SANITIZER)
		set(crt_reason "SW_ENABLE_SANITIZER is on and the sanitizer must see the heap")
	elseif(SYSTEM_NAME STREQUAL "Emscripten")
		set(crt_reason "the web (Emscripten) has no mimalloc port")
	endif()

	set(backend "${REQUESTED}")
	if(crt_reason AND REQUESTED STREQUAL "mimalloc")
		message(FATAL_ERROR "SW_MEMORY_BACKEND=mimalloc is not available: ${crt_reason} - use auto or crt")
	elseif(crt_reason)
		set(backend "crt")
	elseif(REQUESTED STREQUAL "auto")
		set(backend "mimalloc")
	endif()
	set(${OUT_VAR} "${backend}" PARENT_SCOPE)
endfunction()
