#include "pch.h"

#include "Core/Module/DynamicLibrary.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/PlatformOSHeaders.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/Module/ModuleLibraryUtil.h"
#include "Core/String/StringUtil.h"

#if defined( SW_PLATFORM_LINUX )
    #include <link.h>
#endif

namespace sw
{
    namespace
    {
        struct DynamicLibraryInternal
        {
#if defined( SW_PLATFORM_WINDOWS )
            /** @brief `Bin` 과 `Bin/Modules` 를 DLL 사용자 검색 폴더에 한 번 더 추가합니다(`LOAD_LIBRARY_SEARCH_USER_DIRS`, 프로세스 전체에 남습니다). */
            static void addBinarySearchDirectories()
            {
                static const bool s_bAdded = []()
                {
                    for ( const AbsolutePath& directory : { ProcessRoots::get()._binary, ModuleLibraryUtil::getModuleDirectory() } )
                    {
                        if ( FileUtil::isDirectory( directory ) )
                            (void)AddDllDirectory( StringUtil::utf8ToUTF16( FileUtil::toNativeSeparators( directory.str() ).c_str() ).c_str() ); // 실패하면 그 폴더의 의존 DLL 로드가 실패로 알린다
                    }
                    return true;
                }();
                (void)s_bAdded;
            }
#endif

#if SW_WITH_DYNAMIC_MODULES
    #if defined( SW_PLATFORM_WINDOWS )
            /** @brief 이름이 @p pDLLName 인 DLL 이 로드돼 있으면 프로세스 끝까지 언로드되지 않게 고정합니다. 고정했으면 true 를 반환합니다. */
            static bool pinLoadedModule( const utf8* pDLLName )
            {
                HMODULE       hModule  = nullptr;
                const wstring wideName = StringUtil::utf8ToUTF16( pDLLName );
                return GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_PIN, wideName.c_str(), &hModule ) != FALSE;
            }
    #endif

            /** @brief 라이브러리 @p pHandle 이 import 하는 라이브러리 중 지금 로드된 것을 프로세스 끝까지 언로드되지 않게 고정하고, 고정한 수를 반환합니다. 아직 로드되지 않은 것은 로드하지 않는다. */
            static uint32 pinDependencies( void* pHandle )
            {
                uint32 pinnedCount{ 0 };
    #if defined( SW_PLATFORM_WINDOWS )
                // Windows 의 모듈 핸들은 라이브러리의 기준 주소다.
                const uint8*            pBase = static_cast<const uint8*>( pHandle );
                const IMAGE_NT_HEADERS* pNt   = static_cast<const IMAGE_NT_HEADERS*>( ModuleLibraryUtil::findImageNtHeaders( pHandle ) );
                if ( pNt == nullptr )
                    return 0;

                const IMAGE_DATA_DIRECTORY& importDir = pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
                if ( importDir.VirtualAddress != 0 )
                {
                    for ( const IMAGE_IMPORT_DESCRIPTOR* pDesc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>( pBase + importDir.VirtualAddress ); pDesc->Name != 0; ++pDesc )
                    {
                        if ( pinLoadedModule( reinterpret_cast<const utf8*>( pBase + pDesc->Name ) ) )
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
    #elif defined( SW_PLATFORM_ANDROID ) || defined( SW_PLATFORM_WEB )
                // 배포 모듈은 정적으로 묶인다. 고정할 의존 라이브러리가 없다(bionic 에는 dlinfo 도 없다).
                (void)pHandle;
    #else
        #error "Unsupported platform"
    #endif
                return pinnedCount;
            }
#endif // SW_WITH_DYNAMIC_MODULES
        };
    } // namespace

    void* DynamicLibraryView::findSymbol( string_view symbolName ) const
    {
        if ( _pOSHandle == nullptr || symbolName.empty() )
            return nullptr;
        const string symbolNameNt( symbolName );
#if defined( SW_PLATFORM_WINDOWS )
        return reinterpret_cast<void*>( GetProcAddress( static_cast<HMODULE>( _pOSHandle ), symbolNameNt.c_str() ) );
#elif defined( SW_PLATFORM_POSIX )
        return dlsym( _pOSHandle, symbolNameNt.c_str() );
#else
    #error "Unsupported platform"
#endif
    }

