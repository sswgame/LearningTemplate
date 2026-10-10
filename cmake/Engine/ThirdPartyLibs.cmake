# ==============================================================================
# @file cmake/Engine/ThirdPartyLibs.cmake
# @brief 서드파티를 어떻게 붙이나 — SYSTEM include, vcpkg CONFIG 패키지(못 찾으면 구성 실패), 설정 파일 없는 헤더 전용 포트
# ==============================================================================

# ------------------------------------------------------------------------------
# ThirdParty 래퍼 — SYSTEM include
# ------------------------------------------------------------------------------
function(sw_thirdPartySystemIncludes TARGET_NAME)
	cmake_parse_arguments(ARG "" "" "INTERFACE;PUBLIC;PRIVATE" ${ARGN})

	if(NOT TARGET ${TARGET_NAME})
		message(FATAL_ERROR "[ThirdParty] Target not found: ${TARGET_NAME}")
	endif()

	if(ARG_INTERFACE)
		target_include_directories(${TARGET_NAME} SYSTEM INTERFACE ${ARG_INTERFACE})
	endif()

	if(ARG_PUBLIC)
		target_include_directories(${TARGET_NAME} SYSTEM PUBLIC ${ARG_PUBLIC})
	endif()

	if(ARG_PRIVATE)
		target_include_directories(${TARGET_NAME} SYSTEM PRIVATE ${ARG_PRIVATE})
	endif()
endfunction()

# ------------------------------------------------------------------------------
# sw_addVcpkgPackage — CMake 설정 파일이 있는 vcpkg 패키지를 NAME 이라는 INTERFACE 타깃으로 감싼다.
#   못 찾으면 **구성을 세운다**(find_package REQUIRED) — 빈 타깃으로 대신하면 링크(또는 실행)에서야 드러난다.
#   NAME · PACKAGE(기본 NAME) · CONFIG_TARGET(기본 NAME::NAME) · ATTACH_GLOBAL(sw_third_party_includes 에도 건다)
# ------------------------------------------------------------------------------
function(sw_addVcpkgPackage)
	cmake_parse_arguments(ARG "ATTACH_GLOBAL" "NAME;PACKAGE;CONFIG_TARGET" "" ${ARGN})
	if(NOT ARG_NAME)
		message(FATAL_ERROR "sw_addVcpkgPackage: NAME is required")
	endif()
	if(NOT ARG_PACKAGE)
		set(ARG_PACKAGE ${ARG_NAME})
	endif()
	if(NOT ARG_CONFIG_TARGET)
		set(ARG_CONFIG_TARGET "${ARG_NAME}::${ARG_NAME}")
	endif()

	find_package(${ARG_PACKAGE} CONFIG REQUIRED)
	if(NOT TARGET ${ARG_NAME})
		if(NOT TARGET ${ARG_CONFIG_TARGET})
			message(FATAL_ERROR "[ThirdParty] found ${ARG_PACKAGE} but it has no target ${ARG_CONFIG_TARGET} — fix CONFIG_TARGET")
		endif()
		add_library(${ARG_NAME} INTERFACE)
		target_link_libraries(${ARG_NAME} INTERFACE ${ARG_CONFIG_TARGET})
	endif()
	if(ARG_ATTACH_GLOBAL AND TARGET sw_third_party_includes)
		target_link_libraries(sw_third_party_includes INTERFACE ${ARG_NAME})
	endif()
endfunction()

# ------------------------------------------------------------------------------
# sw_addVcpkgHeaderOnly — CMake 설정 파일이 없는 헤더 전용 vcpkg 포트(acl · cgltf). vcpkg include 폴더를 SYSTEM 으로 건다.
#   PROBE(예: cgltf.h)가 그 폴더에 없으면 구성을 세운다.
# ------------------------------------------------------------------------------
function(sw_addVcpkgHeaderOnly)
	cmake_parse_arguments(ARG "" "NAME;PROBE" "" ${ARGN})
	if(NOT ARG_NAME OR NOT ARG_PROBE)
		message(FATAL_ERROR "sw_addVcpkgHeaderOnly: NAME and PROBE are required")
	endif()
	sw_getVcpkgPaths(listIncludeDir listBinDir)
	set(isFound OFF)
	foreach(includeDir IN LISTS listIncludeDir)
		if(EXISTS "${includeDir}/${ARG_PROBE}")
			set(isFound ON)
		endif()
	endforeach()
	if(NOT isFound)
		message(FATAL_ERROR "[ThirdParty] ${ARG_NAME}: ${ARG_PROBE} is not in the vcpkg include folders (${listIncludeDir}) — check vcpkg.json and run vcpkg install")
	endif()
	add_library(${ARG_NAME} INTERFACE)
	sw_linkVcpkgHeaderOnlyTarget(${ARG_NAME})
endfunction()

