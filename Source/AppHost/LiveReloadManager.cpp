#include "pch.h"

#include "AppHost/LiveReloadManager.h"

#include "AppHost/ModuleCallGuard.h"
#include "AppHost/ModuleImagePatch.h"
#include "AppHost/ShadowCopyName.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Common/StdHeaders.h"
#include "Core/File/IFileWatcher.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Module/ModuleImageUtil.h"
#include "Core/Process/Process.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Module/EngineAbiStamp.h"
#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/SceneManager.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/File/Windows/WindowsFileWatcher.h"
#elif defined( SW_PLATFORM_LINUX )
    #include "Core/File/Linux/LinuxFileWatcher.h"
#endif

namespace sw
{
    namespace
    {
        struct LiveReloadManagerInternal
        {
            inline static uint32 s_reloadCount{ 0 };
            /** @brief (리눅스) SONAME 세대입니다. 모듈과 무관하게 하나씩 오르므로 앞부분이 같은 두 모듈도 같은 이름을 받지 않습니다. */
            inline static uint32 s_sonameGeneration{ 0 };

            static void tryDeleteFile( string_view path )
            {
                // 아직 매핑된 그림자 사본은 지금 지울 수 없다. 남은 것은 다음 시작 · 종료의 ShadowCopyName::removeStaleCopies 가 지운다.
                (void)FileUtil::tryRemoveFile( path );
            }

            static void tryDeleteShadowArtifacts( string_view modulePath )
            {
                if ( modulePath.empty() )
                    return;
                tryDeleteFile( modulePath );
                tryDeleteFile( ModuleImageUtil::getDebugSymbolPath( modulePath ) );
            }

            /** @brief 원본을 읽습니다. 링커가 아직 쓰는 중이면 잠겨 있으므로 잠깐씩 기다려 다시 읽습니다. */
            [[nodiscard]] static bool readFileWithRetry( string_view path, vector<uint8>& outBytes )
            {
                for ( int32 retryIndex = 0; retryIndex < 10; ++retryIndex )
                {
                    if ( FileUtil::readFile( path, outBytes ) )
                        return true;
                    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
                }
                return false;
            }

            static void copyDebugSymbolsIfPresent( string_view originalModulePath, string_view shadowModulePath )
            {
                // 원본 심볼은 Bin/Symbols 에, 섀도 복사본의 심볼은 그 복사본 옆에 둔다.
                const string originalDebugPath = ModuleImageUtil::findBuiltDebugSymbolPath( originalModulePath );
                const string shadowDebugPath   = ModuleImageUtil::getDebugSymbolPath( shadowModulePath );
                if ( originalDebugPath.empty() )
                    return;

                if ( FileUtil::copyFile( originalDebugPath, shadowDebugPath ) == false )
                    SW_LOG_WARNING( "Failed to copy debug symbols: %#", shadowDebugPath.c_str() );
            }

            /**
             * @brief 모듈 @p pModule 이 의존 @p dependencyName 을 @p pExpected 이미지에 묶었는지 봅니다.
             * @param pOutActual   모듈이 실제로 묶인 쪽입니다(로그용). 아직 묶이지 않았으면 nullptr 입니다.
             * @param pOutExpected 비교한 기준입니다(로그용).
             * @return 아직 묶이지 않았거나 @p pExpected 에 묶였으면 true 입니다.
             * @details 리눅스는 import 표에서 결속을 읽을 수 없어서, 의존 모듈마다 박힌 도장 상수를 모듈의 검색 범위(자기 + 자기 의존)에서
             *          찾습니다. `dlsym( pModule, ... )` 이 돌려주는 주소는 그 모듈이 **실제로 묶인** 의존의 것입니다.
             */
            static bool isBoundTo( void* pModule, string_view dependencyName, void* pExpected, const void*& pOutActual, const void*& pOutExpected )
            {
#if defined( SW_PLATFORM_WINDOWS )
                pOutExpected = pExpected;
                pOutActual   = ModuleImageUtil::findBoundImportImage( pModule, ModuleImageUtil::formatSharedLibraryName( dependencyName ) );
                return pOutActual == nullptr || pOutActual == pOutExpected;
#elif defined( SW_PLATFORM_LINUX )
                const string stampSymbolName = string{ "sw_moduleEngineAbiStamp_" } + string{ dependencyName };
                pOutExpected                 = ModuleImageUtil::getDynamicSymbol( pExpected, stampSymbolName );
                pOutActual                   = nullptr;
                // 도장이 없는 모듈(정적 링크 · 도장을 박기 전 빌드)은 가릴 방법이 없다. 어긋남으로 보지 않는다.
                if ( pOutExpected == nullptr )
                    return true;
                pOutActual = ModuleImageUtil::getDynamicSymbol( pModule, stampSymbolName );
                return pOutActual == nullptr || pOutActual == pOutExpected;
#else
                (void)pModule;
                (void)dependencyName;
                (void)pExpected;
                pOutActual   = nullptr;
                pOutExpected = nullptr;
                return true;
#endif
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "LiveReloadManager" );

    LiveReloadManager::LiveReloadManager()
        : _mapModule{}
        , _listSharedModule{}
        , _fileWatcher{
#if defined( SW_PLATFORM_WINDOWS )
              make_unique<WindowsFileWatcher>()
#elif defined( SW_PLATFORM_LINUX )
              make_unique<LinuxFileWatcher>()
#else
              nullptr
#endif
          }
        , _buildMutex{}
        , _buildNotice{}
        , _onBeforeCommitBatch{}
        , _drainWorkers{}
        , _listDeferredUnloadImage{}
        , _reloadBatchId{ 0 }
        , _bReloadGraphBroken{ SW_FALSE }
        , _reserved{ 0 }
    {
        // 지운 수일 뿐이다 — 아직 매핑된 사본은 다음 시작 · 종료의 정리가 지운다
        (void)ShadowCopyName::removeStaleCopies( ModuleImageUtil::getModuleDirectory() );

        // 지연 로드 훅은 모듈 DLL 안에 있어 App 의 심볼을 볼 수 없다. 그래서 이 매니저를 Engine.dll 의 창구에 등록해 둔다.
        if ( engine::getModuleHandleProvider() == nullptr )
            engine::setModuleHandleProvider( this );
    }

    LiveReloadManager::~LiveReloadManager()
    {
        shutdown();
    }

