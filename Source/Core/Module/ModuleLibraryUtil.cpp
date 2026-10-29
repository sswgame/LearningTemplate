#include "pch.h"

#include "Core/Module/ModuleLibraryUtil.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
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

#if defined( SW_PLATFORM_LINUX )
            /** @brief `dl_iterate_phdr` 가 라이브러리마다 부르는 곳에 넘기는 문맥입니다. */
            struct LibraryRangeQuery
            {
                uintptr_t _address{ 0 };
                uintptr_t _begin{ 0 };
                uintptr_t _end{ 0 };
                bool      _bFound{ false };
            };

            /** @brief 라이브러리 하나의 PT_LOAD 범위를 모아 @p pContext 의 주소를 담는지 봅니다. 담으면 1 을 반환해 순회를 멈춥니다. */
            static int32 collectLibraryRange( struct dl_phdr_info* pInfo, size_t, void* pContext )
            {
                LibraryRangeQuery* pQuery = static_cast<LibraryRangeQuery*>( pContext );
                uintptr_t          begin  = UINTPTR_MAX;
                uintptr_t          end    = 0;
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
                    const string binaryDir = FileUtil::toNativeSeparators( FileUtil::getBinaryDirectory() );
                    const string moduleDir = FileUtil::toNativeSeparators( ModuleLibraryUtil::getModuleDirectory() );
                    for ( const string& directory : { binaryDir, moduleDir } )
                    {
                        if ( FileUtil::isDirectory( directory ) )
                            (void)AddDllDirectory( StringUtil::utf8ToUTF16( directory.c_str() ).c_str() ); // 실패하면 아래 옛 검색 순서가 찾는다
                    }
                    return true;
                }();
                (void)s_bAdded;
            }
