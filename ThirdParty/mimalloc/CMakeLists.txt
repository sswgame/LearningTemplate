# ==============================================================================
# @file ThirdParty/mimalloc/CMakeLists.txt
# @brief mimalloc 힙(MIT — 배포물 고지는 THIRD_PARTY_NOTICES) — 힙 백엔드가 mimalloc 인 구성(SW_MEMORY_BACKEND)에만 붙는다.
#        헤더는 Source/Core/Memory/MemoryBackendMimalloc.cpp 에서만 include 한다. 오버레이 포트(`vcpkg-port/mimalloc`)가 모든 플랫폼에서 정적 라이브러리로 짓는다 —
#        힙은 Core 오브젝트를 담는 모듈(개발: Engine, 정적 모듈 구성: App) 하나에만 있고 배포할 DLL 이 없다. 설정 파일의 임포트 타깃과 이름이 겹치지 않게
#        감싼 타깃은 `sw_mimalloc` 이다.
# ==============================================================================

if(NOT SW_MEMORY_BACKEND STREQUAL "mimalloc")
	return()
endif()

sw_add_vcpkg_package(
	NAME sw_mimalloc
	PACKAGE mimalloc
	CONFIG_TARGET mimalloc-static
)
