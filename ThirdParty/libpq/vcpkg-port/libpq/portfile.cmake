vcpkg_download_distfile(ARCHIVE
    URLS "https://ftp.postgresql.org/pub/source/v${VERSION}/postgresql-${VERSION}.tar.bz2"
         "https://www.mirrorservice.org/sites/ftp.postgresql.org/source/v${VERSION}/postgresql-${VERSION}.tar.bz2"
    FILENAME "postgresql-${VERSION}.tar.bz2"
    SHA512 1a7606b5b2460a4fc817d491e1e03262df17ece6869fa3d2b80647baffcfb790fbcfa019c3b10fab9f9a14218651b6a358864e40a5d8c5ddc796db2d6e5b469c
)

vcpkg_extract_source_archive(
    SOURCE_PATH
    ARCHIVE "${ARCHIVE}"
    PATCHES
        library-linkage.diff
        libpq-and-client-tools.diff
        libintl.diff
        zic.diff
        windows/macro-def.patch
        windows/spin_delay.patch
        windows/getopt.patch
)

file(GLOB _py3_include_path "${CURRENT_HOST_INSTALLED_DIR}/include/python3*")
string(REGEX MATCH "python3\\.([0-9]+)" _python_version_tmp "${_py3_include_path}")
set(PYTHON_VERSION_MINOR "${CMAKE_MATCH_1}")

vcpkg_cmake_get_vars(cmake_vars_file)
include("${cmake_vars_file}")

# --- SW 저장소 오버레이(ThirdParty/libpq/vcpkg-port) -----------------------------------------------------------------
# 이 저장소의 x64-windows 트리플릿은 clang-cl 체인로드 툴체인을 쓰므로 vcpkg 가 vcvars 환경을 싣지 않는다(체인로드가 있으면
# VCPKG_LOAD_VCVARS_ENV 기본값이 꺼진다). CMake 포트는 툴체인이 컴파일러를 넘겨 서지만, libpq 의 Windows 빌드는 meson 이라
# lld-link 를 이름으로(PATH) 찾고, clang-cl 은 CRT · SDK 헤더 · 라이브러리를 INCLUDE · LIB 에서 찾는다. 그래서 이 포트만 vcvars 가 하는 일
# (MSVC · Windows SDK 도구 폴더를 PATH 에, 헤더 · 라이브러리 폴더를 INCLUDE · LIB 에)을 여기서 한다. 트리플릿에서 하면 트리플릿 파일
# 해시가 모든 포트의 ABI 에 들어가 공유 설치 트리의 포트가 전부 다시 지어진다.
if(VCPKG_TARGET_IS_WINDOWS)
    set(swVswhere "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer/vswhere.exe")
    set(swMsvcRoot "")
    if(EXISTS "${swVswhere}")
        execute_process(COMMAND "${swVswhere}" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64
            -property installationPath OUTPUT_VARIABLE swVsRoot OUTPUT_STRIP_TRAILING_WHITESPACE)
        file(GLOB swMsvcRoots LIST_DIRECTORIES true "${swVsRoot}/VC/Tools/MSVC/*")
        list(SORT swMsvcRoots COMPARE NATURAL ORDER DESCENDING)
        if(swMsvcRoots)
            list(GET swMsvcRoots 0 swMsvcRoot)
        endif()
    endif()
    if(swMsvcRoot STREQUAL "")
        message(FATAL_ERROR "[libpq overlay] MSVC build tools (VC.Tools.x86.x64) not found - the CRT headers are required to build libpq on Windows")
    endif()
    set(swKitsRoot "$ENV{ProgramFiles\(x86\)}/Windows Kits/10")
    file(GLOB swSdkVersions LIST_DIRECTORIES true RELATIVE "${swKitsRoot}/Include" "${swKitsRoot}/Include/10.*")
    list(SORT swSdkVersions COMPARE NATURAL ORDER DESCENDING)
    list(GET swSdkVersions 0 swSdkVersion)
    vcpkg_add_to_path("${swMsvcRoot}/bin/Hostx64/x64")
    vcpkg_add_to_path("${swKitsRoot}/bin/${swSdkVersion}/x64")
    set(swSdkInclude "${swKitsRoot}/Include/${swSdkVersion}")
    set(swSdkLib "${swKitsRoot}/Lib/${swSdkVersion}")
    set(ENV{INCLUDE} "${swMsvcRoot}/include;${swSdkInclude}/ucrt;${swSdkInclude}/um;${swSdkInclude}/shared;${swSdkInclude}/winrt")
    set(ENV{LIB} "${swMsvcRoot}/lib/x64;${swSdkLib}/ucrt/x64;${swSdkLib}/um/x64")
    # meson 은 clang-cl 의 링커를 `lld-link` 이름으로 찾는다 — 툴체인의 LLVM 폴더를 PATH 에.
    get_filename_component(swLlvmBin "${VCPKG_DETECTED_CMAKE_C_COMPILER}" DIRECTORY)
    vcpkg_add_to_path("${swLlvmBin}")
    # PostgreSQL 의 C99 확인은 <tgmath.h> 를 include 하는데, clang 의 tgmath.h 는 UCRT complex.h 의 구조체 복소수와 맞지 않아 clang-cl 에서
    # "C99 아님" 으로 멈춘다. PostgreSQL 은 tgmath 를 쓰지 않으므로 확인 코드에서만 그 줄을 뺀다.
    vcpkg_replace_string("${SOURCE_PATH}/meson.build" "#include <tgmath.h>\n" "" IGNORE_UNCHANGED)
    # PostgreSQL meson 은 MSVC 분기(win32_msvc 헤더 · dirent · getopt · /DEF 링크)를 `cc.get_id() == 'msvc'` 로 고르는데 clang-cl 은
    # 'clang-cl' 이라 MinGW 길로 빠진다(unistd.h · dirent.h 없음). MSVC 와 같은 인자 문법이면 MSVC 길을 타도록 판정을 바꾼다.
    foreach(swMesonFile IN ITEMS "meson.build" "src/port/meson.build" "src/backend/meson.build" "src/bin/pgevent/meson.build")
        vcpkg_replace_string("${SOURCE_PATH}/${swMesonFile}" "cc.get_id() == 'msvc'" "cc.get_argument_syntax() == 'msvc'" IGNORE_UNCHANGED)
        vcpkg_replace_string("${SOURCE_PATH}/${swMesonFile}" "cc.get_id() != 'msvc'" "cc.get_argument_syntax() != 'msvc'" IGNORE_UNCHANGED)
    endforeach()
    # clang-cl 은 GCC 식 <cpuid.h> 도 링크돼 `__get_cpuid` 길을 고르는데, 같은 파일이 <intrin.h> 도 끌어와 `__cpuid` 매크로와 함수가
    # 부딪친다. MSVC 인자 문법이면 MSVC 와 같은 `__cpuid` · `__cpuidex`(intrin.h) 길만 보게 한다.
    vcpkg_replace_string("${SOURCE_PATH}/meson.build" "if cc.links('''\n    #include <cpuid.h>" "if cc.get_argument_syntax() != 'msvc' and cc.links('''\n    #include <cpuid.h>" IGNORE_UNCHANGED)
    # link.exe 의 출력 줄을 줄이려는 /NOEXP · /NOIMPLIB 를 lld-link 는 모른다(파일 이름으로 읽고 멈춘다) — 빼도 산출물은 같다.
    vcpkg_replace_string("${SOURCE_PATH}/meson.build" "  ldflags += '/NOEXP'\n  ldflags_mod += '/NOIMPLIB'\n" "" IGNORE_UNCHANGED)
