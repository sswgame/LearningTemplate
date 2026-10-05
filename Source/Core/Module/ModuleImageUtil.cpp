#include "pch.h"

#include "Core/Module/ModuleImageUtil.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Module/ModuleUnloadListener.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#if defined( SW_PLATFORM_LINUX )
    #include <link.h>
#endif

SW_LOG_CALLER( "ModuleImageUtil" );
namespace sw
{
    namespace
    {
        struct ModuleImageUtilInternal
        {
#if defined( SW_PLATFORM_LINUX )
            /** @brief `dl_iterate_phdr` 가 이미지마다 부르는 곳에 넘기는 문맥입니다. */
            struct ImageRangeQuery
            {
                uintptr_t _address{ 0 };
                uintptr_t _begin{ 0 };
                uintptr_t _end{ 0 };
                bool      _bFound{ false };
            };

            /** @brief 이미지 하나의 PT_LOAD 범위를 모아 @p pContext 의 주소를 담는지 봅니다. 담으면 1 을 반환해 순회를 멈춥니다. */
            static int32 collectImageRange( struct dl_phdr_info* pInfo, size_t, void* pContext )
            {
                ImageRangeQuery* pQuery = static_cast<ImageRangeQuery*>( pContext );
                uintptr_t        begin  = UINTPTR_MAX;
                uintptr_t        end    = 0;
                for ( uint16 headerIndex = 0; headerIndex < pInfo->dlpi_phnum; ++headerIndex )
                {
                    const ElfW( Phdr ) & header = pInfo->dlpi_phdr[headerIndex];
                    if ( header.p_type != PT_LOAD )
                        continue;
                    const uintptr_t segmentBegin = pInfo->dlpi_addr + header.p_vaddr;
                    const uintptr_t segmentEnd   = segmentBegin + header.p_memsz;
                    begin                        = MathUtil::min( begin, segmentBegin );
                    end                          = MathUtil::max( end, segmentEnd );
                }
                if ( begin <= pQuery->_address && pQuery->_address < end )
                {
                    pQuery->_begin  = begin;
                    pQuery->_end    = end;
                    pQuery->_bFound = true;
                    return 1;
                }
                return 0;
            }
#elif defined( SW_PLATFORM_WINDOWS )
            /** @brief 이미지 기준 주소 @p pBase 의 PE NT 머리입니다. DOS · NT 서명이 맞지 않으면 nullptr 입니다. */
            static const IMAGE_NT_HEADERS* findNtHeaders( const void* pBase )
            {
                const uint8*            pBytes = static_cast<const uint8*>( pBase );
                const IMAGE_DOS_HEADER* pDos   = reinterpret_cast<const IMAGE_DOS_HEADER*>( pBytes );
                if ( pDos->e_magic != IMAGE_DOS_SIGNATURE )
                    return nullptr;
                const IMAGE_NT_HEADERS* pNt = reinterpret_cast<const IMAGE_NT_HEADERS*>( pBytes + pDos->e_lfanew );
                return ( pNt->Signature == IMAGE_NT_SIGNATURE ) ? pNt : nullptr;
            }

            /** @brief 이름이 @p pDllName 인 DLL 이 올라와 있으면 프로세스 끝까지 내려가지 않게 고정합니다. 고정했으면 true 입니다. */
            static bool pinLoadedModule( const utf8* pDllName )
            {
                HMODULE hModule = nullptr;
                return GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_PIN, pDllName, &hModule ) != FALSE;
            }