    void LiveReloadManager::shutdown()
    {
        _drainWorkers        = {};
        _onBeforeCommitBatch = {};
        clearReloadCallbacks();

        // 지운 수일 뿐이다 — 아직 매핑된 사본은 다음 시작 · 종료의 정리가 지운다
        (void)ShadowCopyName::removeStaleCopies( ModuleImageUtil::getModuleDirectory() );
        if ( engine::getModuleHandleProvider() == this )
            engine::setModuleHandleProvider( nullptr );

        if ( _fileWatcher != nullptr )
        {
            _fileWatcher->stopWatching();
            _fileWatcher.reset();
        }

        // 언로드를 미룬 옛 이미지를 먼저 내린다. 그것들은 같은 배치의 미룬 이미지나 **지금 살아 있는** 이미지에 묶여 있으므로, 살아
        // 있는 모듈을 먼저 내리면 옛 이미지의 정적 소멸자가 내려간 코드로 뛸 수 있다.
        while ( _listDeferredUnloadImage.empty() == false )
        {
            unloadOldestDeferredBatch();
        }

        if ( _bReloadGraphBroken == SW_TRUE )
        {
            for ( auto& [moduleName, moduleContext] : _mapModule )
            {
                unloadModule( moduleContext );
            }
            _mapModule.clear();
            return;
        }

        vector<string> listName;
        listName.reserve( _mapModule.size() );
        for ( const auto& [name, ctx] : _mapModule )
        {
            listName.push_back( name );
        }

        vector<string> listOrder;
        if ( topoSortSubgraph( listName, listOrder ) == false )
        {
            SW_LOG_ERROR( "Topological sort failed during shutdown — unloading in arbitrary order" );
            for ( auto& [name, ctx] : _mapModule )
            {
                unloadModule( ctx );
            }
            _mapModule.clear();
            return;
        }

        // 역순으로 언로드한다. 의존하는 모듈을 먼저 내리고, 그다음 기반 모듈을 내린다.
        for ( auto it = listOrder.rbegin(); it != listOrder.rend(); ++it )
        {
            auto mapIt = _mapModule.find( *it );
            if ( mapIt != _mapModule.end() )
                unloadModule( mapIt->second );
        }
        _mapModule.clear();
    }

    bool LiveReloadManager::loadSharedModule( string_view moduleName )
    {
        for ( const string& loadedName : _listSharedModule )
        {
            if ( loadedName == moduleName )
                return true;
        }

        const string modulePath = ModuleImageUtil::findModuleLibraryPath( moduleName );
        if ( FileUtil::exists( modulePath ) == false )
        {
            SW_LOG_INFO( "Shared module %# is not built (Bin/Modules · Bin) — nothing links it", moduleName );
            return true;
        }

        // 섀도 복사본과 같은 이유로 올리기 **전에** 도장을 본다. 도장이 없거나 다르면 정적 초기화가 돌기 전에 거절한다.
        vector<uint8> bytes;
        if ( LiveReloadManagerInternal::readFileWithRetry( modulePath, bytes ) == false )
        {
            SW_LOG_ERROR( "Failed to read the shared module %# (locked)", modulePath );
            return false;
        }
        string moduleStamp;
        if ( ModuleImagePatch::findEngineAbiStamp( bytes, moduleStamp ) == false || moduleStamp != engine::getEngineAbiStamp() )
        {
            SW_LOG_ERROR( "Shared module %# was built against different engine headers than the running engine (module '%#', engine %#) — rebuild them together",
                          moduleName, moduleStamp, engine::getEngineAbiStamp() );
            return false;
        }

        // 불변 조건: 여기서 전역 머리는 비어 있다(등록할 때마다 비운다). 올리면 이 모듈의 정적 등록기만 매달린다.
        void* const pSharedHandle = ModuleImageUtil::loadDynamicLibrary( modulePath );
        if ( pSharedHandle == nullptr )
        {
            SW_LOG_ERROR( "Failed to load the shared module %#", modulePath );
            return false;
        }
        (void)ModuleImageUtil::bindDelayLoadImports( pSharedHandle ); // 못 묶으면 경고했다 — 그 import 는 첫 호출에 묶인다
        engine::registerModuleTypes( moduleName );
        _listSharedModule.push_back( string{ moduleName } );
        SW_LOG_INFO( "Shared module loaded: %#", moduleName );
        return true;
    }

    bool LiveReloadManager::registerModule( string_view moduleName, const vector<string>& listDependsOn )
    {
        const string moduleDir = ModuleImageUtil::getModuleDirectory();
        if ( FileUtil::isDirectory( moduleDir ) == false )
        {
            SW_LOG_ERROR( "Module directory does not exist: %#", moduleDir );
            return false;
        }

        ModuleContext& moduleContext      = _mapModule[string( moduleName )];
        moduleContext._moduleName         = moduleName;
        moduleContext._listDependsOn      = listDependsOn;
        moduleContext._tempModulePath     = "";
        moduleContext._originalModulePath = ModuleImageUtil::findModuleLibraryPath( moduleName );
        if ( loadShadowCopyModule( moduleContext ) )
        {
            if ( verifyModuleBindings() == false )
                markGraphBroken( "a module is bound to a stale dependency image after registration" );
            return true;
        }

        // commit 은 새 핸들을 컨텍스트에 넣은 뒤 onAfterReload 가 그래프를 막아도 실패를 돌려준다. 그대로 지우면 이미지 · 등록한 타입 ·
        // 섀도 파일이 프로세스 끝까지 남으므로, 등록을 거두기 전에 평소처럼 내린다.
        if ( moduleContext._pLibraryModule != nullptr )
            unloadModule( moduleContext );
        _mapModule.erase( string( moduleName ) );
        return false;
    }

    void LiveReloadManager::triggerReload( string_view moduleName )
    {
        if ( _bReloadGraphBroken == SW_TRUE )
        {
            SW_LOG_ERROR( "Reload graph is broken — restart the process (ignored %#)", moduleName );
            return;
        }

        auto iter = _mapModule.find( string( moduleName ) );
        if ( iter == _mapModule.end() )
            return;

        ModuleContext& ctx = iter->second;
        SW_LOG_INFO( "Manual Live Reload queued for %# (debounce %#ms)...",
                     moduleName, kMtimeDebounceMs );
        ctx._debounceMtime = FileUtil::getFileTimestamp( ctx._originalModulePath );
        ctx._debounceTimer.resetTimer();
        ctx._debounceTimer.startTimer();
        ctx._bMtimeDebouncing = true;
        ctx._bForceReload     = true;
    }

