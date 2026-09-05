# ==============================================================================
# @file Source/Engine/CMakeLists.txt
# @brief 코어 모듈(RHI, Reflection, Object, Utility, Window 등) 빌드 타겟 스크립트
# @details
# 헤더(Common/Reflection/...)는 소스 구성을 위한 것이며
# 모듈 간 순환 참조(Reflection <-> Graphics 등) 때문에
# 단일 SHARED 라이브러리로 링크합니다.
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 라이브러리 타입 · RHI 소스 목록
#    Dev: SHARED / Shipping: STATIC
#    RHI MODULE 엔트리(Modules/)는 항상 Engine GLOB에서 제외
#    SW_RHI_AS_MODULES=ON 이면 device .cpp도 Engine에서 빼고 RHI_*가 동일 리스트를 씀
# ------------------------------------------------------------------------------
if(SW_SHIPPING_BUILD)
    set(coreLibType STATIC)
else()
    set(coreLibType SHARED)
endif()

include("${CMAKE_SOURCE_DIR}/cmake/Engine/RhiBackendSources.cmake")

set(coreExclude
    "Graphics/RHI/Modules/"
    "Module/DelayLoadNotifyHook.cpp"
)
if(SW_RHI_AS_MODULES OR SW_SHIPPING_BUILD)
    foreach(src IN LISTS SW_RHI_ALL_DEVICE_SOURCES)
        file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}" "${src}")
        string(REPLACE "." "\\." relEsc "${rel}")
        list(APPEND coreExclude "${relEsc}")
    endforeach()
endif()

# ------------------------------------------------------------------------------
# 2) Engine 타겟 — include 전파, Core_objects 재사용
#    Core 소스는 한 번만 컴파일. 동일 .obj를 Engine에 넣어
#    Dev에서 App/Editor가 Engine.dll로부터 FileUtil/Logger를 dllimport
#    ReflectionParser는 Core STATIC만 링크해 Engine.dll 순환을 피함
# ------------------------------------------------------------------------------
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")
foreach(exPattern IN LISTS coreExclude)
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX "${exPattern}")
endforeach()

# ResourcePackReader 가 uncompress 를 직접 부른다.
find_package(ZLIB REQUIRED)

add_library(Engine ${coreLibType} ${ENGINE_SOURCES})

