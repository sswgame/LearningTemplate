# ==============================================================================
# @file Source/Core/CMakeLists.txt
# @brief 호스트/런타임 기초(Common, Log, FileUtil, String 등) STATIC 타겟
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 소스 목록 — 플랫폼 파일 대화상자/워처만 OS별 추가
# ------------------------------------------------------------------------------
set(cfSources
	"${CMAKE_CURRENT_SOURCE_DIR}/CommandLine/CommandLineManager.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Compression/NullCompressionCodec.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Compression/RleCompressionCodec.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Compression/CompressionCodecRegistry.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Compression/CompressionStream.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Container/DynamicBitset.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Delegate/Delegate.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Event/EventDispatcher.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/File/FileUtil.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/GlobalVariable/GlobalVariableManager.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Log/Logger.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Math/MatrixMath.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Math/VectorMath.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Memory/FrameArenaAllocator.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Memory/LinearAllocator.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Memory/Memory.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Memory/PoolAllocator.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Memory/CallStackCapture.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Memory/MemoryProfiler.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Concurrency/DataRaceDetector.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Concurrency/DeadlockDetector.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/String/hashed_string.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/String/StringUtil.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Time/CpuTimer.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Uuid/Uuid.cpp"
)

if(WIN32)
	list(APPEND cfSources "${CMAKE_CURRENT_SOURCE_DIR}/File/Windows/WindowsFileDialog.cpp")
	list(APPEND cfSources "${CMAKE_CURRENT_SOURCE_DIR}/File/Windows/WindowsFileWatcher.cpp")
elseif(APPLE)
	list(APPEND cfSources "${CMAKE_CURRENT_SOURCE_DIR}/File/Mac/MacFileDialog.cpp")
	list(APPEND cfSources "${CMAKE_CURRENT_SOURCE_DIR}/File/Mac/MacFileWatcher.cpp")
elseif(UNIX)
	list(APPEND cfSources "${CMAKE_CURRENT_SOURCE_DIR}/File/Linux/LinuxFileDialog.cpp")
	list(APPEND cfSources "${CMAKE_CURRENT_SOURCE_DIR}/File/Linux/LinuxFileWatcher.cpp")
endif()

# ------------------------------------------------------------------------------
# 2) Core_objects — 한 번만 컴파일
#    ReflectionParser: Core STATIC 링크
#    Engine: 동일 OBJECT를 넣어 Dev에서 Engine.dll로 export
#    SW_EXPORTS: App/Editor(SW_IMPORTS)가 dllimport 할 수 있게
# ------------------------------------------------------------------------------
add_library(Core_objects OBJECT ${cfSources})
set_target_properties(Core_objects PROPERTIES
	POSITION_INDEPENDENT_CODE ON
	FOLDER "Source"
)

target_include_directories(Core_objects
	PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(Core_objects PUBLIC sw_public_source_includes)
target_link_libraries(Core_objects PUBLIC sw_third_party_includes)
if(sw_flag_libraries)
	target_link_libraries(Core_objects PRIVATE ${sw_flag_libraries})
endif()

target_compile_definitions(Core_objects PRIVATE SW_EXPORTS)

if(WIN32)
	target_link_libraries(Core_objects PUBLIC comdlg32 ole32 shell32 dbghelp)
elseif(APPLE)
	target_link_libraries(Core_objects PUBLIC "-framework CoreServices" "-framework CoreFoundation")
endif()

sw_setLogTag(Core_objects Engine)
sw_configurePch(Core_objects)

# ------------------------------------------------------------------------------
# 3) Core STATIC — 파서가 링크하는 껍데기. Engine은 Core_objects를 직접 가져감
# ------------------------------------------------------------------------------
add_library(Core STATIC $<TARGET_OBJECTS:Core_objects>)
set_target_properties(Core PROPERTIES
	FOLDER "Source"
)
target_include_directories(Core
	PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(Core PUBLIC sw_public_source_includes)
target_link_libraries(Core PUBLIC sw_third_party_includes)
if(WIN32)
	target_link_libraries(Core PUBLIC comdlg32 ole32 shell32 dbghelp)
elseif(APPLE)
	target_link_libraries(Core PUBLIC "-framework CoreServices" "-framework CoreFoundation")
endif()
# Engine이 $<TARGET_OBJECTS:Core_objects>를 가져갈 수 있게 의존성 노출
add_dependencies(Core Core_objects)
set_property(TARGET Core PROPERTY SW_CORE_OBJECTS Core_objects)

message(STATUS "[Core] STATIC - foundation (ReflectionParser links Core; Engine shares Core_objects)")
