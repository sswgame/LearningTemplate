# --- SW 저장소 오버레이(ThirdParty/openssl/vcpkg-port) -----------------------------------------------------------------
# 이 저장소의 x64-windows 트리플릿은 clang-cl 체인로드 툴체인을 쓰므로 vcpkg 가 vcvars 환경을 싣지 않는다(체인로드가 있으면
# VCPKG_LOAD_VCVARS_ENV 기본값이 꺼진다). CMake 포트는 툴체인이 컴파일러를 넘겨 서지만, OpenSSL 의 Windows 빌드는 perl Configure +
# nmake 라 nmake · rc · mt 를 PATH 에서, CRT · SDK 헤더 · 라이브러리를 INCLUDE · LIB 에서 찾는다. 그래서 이 포트만 vcvars 가 하는 일
# (MSVC · Windows SDK 도구 폴더를 PATH 에, 헤더 · 라이브러리 폴더를 INCLUDE · LIB 에)을 여기서 한다. 트리플릿에서 하면 트리플릿 파일
# 해시가 모든 포트의 ABI 에 들어가 공유 설치 트리의 포트가 전부 다시 지어진다.
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
    message(FATAL_ERROR "[openssl overlay] MSVC build tools (VC.Tools.x86.x64) not found - nmake is required to build OpenSSL on Windows")
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
find_program(NMAKE nmake REQUIRED)
# --- 오버레이 끝 ------------------------------------------------------------------------------------------------------

# Need cmd to pass quoted CC from nmake to mkbuildinf.pl, GH-37134
find_program(CMD_EXECUTABLE cmd HINTS ENV PATH NO_DEFAULT_PATH REQUIRED)
cmake_path(NATIVE_PATH CMD_EXECUTABLE cmd)
set(ENV{COMSPEC} "${cmd}")

vcpkg_find_acquire_program(PERL)
get_filename_component(PERL_EXE_PATH "${PERL}" DIRECTORY)
vcpkg_add_to_path("${PERL_EXE_PATH}")

vcpkg_cmake_get_vars(cmake_vars_file)
include("${cmake_vars_file}")

if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x86")
    set(OPENSSL_ARCH VC-WIN32)
elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    set(OPENSSL_ARCH VC-WIN64A)
elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm")
    set(OPENSSL_ARCH VC-WIN32-ARM)
elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
    if(VCPKG_TARGET_IS_UWP)
        set(OPENSSL_ARCH VC-WIN64-ARM)
    elseif(VCPKG_DETECTED_CMAKE_C_COMPILER_ID MATCHES "Clang")
        set(OPENSSL_ARCH VC-CLANG-WIN64-CLANGASM-ARM)
    else()
        set(OPENSSL_ARCH VC-WIN64-CLANGASM-ARM)
    endif()
else()
    message(FATAL_ERROR "Unsupported target architecture: ${VCPKG_TARGET_ARCHITECTURE}")
endif()

if(VCPKG_TARGET_IS_UWP)
    vcpkg_list(APPEND CONFIGURE_OPTIONS
        no-unit-test
        no-asm
        no-uplink
    )
    string(APPEND OPENSSL_ARCH "-UWP")
endif()

if(VCPKG_CONCURRENCY GREATER "1")
    vcpkg_list(APPEND CONFIGURE_OPTIONS no-makedepend)
endif()

cmake_path(NATIVE_PATH CURRENT_PACKAGES_DIR NORMALIZE current_packages_dir_native)

