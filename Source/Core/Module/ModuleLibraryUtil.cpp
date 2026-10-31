#include "pch.h"

#include "Core/Module/ModuleLibraryUtil.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/PlatformOSHeaders.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#if defined( SW_PLATFORM_LINUX )
    #include <link.h>
#endif

SW_LOG_CALLER( "ModuleLibraryUtil" );
namespace sw
{
    namespace
    {
        /** @brief `setModuleSubdirectory` 가 옮긴 모듈 하나 — 고정 배열이라 정적 소멸 · 할당자 순서와 엮이지 않는다. */
        struct ModuleSubdirectory
        {
            utf8 _arrBaseName[64]{};
            utf8 _arrDirectory[constant::kMaxPathSize]{};
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

#if defined( SW_PLATFORM_WINDOWS )
            /** @brief 라이브러리 기준 주소 @p pBase 의 PE NT 머리입니다. DOS · NT 서명이 맞지 않으면 nullptr 입니다. */
            static const IMAGE_NT_HEADERS* findNtHeaders( const void* pBase )
            {
                const uint8*            pBytes = static_cast<const uint8*>( pBase );
                const IMAGE_DOS_HEADER* pDos   = reinterpret_cast<const IMAGE_DOS_HEADER*>( pBytes );
                if ( pDos->e_magic != IMAGE_DOS_SIGNATURE )
                    return nullptr;
                const IMAGE_NT_HEADERS* pNt = reinterpret_cast<const IMAGE_NT_HEADERS*>( pBytes + pDos->e_lfanew );
                return ( pNt->Signature == IMAGE_NT_SIGNATURE ) ? pNt : nullptr;
            }

            /** @brief 이름이 @p pDLLName 인 DLL 이 로드돼 있으면 프로세스 끝까지 언로드되지 않게 고정합니다. 고정했으면 true 를 반환합니다. */
            static bool pinLoadedModule( const utf8* pDLLName )
            {
                HMODULE       hModule  = nullptr;
                const wstring wideName = StringUtil::utf8ToUTF16( pDLLName );
                return GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_PIN, wideName.c_str(), &hModule ) != FALSE;
            }

