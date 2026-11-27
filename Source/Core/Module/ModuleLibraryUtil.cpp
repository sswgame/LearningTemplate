#include "pch.h"

#include "Core/Module/ModuleLibraryUtil.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/PlatformOSHeaders.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    namespace
    {
        /** @brief `setModuleSubdirectory` 가 옮긴 모듈 하나 — 고정 배열이라 정적 소멸 · 할당자 순서와 엮이지 않는다. */
        struct ModuleSubdirectory
        {
            utf8 _arrBaseName[64]{};
            utf8 _arrDirectory[constant::file::kMaxPathSize]{};
        };

        /**
         * @brief 옮긴 모듈 하나입니다(`setModuleSubdirectory` 만 쓴다). 기동 스레드가 한 번 적고 그 뒤에 읽는다.
         * @details Core 의 정적 함수 묶음이 읽는 칸이라 서비스 범위에 맡길 수 없다 — 읽는 곳(`findModuleLibraryPath`)을 서비스 범위 없이 도는 도구 · 테스트 · 모듈 호스트도
         *          부른다. 고정 배열이라 상수 초기화되므로 지연 생성하는 접근자를 두지 않고 이 변수 하나가 소유한다.
         */
        ModuleSubdirectory s_moduleSubdirectory;

        struct ModuleLibraryUtilInternal
        {
            /** @brief @p baseName 이 옮긴 모듈이면 그 `Bin` 상대 폴더를, 아니면 빈 뷰를 돌려줍니다. */
            static string_view findModuleSubdirectory( string_view baseName )
            {
                const ModuleSubdirectory& entry = s_moduleSubdirectory;
                if ( entry._arrDirectory[0] == '\0' || baseName != string_view{ entry._arrBaseName } )
                    return {};
                return string_view{ entry._arrDirectory };
            }
        };
    } // namespace

    string_view ModuleLibraryUtil::getSharedLibraryPrefix()
    {
#if defined( SW_PLATFORM_WINDOWS )
        return "";
#elif defined( SW_PLATFORM_POSIX )
        return "lib";
#else
    #error "Unsupported platform"
#endif
    }

    string_view ModuleLibraryUtil::getSharedLibraryExtension()
    {
#if defined( SW_PLATFORM_WINDOWS )
        return ".dll";
#elif defined( SW_PLATFORM_POSIX )
        return ".so";
#else
    #error "Unsupported platform"
#endif
    }

    string ModuleLibraryUtil::formatSharedLibraryName( string_view baseName )
    {
        StringBuilder<constant::buffer::kMax128> sb;
        sb.append( getSharedLibraryPrefix() ).append( baseName ).append( getSharedLibraryExtension() );
        return string( sb.view() );
    }

    AbsolutePath ModuleLibraryUtil::getModuleDirectory()
    {
        return ProcessRoots::get()._binary.join( kModuleFolder );
    }

    void ModuleLibraryUtil::setModuleSubdirectory( string_view baseName, string_view binRelativeDirectory )
    {
        ModuleSubdirectory& entry = s_moduleSubdirectory;
        entry                     = ModuleSubdirectory{};
        if ( binRelativeDirectory.empty() )
            return;
        if ( baseName.size() >= SW_COUNT_OF( entry._arrBaseName ) || binRelativeDirectory.size() >= SW_COUNT_OF( entry._arrDirectory ) )
        {
            SW_LOG_ERROR_ID( "ModuleLibraryUtil.ModuleSubdirectoryTooLong", "Module subdirectory for %# is too long - the module stays in %#", string{ baseName }.c_str(), kModuleFolder );
            return;
        }
        StringUtil::strncpy( entry._arrBaseName, string{ baseName }.c_str(), static_cast<uint32>( SW_COUNT_OF( entry._arrBaseName ) ) );
        StringUtil::strncpy( entry._arrDirectory, string{ binRelativeDirectory }.c_str(), static_cast<uint32>( SW_COUNT_OF( entry._arrDirectory ) ) );
    }

    bool ModuleLibraryUtil::getModuleSubdirectory( string& outBaseName, string& outBinRelativeDirectory )
    {
        const ModuleSubdirectory& entry = s_moduleSubdirectory;
        if ( entry._arrDirectory[0] == '\0' )
            return false;
        outBaseName             = entry._arrBaseName;
        outBinRelativeDirectory = entry._arrDirectory;
        return true;
    }

    AbsolutePath ModuleLibraryUtil::findModuleLibraryPath( string_view baseName )
    {
        const string fileName = formatSharedLibraryName( baseName );
        // 옮긴 모듈은 그 폴더에만 있다 — 없으면 그 경로를 돌려 부르는 쪽이 "없다" 를 알리게 한다(활성 게임의 `Bin/Modules` 사본으로 넘어가지 않는다).
        const string_view movedDirectory = ModuleLibraryUtilInternal::findModuleSubdirectory( baseName );
        if ( movedDirectory.empty() == false )
            return ProcessRoots::get()._binary.join( movedDirectory ).join( fileName );
        AbsolutePath modulePath = getModuleDirectory().join( fileName );
        if ( FileUtil::exists( modulePath ) == false )
        {
            const AbsolutePath binaryPath = ProcessRoots::get()._binary.join( fileName );
            if ( FileUtil::exists( binaryPath ) )
                modulePath = binaryPath;
        }
        return modulePath;
    }

    bool ModuleLibraryUtil::pinLibraryContaining( const void* pAddress )
    {
        if ( pAddress == nullptr )
            return false;
#if defined( SW_PLATFORM_WINDOWS )
        HMODULE hModule = nullptr;
        return GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, static_cast<LPCWSTR>( pAddress ), &hModule ) != FALSE;