target_include_directories(Engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(Engine
    PUBLIC
        sw_public_source_includes
    PRIVATE
        sw_global_options
        # 리소스 팩 압축 해제(ResourcePackReader). Core 는 압축 라이브러리에 종속되지 않게
        # 두고(ICompressionCodec 주석 참고), 실제로 쓰는 Engine 에만 붙인다.
        #
        # raw 이름 `zlib` 을 넘기면 안 된다. 그건 링커에 그대로 `-lzlib` 로 전달되는데,
        # Windows vcpkg 는 zlib.lib 라 우연히 맞지만 Linux 는 libz.so 라 못 찾는다. 공유
        # 라이브러리는 미해결 심볼을 허용해서 libEngine.so 링크는 통과하고, 그걸 링크하는
        # App 에서야 `undefined reference to uncompress` 로 터진다. 임포트 타겟을 쓴다.
        ZLIB::ZLIB
)
if(sw_flag_libraries)
    target_link_libraries(Engine PRIVATE ${sw_flag_libraries})
endif()

target_compile_definitions(Engine PRIVATE "SW_LOG_TAG=\"Engine\"" SW_ENGINE_INTERNAL)
sw_configurePch(Engine "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")

target_sources(Engine PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../sw.natvis"
)

target_sources(Engine PRIVATE $<TARGET_OBJECTS:Core_objects>)
add_dependencies(Engine Core_objects)

# ThirdParty include/link는 PRIVATE: Engine 외부로 헤더 노출을 차단하여 C-ABI 격리 유지
if(TARGET sw_third_party_includes)
    target_link_libraries(Engine PRIVATE sw_third_party_includes)
endif()

# ------------------------------------------------------------------------------
# 3) RHI 링크 — MODULE이 아니면 Shipping은 백엔드 하나, Dev는 모놀리식
# ------------------------------------------------------------------------------
if(NOT SW_RHI_AS_MODULES)
    if(SW_SHIPPING_BUILD)
        if(NOT SW_SHIPPING_RHI_BACKEND)
            if(WIN32)
                set(SW_SHIPPING_RHI_BACKEND "DX12" CACHE STRING "Shipping용 대상 RHI 백엔드")
            else()
                set(SW_SHIPPING_RHI_BACKEND "Vulkan" CACHE STRING "Shipping용 대상 RHI 백엔드")
            endif()
        endif()
        message(STATUS "[Engine] Shipping monolithic RHI backend: ${SW_SHIPPING_RHI_BACKEND}")

        if(SW_SHIPPING_RHI_BACKEND STREQUAL "DX12" AND TARGET sw_graphics_dx12_libs)
            target_sources(Engine PRIVATE ${SW_RHI_DX12_DEVICE_SOURCES})
            target_link_libraries(Engine PUBLIC sw_graphics_dx12_libs)
            target_compile_definitions(Engine PUBLIC SW_RHI_TARGET_DX12)
        elseif(SW_SHIPPING_RHI_BACKEND STREQUAL "DX11" AND TARGET sw_graphics_dx11_libs)
            target_sources(Engine PRIVATE ${SW_RHI_DX11_DEVICE_SOURCES})
            target_link_libraries(Engine PUBLIC sw_graphics_dx11_libs)
            target_compile_definitions(Engine PUBLIC SW_RHI_TARGET_DX11)
        elseif(SW_SHIPPING_RHI_BACKEND STREQUAL "Vulkan" AND TARGET sw_graphics_vulkan_libs)
            target_sources(Engine PRIVATE ${SW_RHI_VULKAN_DEVICE_SOURCES})
            target_link_libraries(Engine PUBLIC sw_graphics_vulkan_libs)
            target_compile_definitions(Engine PUBLIC SW_RHI_TARGET_VULKAN)
        elseif(SW_SHIPPING_RHI_BACKEND STREQUAL "OpenGL" AND TARGET sw_graphics_gl_libs)
            target_sources(Engine PRIVATE ${SW_RHI_GL_DEVICE_SOURCES})
            target_link_libraries(Engine PUBLIC sw_graphics_gl_libs)
            target_compile_definitions(Engine PUBLIC SW_RHI_TARGET_OPENGL)
        endif()
    else()
        if(TARGET sw_graphics_libs)
            target_link_libraries(Engine PUBLIC sw_graphics_libs)
        endif()
    endif()
endif()

# ------------------------------------------------------------------------------
# 4) export / IPO / delay-load 훅 경로 / 로그 태그 / Windows 시스템 라이브러리
#    SW_DELAYLOAD_HOOK_SOURCE는 sw_addDelayloadHook(Targets.cmake)이 참조
# ------------------------------------------------------------------------------
sw_configureEngineDllExports(Engine ${coreLibType})
set_target_properties(Engine PROPERTIES FOLDER "Source/Engine")

set_property(TARGET Engine PROPERTY SW_DELAYLOAD_HOOK_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/Utility/Module/DelayLoadNotifyHook.cpp")

# Windows: Input(XInput) + Audio(XAudio2) + 파일 대화상자 + 셰이더 컴파일러 유틸리티
if(WIN32)
    target_link_libraries(Engine PRIVATE
        comdlg32.lib
        d3dcompiler.lib
        dbghelp.lib
        delayimp.lib
        mfplat.lib
        mfreadwrite.lib
        mfuuid.lib
        ole32.lib
        shell32.lib
        xaudio2.lib
    )
    target_link_options(Engine PRIVATE
        /DELAYLOAD:d3dcompiler_47.dll
        /DELAYLOAD:mfplat.dll
        /DELAYLOAD:mfreadwrite.dll
        /DELAYLOAD:xaudio2_9.dll
    )
endif()

if(SW_RHI_AS_MODULES)
    target_compile_definitions(Engine PRIVATE SW_RHI_AS_MODULES)
    message(STATUS "[Engine] SW_RHI_AS_MODULES=ON -> RHI device sources excluded; loading RHI_* modules")
else()
    message(STATUS "[Engine] SW_RHI_AS_MODULES=OFF -> RHI device sources linked into Engine")
endif()
# ------------------------------------------------------------------------------
# 5) Unity — FrameRenderer/Material 분할 TU는 공유 내부 헤더 병합을 피함
# ------------------------------------------------------------------------------
if(COMMAND sw_setUnityBuild)
	sw_setUnityBuild(Engine BATCH_SIZE 12)
	if(COMMAND sw_skipUnitySources)
		# DelayLoadNotifyHook은 이미 EXCLUDE에 포함됨.
		# FrameRenderer/Material 분할 TU: 공유 내부 헤더의 Unity 병합을 피함.
		sw_skipUnitySources(Engine
			"${CMAKE_CURRENT_SOURCE_DIR}/Graphics/RenderPass/FrameRenderer.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/Graphics/RenderPass/FrameRendererResources.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/Graphics/RenderPass/FrameRendererPassExecute.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/Graphics/RenderPass/FrameRendererDraw.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/Graphics/RenderPass/FrameRendererConstants.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/Graphics/RenderPass/FrameRendererPso.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/Graphics/Material/Material.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/Graphics/Material/MaterialPacking.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/Graphics/Material/MaterialIO.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/Graphics/Material/MaterialInstance.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/Graphics/Material/MaterialCache.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/Graphics/RHI/IRHIDevice.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/Graphics/Shader/ShaderReflection.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/Graphics/Shader/ShaderReflectionSpirv.cpp"
			"${CMAKE_CURRENT_SOURCE_DIR}/Graphics/Shader/ShaderReflectionDx.cpp"
		)
	endif()
endif()

# ------------------------------------------------------------------------------
# 6) ENUM() 코드젠 — ReflectionParser 필요 (루트가 Tools 이후 이 파일을 호출)
#    RHI_* MODULE은 런타임 로드이므로 빌드 순서만 묶음 (링크하지 않음)
# ------------------------------------------------------------------------------
sw_addReflectionStep(Engine
	INCLUDES "${CMAKE_SOURCE_DIR}/Source"
)
sw_addReflectBuiltinsGen(Engine)

# 선택적 RHI 백엔드 MODULE 플러그인 (DX는 Windows 전용, GL/Vulkan은 전 플랫폼)
if(SW_RHI_AS_MODULES)
    add_subdirectory(Graphics/RHI/Modules)
endif()