            /** @brief `Bin` 과 `Bin/Modules` 를 DLL 사용자 검색 폴더에 한 번 더 추가합니다(`LOAD_LIBRARY_SEARCH_USER_DIRS`, 프로세스 전체에 남습니다). */
            static void addBinarySearchDirectories()
            {
                static const bool s_bAdded = []()
                {
                    const string binaryDir = FileUtil::toNativeSeparators( ProcessRoots::get()._binary.str() );
                    const string moduleDir = FileUtil::toNativeSeparators( ModuleLibraryUtil::getModuleDirectory() );
                    for ( const string& directory : { binaryDir, moduleDir } )
                    {
                        if ( FileUtil::isDirectory( directory ) )
                            (void)AddDllDirectory( StringUtil::utf8ToUTF16( directory.c_str() ).c_str() ); // 실패하면 그 폴더의 의존 DLL 로드가 실패로 알린다
                    }
                    return true;
                }();
                (void)s_bAdded;
            }
#endif
        };
    } // namespace

    string_view ModuleLibraryUtil::getSharedLibraryPrefix()
    {
#if defined( SW_PLATFORM_WINDOWS )
        return "";
#else
        return "lib";
#endif
    }

    string_view ModuleLibraryUtil::getSharedLibraryExtension()
    {
#if defined( SW_PLATFORM_WINDOWS )
        return ".dll";
#else
        return ".so";
#endif
    }

    string ModuleLibraryUtil::formatSharedLibraryName( string_view baseName )
    {
        StringBuilder<constant::kMaxBuffer128> sb;
        sb.append( getSharedLibraryPrefix() ).append( baseName ).append( getSharedLibraryExtension() );
        return string( sb.view() );
    }

    string ModuleLibraryUtil::getDebugSymbolPath( string_view libraryPath )
    {
#if defined( SW_PLATFORM_WINDOWS )
        return FileUtil::replaceExtension( libraryPath, ".pdb" );
#else
        return FileUtil::replaceExtension( libraryPath, ".debug" );
#endif
    }

    string ModuleLibraryUtil::findBuiltDebugSymbolPath( string_view libraryPath )
    {
        const string besideLibrary = getDebugSymbolPath( libraryPath );
        string_view  fileName;
        FileUtil::getFileNamePart( besideLibrary, fileName );
        string symbolDirectory = FileUtil::joinPath( ProcessRoots::get()._binary.str(), kSymbolFolder );
        // 옮긴 모듈(게임 팩)은 그 폴더에서 낸 라이브러리일 때만 `Symbols/<폴더>` 다 — 같은 이름의 `Bin/Symbols/SWGame.pdb` 는 활성 게임의 것이다.
        const ModuleSubdirectory& moved = s_moduleSubdirectory;
        if ( moved._arrDirectory[0] != '\0' )
        {
            const string movedDirectory = FileUtil::joinPath( ProcessRoots::get()._binary.str(), moved._arrDirectory );
            if ( FileUtil::pathsEqualNormalized( FileUtil::getDirectoryPart( libraryPath ), movedDirectory ) )
                symbolDirectory = FileUtil::joinPath( symbolDirectory, moved._arrDirectory );
        }
        string found = FileUtil::joinPath( symbolDirectory, fileName );
        if ( FileUtil::exists( found ) == false )
            found = FileUtil::exists( besideLibrary ) ? besideLibrary : string{};
        return found;
    }

    string ModuleLibraryUtil::getModuleDirectory()
    {
        return FileUtil::joinPath( ProcessRoots::get()._binary.str(), kModuleFolder );
    }

    void ModuleLibraryUtil::setModuleSubdirectory( string_view baseName, string_view binRelativeDirectory )
    {
        ModuleSubdirectory& entry = s_moduleSubdirectory;
        entry                     = ModuleSubdirectory{};
        if ( binRelativeDirectory.empty() )
            return;
        if ( baseName.size() >= std::size( entry._arrBaseName ) || binRelativeDirectory.size() >= std::size( entry._arrDirectory ) )
        {
            SW_LOG_ERROR( "Module subdirectory for %# is too long - the module stays in %#", string{ baseName }.c_str(), kModuleFolder );
            return;
        }
        StringUtil::strncpy( entry._arrBaseName, string{ baseName }.c_str(), static_cast<uint32>( std::size( entry._arrBaseName ) ) );
        StringUtil::strncpy( entry._arrDirectory, string{ binRelativeDirectory }.c_str(), static_cast<uint32>( std::size( entry._arrDirectory ) ) );
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

    string ModuleLibraryUtil::findModuleLibraryPath( string_view baseName )
    {
        const string fileName = formatSharedLibraryName( baseName );
        // 옮긴 모듈은 그 폴더에만 있다 — 없으면 그 경로를 돌려 부르는 쪽이 "없다" 를 알리게 한다(활성 게임의 `Bin/Modules` 사본으로 넘어가지 않는다).
        const string_view movedDirectory = ModuleLibraryUtilInternal::findModuleSubdirectory( baseName );
        if ( movedDirectory.empty() == false )
            return FileUtil::joinPath( FileUtil::joinPath( ProcessRoots::get()._binary.str(), movedDirectory ), fileName );
        string modulePath = FileUtil::joinPath( getModuleDirectory(), fileName );
        if ( FileUtil::exists( modulePath ) == false )
        {
            const string binaryPath = FileUtil::joinPath( ProcessRoots::get()._binary.str(), fileName );
            if ( FileUtil::exists( binaryPath ) )
                modulePath = binaryPath;
        }
        return modulePath;
    }

    void* ModuleLibraryUtil::loadLibraryAt( const AbsolutePath& libraryPath )
    {
#if defined( SW_PLATFORM_WINDOWS )
        // `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR` 은 경로의 폴더를 `\` 로 자른다 - `/` 경로면 그 폴더를 못 찾아 의존 DLL 이 ERROR_MOD_NOT_FOUND(126) 가 된다.
        // 경로는 UTF-8 이라 W 판에 넘긴다(A 판은 ANSI 코드 페이지로 읽어 한글 경로가 깨진다).
        ModuleLibraryUtilInternal::addBinarySearchDirectories();
        const wstring widePath = StringUtil::utf8ToUTF16( FileUtil::toNativeSeparators( libraryPath.str() ).c_str() );
        const HMODULE hModule  = LoadLibraryExW( widePath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS );
        if ( hModule == nullptr )
            SW_LOG_WARNING( "LoadLibrary failed for %# (Win32 error %#)", libraryPath.c_str(), static_cast<uint32>( GetLastError() ) ); // 126 의존 DLL 없음, 1114 DllMain 실패 등
        return hModule;
#else
        void* const pHandle = dlopen( libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL );
        if ( pHandle == nullptr )
        {
            const utf8* pReason = dlerror(); // 실패 이유는 이것 하나뿐이다
            SW_LOG_WARNING( "dlopen failed for %#: %#", libraryPath.c_str(), ( pReason != nullptr ) ? pReason : "(no reason)" );
        }
        return pHandle;
#endif
    }

    void* ModuleLibraryUtil::loadSystemLibrary( string_view libraryName )
    {
        if ( libraryName.empty() )
            return nullptr;
        const string name{ libraryName };
#if defined( SW_PLATFORM_WINDOWS )
        return LoadLibraryExW( StringUtil::utf8ToUTF16( name.c_str() ).c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32 );
#else
        return dlopen( name.c_str(), RTLD_NOW | RTLD_LOCAL );
#endif
    }

    void* ModuleLibraryUtil::findLoadedLibrary( string_view libraryName )
    {
        if ( libraryName.empty() )
            return nullptr;
        const string name{ libraryName };
#if defined( SW_PLATFORM_WINDOWS )
        HMODULE hModule = nullptr;
        if ( GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_PIN, StringUtil::utf8ToUTF16( name.c_str() ).c_str(), &hModule ) == FALSE )
            return nullptr;
        return hModule;
#else
        return dlopen( name.c_str(), RTLD_NOW | RTLD_NOLOAD | RTLD_NODELETE );
#endif
    }

    void* ModuleLibraryUtil::getDynamicSymbol( void* pHandle, string_view symbolName )
    {
        if ( pHandle == nullptr || symbolName.empty() )
            return nullptr;

        const string symbolNameNt( symbolName );
#if defined( SW_PLATFORM_WINDOWS )
        return reinterpret_cast<void*>( GetProcAddress( static_cast<HMODULE>( pHandle ), symbolNameNt.c_str() ) );
#else
        return dlsym( pHandle, symbolNameNt.c_str() );
#endif
    }

    void ModuleLibraryUtil::unloadDynamicLibrary( void* pHandle )
    {
        if ( pHandle == nullptr )
            return;

#if defined( SW_PLATFORM_WINDOWS )
        FreeLibrary( static_cast<HMODULE>( pHandle ) );
#else
        dlclose( pHandle );
#endif
    }

    uint32 ModuleLibraryUtil::pinDynamicLibraryDependencies( void* pHandle )
    {
        if ( pHandle == nullptr )
            return 0;

        uint32 pinnedCount{ 0 };
#if defined( SW_PLATFORM_WINDOWS )
        // Windows 의 모듈 핸들은 라이브러리의 기준 주소다.
        const uint8*            pBase = static_cast<const uint8*>( pHandle );
        const IMAGE_NT_HEADERS* pNt   = ModuleLibraryUtilInternal::findNtHeaders( pHandle );
        if ( pNt == nullptr )
            return 0;

        const IMAGE_DATA_DIRECTORY& importDir = pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if ( importDir.VirtualAddress != 0 )
        {
            for ( const IMAGE_IMPORT_DESCRIPTOR* pDesc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>( pBase + importDir.VirtualAddress ); pDesc->Name != 0; ++pDesc )
            {
                if ( ModuleLibraryUtilInternal::pinLoadedModule( reinterpret_cast<const utf8*>( pBase + pDesc->Name ) ) )
                    ++pinnedCount;
            }
        }

#elif defined( SW_PLATFORM_LINUX )
        struct link_map* pLinkMap            = nullptr;
        const bool       bLinkMapUnavailable = dlinfo( pHandle, RTLD_DI_LINKMAP, &pLinkMap ) != 0 || pLinkMap == nullptr || pLinkMap->l_ld == nullptr;
        if ( bLinkMapUnavailable )
            return 0;

        uintptr_t stringTable{ 0 };
        for ( const ElfW( Dyn )* pEntry = pLinkMap->l_ld; pEntry->d_tag != DT_NULL; ++pEntry )
        {
            if ( pEntry->d_tag == DT_STRTAB )
                stringTable = static_cast<uintptr_t>( pEntry->d_un.d_ptr );
        }
        if ( stringTable == 0 )
            return 0;
        // glibc 는 동적 섹션의 주소 필드를 적재 주소로 고쳐 두지만, 동적 섹션이 읽기 전용이면 파일 기준 값 그대로다.
        if ( stringTable < static_cast<uintptr_t>( pLinkMap->l_addr ) )
            stringTable += static_cast<uintptr_t>( pLinkMap->l_addr );

        for ( const ElfW( Dyn )* pEntry = pLinkMap->l_ld; pEntry->d_tag != DT_NULL; ++pEntry )
        {
            if ( pEntry->d_tag != DT_NEEDED )
                continue;
            // NOLOAD 는 이미 로드된 것만 찾는다(SONAME 으로 맞춘다). NODELETE 는 그 라이브러리를 프로세스 끝까지 남긴다. 찾느라 늘린 참조는 바로 돌려준다.
            const utf8* pName       = reinterpret_cast<const utf8*>( stringTable + static_cast<uintptr_t>( pEntry->d_un.d_val ) );
            void*       pDependency = dlopen( pName, RTLD_LAZY | RTLD_NOLOAD | RTLD_NODELETE );
            if ( pDependency == nullptr )
                continue;
            dlclose( pDependency );
            ++pinnedCount;
        }
#endif
        return pinnedCount;
    }