            /** @brief `EnumerateLoadedModulesW64` 가 이미지마다 부르는 곳 — 기준 주소(= 모듈 핸들)를 @p pContext 의 목록에 모읍니다. */
            static BOOL CALLBACK collectLoadedModule( PCWSTR, DWORD64 moduleBase, ULONG, PVOID pContext )
            {
                static_cast<vector<void*>*>( pContext )->push_back( reinterpret_cast<void*>( static_cast<uintptr_t>( moduleBase ) ) );
                return TRUE;
            }
#endif
        };
    } // namespace

    string_view ModuleImageUtil::getSharedLibraryPrefix()
    {
#if defined( SW_PLATFORM_WINDOWS )
        return "";
#else
        return "lib";
#endif
    }

    string_view ModuleImageUtil::getSharedLibraryExtension()
    {
#if defined( SW_PLATFORM_WINDOWS )
        return ".dll";
#else
        return ".so";
#endif
    }

    string ModuleImageUtil::formatSharedLibraryName( string_view baseName )
    {
        StringBuilder<constant::kMaxBuffer128> sb;
        sb.append( getSharedLibraryPrefix() ).append( baseName ).append( getSharedLibraryExtension() );
        return string( sb.view() );
    }

    string ModuleImageUtil::getDebugSymbolPath( string_view libraryPath )
    {
#if defined( SW_PLATFORM_WINDOWS )
        return FileUtil::replaceExtension( libraryPath, ".pdb" );
#else
        return FileUtil::replaceExtension( libraryPath, ".debug" );
#endif
    }

    void* ModuleImageUtil::loadDynamicLibrary( string_view libraryName )
    {
        if ( libraryName.empty() )
            return nullptr;

#if defined( SW_PLATFORM_WINDOWS )
        string absPath;
        if ( FileUtil::makeAbsolutePath( libraryName, absPath ) && FileUtil::fileExists( absPath ) )
        {
            // Windows 커널 로더(LOAD_WITH_ALTERED_SEARCH_PATH)는 '\'(백슬래시)를 기준으로 디렉터리를 잘라 DLL 검색 경로의
            // 첫 순위로 넣는다. '/' 경로를 넘기면 디렉터리 해석에 실패해 의존 DLL(Engine.dll 등)을 찾지 못하고
            // ERROR_MOD_NOT_FOUND(126) 오류가 나므로, 네이티브 구분자('\')로 바꾼다.
            const string nativePath = FileUtil::toNativeSeparators( absPath );

            const string dir = FileUtil::getDirectoryPart( nativePath );
            utf8         arrPreviousDllDir[constant::kMaxPathSize]{};
            const DWORD  previousDllDirLen = GetDllDirectoryA( static_cast<DWORD>( sizeof( arrPreviousDllDir ) ), arrPreviousDllDir );
            if ( dir.empty() == false )
                SetDllDirectoryA( dir.c_str() );

            // 1) LOAD_WITH_ALTERED_SEARCH_PATH 로 대상 DLL 의 위치를 가장 먼저 검색해 로드한다
            HMODULE hModule = LoadLibraryExA( nativePath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH );
            // 2) LOAD_WITH_ALTERED_SEARCH_PATH 를 쓰면 SetDllDirectory 가 무시되는 Win32 제약이 있어, 실패하면 LoadLibraryA 로 다시 시도한다
            if ( hModule == nullptr )
                hModule = LoadLibraryA( nativePath.c_str() );

            if ( previousDllDirLen > 0 )
                SetDllDirectoryA( arrPreviousDllDir );
            else
                SetDllDirectoryA( nullptr );

            if ( hModule != nullptr )
                return hModule;
        }
        const string  nativeName = FileUtil::toNativeSeparators( libraryName );
        const HMODULE hModule    = LoadLibraryA( nativeName.c_str() );
        if ( hModule == nullptr )
        {
            // 실패 이유는 GetLastError 하나뿐이다 — 남기지 않으면 부르는 쪽은 "로드 실패" 밖에 모른다(126 의존 DLL 없음, 1114 DllMain 실패 등).
            const DWORD errorCode = GetLastError();
            SW_LOG_WARNING( "LoadLibrary failed for %# (Win32 error %#)", nativeName.c_str(), static_cast<uint32>( errorCode ) );
        }
        return hModule;
#else
        string absPath;
        if ( FileUtil::makeAbsolutePath( libraryName, absPath ) && FileUtil::fileExists( absPath ) )
        {
            void* pHandle = dlopen( absPath.c_str(), RTLD_NOW | RTLD_LOCAL );
            if ( pHandle != nullptr )
                return pHandle;
        }
        const string libraryNameNt( libraryName );
        void*        pHandle = dlopen( libraryNameNt.c_str(), RTLD_NOW | RTLD_LOCAL );
        if ( pHandle == nullptr )
        {
            const utf8* pReason = dlerror(); // 실패 이유는 이것 하나뿐이다
            SW_LOG_WARNING( "dlopen failed for %#: %#", libraryNameNt.c_str(), ( pReason != nullptr ) ? pReason : "(no reason)" );
        }
        return pHandle;
#endif
    }

    void* ModuleImageUtil::getDynamicSymbol( void* pHandle, string_view symbolName )
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

    uint32 ModuleImageUtil::bindDelayLoadImports( void* pHandle )
    {
        using BindDelayLoadImportsFunction         = uint32 ( * )();
        const BindDelayLoadImportsFunction pfnBind = reinterpret_cast<BindDelayLoadImportsFunction>( getDynamicSymbol( pHandle, kBindDelayLoadImportsSymbol ) );
        if ( pfnBind == nullptr )
            return 0;
        const uint32 failedCount = pfnBind();
        if ( failedCount > 0 )
            SW_LOG_WARNING( "A module could not bind %# delay-loaded DLL(s) up front - those imports bind on their first call", failedCount );
        return failedCount;
    }

    uint32 ModuleImageUtil::bindDelayLoadImportsOfLoadedModules()
    {
        vector<void*> listHandle;
        collectLoadedModuleHandles( listHandle );
        uint32 failedCount{ 0 };
        for ( void* pHandle : listHandle )
            failedCount += bindDelayLoadImports( pHandle );
        return failedCount;
    }

    void ModuleImageUtil::collectLoadedModuleHandles( vector<void*>& outListHandle )
    {
        outListHandle.clear();
#if defined( SW_PLATFORM_WINDOWS )
        (void)EnumerateLoadedModulesW64( GetCurrentProcess(), &ModuleImageUtilInternal::collectLoadedModule, &outListHandle ); // 실패하면 빈 목록
#endif
    }

    bool ModuleImageUtil::findLoadedImageRange( const void* pAddressInside, const void*& pOutBegin, const void*& pOutEnd )
    {
        pOutBegin = nullptr;
        pOutEnd   = nullptr;
        if ( pAddressInside == nullptr )
            return false;
#if defined( SW_PLATFORM_WINDOWS )
        HMODULE hModule = nullptr;
        if ( GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                 static_cast<LPCWSTR>( pAddressInside ), &hModule ) == FALSE )
            return false;
        const IMAGE_NT_HEADERS* pNt = ModuleImageUtilInternal::findNtHeaders( hModule );
        if ( pNt == nullptr )
            return false;
        pOutBegin = hModule;
        pOutEnd   = reinterpret_cast<const uint8*>( hModule ) + pNt->OptionalHeader.SizeOfImage;
        return true;