    void LiveReloadManager::notifyBuildStarted()
    {
        std::scoped_lock<mutex> lock( _buildMutex );
        _buildNotice._bInProgress = true;
    }

    void LiveReloadManager::notifyBuildFinished( bool bSucceeded, string_view targetName )
    {
        std::scoped_lock<mutex> lock( _buildMutex );
        _buildNotice._bInProgress      = false;
        _buildNotice._bFinishedPending = true;
        _buildNotice._bSucceeded       = bSucceeded;
        _buildNotice._targetName       = string( targetName );
    }

    bool LiveReloadManager::consumeBuildNotice()
    {
        BuildNotice notice;
        {
            std::scoped_lock<mutex> lock( _buildMutex );
            notice                         = _buildNotice;
            _buildNotice._bFinishedPending = false;
        }
        if ( notice._bFinishedPending )
        {
            if ( notice._bSucceeded == false )
            {
                // 실패한 빌드가 다시 쓴 모듈은 반쯤 된 집합일 수 있다 — 올리지 않는다. 파일은 그대로라 다음에 성공한 빌드의 변경과 함께 올라간다.
                for ( auto& [name, ctx] : _mapModule )
                {
                    if ( ctx._bMtimeDebouncing || ctx._bForceReload )
                        SW_LOG_WARNING( "Build failed - %# is not reloaded from its partial output", ctx._moduleName );
                    ctx._bMtimeDebouncing = false;
                    ctx._bForceReload     = false;
                }
            }
            else if ( notice._targetName.empty() == false && _mapModule.find( notice._targetName ) != _mapModule.end() )
            {
                triggerReload( notice._targetName );
            }
        }
        return notice._bInProgress;
    }

    void LiveReloadManager::update()
    {
        vector<FileChangeEvent> listEvent;

        BLOCK( "Poll File Events" )
        {
            if ( _fileWatcher != nullptr )
                _fileWatcher->pollEvents( listEvent );
        }

        for ( const FileChangeEvent& changeEvent : listEvent )
        {
            if ( changeEvent._action == FileWatcherAction::Modified )
            {
                string fullPath = FileUtil::joinPath( changeEvent._directory, changeEvent._filename );

                for ( auto& [moduleName, moduleContext] : _mapModule )
                {
                    if ( moduleContext._originalModulePath != fullPath )
                        continue;

                    const uint64 sourceMtime = FileUtil::getFileTimestamp( moduleContext._originalModulePath );
                    if ( sourceMtime <= moduleContext._loadedSourceMtime )
                        continue;

                    moduleContext._debounceMtime = sourceMtime;
                    moduleContext._debounceTimer.resetTimer();
                    moduleContext._debounceTimer.startTimer();
                    moduleContext._bMtimeDebouncing = true;
                }
            }
        }

        // 이 프로세스가 시킨 빌드가 도는 동안은 변경을 모아 두기만 한다 — 연쇄 빌드의 반쯤 된 집합을 올리지 않는다(`notifyBuildStarted`).
        const bool bBuildInProgress = consumeBuildNotice();
        for ( auto& [name, ctx] : _mapModule )
        {
            if ( ctx._bMtimeDebouncing == false || bBuildInProgress )
                continue;

            ctx._debounceTimer.updateTimer();
            const float32 elapsedSec = ctx._debounceTimer.getTotalTime();
            if ( elapsedSec < static_cast<float32>( kMtimeDebounceMs ) / 1000.0f )
                continue;

            const uint64 sourceMtime = FileUtil::getFileTimestamp( ctx._originalModulePath );
            if ( sourceMtime != ctx._debounceMtime )
            {
                ctx._debounceMtime = sourceMtime;
                ctx._debounceTimer.resetTimer();
                ctx._debounceTimer.startTimer();
                continue;
            }

            ctx._bMtimeDebouncing = false;
            const bool bForce     = ctx._bForceReload.exchange( false );
            if ( bForce || sourceMtime > ctx._loadedSourceMtime )
            {
                SW_LOG_TRACE( "FileWatcher settled — queuing reload for %#", ctx._moduleName );
                ctx._bPendingReload = true;
            }
        }

        if ( _bReloadGraphBroken == SW_TRUE )
            return;

        vector<string> listPendingRoot;
        BLOCK( "Collect Pending Reloads" )
        {
            for ( auto& [name, ctx] : _mapModule )
            {
                if ( ctx._bPendingReload )
                {
                    ctx._bPendingReload = false;
                    listPendingRoot.push_back( ctx._moduleName );
                }
            }
        }

        BLOCK( "Reload Cascade" )
        {
            vector<string> listSubgraph;
            for ( const string& root : listPendingRoot )
            {
                collectDependentClosure( root, listSubgraph );
            }
            if ( listSubgraph.empty() == false )
                reloadCascade( listSubgraph );
        }
    }

    void LiveReloadManager::setOnBeforeReload( string_view moduleName, OnBeforeReloadDelegate delegate )
    {
        _mapModule[string( moduleName )]._onBeforeReload = std::move( delegate );
    }

    void LiveReloadManager::setOnAfterReload( string_view moduleName, OnAfterReloadDelegate delegate )
    {
        _mapModule[string( moduleName )]._onAfterReload = std::move( delegate );
    }

    void LiveReloadManager::setOnReloadFault( string_view moduleName, OnReloadFaultDelegate delegate )
    {
        _mapModule[string( moduleName )]._onReloadFault = std::move( delegate );
    }

    void LiveReloadManager::setOnValidateImage( string_view moduleName, OnValidateImageDelegate delegate )
    {
        _mapModule[string( moduleName )]._onValidateImage = std::move( delegate );
    }

    void LiveReloadManager::setOnBeforeCommitBatch( OnBeforeCommitBatchDelegate delegate )
    {
        _onBeforeCommitBatch = std::move( delegate );
    }

    void LiveReloadManager::setDrainWorkers( DrainWorkersDelegate delegate )
    {
        _drainWorkers = std::move( delegate );
    }

    void LiveReloadManager::clearReloadCallbacks()
    {
        for ( auto& [name, ctx] : _mapModule )
        {
            ctx._onBeforeReload  = {};
            ctx._onAfterReload   = {};
            ctx._onReloadFault   = {};
            ctx._onValidateImage = {};
        }
    }