#endif
        };
        /**
         * @class ModuleUnloadHandlerList
         * @brief 살아 있는 언로드 처리기 목록과 그 잠금입니다(`ModuleLibraryUtil::registerUnloadHandler`). 구독이 풀리면 그 처리기를 뺀다.
         * @details 함수 지역 static 으로 둔다(`getModuleUnloadHandlerList`) — 정적 처리기(로그 리스너)가 정적 초기화 중에 오르므로 목록이 그보다 먼저 서야 하고,
         *          먼저 선 것이 나중에 사라지므로 정적 처리기의 구독이 죽은 목록을 밟지 않는다(목록이 먼저 사라지면 구독이 무효가 된다).
         */
        class ModuleUnloadHandlerList final : public SubscriptionSource
        {
        public:
            /** @brief 처리기 하나를 부른 결과입니다. */
            struct ReleaseResult
            {
                const utf8* _pHandlerName{ nullptr };
                uint32      _releasedCount{ 0 };
                bool        _bKeepLibraryMapped{ false };
                bool        _bExpected{ false };
            };

            Subscription add( const ModuleUnloadHandlerDesc& desc, const ModuleUnloadFunction& function )
            {
                uint64 id = 0;
                if ( isWalkingThread() )
                {
                    id = _nextID++; // 훑기 안에서 — 잠금은 훑기가 쥐고 있다
                    _listEntry.push_back( Entry{ id, desc, function } );
                }
                else
                {
                    ScopedLock<mutex> lock{ _mutex };
                    id = _nextID++;
                    _listEntry.push_back( Entry{ id, desc, function } );
                }
                return issueSubscription( id );
            }

            /** @brief 살아 있는 모든 처리기에 범위를 넘깁니다. 시작 때의 사본을 돌고, 앞 처리기 안에서 풀린 것은 건너뛴다. */
            void releaseAllWithin( const void* pBegin, const void* pEnd, vector<ReleaseResult>& outListResult )
            {
                outListResult.clear();
                ScopedLock<mutex>   lock{ _mutex };
                const vector<Entry> listSnapshot = _listEntry;
                _walkingThread                   = std::this_thread::get_id();
                outListResult.reserve( listSnapshot.size() );
                for ( const Entry& entry : listSnapshot )
                {
                    if ( VectorUtil::findIf( _listEntry, [&entry]( const Entry& live )
                    { return live._id == entry._id; } ) == nullptr )
                        continue;
                    ReleaseResult result{};
                    result._pHandlerName  = entry._desc._pName;
                    result._bExpected     = entry._desc._bReleaseExpected;
                    result._releasedCount = entry._function( pBegin, pEnd, result._bKeepLibraryMapped );
                    outListResult.push_back( result );
                }
                _walkingThread = std::thread::id{};
            }

        private:
            struct Entry
            {
                uint64                  _id{ 0 };
                ModuleUnloadHandlerDesc _desc{};
                ModuleUnloadFunction    _function{};
            };

            /** @brief 지금 스레드가 훑기 중인 그 스레드인지 묻습니다(그러면 이미 잠금을 쥐고 있다). */
            bool isWalkingThread() const { return _walkingThread == std::this_thread::get_id(); }

            void releaseSubscription( const uint64 id ) override
            {
                const auto removeEntry = [this, id]()
                { VectorUtil::removeIf( _listEntry, [id]( const Entry& entry )
                  { return entry._id == id; } ); };
                if ( isWalkingThread() )
                {
                    removeEntry(); // 훑기 안에서 — 훑기는 사본을 돌고 지워진 것은 건너뛴다
                    return;
                }
                ScopedLock<mutex> lock{ _mutex };
                removeEntry();
            }

            mutex           _mutex;
            vector<Entry>   _listEntry;
            uint64          _nextID{ 1 };
            std::thread::id _walkingThread; ///< `releaseAllWithin` 이 잠금을 쥐고 도는 스레드(돌지 않으면 빈 id)
        };

        ModuleUnloadHandlerList& getModuleUnloadHandlerList()
        {
            static ModuleUnloadHandlerList s_list;
            return s_list;
        }
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
        string symbolDirectory = FileUtil::joinPath( FileUtil::getBinaryDirectory(), kSymbolFolder );
        // 옮긴 모듈(게임 팩)은 그 폴더에서 낸 라이브러리일 때만 `Symbols/<폴더>` 다 — 같은 이름의 `Bin/Symbols/SWGame.pdb` 는 활성 게임의 것이다.
        const ModuleSubdirectory& moved = s_moduleSubdirectory;
        if ( moved._arrDirectory[0] != '\0' )
        {
            const string movedDirectory = FileUtil::joinPath( FileUtil::getBinaryDirectory(), moved._arrDirectory );
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
        return FileUtil::joinPath( FileUtil::getBinaryDirectory(), kModuleFolder );
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
            return FileUtil::joinPath( FileUtil::joinPath( FileUtil::getBinaryDirectory(), movedDirectory ), fileName );
        string modulePath = FileUtil::joinPath( getModuleDirectory(), fileName );
        if ( FileUtil::exists( modulePath ) == false )
        {
            const string binaryPath = FileUtil::joinPath( FileUtil::getBinaryDirectory(), fileName );
            if ( FileUtil::exists( binaryPath ) )
                modulePath = binaryPath;
        }
        return modulePath;
    }

    void* ModuleLibraryUtil::loadDynamicLibrary( string_view libraryName )
    {
        if ( libraryName.empty() )
            return nullptr;

#if defined( SW_PLATFORM_WINDOWS )
        string absPath;
        if ( FileUtil::makeAbsolutePath( libraryName, absPath ) && FileUtil::exists( absPath ) )
        {
            // Windows 커널 로더(LOAD_WITH_ALTERED_SEARCH_PATH)는 '\'(백슬래시)를 기준으로 디렉터리를 잘라 DLL 검색 경로의
            // 첫 순위로 넣는다. '/' 경로를 넘기면 디렉터리 해석에 실패해 의존 DLL(Engine.dll 등)을 찾지 못하고
            // ERROR_MOD_NOT_FOUND(126) 오류가 나므로, 네이티브 구분자('\')로 바꾼다.
            const string nativePath = FileUtil::toNativeSeparators( absPath );

            // 경로는 UTF-8 이다. W 버전 함수에 UTF-16 으로 넘긴다(A 버전은 ANSI 코드 페이지로 읽어, 매니페스트가 UTF-8 로 바꿔 두지 않은 호스트에서 한글 경로가 깨진다).
            const wstring widePath = StringUtil::utf8ToUTF16( nativePath.c_str() );
            const wstring wideDir  = StringUtil::utf8ToUTF16( FileUtil::getDirectoryPart( nativePath ).c_str() );
            utf16         arrPreviousDLLDir[constant::kMaxPathSize]{};
            const DWORD   previousDLLDirLen = GetDllDirectoryW( static_cast<DWORD>( constant::kMaxPathSize ), arrPreviousDLLDir );
            if ( wideDir.empty() == false )
                SetDllDirectoryW( wideDir.c_str() );

            // 0) 모듈은 `Bin/Modules` 에 있고 그 의존(서드파티, GameFramework DLL)은 `Bin` 에 있다. 실행 파일이 `TestBin` 이면 `Bin` 은 응용 프로그램
            //    폴더도 아니다. 그래서 두 폴더를 사용자 검색 폴더에 한 번 더 추가하고, DLL 폴더와 기본 폴더(응용 프로그램, System32, 사용자 폴더)로 찾는다.
            ModuleLibraryUtilInternal::addBinarySearchDirectories();
            HMODULE hModule = LoadLibraryExW( widePath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS );
            // 1) LOAD_WITH_ALTERED_SEARCH_PATH 로 대상 DLL 의 위치를 가장 먼저 검색해 로드한다
            if ( hModule == nullptr )
                hModule = LoadLibraryExW( widePath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH );
            // 2) LOAD_WITH_ALTERED_SEARCH_PATH 를 쓰면 SetDllDirectory 가 무시되는 Win32 제약이 있어, 실패하면 LoadLibraryW 로 다시 시도한다
            if ( hModule == nullptr )
                hModule = LoadLibraryW( widePath.c_str() );

            if ( previousDLLDirLen > 0 && previousDLLDirLen < constant::kMaxPathSize )
                SetDllDirectoryW( arrPreviousDLLDir );
            else
                SetDllDirectoryW( nullptr );

            if ( hModule != nullptr )
                return hModule;
        }
        const string  nativeName = FileUtil::toNativeSeparators( libraryName );
        const HMODULE hModule    = LoadLibraryW( StringUtil::utf8ToUTF16( nativeName.c_str() ).c_str() );
        if ( hModule == nullptr )
        {
            // 실패 이유는 GetLastError 하나뿐이다. 남기지 않으면 부르는 쪽은 "로드 실패"밖에 모른다(126 의존 DLL 없음, 1114 DllMain 실패 등).
            const DWORD errorCode = GetLastError();
            SW_LOG_WARNING( "LoadLibrary failed for %# (Win32 error %#)", nativeName.c_str(), static_cast<uint32>( errorCode ) );
        }
        return hModule;
#else
        string absPath;
        if ( FileUtil::makeAbsolutePath( libraryName, absPath ) && FileUtil::exists( absPath ) )
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

    bool ModuleLibraryUtil::findLoadedLibraryRange( const void* pAddressInside, const void*& pOutBegin, const void*& pOutEnd )
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
        const IMAGE_NT_HEADERS* pNt = ModuleLibraryUtilInternal::findNtHeaders( hModule );
        if ( pNt == nullptr )
            return false;
        pOutBegin = hModule;
        pOutEnd   = reinterpret_cast<const uint8*>( hModule ) + pNt->OptionalHeader.SizeOfImage;
        return true;
#elif defined( SW_PLATFORM_LINUX )
        ModuleLibraryUtilInternal::LibraryRangeQuery query{};
        query._address = reinterpret_cast<uintptr_t>( pAddressInside );
        dl_iterate_phdr( &ModuleLibraryUtilInternal::collectLibraryRange, &query );
        if ( query._bFound == false )
            return false;
        pOutBegin = reinterpret_cast<const void*>( query._begin );
        pOutEnd   = reinterpret_cast<const void*>( query._end );
        return true;
#else
        return false;
#endif
    }

    bool ModuleLibraryUtil::findDynamicLibraryRange( void* pHandle, const void*& pOutBegin, const void*& pOutEnd )
    {
        pOutBegin = nullptr;
        pOutEnd   = nullptr;
        if ( pHandle == nullptr )
            return false;
#if defined( SW_PLATFORM_WINDOWS )
        // Windows 의 모듈 핸들은 라이브러리의 기준 주소다.
        return findLoadedLibraryRange( pHandle, pOutBegin, pOutEnd );
#elif defined( SW_PLATFORM_LINUX )
        // 리눅스 핸들은 주소가 아니다. 링크 맵이 가리키는 동적 섹션은 라이브러리 안에 있다.
        struct link_map* pLinkMap = nullptr;
        if ( dlinfo( pHandle, RTLD_DI_LINKMAP, &pLinkMap ) != 0 || pLinkMap == nullptr )
            return false;
        return findLoadedLibraryRange( pLinkMap->l_ld, pOutBegin, pOutEnd );
#else
        return false;
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
    uint32 ModuleLibraryUtil::releaseModuleCode( string_view moduleName, const void* pBegin, const void* pEnd, bool* pOutKeepLibraryMapped )
    {
        if ( pOutKeepLibraryMapped != nullptr )
            *pOutKeepLibraryMapped = false;

        // 모듈은 자기가 등록한 것을 스스로 해제해야 한다 — 등록 API 는 구독(`Subscription`)을 돌려주므로 구독을 든 객체가 사라지면 풀린다.
        // 이 훑기는 정리가 아니라 탐지기다: 여기서 뗀 것이 있으면 형식 밖의 등록(원시 포인터 · 이름 등록)이 해제를 빠뜨렸다는 뜻이라 오류로 남긴다(뗀 뒤 라이브러리는 내린다).
        vector<ModuleUnloadHandlerList::ReleaseResult> listResult;
        if ( pBegin != nullptr && pEnd != nullptr )
            getModuleUnloadHandlerList().releaseAllWithin( pBegin, pEnd, listResult );

        uint32 releasedCount{ 0 };
        for ( const ModuleUnloadHandlerList::ReleaseResult& result : listResult )
        {
            releasedCount += result._releasedCount;
            if ( result._releasedCount > 0 && result._bExpected )
                SW_LOG_INFO( "Module %#: released %# %# before unloading its image", moduleName, result._releasedCount, result._pHandlerName );
            else if ( result._releasedCount > 0 )
                SW_LOG_ERROR_ID( "Module.ListenerLeftBehind", "Module %# left %# %# behind — released them before unloading its image", moduleName, result._releasedCount, result._pHandlerName );
            // 해제할 수 없는 것(다른 코드가 아직 구독하는 이 라이브러리의 이벤트 채널)이 남았다. 언로드하면 다음 발행이나 종료 때 언로드된 코드로 점프한다.
            if ( result._bKeepLibraryMapped )
            {
                SW_LOG_WARNING( "Module %# is still referenced by %# that other code uses — keeping its image mapped until exit", moduleName, result._pHandlerName );
                if ( pOutKeepLibraryMapped != nullptr )
                    *pOutKeepLibraryMapped = true;
            }
        }
        return releasedCount;
    }

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

    bool ModuleLibraryUtil::releaseLibraryCode( string_view moduleName, void* pHandle )
    {
        const void* pBegin{ nullptr };
        const void* pEnd{ nullptr };
        if ( pHandle == nullptr || findDynamicLibraryRange( pHandle, pBegin, pEnd ) == false )
            return true;
        bool bKeepLibraryMapped{ false };
        (void)releaseModuleCode( moduleName, pBegin, pEnd, &bKeepLibraryMapped ); // 뗀 것은 releaseModuleCode 가 경고로 남긴다
        return bKeepLibraryMapped == false;
    }

    bool ModuleLibraryUtil::unloadModuleLibrary( string_view moduleName, void* pHandle )
    {
        if ( pHandle == nullptr )
            return false;
        if ( releaseLibraryCode( moduleName, pHandle ) == false )
            return false;

        // 이 라이브러리가 끌어온 의존 라이브러리가 함께 언로드되지 않게 한다. 그 라이브러리의 코드를 가리키는 등록은 여기서 해제할 수 없다.
        (void)pinDynamicLibraryDependencies( pHandle );
        unloadDynamicLibrary( pHandle );
        return true;
    }

    Subscription ModuleLibraryUtil::registerUnloadHandler( const ModuleUnloadHandlerDesc& desc, const ModuleUnloadFunction& function )
    {
        return getModuleUnloadHandlerList().add( desc, function );
    }

#endif // SW_WITH_DYNAMIC_MODULES

    bool ModuleLibraryUtil::isAddressWithin( const void* pCode, const void* pBegin, const void* pEnd )
    {
        const uintptr_t code = reinterpret_cast<uintptr_t>( pCode );
        return code != 0 && reinterpret_cast<uintptr_t>( pBegin ) <= code && code < reinterpret_cast<uintptr_t>( pEnd );
    }

    const void* ModuleLibraryUtil::findVtableAddress( const void* pObject )
    {
        if ( pObject == nullptr )
            return nullptr;
        return *static_cast<const void* const*>( pObject );
    }
} // namespace sw