#if SW_WITH_DYNAMIC_MODULES
    void* ModuleLibraryUtil::findBoundImportLibrary( void* pHandle, string_view dependencyFileName )
    {
    #if defined( SW_PLATFORM_WINDOWS )
        if ( pHandle == nullptr )
            return nullptr;
        const IMAGE_NT_HEADERS* pNt = ModuleLibraryUtilInternal::findNtHeaders( pHandle );
        if ( pNt == nullptr )
            return nullptr;
        const uint8* pBase = static_cast<const uint8*>( pHandle );

        const IMAGE_DATA_DIRECTORY& importDir = pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if ( importDir.VirtualAddress != 0 )
        {
            const IMAGE_IMPORT_DESCRIPTOR* pDesc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>( pBase + importDir.VirtualAddress );
            for ( ; pDesc->Name != 0; ++pDesc )
            {
                const utf8* pName = reinterpret_cast<const utf8*>( pBase + pDesc->Name );
                if ( StringUtil::equals( string_view{ pName }, dependencyFileName, true ) == false )
                    continue;
                const IMAGE_THUNK_DATA* pThunk = reinterpret_cast<const IMAGE_THUNK_DATA*>( pBase + pDesc->FirstThunk );
                if ( pThunk->u1.Function == 0 )
                    return nullptr;
                HMODULE hBound = nullptr;
                GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    reinterpret_cast<LPCWSTR>( pThunk->u1.Function ), &hBound );
                return hBound;
            }
        }
        return nullptr;
    #else
        (void)pHandle; // 리눅스는 import 테이블에서 바인딩을 읽을 수 없다. 부르는 쪽이 심볼로 판단한다.
        (void)dependencyFileName;
        return nullptr;
    #endif
    }

    bool ModuleLibraryUtil::unloadModuleLibrary( string_view moduleName, void* pHandle )
    {
        if ( pHandle == nullptr )
            return false;
        (void)moduleName; // 이름은 로그 자리 — 등록은 내리기 전에 모듈 경로가 뗐다(`engine::unregisterModuleTypes`)

        // 이 라이브러리가 끌어온 의존 라이브러리가 함께 언로드되지 않게 한다. 그 라이브러리를 가리키는 등록은 여기서 뗄 수 없다.
        (void)pinDynamicLibraryDependencies( pHandle );
        unloadDynamicLibrary( pHandle );
        return true;
    }

#endif // SW_WITH_DYNAMIC_MODULES

} // namespace sw
