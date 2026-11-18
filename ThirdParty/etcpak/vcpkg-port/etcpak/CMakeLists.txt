# etcpak 의 블록 압축(ProcessRGB) · 풀기(Decode) 만 짓는다. 응용 프로그램 · libpng · Tracy 는 뺀다.
# -march 를 주지 않는다 — SSE4.1 · AVX2 길은 컴파일러 깃발이 있을 때만 켜지므로 스칼라 길로 지어 빌드 기계와 상관없이 같은 블록을 낸다(쿠킹 결과가 DDC 키로 쓰인다).
cmake_minimum_required(VERSION 3.16)
project(etcpak C CXX)

set(source_dir "${CMAKE_CURRENT_LIST_DIR}/..")

add_library(etcpak STATIC
    ${source_dir}/ProcessRGB.cpp
    ${source_dir}/Tables.cpp
    ${source_dir}/Dither.cpp
    ${source_dir}/Decode.cpp
    ${source_dir}/bcdec.c)

target_compile_features(etcpak PRIVATE cxx_std_20)
target_include_directories(etcpak PUBLIC
    $<BUILD_INTERFACE:${source_dir}>
    $<INSTALL_INTERFACE:include/etcpak>)
if(MSVC)
    target_compile_definitions(etcpak PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()

install(TARGETS etcpak EXPORT unofficial-etcpak-targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)
install(FILES
        ${source_dir}/ProcessRGB.hpp
        ${source_dir}/Decode.hpp
        ${source_dir}/ForceInline.hpp
    DESTINATION include/etcpak)
install(EXPORT unofficial-etcpak-targets
    NAMESPACE unofficial::etcpak::
    FILE unofficial-etcpak-config.cmake
    DESTINATION share/unofficial-etcpak)