# Clang always uses /Z7;  Patching /Zi /Fd<Name> out of openssl requires more work.
set(OPENSSL_BUILD_MAKES_PDBS ON)
if (VCPKG_DETECTED_CMAKE_C_COMPILER_ID MATCHES "Clang" OR VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    set(OPENSSL_BUILD_MAKES_PDBS OFF)
endif()

cmake_path(NATIVE_PATH VCPKG_DETECTED_CMAKE_C_COMPILER NORMALIZE cc)
if(OPENSSL_ARCH MATCHES "CLANG")
    vcpkg_find_acquire_program(CLANG)
    cmake_path(GET CLANG PARENT_PATH clang_path)
    vcpkg_add_to_path("${clang_path}")
    if(VCPKG_DETECTED_CMAKE_C_COMPILER_ID MATCHES "Clang")
        string(APPEND VCPKG_COMBINED_C_FLAGS_DEBUG " --target=aarch64-win32-msvc")
        string(APPEND VCPKG_COMBINED_C_FLAGS_RELEASE " --target=aarch64-win32-msvc")
    endif()
endif()
if(OPENSSL_ARCH MATCHES "CLANGASM")
    vcpkg_list(APPEND CONFIGURE_OPTIONS "ASFLAGS=--target=aarch64-win32-msvc")
else()
    vcpkg_find_acquire_program(NASM)
    cmake_path(NATIVE_PATH NASM NORMALIZE as)
    cmake_path(GET NASM PARENT_PATH nasm_path)
    vcpkg_add_to_path("${nasm_path}") # Needed by Configure
endif()

cmake_path(NATIVE_PATH VCPKG_DETECTED_CMAKE_AR NORMALIZE ar)
cmake_path(NATIVE_PATH VCPKG_DETECTED_CMAKE_LINKER NORMALIZE ld)

# We can't set openssldir because that would leak build machine information into the built binaries,
# and introduce vulnerabilities where OpenSSL would search those locations at runtime, potentially
# unexpectedly loading code from there. For example CVE-2019-12572
#
# Put the built bits in subdirectories with DESTDIR then move them where they go after the fact
# instead.
vcpkg_build_nmake(
    SOURCE_PATH "${SOURCE_PATH}"
    PREFER_JOM
    CL_LANGUAGE NONE
    PRERUN_SHELL_RELEASE "${PERL}" Configure
        ${CONFIGURE_OPTIONS} 
        ${OPENSSL_ARCH}
        "AS=${as}"
        "CC=${cc}"
        "CFLAGS=${VCPKG_COMBINED_C_FLAGS_RELEASE}"
        "AR=${ar}"
        "ARFLAGS=${VCPKG_COMBINED_STATIC_LINKER_FLAGS_RELEASE}"
        "LD=${ld}"
        "LDFLAGS=${VCPKG_COMBINED_SHARED_LINKER_FLAGS_RELEASE}"
    PRERUN_SHELL_DEBUG "${PERL}" Configure
        ${CONFIGURE_OPTIONS}
        ${OPENSSL_ARCH}
        --debug
        "AS=${as}"
        "CC=${cc}"
        "CFLAGS=${VCPKG_COMBINED_C_FLAGS_DEBUG}"
        "AR=${ar}"
        "ARFLAGS=${VCPKG_COMBINED_STATIC_LINKER_FLAGS_DEBUG}"
        "LD=${ld}"
        "LDFLAGS=${VCPKG_COMBINED_SHARED_LINKER_FLAGS_DEBUG}"
    PROJECT_NAME "makefile"
    TARGET install_dev install_modules ${INSTALL_FIPS}
    LOGFILE_ROOT install
    OPTIONS
        "INSTALL_PDBS=${OPENSSL_BUILD_MAKES_PDBS}" # install-pdbs.patch
    OPTIONS_RELEASE
        "DESTDIR=${current_packages_dir_native}"
        install_runtime install_ssldirs # extra targets
    OPTIONS_DEBUG
        "DESTDIR=${current_packages_dir_native}/debug"
)

function(z_rearrange_openssl_dirs)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "OUT_PROGRAM_FILES_DIR;FLAVOR_PREFIX" "")

    if(DEFINED arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "z_rearrange_openssl_dirs was passed extra arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()

    # The resulting directory will contain something like "Program Files" or "Program Files (x86)";
    # globbing here to be architecture agnostic
    set(prefix_packages_dir "${CURRENT_PACKAGES_DIR}${arg_FLAVOR_PREFIX}")
    file(GLOB flavor_programfiles_dir LIST_DIRECTORIES true "${prefix_packages_dir}/Program*")
    if(NOT flavor_programfiles_dir)
        message(FATAL_ERROR "${flavor_programfiles_dir}: error: couldn't find program files dir")
    endif()

    if(DEFINED arg_OUT_PROGRAM_FILES_DIR)
        set("${arg_OUT_PROGRAM_FILES_DIR}" "${flavor_programfiles_dir}" PARENT_SCOPE)
    endif()

    set(flavor_openssl_dir "${flavor_programfiles_dir}/OpenSSL")
    if(NOT EXISTS "${flavor_openssl_dir}")
        message(FATAL_ERROR "${flavor_openssl_dir}: should exist and be OpenSSLDir")
    endif()

    # ideally we would use RENAME rather than COPY and REMOVE_RECURSE but CMake doesn't have an out
    # of the box way to do that correctly merging directories
    file(GLOB flavor_openssl_dirs LIST_DIRECTORIES true "${flavor_openssl_dir}/*")
    file(COPY ${flavor_openssl_dirs} DESTINATION "${prefix_packages_dir}")
    file(REMOVE_RECURSE "${flavor_openssl_dir}")
endfunction()

z_rearrange_openssl_dirs(FLAVOR_PREFIX "" OUT_PROGRAM_FILES_DIR release_programfiles)
if(NOT VCPKG_BUILD_TYPE)
    z_rearrange_openssl_dirs(FLAVOR_PREFIX "/debug" OUT_PROGRAM_FILES_DIR debug_programfiles)
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
    file(REMOVE_RECURSE "${debug_programfiles}")
    file(REMOVE "${CURRENT_PACKAGES_DIR}/debug/bin/c_rehash.pl")
endif()

set(scripts "bin/c_rehash.pl" "misc/CA.pl" "misc/tsget.pl")
if("tools" IN_LIST FEATURES)
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/tools/${PORT}")
    file(COPY_FILE "${release_programfiles}/Common Files/SSL/openssl.cnf" "${CURRENT_PACKAGES_DIR}/tools/${PORT}/openssl.cnf")
    if("fips" IN_LIST FEATURES)
	    file(COPY_FILE "${release_programfiles}/Common Files/SSL/fipsmodule.cnf" "${CURRENT_PACKAGES_DIR}/tools/${PORT}/fipsmodule.cnf")
    endif()

    file(RENAME "${CURRENT_PACKAGES_DIR}/bin/c_rehash.pl" "${CURRENT_PACKAGES_DIR}/tools/${PORT}/c_rehash.pl")
    file(RENAME "${release_programfiles}/Common Files/SSL/misc/CA.pl" "${CURRENT_PACKAGES_DIR}/tools/${PORT}/CA.pl")
    file(RENAME "${release_programfiles}/Common Files/SSL/misc/tsget.pl" "${CURRENT_PACKAGES_DIR}/tools/${PORT}/tsget.pl")
    vcpkg_copy_tools(TOOL_NAMES openssl AUTO_CLEAN)
else()
    file(REMOVE
        "${CURRENT_PACKAGES_DIR}/bin/c_rehash.pl"
        "${release_programfiles}/Common Files/SSL/misc/CA.pl"
        "${release_programfiles}/Common Files/SSL/misc/tsget.pl"
        )

    if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
        file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/bin" "${CURRENT_PACKAGES_DIR}/debug/bin")
    endif()
endif()

vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup()

file(REMOVE_RECURSE # to pass empty directories check
    "${release_programfiles}/Common Files/SSL/certs"
    "${release_programfiles}/Common Files/SSL/misc"
    "${release_programfiles}/Common Files/SSL/private"
)
