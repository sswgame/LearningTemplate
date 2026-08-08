# MSVC compiler flags.

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
	return()
endif()

# File-scope include: plain set() (PARENT_SCOPE is a no-op outside a function).
set(CMAKE_MSVC_PARALLEL_COMPILE ON)

add_library(sw_compiler_msvc INTERFACE)

# -----------------------------------------------------------------------------
# Compile: charset → codegen → warnings → external → config
# -----------------------------------------------------------------------------
target_compile_options(sw_compiler_msvc INTERFACE
	# Charset
	/utf-8

	# Codegen
	/EHsc
	/MP
	/bigobj

	# Warnings
	/W4
	/wd4201
	/wd4251

	# Vendored ThirdParty (SYSTEM /external)
	/external:W0
	/external:I${CMAKE_SOURCE_DIR}/ThirdParty

	# Debug
	$<$<CONFIG:Debug>:/Zi>
	$<$<CONFIG:Debug>:/Od>

	# Release
	$<$<CONFIG:Release>:/O2>
)

# -----------------------------------------------------------------------------
# Link
# -----------------------------------------------------------------------------
target_link_options(sw_compiler_msvc INTERFACE
	$<$<CONFIG:Debug>:/INCREMENTAL>
	$<$<CONFIG:Debug>:/DEBUG:FASTLINK>
	$<$<CONFIG:Release>:/INCREMENTAL:NO>
	$<$<CONFIG:Release>:/OPT:REF>
	$<$<CONFIG:Release>:/OPT:ICF>
)

set_property(TARGET sw_compiler_msvc PROPERTY
	MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)

target_compile_definitions(sw_compiler_msvc INTERFACE SW_COMPILER_MSVC)

list(APPEND sw_flag_libraries sw_compiler_msvc)