    void LiveReloadManager::markGraphBroken( string_view reason )
    {
        _bReloadGraphBroken = SW_TRUE;
        (void)reason;
        SW_LOG_ERROR( "Reload graph broken (%#) — restart the process; further live reloads are disabled",
                      reason );
    }

    bool LiveReloadManager::verifyModuleBindings() const
    {
        bool bAllBound = true;
        for ( const auto& [moduleName, moduleContext] : _mapModule )
        {
            if ( moduleContext._pLibraryModule == nullptr )
                continue;
            for ( const string& dependencyName : moduleContext._listDependsOn )
            {
                // 이 매니저가 올리지 않은 의존(Engine 처럼 처음부터 링크된 것)은 교체되지 않으므로 가릴 것이 없다.
                const auto dependencyIt = _mapModule.find( dependencyName );
                if ( dependencyIt == _mapModule.end() || dependencyIt->second._pLibraryModule == nullptr )
                    continue;

                const void* pActual{ nullptr };
                const void* pExpected{ nullptr };
                if ( LiveReloadManagerInternal::isBoundTo( moduleContext._pLibraryModule, dependencyName, dependencyIt->second._pLibraryModule, pActual, pExpected ) )
                    continue;

                SW_LOG_ERROR( "Module %# is bound to a stale %# image (bound %#, current %#)", moduleName, dependencyName, pActual, pExpected );
                bAllBound = false;
            }
        }
        return bAllBound;
    }

    void* LiveReloadManager::getModuleHandle( string_view moduleName ) const
    {
        auto it = _mapModule.find( string( moduleName ) );
        return it != _mapModule.end() ? it->second._pLibraryModule : nullptr;
    }

    void LiveReloadManager::rewriteShadowSonames( ModuleContext& ctx, vector<uint8>& inoutBytes )
    {
        // 복사본은 원본에서 막 복사했으므로 SONAME 은 늘 원본 이름이다. 처음 한 번 읽어 둔다(의존 모듈의 NEEDED 가 이 이름을 적고 있다).
        if ( ctx._soname._original.empty() )
            (void)ModuleImagePatch::readSoname( inoutBytes, ctx._soname._original ); // 못 읽으면(ELF 가 아님) 이름이 비어 아래가 건너뛴다

        if ( ctx._soname._original.empty() == false )
        {
            const string generationName = ModuleImagePatch::makeGenerationName( ctx._soname._original, ++LiveReloadManagerInternal::s_sonameGeneration );
            const bool   bRenamed       = generationName.empty() == false &&
                                  ModuleImagePatch::replaceDynamicString( inoutBytes, ModuleImagePatch::kTagSoname, ctx._soname._original, generationName ) > 0;
            if ( bRenamed )
                ctx._soname._current = generationName;
        }

        // 의존 모듈의 NEEDED 를 그 의존의 지금 이름으로. 연쇄 리로드는 의존 순서로 prepare 하므로, 같은 연쇄에서 바뀌는 의존은 이미
        // 새 이름을 들고 있다(`_current`). 이 매니저가 올리지 않은 의존(Engine 등)은 원래 이름 그대로 둔다.
        for ( const string& dependencyName : ctx._listDependsOn )
        {
            const auto dependencyIt = _mapModule.find( dependencyName );
            if ( dependencyIt == _mapModule.end() )
                continue;
            const SonameState& dependency = dependencyIt->second._soname;
            if ( dependency._original.empty() || dependency._current.empty() )
                continue;
            ModuleImagePatch::replaceDynamicString( inoutBytes, ModuleImagePatch::kTagNeeded, dependency._original, dependency._current );
        }
    }

    bool LiveReloadManager::loadShadowCopyModule( ModuleContext& ctx )
    {
        PreparedShadow prepared;
        if ( prepareShadowCopy( ctx, prepared ) == false )
            return false;
        if ( commitShadowCopy( ctx, prepared ) )
            return true;
        abortShadowCopy( ctx, prepared );
        return false;
    }

