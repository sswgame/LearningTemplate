# ==============================================================================
# @file Source/Core/CMakeLists.txt
# @brief 호스트 · 런타임의 기반(Common, Log, FileUtil, String 등)을 담는 STATIC 타깃
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 소스 목록 — 파일 다이얼로그 · 워처만 OS 별로 추가한다
# ------------------------------------------------------------------------------
set(cfSources
	"${CMAKE_CURRENT_SOURCE_DIR}/CommandLine/CommandLineManager.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Common/Macros.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Compression/CompressionCodecRegistry.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Compression/CompressionStream.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Compression/NullCompressionCodec.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Compression/RleCompressionCodec.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Concurrency/DataRaceDetector.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Concurrency/DeadlockDetector.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Concurrency/Futex.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Container/DynamicBitset.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Delegate/Delegate.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Module/ModuleCodeHolder.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Module/ModuleImageUtil.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Event/EventDispatcher.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/File/FileUtil.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/File/IFileWatcher.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/File/PlatformFileUtil.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/GlobalVariable/GlobalVariableManager.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Log/ConsoleLogOutput.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Log/FileLogOutput.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Log/Logger.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Math/MatrixMath.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Math/VectorMath.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Memory/FrameArenaAllocator.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Memory/LinearAllocator.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Memory/Memory.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Memory/MemoryProfiler.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Memory/PoolAllocator.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Process/CallStackCapture.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Process/CrashContext.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Process/Process.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/String/StringUtil.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/String/TagID.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/String/hashed_string.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Task/TaskManager.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Task/TaskNode.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Task/TaskNodePool.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Task/TaskTypes.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Time/CpuClock.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Time/CpuTimer.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Uuid/Uuid.cpp"
)

set(listWindowsSource
	"${CMAKE_CURRENT_SOURCE_DIR}/File/Windows/WindowsFileDialog.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/File/Windows/WindowsFileWatcher.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Process/Windows/WindowsCallStackCapture.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Process/Windows/WindowsCrashHandler.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Process/Windows/WindowsProcess.cpp"
)
set(listPosixSource
	"${CMAKE_CURRENT_SOURCE_DIR}/Process/Posix/PosixCallStackCapture.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Process/Posix/PosixCrashHandler.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Process/Posix/PosixProcess.cpp"
)
set(listLinuxSource
	"${CMAKE_CURRENT_SOURCE_DIR}/File/Linux/LinuxFileDialog.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/File/Linux/LinuxFileWatcher.cpp"
)

# 고르지 않은 OS 의 것은 소스 검사 게이트에 "이 구성이 짓지 않는다" 로 적는다(`sw_declareUnbuiltSources`).
if(WIN32)
	list(APPEND cfSources ${listWindowsSource})
	sw_declareUnbuiltSources(${listPosixSource} ${listLinuxSource})
elseif(UNIX)
	list(APPEND cfSources ${listLinuxSource} ${listPosixSource})
	sw_declareUnbuiltSources(${listWindowsSource})
endif()

# ------------------------------------------------------------------------------
# 2) Core_objects — 한 번만 컴파일한다
#    ReflectionParser: Core STATIC 을 링크한다
#    Engine: 같은 OBJECT 를 넣어 Dev 에서 Engine.dll 로 export 한다
#    SW_EXPORTS: App · Editor(SW_IMPORTS)가 dllimport 할 수 있게 한다
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

if(NOT SW_SHIPPING_BUILD)
	target_compile_definitions(Core_objects PRIVATE SW_EXPORTS)
endif()

if(WIN32)
	target_link_libraries(Core_objects PUBLIC comdlg32 ole32 shell32 dbghelp synchronization)
endif()

target_compile_definitions(Core_objects PRIVATE "SW_LOG_TAG=\"Engine\"")
sw_configurePch(Core_objects "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")
target_link_libraries(Core_objects PRIVATE sw_global_options)

# ------------------------------------------------------------------------------
# 3) Core STATIC — 파서가 링크하는 껍데기. Engine 은 Core_objects 를 직접 가져간다
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
	target_link_libraries(Core PUBLIC comdlg32 ole32 shell32 dbghelp synchronization)
endif()

# Engine 이 $<TARGET_OBJECTS:Core_objects> 를 가져갈 수 있도록 의존성을 드러낸다
add_dependencies(Core Core_objects)
set_property(TARGET Core PROPERTY SW_CORE_OBJECTS Core_objects)

message(STATUS "[Core] STATIC - foundation (ReflectionParser links Core; Engine shares Core_objects)")