# ------------------------------------------------------------------------------
# sw_findEngineCompressionLibraries — Engine 이 링크할 압축 코덱(zlib · LZ4 · Zstd)의 링크 항목을 OUT_VAR 에 담는다(순서 그대로 링크한다).
#   Engine 의 CMakeLists 가 부른다 — 임포트 타깃은 부른 디렉터리에 생긴다(ThirdParty/zlib 의 ZLIB::ZLIB 은 그 폴더에서만 보인다).
#   Core 는 압축 라이브러리에 의존하지 않고(ICompressionCodec), ReflectionParser 가 끌고 가지 않게 Engine 에만 붙인다.
#
#   - zlib: 리소스 팩 압축 해제(ResourcePackReader 가 uncompress 를 직접 부른다). raw 이름 `zlib` 은 링커에 `-lzlib` 로 가서
#     Windows(zlib.lib)에서만 우연히 맞고 리눅스(libz.so)는 못 찾는다 — 공유 라이브러리는 미해결 심볼을 허용해 libEngine.so 링크는 통과하고
#     App 에서야 `undefined reference to uncompress` 로 터진다. 그래서 임포트 타깃을 쓰고, 임포트 타깃 **하나에만** 기대지 않는다:
#     리눅스 CI 에서 링크 줄에 zlib 이 아예 붙지 않은 일이 세 번 있었다(임포트 타깃이 왜 비었는지는 재현하지 못했다). find_package 가 채우는
#     ZLIB_LIBRARIES(`optimized;<경로>;debug;<경로>` — 확정된 경로 목록)를 같이 건다(중복 지정은 링커가 무시한다).
#   - LZ4 · Zstd: CompressionCodecRegistry 에 EngineLoop 이 등록하는 코덱. zstd 는 vcpkg 구성(정적/공유)에 따라 타깃 이름이 달라
#     하나만 적으면 다른 구성에서 configure 가 조용히 지나가고 링크에서 터진다 — 셋 중 있는 것을 고른다.
# ------------------------------------------------------------------------------
function(sw_findEngineCompressionLibraries OUT_VAR)
	find_package(ZLIB REQUIRED)
	find_package(lz4 CONFIG REQUIRED)
	find_package(zstd CONFIG REQUIRED)

	if(TARGET zstd::libzstd)
		set(swZstdTarget zstd::libzstd)
	elseif(TARGET zstd::libzstd_shared)
		set(swZstdTarget zstd::libzstd_shared)
	elseif(TARGET zstd::libzstd_static)
		set(swZstdTarget zstd::libzstd_static)
	else()
		message(FATAL_ERROR "zstd 임포트 타겟을 찾지 못했습니다 (zstd::libzstd / _shared / _static).")
	endif()
	message(STATUS "[Engine] lz4=lz4::lz4 zstd=${swZstdTarget}")

	# 무엇으로 풀렸는지 configure 로그에 남긴다. 링크 오류가 나면 링크 구문만 보고는 ZLIB::ZLIB 이 실제로 무엇을 가리키는지
	# (정적/공유, debug/release, vcpkg/시스템) 알 수 없다.
	get_target_property(swZlibType ZLIB::ZLIB TYPE)
	get_target_property(swZlibImportedConfig ZLIB::ZLIB IMPORTED_CONFIGURATIONS)
	get_target_property(swZlibLocationDebug ZLIB::ZLIB IMPORTED_LOCATION_DEBUG)
	get_target_property(swZlibLocationRelease ZLIB::ZLIB IMPORTED_LOCATION_RELEASE)
	get_target_property(swZlibLocationNoConfig ZLIB::ZLIB IMPORTED_LOCATION)
	message(STATUS "[Engine] ZLIB::ZLIB type=${swZlibType} configs=${swZlibImportedConfig}")
	message(STATUS "[Engine] ZLIB::ZLIB loc(debug)=${swZlibLocationDebug} loc(release)=${swZlibLocationRelease} loc=${swZlibLocationNoConfig}")
	message(STATUS "[Engine] ZLIB_LIBRARIES=${ZLIB_LIBRARIES} ZLIB_INCLUDE_DIRS=${ZLIB_INCLUDE_DIRS}")

	# 임포트 타깃이 아무 라이브러리도 가리키지 않으면 링크는 조용히 통과하고 그것을 링크하는 실행 파일에서야 터진다. 여기서 먼저 멈춘다.
	if(NOT swZlibLocationDebug AND NOT swZlibLocationRelease AND NOT swZlibLocationNoConfig AND NOT ZLIB_LIBRARIES)
		message(FATAL_ERROR "ZLIB::ZLIB 이 어떤 라이브러리도 가리키지 않습니다 — 이대로 링크하면 libEngine 이 uncompress 를 미해결로 남깁니다.")
	endif()

	set(${OUT_VAR} ZLIB::ZLIB ${ZLIB_LIBRARIES} lz4::lz4 ${swZstdTarget} PARENT_SCOPE)
endfunction()