    bool LiveReloadManager::prepareShadowCopy( ModuleContext& ctx, PreparedShadow& out )
    {
        out = {};
        BLOCK( "Check Original Module" )
        {
            if ( FileUtil::exists( ctx._originalModulePath ) == false )
            {
                SW_LOG_ERROR( "Original module not found: %#", ctx._originalModulePath.c_str() );
                return false;
            }
        }

        out._sourceMtime = FileUtil::getFileTimestamp( ctx._originalModulePath );

        ++LiveReloadManagerInternal::s_reloadCount;
        const string tempName = ShadowCopyName::make( ctx._moduleName, Process::getCurrentProcessId(), LiveReloadManagerInternal::s_reloadCount, out._sourceMtime );
        const string execDir  = FileUtil::getDirectoryPart( ctx._originalModulePath );

        BLOCK( "Create Shadow Copy" )
        {
            // 원본을 한 번 읽어 대조하고(리눅스는 고치고) 복사본으로 쓴다. 대조에 걸리면 복사본을 만들기 전이라 치울 것이 없다.
            vector<uint8> bytes;
            if ( LiveReloadManagerInternal::readFileWithRetry( ctx._originalModulePath, bytes ) == false )
            {
                SW_LOG_ERROR( "Failed to read the module for a shadow copy (locked): %#", ctx._originalModulePath.c_str() );
                return false;
            }

            // 올리기 **전에** 본다. 올리면 정적 초기화가 돌므로, 돌고 있는 엔진과 다른 헤더로 빌드된 모듈은 그 전에 거절해야 한다.
            // 도장이 **없는** 모듈도 거절한다 — 도장을 박지 않는 빌드 규칙으로 만든 모듈이 대조 없이 올라오지 않게(동적 모듈은 모두
            // `sw_registerDynamicModule` 이 도장을 박는다).
            string     moduleStamp;
            const bool bEngineAbiMismatch = ModuleImagePatch::findEngineAbiStamp( bytes, moduleStamp ) == false || moduleStamp != engine::getEngineAbiStamp();
            if ( bEngineAbiMismatch )
            {
                // 첫 로드면 지킬 옛 모듈이 없다 — 엔진과 모듈 중 한쪽만 다시 빌드된 것이라 둘을 함께 빌드해야 한다.
                const utf8* pHint = ctx._pLibraryModule != nullptr ? "keeping the old module; restart to pick up the engine change"
                                                                   : "rebuild the engine and its modules together";
                SW_LOG_ERROR( "Module %# was built against different Core/Engine headers than the running engine (module %#, engine %#) — %#",
                              ctx._moduleName, moduleStamp, engine::getEngineAbiStamp(), pHint );
                return false;
            }
#if defined( SW_PLATFORM_LINUX )
            rewriteShadowSonames( ctx, bytes );
#endif

            out._tempPath = FileUtil::joinPath( execDir, ModuleImageUtil::formatSharedLibraryName( tempName ) );
            if ( FileUtil::writeFile( out._tempPath, bytes.data(), bytes.size() ) == false )
            {
                ctx._soname._current = ctx._soname._loaded;
                SW_LOG_ERROR( "Failed to write the shadow copy: %#", out._tempPath.c_str() );
                LiveReloadManagerInternal::tryDeleteShadowArtifacts( out._tempPath );
                out = {};
                return false;
            }

            LiveReloadManagerInternal::copyDebugSymbolsIfPresent( ctx._originalModulePath, out._tempPath );
        }

        BLOCK( "Load Dynamic Library" )
        {
            // 불변 조건: engine::registerModuleTypes 가 로드할 때마다 전역 헤드를 nullptr 로 비우므로, 여기에 들어올 때 세 헤드는
            // 항상 nullptr 이다(연쇄 교체의 두 번째 모듈 이후도 마찬가지다). abort 는 이 스냅샷을 되돌리므로, 정상 상태에서는
            // nullptr 로 되돌리는 것이 올바른 결과다. (검증: SmokeTest Architecture.LiveReloadRegistrarContentLifecycle)
            out._pPreviousTypeHead     = TypeRegistrar::getHead();
            out._pPreviousEnumHead     = EnumRegistrar::getHead();
            out._pPreviousVariableHead = GlobalVariableRegistrar::getHead();

            out._pHandle = ModuleImageUtil::loadDynamicLibrary( out._tempPath );
            if ( out._pHandle == nullptr )
            {
                ctx._soname._current = ctx._soname._loaded;
                SW_LOG_ERROR( "Failed to load dynamic library (keeping old): %#", out._tempPath.c_str() );
                LiveReloadManagerInternal::tryDeleteShadowArtifacts( out._tempPath );
                out = {};
                return false;
            }

            out._pTypeHead     = TypeRegistrar::getHead();
            out._pEnumHead     = EnumRegistrar::getHead();
            out._pVariableHead = GlobalVariableRegistrar::getHead();

            TypeRegistrar::getHead()           = nullptr;
            EnumRegistrar::getHead()           = nullptr;
            GlobalVariableRegistrar::getHead() = nullptr;
        }

        BLOCK( "Validate New Image" )
        {
            // 옛 이미지가 아직 도는 동안 호스트가 새 이미지를 거절할 자리다. 거절은 적용 전 실패라 옛 것을 두고 그래프를 막지 않는다.
            // 새 이미지의 코드가 불리므로 onAfterReload 와 같이 지킨다.
            if ( ctx._onValidateImage.isBound() )
            {
                bool        bAccepted{ false };
                uint32      faultCode{ 0 };
                void* const pNewHandle = out._pHandle;
                const bool  bCompleted = ModuleCallGuard::run(
                    SW_DELEGATE_LAMBDA( Delegate<void()>, [&ctx, &bAccepted, pNewHandle]()
                 {
                    bAccepted = ctx._onValidateImage( pNewHandle );
                } ),
                    faultCode );
                if ( bCompleted == false || bAccepted == false )
                {
                    const utf8* pOutcome = ( ctx._pLibraryModule != nullptr ) ? "keeping the old module" : "it is not loaded";
                    if ( bCompleted == false )
                        SW_LOG_ERROR( "Module %# faulted (code 0x%#) while the host checked it — %#", ctx._moduleName,
                                      Fmt( faultCode, Format( 8, Format::Padding::Zero ).hex() ), pOutcome );
                    else
                        SW_LOG_ERROR( "Module %# was rejected by the host before it took over — %#", ctx._moduleName, pOutcome );
                    abortShadowCopy( ctx, out );
                    out = {};
                    return false;
                }
            }
        }

        return true;
    }

    bool LiveReloadManager::commitShadowCopy( ModuleContext& ctx, PreparedShadow& prepared )
    {
        if ( prepared._pHandle == nullptr )
            return false;

        const string previousTempModule = ctx._tempModulePath;
        void*        pPreviousHandle    = ctx._pLibraryModule;
        bool         bKeepPreviousImage{ false };

        BLOCK( "Swap Module Handles" )
        {
            // onBeforeReload 가 모듈 자원을 건드리기 전에 워커가 옛 이미지에서 빠져나와 있어야 한다.
            if ( pPreviousHandle != nullptr && drainTasksBeforeUnload() == false )
                return false;

            if ( ctx._onBeforeReload.isBound() && pPreviousHandle != nullptr )
                ctx._onBeforeReload();

            if ( pPreviousHandle != nullptr )
            {
                // onBefore 뒤에 남은 작업을 비운다. 이미 모듈을 내렸으면 교체를 계속하고, 제한 시간을 넘기면 그래프를 깨진 상태로 표시만 한다.
                drainTasksBeforeUnload();
                engine::unregisterModuleTypes( ctx._moduleName );
                bKeepPreviousImage = ModuleImageUtil::releaseImageCode( ctx._moduleName, pPreviousHandle ) == false;
            }

            ctx._pLibraryModule = prepared._pHandle;
            // 이 이미지의 코드가 처음 돌기 전, 의존 이미지(먼저 커밋됨)가 등록된 뒤에 지연 import 를 묶는다 — 첫 호출이 묶으면 첫 float 인자가 망가진다.
            (void)ModuleImageUtil::bindDelayLoadImports( ctx._pLibraryModule ); // 못 묶으면 경고했다
            ctx._tempModulePath    = prepared._tempPath;
            ctx._loadedSourceMtime = prepared._sourceMtime;
            ctx._soname._loaded    = ctx._soname._current;
            prepared._pHandle      = nullptr;
            prepared._tempPath.clear();

            engine::registerModuleTypes(
                ctx._moduleName,
                prepared._pTypeHead,
                prepared._pEnumHead,
                prepared._pVariableHead );
            prepared._pTypeHead     = nullptr;
            prepared._pEnumHead     = nullptr;
            prepared._pVariableHead = nullptr;

            invokeAfterReload( ctx );

            // 옛 이미지는 바로 내리지 않고 언로드를 미룬다. 그래프가 깨진 경우도 같다(핸들을 잃지 않는다).
            if ( pPreviousHandle != nullptr )
                deferImageUnload( ctx._moduleName, pPreviousHandle, previousTempModule, bKeepPreviousImage );
        }

        if ( _bReloadGraphBroken == SW_TRUE )
        {
            SW_LOG_ERROR( "Module %# committed but onAfter marked the graph broken",
                          ctx._moduleName );
            return false;
        }

        SW_LOG_INFO( "Module loaded (shadow: %#)", ctx._tempModulePath.c_str() );
        return true;
    }

