# ==============================================================================
# @file cmake/Engine/BuildLayout.cmake
# @brief 산출물이 어디에 놓이나 — 출력 경로, 전역 옵션 타겟, IPO, 런타임 복사 큐
# ==============================================================================

# 이 파일은 함수만 정의하지 않는다. include 되는 순간 출력 경로와 sw_global_options 가 정해진다.
# 그래서 TargetRules.cmake 보다 먼저 include 해야 한다.

# ------------------------------------------------------------------------------
# 출력 경로 — Ninja 단일 설정 → 평탄한 Bin/Lib (LiveReload와 동일)
# ------------------------------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${sw_output_directory}/Bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${sw_output_directory}/Lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${sw_output_directory}/Lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${sw_output_directory}/Bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${sw_output_directory}/Bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${sw_output_directory}/Lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${sw_output_directory}/Lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${sw_output_directory}/Lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${sw_output_directory}/Lib")

if(EXISTS "${CMAKE_SOURCE_DIR}/Resource")
	install(DIRECTORY "${CMAKE_SOURCE_DIR}/Resource" DESTINATION .)
endif()

# ------------------------------------------------------------------------------
# Dev / Shipping 레이아웃 및 글로벌 옵션 (Modern CMake INTERFACE)
# ------------------------------------------------------------------------------
add_library(sw_global_options INTERFACE)

# 컴파일 플래그 모듈(Architecture/Platform/Compiler/BuildType/Options)이 만든 INTERFACE 타겟을
# 여기서 한 번 흡수한다. 예전엔 sw_flag_libraries 가 **리스트 변수**라 타겟마다
#   if(sw_flag_libraries) target_link_libraries(t PRIVATE ${sw_flag_libraries}) endif()
# 를 다시 써야 했다(11곳). 리스트가 비어 있을 수 있어 가드까지 필요했다. 타겟 하나로 묶으면
# sw_global_options 만 걸면 되고, 새 플래그 모듈이 늘어도 소비자는 고칠 게 없다.
if(sw_flag_libraries)
	target_link_libraries(sw_global_options INTERFACE ${sw_flag_libraries})
endif()

if(SW_SHIPPING_BUILD)
	target_compile_definitions(sw_global_options INTERFACE SW_SHIPPING)
	set(SW_RHI_AS_MODULES OFF CACHE BOOL "RHI 백엔드(DX11/DX12/GL/Vulkan)를 MODULE 플러그인으로 빌드" FORCE)
	message(STATUS "[BuildConfig] Shipping: Engine/SWGame STATIC, Editor off (SW_SHIPPING_BUILD=ON)")
	message(STATUS "[BuildConfig] SW_RHI_AS_MODULES=OFF (CACHE FORCE)")
else()
	message(STATUS "[BuildConfig] Dev: Engine SHARED, Editor/SWGame MODULE (type=${CMAKE_BUILD_TYPE})")
endif()

# 두 매크로 모두 헤더(Mutex.h)와 전역 연산자에 영향을 주므로 모든 타겟에 동일하게 적용해야 한다.
if(SW_ENABLE_DEADLOCK_DETECTION)
	target_compile_definitions(sw_global_options INTERFACE SW_ENABLE_DEADLOCK_DETECTION)
	message(STATUS "[BuildConfig] sw::Mutex deadlock detection enabled (slow)")
endif()

if(SW_ENABLE_STL_CONTAINER)
	target_compile_definitions(sw_global_options INTERFACE SW_ENABLE_STL_CONTAINER)
	message(STATUS "[BuildConfig] SW_ENABLE_STL_CONTAINER=ON -> Using std::allocator for containers")
endif()

# ------------------------------------------------------------------------------
# IPO — 전역 CMAKE_INTERPROCEDURAL_OPTIMIZATION (Release)
# ------------------------------------------------------------------------------
set(sw_ipo_supported FALSE)

if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
	include(CheckIPOSupported)
	check_ipo_supported(RESULT sw_ipo_supported OUTPUT sw_ipo_error)

	if(sw_ipo_supported)
		set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
		message(STATUS "[BuildConfig] IPO enabled globally (opt-out for Tools/Tests/Editor)")
	elseif(sw_ipo_error)
		message(STATUS "[BuildConfig] IPO unsupported: ${sw_ipo_error}")
	endif()
endif()


# ------------------------------------------------------------------------------
# 런타임 파일 복사 큐 — POST_BUILD는 sw_emitRuntimeCopies가 한 번에 방출
# ------------------------------------------------------------------------------
function(sw_queueRuntimeCopy TARGET_NAME SRC_FILE)
	if(NOT TARGET ${TARGET_NAME})
		message(FATAL_ERROR "sw_queueRuntimeCopy: target '${TARGET_NAME}' does not exist")
	endif()

	if(NOT SRC_FILE OR NOT EXISTS "${SRC_FILE}")
		return()
	endif()

	set_property(TARGET ${TARGET_NAME} APPEND PROPERTY SW_RUNTIME_COPY_FILES "${SRC_FILE}")
endfunction()

function(sw_emitRuntimeCopies TARGET_NAME)
	if(NOT TARGET ${TARGET_NAME})
		message(FATAL_ERROR "sw_emitRuntimeCopies: target '${TARGET_NAME}' does not exist")
	endif()

	get_property(already TARGET ${TARGET_NAME} PROPERTY SW_RUNTIME_COPIES_EMITTED)

	if(already)
		return()
	endif()

	get_property(files TARGET ${TARGET_NAME} PROPERTY SW_RUNTIME_COPY_FILES)

	if(NOT files)
		return()
	endif()

	list(REMOVE_DUPLICATES files)
	set(names "")
	set(commands "")

	foreach(src IN LISTS files)
		get_filename_component(name "${src}" NAME)
		list(APPEND names "${name}")
		list(APPEND commands
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${src}"
			"$<TARGET_FILE_DIR:${TARGET_NAME}>/${name}"
		)
	endforeach()

	list(JOIN names ", " summary)
	add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
		${commands}
		COMMENT "[${TARGET_NAME}] Runtime deps: ${summary}"
		VERBATIM
	)
	set_property(TARGET ${TARGET_NAME} PROPERTY SW_RUNTIME_COPIES_EMITTED TRUE)
endfunction()