#elif defined( SW_PLATFORM_POSIX )
        Dl_info info{};
        if ( dladdr( pAddress, &info ) == 0 || info.dli_fname == nullptr )
            return false;
        return dlopen( info.dli_fname, RTLD_NOW | RTLD_NOLOAD | RTLD_NODELETE ) != nullptr; // 고정한 참조는 놓지 않는다
#else
    #error "Unsupported platform"
#endif
    }

    string ModuleLibraryUtil::findImageNameContaining( const void* pAddress )
    {
        string path;
#if defined( SW_PLATFORM_WINDOWS )
        HMODULE hModule = nullptr;
        if ( GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>( pAddress ), &hModule ) == FALSE )
            return {};
        utf16       arrPath[constant::buffer::kMax1024]{};
        const DWORD length = GetModuleFileNameW( hModule, arrPath, static_cast<DWORD>( std::size( arrPath ) ) );
        if ( length == 0 || length >= std::size( arrPath ) )
            return {};
        path = StringUtil::utf16ToUTF8( arrPath );
#elif defined( SW_PLATFORM_POSIX )
        Dl_info info{};
        if ( dladdr( pAddress, &info ) == 0 || info.dli_fname == nullptr )
            return {};
        path = info.dli_fname;
#else
    #error "Unsupported platform"
#endif
        string            stem   = FileUtil::removeExtension( FileUtil::getFileNamePart( path ) );
        const string_view prefix = getSharedLibraryPrefix();
        if ( prefix.empty() == false && StringUtil::startsWith( stem, prefix ) )
            stem.erase( 0, prefix.size() );
        return stem;
    }

    // 배포 실행 파일도 제 빌드 ID 를 읽는다(`ModuleBuildID`) — 동적 모듈 블록 밖에 둔다.
    const void* ModuleLibraryUtil::findImageNtHeaders( const void* pBase )
    {
#if defined( SW_PLATFORM_WINDOWS )
        if ( pBase == nullptr )
            return nullptr;
        const uint8*            pBytes = static_cast<const uint8*>( pBase );
        const IMAGE_DOS_HEADER* pDos   = reinterpret_cast<const IMAGE_DOS_HEADER*>( pBytes );
        if ( pDos->e_magic != IMAGE_DOS_SIGNATURE )
            return nullptr;
        const IMAGE_NT_HEADERS* pNt = reinterpret_cast<const IMAGE_NT_HEADERS*>( pBytes + pDos->e_lfanew );
        return ( pNt->Signature == IMAGE_NT_SIGNATURE ) ? pNt : nullptr;
#elif defined( SW_PLATFORM_POSIX )
        (void)pBase;
        return nullptr;
#else
    #error "Unsupported platform"
#endif
    }
} // namespace sw