    void LiveReloadManager::invokeAfterReload( ModuleContext& ctx )
    {
        // 새 이미지의 코드가 처음 도는 자리다. 여기서 죽으면 에디터째 내려가 저장하지 않은 작업을 잃으므로 지킨다.
        if ( ctx._onAfterReload.isBound() == false )
            return;
        uint32     faultCode{ 0 };
        const bool bCompleted = ModuleCallGuard::run(
            SW_DELEGATE_LAMBDA( Delegate<void()>, [&ctx]()
        {
            ctx._onAfterReload( ctx._pLibraryModule );
        } ),
            faultCode );
        if ( bCompleted == false )
        {
            SW_LOG_ERROR( "Module %# faulted (code 0x%#) while starting after the reload — dropping what it handed out; save your work and restart",
                          ctx._moduleName, Fmt( faultCode, Format( 8, Format::Padding::Zero ).hex() ) );
            markGraphBroken( "a module faulted in onAfterReload" );
            if ( ctx._onReloadFault.isBound() )
                ctx._onReloadFault( faultCode );
        }
    }

    bool LiveReloadManager::runAfterReload( string_view moduleName )
    {
        const auto it = _mapModule.find( string( moduleName ) );
        if ( it == _mapModule.end() || it->second._pLibraryModule == nullptr )
        {
            SW_LOG_ERROR( "Module %# is not loaded - nothing to start", moduleName );
            return false;
        }
        invokeAfterReload( it->second );
        return _bReloadGraphBroken == SW_FALSE;
    }

    void LiveReloadManager::abortShadowCopy( ModuleContext& ctx, PreparedShadow& prepared )
    {
        ctx._soname._current = ctx._soname._loaded;
        if ( prepared._pHandle != nullptr )
        {
            TypeRegistrar::getHead()           = prepared._pPreviousTypeHead;
            EnumRegistrar::getHead()           = prepared._pPreviousEnumHead;
            GlobalVariableRegistrar::getHead() = prepared._pPreviousVariableHead;
            if ( ModuleImageUtil::releaseImageCode( ctx._moduleName, prepared._pHandle ) )
                ModuleImageUtil::unloadDynamicLibrary( prepared._pHandle );
            prepared._pHandle = nullptr;
        }

        prepared._pTypeHead             = nullptr;
        prepared._pEnumHead             = nullptr;
        prepared._pVariableHead         = nullptr;
        prepared._pPreviousTypeHead     = nullptr;
        prepared._pPreviousEnumHead     = nullptr;
        prepared._pPreviousVariableHead = nullptr;
        LiveReloadManagerInternal::tryDeleteShadowArtifacts( prepared._tempPath );
        prepared._tempPath.clear();
        prepared._sourceMtime = 0;
    }

    void LiveReloadManager::unloadModule( ModuleContext& ctx )
    {
        if ( ctx._onBeforeReload.isBound() && ctx._pLibraryModule != nullptr )
            ctx._onBeforeReload();

        if ( ctx._pLibraryModule != nullptr )
        {
            SW_LOG_INFO( "Unloading module %# (handle=%#)", ctx._moduleName.c_str(), ctx._pLibraryModule );
            drainTasksBeforeUnload();

            engine::unregisterModuleTypes( ctx._moduleName );
            if ( ModuleImageUtil::releaseImageCode( ctx._moduleName, ctx._pLibraryModule ) )
                ModuleImageUtil::unloadDynamicLibrary( ctx._pLibraryModule );
            ctx._pLibraryModule = nullptr;
        }

        LiveReloadManagerInternal::tryDeleteShadowArtifacts( ctx._tempModulePath );
        ctx._tempModulePath.clear();
    }

    void LiveReloadManager::deferImageUnload( string_view moduleName, void* pHandle, string_view tempPath, bool bKeepMapped )
    {
        if ( pHandle == nullptr )
            return;

        DeferredUnloadImage deferredImage{};
        deferredImage._moduleName  = string{ moduleName };
        deferredImage._tempPath    = string{ tempPath };
        deferredImage._pHandle     = pHandle;
        deferredImage._batchId     = _reloadBatchId;
        deferredImage._bKeepMapped = bKeepMapped;
        _listDeferredUnloadImage.push_back( std::move( deferredImage ) );

        // 목록은 배치 순서이고 배치 번호는 연쇄마다 하나씩 오른다. 가장 오래된 것이 마지막 N 번의 연쇄 밖이면 내린다.
        while ( _listDeferredUnloadImage.back()._batchId - _listDeferredUnloadImage.front()._batchId >= kMaxDeferredUnloadBatchCount )
        {
            unloadOldestDeferredBatch();
        }
    }

    void LiveReloadManager::unloadOldestDeferredBatch()
    {
        if ( _listDeferredUnloadImage.empty() )
            return;

        const uint32 oldestBatchId = _listDeferredUnloadImage.front()._batchId;
        size_t       batchEnd{ 0 };
        while ( batchEnd < _listDeferredUnloadImage.size() && _listDeferredUnloadImage[batchEnd]._batchId == oldestBatchId )
        {
            ++batchEnd;
        }

        for ( size_t imageIndex = batchEnd; imageIndex > 0; --imageIndex )
        {
            const DeferredUnloadImage& deferredImage = _listDeferredUnloadImage[imageIndex - 1];
            if ( deferredImage._bKeepMapped )
            {
                // 다른 코드가 아직 구독하는 이벤트 채널을 이 이미지가 만들었다(`ModuleImageUtil::releaseModuleCode`). 내리지도, 파일을 지우지도 않는다.
                SW_LOG_INFO( "Keeping deferred module image %# mapped (batch %#) — an event channel it created is still subscribed", deferredImage._moduleName,
                             deferredImage._batchId );
                continue;
            }
            SW_LOG_INFO( "Unloading deferred module image %# (batch %#, handle=%#)", deferredImage._moduleName, deferredImage._batchId, deferredImage._pHandle );
            ModuleImageUtil::unloadDynamicLibrary( deferredImage._pHandle );
            LiveReloadManagerInternal::tryDeleteShadowArtifacts( deferredImage._tempPath );
        }
        _listDeferredUnloadImage.erase( _listDeferredUnloadImage.begin(), _listDeferredUnloadImage.begin() + static_cast<std::ptrdiff_t>( batchEnd ) );
    }