    DynamicLibraryView DynamicLibraryView::findLoadedAndPin( string_view libraryName )
    {
        if ( libraryName.empty() )
            return {};
        const string name{ libraryName };
#if defined( SW_PLATFORM_WINDOWS )
        HMODULE hModule = nullptr;
        if ( GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_PIN, StringUtil::utf8ToUTF16( name.c_str() ).c_str(), &hModule ) == FALSE )
            return {};
        return DynamicLibraryView{ hModule };
#elif defined( SW_PLATFORM_POSIX )
        return DynamicLibraryView{ dlopen( name.c_str(), RTLD_NOW | RTLD_NOLOAD | RTLD_NODELETE ) }; // 고정한 참조는 놓지 않는다
#else
    #error "Unsupported platform"
#endif
    }

    DynamicLibrary DynamicLibrary::loadAt( const AbsolutePath& libraryPath )
    {
#if defined( SW_PLATFORM_WINDOWS )
        // `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR` 은 경로의 폴더를 `\` 로 자른다 - `/` 경로면 그 폴더를 못 찾아 의존 DLL 이 ERROR_MOD_NOT_FOUND(126) 가 된다.
        // 경로는 UTF-8 이라 W 판에 넘긴다(A 판은 ANSI 코드 페이지로 읽어 한글 경로가 깨진다).
        DynamicLibraryInternal::addBinarySearchDirectories();
        const wstring widePath = StringUtil::utf8ToUTF16( FileUtil::toNativeSeparators( libraryPath.str() ).c_str() );
        const HMODULE hModule  = LoadLibraryExW( widePath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS );
        if ( hModule == nullptr )
            SW_LOG_WARNING_ID( "ModuleLibraryUtil.LoadLibraryFailedWin32Error", "LoadLibrary failed for %# (Win32 error %#)", libraryPath.c_str(), static_cast<uint32>( GetLastError() ) ); // 126 의존 DLL 없음, 1114 DllMain 실패 등
        return DynamicLibrary{ hModule };
#elif defined( SW_PLATFORM_POSIX )
        void* const pHandle = dlopen( libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL );
        if ( pHandle == nullptr )
        {
            const utf8* pReason = dlerror(); // 실패 이유는 이것 하나뿐이다
            SW_LOG_WARNING_ID( "ModuleLibraryUtil.DlopenFailed", "dlopen failed for %#: %#", libraryPath.c_str(), ( pReason != nullptr ) ? pReason : "(no reason)" );
        }
        return DynamicLibrary{ pHandle };
#else
    #error "Unsupported platform"
#endif
    }

    DynamicLibrary DynamicLibrary::loadSystem( string_view libraryName )
    {
        if ( libraryName.empty() )
            return {};
        const string name{ libraryName };
#if defined( SW_PLATFORM_WINDOWS )
        return DynamicLibrary{ LoadLibraryExW( StringUtil::utf8ToUTF16( name.c_str() ).c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32 ) };
#elif defined( SW_PLATFORM_POSIX )
        return DynamicLibrary{ dlopen( name.c_str(), RTLD_NOW | RTLD_LOCAL ) };
#else
    #error "Unsupported platform"
#endif
    }

    void DynamicLibrary::reset() noexcept
    {
        if ( _pOSHandle == nullptr )
            return;
#if defined( SW_PLATFORM_WINDOWS )
        FreeLibrary( static_cast<HMODULE>( _pOSHandle ) );
#elif defined( SW_PLATFORM_POSIX )
        dlclose( _pOSHandle );
#else
    #error "Unsupported platform"
#endif
        _pOSHandle = nullptr;
    }

#if SW_WITH_DYNAMIC_MODULES
    bool DynamicLibrary::unloadModule()
    {
        if ( _pOSHandle == nullptr )
            return false;
        // 이 라이브러리가 끌어온 의존 라이브러리가 함께 언로드되지 않게 한다. 그 라이브러리를 가리키는 등록은 여기서 뗄 수 없다.
        (void)DynamicLibraryInternal::pinDependencies( _pOSHandle );
        reset();
        return true;
    }
#endif // SW_WITH_DYNAMIC_MODULES
} // namespace sw
