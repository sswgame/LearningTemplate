# ==============================================================================
# @file ThirdParty/joltphysics/vcpkg-port/joltphysics/portfile.cmake
# @brief vcpkg 내장 joltphysics 포트(5.6.0#1)와 같고, `CROSS_PLATFORM_DETERMINISTIC=ON` 만 다르다.
#        내장 포트는 끄고 짓는다 — 그러면 Jolt 가 /fp:fast 로 지어져 Debug · Release 가 다른 시뮬레이션을 낸다(천 쇼케이스의 깃발이 구성마다 달랐다).
#        켜면 /fp:precise 에 FMA 를 쓰지 않아 구성 · 컴파일러가 달라도 같은 결과다(Jolt 문서의 결정적 시뮬레이션 · 락스텝 설정 — 넷 상태 해시도 이것에 기댄다).
#        트리플릿의 포트별 옵션(VCPKG_CMAKE_CONFIGURE_OPTIONS)으로도 되지만 트리플릿이 바뀌면 모든 포트를 다시 짓는다 — 그래서 오버레이로 둔다.
#        업스트림 포트가 옵션을 기능으로 내주거나 vcpkg 를 올릴 때 이 오버레이를 다시 본다.
# ==============================================================================
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO jrouwe/JoltPhysics
    REF "v${VERSION}"
    SHA512 bc6f2436bef91a6ffd09eee98186be645f6e9a9b3f65a7a645abf00432ac40ada03320c1fe946661817a94eda372cf8d88e4889991cdd25f1c16ecf9a4486677
    HEAD_REF master
)

string(COMPARE EQUAL "${VCPKG_CRT_LINKAGE}" "static" USE_STATIC_CRT)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        debugrenderer       DEBUG_RENDERER_IN_DEBUG_AND_RELEASE
        profiler            PROFILER_IN_DEBUG_AND_RELEASE
        rtti                CPP_RTTI_ENABLED
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}/Build"
    OPTIONS
        -DTARGET_UNIT_TESTS=OFF
        -DTARGET_HELLO_WORLD=OFF
        -DTARGET_PERFORMANCE_TEST=OFF
        -DTARGET_SAMPLES=OFF
        -DTARGET_VIEWER=OFF
        -DCROSS_PLATFORM_DETERMINISTIC=ON
        -DINTERPROCEDURAL_OPTIMIZATION=OFF
        -DUSE_STATIC_MSVC_RUNTIME_LIBRARY=${USE_STATIC_CRT}
        -DENABLE_ALL_WARNINGS=OFF
        -DOVERRIDE_CXX_FLAGS=OFF
        -DJPH_USE_DX12=OFF
        -DJPH_USE_VK=OFF
        -DJPH_USE_MTL=OFF
        ${FEATURE_OPTIONS}
    OPTIONS_RELEASE
        -DGENERATE_DEBUG_SYMBOLS=OFF
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
vcpkg_cmake_config_fixup(PACKAGE_NAME Jolt CONFIG_PATH "lib/cmake/Jolt")

if(VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
    vcpkg_replace_string(
        "${CURRENT_PACKAGES_DIR}/include/Jolt/Core/Core.h"
        "#pragma once"
        [=[#pragma once

#ifndef JPH_SHARED_LIBRARY
#define JPH_SHARED_LIBRARY
#endif
#ifndef JPH_FLOATING_POINT_EXCEPTIONS_ENABLED
#define JPH_FLOATING_POINT_EXCEPTIONS_ENABLED
#endif
#ifndef JPH_USE_CPU_COMPUTE
#define JPH_USE_CPU_COMPUTE
#endif
#ifndef JPH_OBJECT_STREAM
#define JPH_OBJECT_STREAM
#endif]=]
    )
endif()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