    bool LiveReloadManager::drainTasksBeforeUnload()
    {
        // App 경로에서는 _drainWorkers(= ModuleHost::drainRenderWorkers)가 실제로 비우고, 아래 폴백은 헤드리스 · 테스트에서만
        // 돈다. 제한 시간 상수는 양쪽이 함께 쓴다.
        constexpr uint32 kDrainTimeoutMs = LiveReloadManager::kModuleDrainTimeoutMs;

        // onBeforeReload 는 모듈에서 시작된 작업을 멈춰야 한다. 실행 중인 태스크를 비워 콜백이 옛 이미지에 들어가지 못하게 한다.
        // clear() 는 부르지 않는다. 그러면 관계없는 GpuScene · 씬 로드 작업까지 버리고 onTaskFinished 정리를 건너뛴다.
        if ( engine::areEngineServicesBound() )
            engine::getSceneManager().cancelPendingAsyncLoads();

        // 렌더 스레드는 TaskManager 밖에서 돈다. Present 훅이 모듈 코드를 실행 중일 수 있으므로 먼저 비운다.
        if ( _drainWorkers.isBound() )
        {
            _drainWorkers();
            if ( _bReloadGraphBroken == SW_TRUE )
                return false;
            return true;
        }

        if ( engine::areEngineServicesBound() && engine::getTaskManager().waitAll( kDrainTimeoutMs ) == false )
        {
            markGraphBroken( "task drain timeout before unload" );
            return false;
        }
        return _bReloadGraphBroken == SW_FALSE;
    }

    void LiveReloadManager::collectDependentClosure( string_view root, vector<string>& outListUnique ) const
    {
        unordered_set<string> uniqueVisited;
        for ( const string& existing : outListUnique )
        {
            uniqueVisited.insert( existing );
        }

        vector<string> listStack;
        listStack.push_back( string( root ) );
        while ( listStack.empty() == false )
        {
            const string cur = listStack.back();
            listStack.pop_back();
            if ( uniqueVisited.insert( cur ).second == false )
                continue;
            if ( _mapModule.find( cur ) == _mapModule.end() )
                continue;
            outListUnique.push_back( cur );
            for ( const auto& [moduleName, ctx] : _mapModule )
            {
                for ( const string& dep : ctx._listDependsOn )
                {
                    if ( dep == cur )
                        listStack.push_back( moduleName );
                }
            }
        }
    }

    bool LiveReloadManager::topoSortSubgraph( const vector<string>& listName, vector<string>& outListOrdered ) const
    {
        outListOrdered.clear();
        unordered_set<string> uniqueSubgraph;
        uniqueSubgraph.reserve( listName.size() );
        for ( const string& name : listName )
        {
            if ( _mapModule.find( name ) != _mapModule.end() )
                uniqueSubgraph.insert( name );
        }
        if ( uniqueSubgraph.empty() )
            return true;

        unordered_map<string, uint32> mapIndegree;
        for ( const string& name : uniqueSubgraph )
        {
            mapIndegree[name] = 0;
        }

        for ( const string& name : uniqueSubgraph )
        {
            const auto found = _mapModule.find( name );
            for ( const string& dep : found->second._listDependsOn )
            {
                if ( uniqueSubgraph.find( dep ) != uniqueSubgraph.end() )
                    mapIndegree[name] += 1;
            }
        }

        vector<string> listReady;
        for ( const string& name : uniqueSubgraph )
        {
            if ( mapIndegree[name] == 0 )
                listReady.push_back( name );
        }
        std::sort( listReady.begin(), listReady.end() );

        vector<string> listOrder;
        listOrder.reserve( uniqueSubgraph.size() );
        size_t cursor{ 0 };
        while ( cursor < listReady.size() )
        {
            const size_t levelBegin = cursor;
            const size_t levelEnd   = listReady.size();
            for ( size_t levelIndex = levelBegin; levelIndex < levelEnd; ++levelIndex )
            {
                const string readyModuleName = listReady[levelIndex];
                listOrder.push_back( readyModuleName );
                for ( const string& name : uniqueSubgraph )
                {
                    const auto found = _mapModule.find( name );
                    for ( const string& dep : found->second._listDependsOn )
                    {
                        if ( dep != readyModuleName )
                            continue;

                        uint32& deg = mapIndegree[name];
                        if ( deg == 0 )
                            continue;
                        deg -= 1;
                        if ( deg == 0 )
                            listReady.push_back( name );
                    }
                }
            }
            cursor = levelEnd;
            if ( listReady.size() > levelEnd )
                std::sort( listReady.begin() + static_cast<std::ptrdiff_t>( levelEnd ), listReady.end() );
        }

        if ( listOrder.size() != uniqueSubgraph.size() )
        {
            string cycleModules;
            for ( const string& name : uniqueSubgraph )
            {
                if ( std::find( listOrder.begin(), listOrder.end(), name ) == listOrder.end() )
                {
                    if ( cycleModules.empty() == false )
                        cycleModules += ", ";
                    cycleModules += name;
                }
            }
            SW_LOG_ERROR( "Dependency cycle in module graph (sorted %# / %#). Cyclic modules: %s",
                          static_cast<uint32>( listOrder.size() ), static_cast<uint32>( uniqueSubgraph.size() ), cycleModules.c_str() );
            return false;
        }

        outListOrdered = std::move( listOrder );
        return true;
    }

