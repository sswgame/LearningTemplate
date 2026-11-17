# ==============================================================================
# @file ThirdParty/mimalloc/CMakeLists.txt
# @brief mimalloc 힙(MIT — 배포물 고지는 THIRD_PARTY_NOTICES) — 힙 백엔드로 고른 빌드(SW_MEMORY_BACKEND=mimalloc)에만 붙는다.
#        헤더는 Source/Core/Memory/MemoryBackendMimalloc.cpp 에서만 include 한다. vcpkg 래퍼가 `mimalloc` · `mimalloc-static` 을 둘 다 만든다
#        (x64-windows 는 mimalloc.dll, 리눅스 · Android 는 정적). 설정 파일의 임포트 타깃과 이름이 겹치지 않게 감싼 타깃은 `sw_mimalloc` 이다.
# ==============================================================================

if(NOT SW_MEMORY_BACKEND STREQUAL "mimalloc")
	return()
endif()

sw_add_vcpkg_package(
	NAME sw_mimalloc
	PACKAGE mimalloc
	CONFIG_TARGET mimalloc
)