endif()
# --- 오버레이 끝 ------------------------------------------------------------------------------------------------------

vcpkg_find_acquire_program(BISON)
vcpkg_find_acquire_program(FLEX)
vcpkg_find_acquire_program(PERL)

vcpkg_list(SET MESON_OPTIONS_RELEASE)
vcpkg_list(SET MESON_OPTIONS)
foreach(option IN ITEMS icu lz4 zlib zstd)
    if(option IN_LIST FEATURES)
        list(APPEND MESON_OPTIONS -D${option}=enabled)
    endif()
endforeach()

if("openssl" IN_LIST FEATURES)
    list(APPEND MESON_OPTIONS -Dssl=openssl)
else()
    list(APPEND MESON_OPTIONS -Dssl=none)
endif()

if("nls" IN_LIST FEATURES)
    list(APPEND MESON_OPTIONS -Dnls=enabled)
endif()

if("client" IN_LIST FEATURES)
    list(APPEND MESON_OPTIONS_RELEASE -Dtools=enabled)
    if(VCPKG_CROSSCOMPILING)
        list(APPEND MESON_OPTIONS "-DZIC=${CURRENT_HOST_INSTALLED_DIR}/manual-tools/${PORT}/zic${VCPKG_HOST_EXECUTABLE_SUFFIX}")
    endif()
endif()

vcpkg_configure_meson(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -Dauto_features=disabled
        ${MESON_OPTIONS}
        # cannot use ADDITIONAL_BINARIES for "native" programs
        "-DBISON=['${BISON}']"
        "-DFLEX=['${FLEX}']"
        "-DPERL=${PERL}"
    OPTIONS_RELEASE
        ${MESON_OPTIONS_RELEASE}
)
vcpkg_install_meson()
vcpkg_fixup_pkgconfig()

if(VCPKG_TARGET_IS_WINDOWS AND NOT VCPKG_TARGET_IS_MINGW AND VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
    file(GLOB pc_files "${CURRENT_PACKAGES_DIR}/lib/pkgconfig/*.pc" "${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig/*.pc")
    foreach(file IN LISTS pc_files)
        vcpkg_replace_string("${file}" " -l(pq|pg|ecpg)" " -llib\\1" REGEX)
    endforeach()
endif()

vcpkg_copy_tools(TOOL_NAMES ecpg AUTO_CLEAN)
if("client" IN_LIST FEATURES)
    if(NOT VCPKG_CROSSCOMPILING)
        vcpkg_copy_tools(TOOL_NAMES zic AUTO_CLEAN DESTINATION "${CURRENT_PACKAGES_DIR}/manual-tools/${PORT}")
    endif()
    vcpkg_copy_tools(
        TOOL_NAMES
            clusterdb createdb createuser
            dropdb dropuser
            pg_amcheck
            pg_basebackup pgbench
            pg_combinebackup pg_config pg_createsubscriber
            pg_dump pg_dumpall
            pg_isready
            pg_receivewal pg_recvlogical pg_restore
            pg_verifybackup
            psql
            reindexdb
            vacuumdb
        AUTO_CLEAN
    )
endif()


file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

configure_file("${CMAKE_CURRENT_LIST_DIR}/vcpkg-cmake-wrapper.cmake" "${CURRENT_PACKAGES_DIR}/share/postgresql/vcpkg-cmake-wrapper.cmake" @ONLY)
file(INSTALL "${CURRENT_PORT_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYRIGHT")