    void LiveReloadManager::reloadCascade( const vector<string>& listSubgraphName )
    {
        vector<string> listOrder;
        if ( topoSortSubgraph( listSubgraphName, listOrder ) == false )
        {
            markGraphBroken( "dependency cycle" );
            return;
        }
        if ( listOrder.empty() )
            return;

        SW_LOG_TRACE( "Cascade reload count=%#", static_cast<uint32>( listOrder.size() ) );

        struct PreparedEntry
        {
            ModuleContext* _pCtx{ nullptr };
            PreparedShadow _shadow;
        };
        vector<PreparedEntry> listPrepared;
        listPrepared.reserve( listOrder.size() );

        bool bPrepareOk{ true };
        for ( const string& name : listOrder )
        {
            auto found = _mapModule.find( name );
            if ( found == _mapModule.end() )
                continue;
            PreparedEntry entry;
            entry._pCtx = &found->second;
            if ( prepareShadowCopy( *entry._pCtx, entry._shadow ) == false )
            {
                SW_LOG_ERROR( "Cascade prepare failed for %# — aborting, previous modules kept",
                              name );
                bPrepareOk = false;
                break;
            }
            listPrepared.push_back( std::move( entry ) );
        }

        if ( bPrepareOk == false )
        {
            for ( PreparedEntry& entry : listPrepared )
            {
                abortShadowCopy( *entry._pCtx, entry._shadow );
            }
            return;
        }

        // 배치 직전 콜백의 거절은 적용 전 실패다 — 아무것도 내리지 않았으니 새 이미지만 버리고 옛 것으로 계속 돈다.
        if ( _onBeforeCommitBatch.isBound() && _onBeforeCommitBatch( listOrder ) == false )
        {
            SW_LOG_ERROR( "Cascade reload refused before the first commit — keeping the previous modules" );
            for ( PreparedEntry& entry : listPrepared )
            {
                abortShadowCopy( *entry._pCtx, entry._shadow );
            }
            return;
        }

        ++_reloadBatchId;

        size_t committed{ 0 };
        for ( size_t moduleIndex = 0; moduleIndex < listPrepared.size(); ++moduleIndex )
        {
            PreparedEntry& entry = listPrepared[moduleIndex];
            if ( _bReloadGraphBroken == SW_TRUE )
            {
                SW_LOG_ERROR( "Cascade abort before commit of %# — graph already broken",
                              entry._pCtx->_moduleName );
                abortShadowCopy( *entry._pCtx, entry._shadow );
                for ( size_t otherModuleIndex = moduleIndex + 1; otherModuleIndex < listPrepared.size(); ++otherModuleIndex )
                {
                    abortShadowCopy( *listPrepared[otherModuleIndex]._pCtx, listPrepared[otherModuleIndex]._shadow );
                }
                if ( committed > 0 )
                    markGraphBroken( "partial cascade commit" );
                return;
            }

            if ( commitShadowCopy( *entry._pCtx, entry._shadow ) )
            {
                ++committed;
                continue;
            }

            SW_LOG_ERROR( "Cascade commit failed for %# — aborting remaining commits",
                          entry._pCtx->_moduleName );
            abortShadowCopy( *entry._pCtx, entry._shadow );
            for ( size_t otherModuleIndex = moduleIndex + 1; otherModuleIndex < listPrepared.size(); ++otherModuleIndex )
            {
                abortShadowCopy( *listPrepared[otherModuleIndex]._pCtx, listPrepared[otherModuleIndex]._shadow );
            }

            if ( committed > 0 || _bReloadGraphBroken == SW_FALSE )
                markGraphBroken( "partial cascade commit" );
            return;
        }

        // 새 이미지가 모두 올라왔다. 의존 모듈이 옛 이미지에 묶였으면, 옛 이미지를 내린 지금 그리로 뛰는 코드가 죽는다 — 여기서 막는다.
        if ( verifyModuleBindings() == false )
            markGraphBroken( "a module is bound to a stale dependency image after the cascade" );
    }

    LiveReloadManager::ModuleContext::ModuleContext() noexcept
        : _onBeforeReload{}
        , _onAfterReload{}
        , _onReloadFault{}
        , _onValidateImage{}
        , _moduleName{}
        , _originalModulePath{}
        , _tempModulePath{}
        , _listDependsOn{}
        , _soname{}
        , _pLibraryModule{ nullptr }
        , _loadedSourceMtime{ 0 }
        , _debounceMtime{ 0 }
        , _debounceTimer{}
        , _bPendingReload{ false }
        , _bMtimeDebouncing{ false }
        , _bForceReload{ false }
    {
    }

    LiveReloadManager::ModuleContext::ModuleContext( ModuleContext&& other ) noexcept
        : _onBeforeReload{ std::move( other._onBeforeReload ) }
        , _onAfterReload{ std::move( other._onAfterReload ) }
        , _onReloadFault{ std::move( other._onReloadFault ) }
        , _onValidateImage{ std::move( other._onValidateImage ) }
        , _moduleName{ std::move( other._moduleName ) }
        , _originalModulePath{ std::move( other._originalModulePath ) }
        , _tempModulePath{ std::move( other._tempModulePath ) }
        , _listDependsOn{ std::move( other._listDependsOn ) }
        , _soname{ std::move( other._soname ) }
        , _pLibraryModule{ other._pLibraryModule }
        , _loadedSourceMtime{ other._loadedSourceMtime }
        , _debounceMtime{ other._debounceMtime }
        , _debounceTimer{ other._debounceTimer }
        , _bPendingReload{ other._bPendingReload.load() }
        , _bMtimeDebouncing{ other._bMtimeDebouncing.load() }
        , _bForceReload{ other._bForceReload.load() }
    {
        other._pLibraryModule = nullptr;
        other._bPendingReload.store( false );
        other._bMtimeDebouncing.store( false );
        other._bForceReload.store( false );
    }

    LiveReloadManager::ModuleContext& LiveReloadManager::ModuleContext::operator=( ModuleContext&& other ) noexcept
    {
        if ( this != &other )
        {
            _onBeforeReload     = std::move( other._onBeforeReload );
            _onAfterReload      = std::move( other._onAfterReload );
            _onReloadFault      = std::move( other._onReloadFault );
            _onValidateImage    = std::move( other._onValidateImage );
            _moduleName         = std::move( other._moduleName );
            _originalModulePath = std::move( other._originalModulePath );
            _tempModulePath     = std::move( other._tempModulePath );
            _listDependsOn      = std::move( other._listDependsOn );
            _soname             = std::move( other._soname );
            _pLibraryModule     = other._pLibraryModule;
            _loadedSourceMtime  = other._loadedSourceMtime;
            _debounceMtime      = other._debounceMtime;
            _debounceTimer      = other._debounceTimer;
            _bPendingReload.store( other._bPendingReload.load() );
            _bMtimeDebouncing.store( other._bMtimeDebouncing.load() );
            _bForceReload.store( other._bForceReload.load() );

            other._pLibraryModule = nullptr;
            other._bPendingReload.store( false );
            other._bMtimeDebouncing.store( false );
            other._bForceReload.store( false );
        }
        return *this;
    }
} // namespace sw