#elif defined( SW_PLATFORM_LINUX )
        ModuleImageUtilInternal::ImageRangeQuery query{};
        query._address = reinterpret_cast<uintptr_t>( pAddressInside );
        dl_iterate_phdr( &ModuleImageUtilInternal::collectImageRange, &query );
        if ( query._bFound == false )
            return false;
        pOutBegin = reinterpret_cast<const void*>( query._begin );
        pOutEnd   = reinterpret_cast<const void*>( query._end );
        return true;
#else
        return false;
#endif
    }

    bool ModuleImageUtil::findDynamicLibraryRange( void* pHandle, const void*& pOutBegin, const void*& pOutEnd )
    {
        pOutBegin = nullptr;
        pOutEnd   = nullptr;
        if ( pHandle == nullptr )
            return false;
#if defined( SW_PLATFORM_WINDOWS )
        // Windows 의 모듈 핸들은 이미지의 기준 주소다.
        return findLoadedImageRange( pHandle, pOutBegin, pOutEnd );
#elif defined( SW_PLATFORM_LINUX )
        // 리눅스 핸들은 주소가 아니다. 링크 맵이 가리키는 동적 섹션은 이미지 안에 있다.
        struct link_map* pLinkMap = nullptr;
        if ( dlinfo( pHandle, RTLD_DI_LINKMAP, &pLinkMap ) != 0 || pLinkMap == nullptr )
            return false;
        return findLoadedImageRange( pLinkMap->l_ld, pOutBegin, pOutEnd );
#else
        return false;
#endif
    }

    void ModuleImageUtil::unloadDynamicLibrary( void* pHandle )
    {
        if ( pHandle == nullptr )
            return;

#if defined( SW_PLATFORM_WINDOWS )
        FreeLibrary( static_cast<HMODULE>( pHandle ) );
#else
        dlclose( pHandle );
#endif
    }

    uint32 ModuleImageUtil::pinDynamicLibraryDependencies( void* pHandle )
    {
        if ( pHandle == nullptr )
            return 0;

        uint32 pinnedCount{ 0 };
#if defined( SW_PLATFORM_WINDOWS )
        // Windows 의 모듈 핸들은 이미지의 기준 주소다.
        const uint8*            pBase = static_cast<const uint8*>( pHandle );
        const IMAGE_NT_HEADERS* pNt   = ModuleImageUtilInternal::findNtHeaders( pHandle );
        if ( pNt == nullptr )
            return 0;

        const IMAGE_DATA_DIRECTORY& importDir = pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if ( importDir.VirtualAddress != 0 )
        {
            for ( const IMAGE_IMPORT_DESCRIPTOR* pDesc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>( pBase + importDir.VirtualAddress ); pDesc->Name != 0; ++pDesc )
            {
                if ( ModuleImageUtilInternal::pinLoadedModule( reinterpret_cast<const utf8*>( pBase + pDesc->Name ) ) )
                    ++pinnedCount;
            }
        }

        const IMAGE_DATA_DIRECTORY& delayDir = pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
        if ( delayDir.VirtualAddress != 0 )
        {
            for ( const IMAGE_DELAYLOAD_DESCRIPTOR* pDesc = reinterpret_cast<const IMAGE_DELAYLOAD_DESCRIPTOR*>( pBase + delayDir.VirtualAddress ); pDesc->DllNameRVA != 0;
                  ++pDesc )
            {
                if ( pDesc->Attributes.RvaBased == 0 )
                    continue;
                if ( ModuleImageUtilInternal::pinLoadedModule( reinterpret_cast<const utf8*>( pBase + pDesc->DllNameRVA ) ) )
                    ++pinnedCount;
            }
        }
#elif defined( SW_PLATFORM_LINUX )
        struct link_map* pLinkMap = nullptr;
        if ( dlinfo( pHandle, RTLD_DI_LINKMAP, &pLinkMap ) != 0 || pLinkMap == nullptr || pLinkMap->l_ld == nullptr )
            return 0;

        uintptr_t stringTable{ 0 };
        for ( const ElfW( Dyn )* pEntry = pLinkMap->l_ld; pEntry->d_tag != DT_NULL; ++pEntry )
        {
            if ( pEntry->d_tag == DT_STRTAB )
                stringTable = static_cast<uintptr_t>( pEntry->d_un.d_ptr );
        }
        if ( stringTable == 0 )
            return 0;
        // glibc 는 동적 섹션의 주소 칸을 적재 주소로 고쳐 두지만, 동적 섹션이 읽기 전용이면 파일 기준 값 그대로다.
        if ( stringTable < static_cast<uintptr_t>( pLinkMap->l_addr ) )
            stringTable += static_cast<uintptr_t>( pLinkMap->l_addr );

        for ( const ElfW( Dyn )* pEntry = pLinkMap->l_ld; pEntry->d_tag != DT_NULL; ++pEntry )
        {
            if ( pEntry->d_tag != DT_NEEDED )
                continue;
            // NOLOAD 는 이미 올라온 것만 찾는다(SONAME 으로 맞춘다). NODELETE 는 그 이미지를 프로세스 끝까지 남긴다 — 찾느라 올린 참조는 바로 돌려준다.
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

    uint32 ModuleImageUtil::releaseModuleCode( string_view moduleName, const void* pBegin, const void* pEnd, bool* pOutKeepImageMapped )
    {
        if ( pOutKeepImageMapped != nullptr )
            *pOutKeepImageMapped = false;

        // 모듈은 자기가 단 것을 스스로 떼야 한다(에디터는 ImGuiEditor::shutdown · ~ConsolePanel 에서 뗀다). 여기서 뗀 것이 있으면
        // 그 정리가 빠졌다는 뜻이라 경고로 남긴다. 늘 0 이어야 한다.
        vector<IModuleUnloadListener::ReleaseResult> listResult;
        IModuleUnloadListener::releaseAllWithin( pBegin, pEnd, listResult );

        uint32 releasedCount{ 0 };
        for ( const IModuleUnloadListener::ReleaseResult& result : listResult )
        {
            releasedCount += result._releasedCount;
            if ( result._releasedCount > 0 )
                SW_LOG_WARNING( "Module %# left %# %# behind — released them before unloading its image", moduleName, result._releasedCount, result._pListenerName );
            // 떼어 낼 수 없는 것(다른 코드가 아직 구독하는 이 이미지의 이벤트 채널)이 남았다. 내리면 다음 발행 · 종료 때 내려간 코드로 뛴다.
            if ( result._bKeepImageMapped )
            {
                SW_LOG_WARNING( "Module %# is still referenced by %# that other code uses — keeping its image mapped until exit", moduleName, result._pListenerName );
                if ( pOutKeepImageMapped != nullptr )
                    *pOutKeepImageMapped = true;
            }
        }
        return releasedCount;
    }

    void* ModuleImageUtil::findBoundImportImage( void* pHandle, string_view dependencyFileName )
    {
#if defined( SW_PLATFORM_WINDOWS )
        if ( pHandle == nullptr )
            return nullptr;
        const IMAGE_NT_HEADERS* pNt = ModuleImageUtilInternal::findNtHeaders( pHandle );
        if ( pNt == nullptr )
            return nullptr;
        const uint8* pBase = static_cast<const uint8*>( pHandle );

        const IMAGE_DATA_DIRECTORY& delayDir = pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
        if ( delayDir.VirtualAddress != 0 )
        {
            const IMAGE_DELAYLOAD_DESCRIPTOR* pDesc = reinterpret_cast<const IMAGE_DELAYLOAD_DESCRIPTOR*>( pBase + delayDir.VirtualAddress );
            for ( ; pDesc->DllNameRVA != 0; ++pDesc )
            {
                if ( pDesc->Attributes.RvaBased == 0 )
                    continue;
                const utf8* pName = reinterpret_cast<const utf8*>( pBase + pDesc->DllNameRVA );
                if ( StringUtil::equals( string_view{ pName }, dependencyFileName, true ) )
                    return *reinterpret_cast<void* const*>( pBase + pDesc->ModuleHandleRVA );
            }
        }

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
        (void)pHandle; // 리눅스는 import 표에서 결속을 읽을 수 없다 — 부르는 쪽이 심볼로 가린다
        (void)dependencyFileName;
        return nullptr;
#endif
    }

    bool ModuleImageUtil::releaseImageCode( string_view moduleName, void* pHandle )
    {
        const void* pBegin{ nullptr };
        const void* pEnd{ nullptr };
        if ( pHandle == nullptr || findDynamicLibraryRange( pHandle, pBegin, pEnd ) == false )
            return true;
        bool bKeepImageMapped{ false };
        (void)releaseModuleCode( moduleName, pBegin, pEnd, &bKeepImageMapped ); // 뗀 것은 releaseModuleCode 가 경고로 남긴다
        return bKeepImageMapped == false;
    }

    bool ModuleImageUtil::unloadModuleImage( string_view moduleName, void* pHandle )
    {
        if ( pHandle == nullptr )
            return false;
        if ( releaseImageCode( moduleName, pHandle ) == false )
            return false;

        // 이 이미지가 끌어온 의존 이미지가 함께 내려가지 않게 한다. 그 이미지의 코드를 쥔 등록은 여기서 뗄 수 없다.
        (void)pinDynamicLibraryDependencies( pHandle );
        unloadDynamicLibrary( pHandle );
        return true;
    }
} // namespace sw
