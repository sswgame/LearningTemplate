#include "pch.h"

#include "ModuleManager/LiveReload/LiveReloadManager.h"

#include "Core/Common/PlatformOSHeaders.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/IFileWatcher.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Module/ModuleLibraryUtil.h"
#include "Core/String/RegistrationList.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Module/EngineABIStamp.h"
#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/SceneManager.h"

#include "ModuleManager/LiveReload/ModuleCallGuard.h"
#include "ModuleManager/LiveReload/ModuleLibraryPatch.h"
#include "ModuleManager/LiveReload/ShadowCopyName.h"

#include "sw/config/ConfigConstants.h"

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
            /**
             * @brief 두 모듈 이름이 같은 모듈 파일을 가리키는지 봅니다.
             * @details Windows 파일 시스템은 대소문자를 가리지 않아 'gameframework' 도 GameFramework.dll 을 엽니다. 그래서 거기서는 대소문자를
             *          무시하고 비교합니다. 그렇지 않으면 철자만 바꿔 공용 모듈 거절을 지나갑니다. 리눅스는 파일 이름이 대소문자를 가리므로 그대로 비교합니다.
             */
            static bool isSameModuleName( string_view lhs, string_view rhs )
            {
#if defined( SW_PLATFORM_WINDOWS )
                return StringUtil::equals( lhs, rhs, true );
#else
                return lhs == rhs;
#endif
            }

            /**
             * @brief 모듈 @p pModule 이 의존 @p dependencyName 을 @p pExpected 라이브러리에 묶었는지 봅니다.
             * @param dependencyFileName (Windows) 모듈의 import 표가 그 의존을 부르는 이름입니다(세대 이름 — `ModuleShadowCopy::writeShadowCopy`).
             * @param pOutActual   모듈이 실제로 묶인 쪽입니다(로그용). 그 의존을 import 하지 않으면 nullptr 입니다.
             * @param pOutExpected 비교한 기준입니다(로그용).
             * @return 아직 묶이지 않았거나 @p pExpected 에 묶였으면 true 입니다.
             * @details 리눅스는 import 표에서 결속을 읽을 수 없어서, 의존 모듈마다 박힌 스탬프 상수를 모듈의 검색 범위(자기 + 자기 의존)에서
             *          찾습니다. `dlsym( pModule, ... )` 이 돌려주는 주소는 그 모듈이 **실제로 묶인** 의존의 것입니다.
             */
            static bool isBoundTo( void* pModule, string_view dependencyName, string_view dependencyFileName, void* pExpected, const void*& pOutActual,
                                   const void*& pOutExpected )
            {
#if defined( SW_PLATFORM_WINDOWS )
                (void)dependencyName;
                pOutExpected = pExpected;
                pOutActual   = ModuleLibraryUtil::findBoundImportLibrary( pModule, dependencyFileName );
                return pOutActual == nullptr || pOutActual == pOutExpected;
#elif defined( SW_PLATFORM_LINUX )
                (void)dependencyFileName;
                const string stampSymbolName = string{ "sw_moduleEngineABIStamp_" } + string{ dependencyName };
                pOutExpected                 = ModuleLibraryUtil::getDynamicSymbol( pExpected, stampSymbolName );
                pOutActual                   = nullptr;
                // 스탬프가 없는 모듈(정적 링크 · 스탬프를 박기 전 빌드)은 가릴 방법이 없다. 어긋남으로 보지 않는다.
                if ( pOutExpected == nullptr )
                    return true;
                pOutActual = ModuleLibraryUtil::getDynamicSymbol( pModule, stampSymbolName );
                return pOutActual == nullptr || pOutActual == pOutExpected;
#else
                (void)pModule;
                (void)dependencyName;
                (void)dependencyFileName;
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
        , _dependencyGraph{}
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
        , _buildStampPath{}
        , _seenBuildStamp{}
        , _buildMutex{}
        , _buildNotice{}
        , _listBeforeCommitBatch{}
        , _drainWorkers{}
        , _listCallbackRegistration{}
        , _nextCallbackID{ 1 }
        , _deferredUnloadQueue{ kMaxDeferredUnloadBatchCount }
        , _completedReloadCount{ 0 }
        , _bReloadGraphBroken{ SW_FALSE }
        , _bBuildStampPending{ SW_FALSE }
        , _reserved{ 0 }
    {
        // 지운 수일 뿐이다. 아직 매핑된 사본은 다음 시작이나 종료의 정리가 지운다.
        (void)ShadowCopyName::removeStaleCopies( ModuleLibraryUtil::getModuleDirectory() );
    }

    LiveReloadManager::~LiveReloadManager()
    {
        shutdown();
    }

    void LiveReloadManager::shutdown()
    {
        // 남은 구독은 무효로 만든다 — 주인이 나중에 놓아도 아무것도 풀지 않는다(칸은 아래 기록 비우기와 함께 사라진다).
        // 아래 언로드가 콜백을 부르지 않도록 칸도 지금 비운다(매니저가 내려가는 중이라 다시 설 인스턴스가 없다).
        detachAllSubscriptions();
        for ( const CallbackRegistration& registration : _listCallbackRegistration )
        {
            clearSlot( registration );
        }
        _listCallbackRegistration.clear();

        // 지운 수일 뿐이다. 아직 매핑된 사본은 다음 시작이나 종료의 정리가 지운다.
        (void)ShadowCopyName::removeStaleCopies( ModuleLibraryUtil::getModuleDirectory() );

        if ( _fileWatcher != nullptr )
        {
            _fileWatcher->stopWatching();
            _fileWatcher.reset();
        }

        // 언로드를 미룬 옛 라이브러리를 먼저 언로드한다. 그것들은 같은 배치의 미룬 라이브러리나 **지금 살아 있는** 라이브러리에 바인딩돼 있으므로, 살아
        // 있는 모듈을 먼저 언로드하면 옛 라이브러리의 정적 소멸자가 언로드된 코드로 점프할 수 있다.
        _deferredUnloadQueue.unloadAll();

        if ( _bReloadGraphBroken == SW_TRUE )
        {
            for ( auto& [moduleName, moduleRecord] : _mapModule )
            {
                unloadModule( moduleRecord );
            }
            clearRecords();
            unloadSharedModules();
            return;
        }

        vector<string> listName;
        _dependencyGraph.getModuleNames( listName );

        vector<string> listOrder;
        if ( _dependencyGraph.sortByDependency( listName, listOrder ) == false )
        {
            SW_LOG_ERROR( "Topological sort failed during shutdown — unloading in arbitrary order" );
            for ( auto& [name, context] : _mapModule )
            {
                unloadModule( context );
            }
            clearRecords();
            unloadSharedModules();
            return;
        }

        // 역순으로 언로드한다. 의존하는 모듈을 먼저 언로드하고, 그다음 기반 모듈을 언로드한다.
        for ( auto it = listOrder.rbegin(); it != listOrder.rend(); ++it )
        {
            auto mapIt = _mapModule.find( *it );
            if ( mapIt != _mapModule.end() )
                unloadModule( mapIt->second );
        }
        clearRecords();
        unloadSharedModules();
    }

    bool LiveReloadManager::loadSharedModule( string_view moduleName )
    {
        for ( const auto& [registeredName, registeredRecord] : _mapModule )
        {
            (void)registeredRecord;
            if ( LiveReloadManagerInternal::isSameModuleName( registeredName, moduleName ) )
            {
                SW_LOG_ERROR( "Module %# is already registered for reload - it cannot also be a shared module that never unloads", moduleName );
                return false;
            }
        }
        for ( const SharedLibrary& loaded : _listSharedModule )
        {
            if ( LiveReloadManagerInternal::isSameModuleName( loaded._name, moduleName ) )
                return true;
        }

        const string modulePath = ModuleLibraryUtil::findModuleLibraryPath( moduleName );
        if ( FileUtil::exists( modulePath ) == false )
        {
            SW_LOG_INFO( "Shared module %# is not built (Bin/Modules · Bin) — nothing links it", moduleName );
            return true;
        }

        // 섀도 복사본과 같은 이유로 로드하기 **전에** 스탬프를 확인한다. 스탬프가 없거나 다르면 정적 초기화가 돌기 전에 거절한다.
        vector<uint8> bytes;
        if ( ModuleShadowCopy::readModuleFile( modulePath, bytes ) == false )
        {
            SW_LOG_ERROR( "Failed to read the shared module %# (locked)", modulePath );
            return false;
        }
        string moduleStamp;
        if ( ModuleLibraryPatch::findEngineABIStamp( bytes, moduleStamp ) == false || moduleStamp != engine::getEngineABIStamp() )
        {
            SW_LOG_ERROR( "Shared module %# was built against different engine headers than the running engine (module '%#', engine %#) — rebuild them together",
                          moduleName, moduleStamp, engine::getEngineABIStamp() );
            return false;
        }
        if ( ModuleShadowCopy::rejectsDelayImports( moduleName, modulePath, bytes ) )
            return false;

        // 불변 조건: 여기서 전역 헤드는 비어 있다(등록할 때마다 비운다). 로드하면 이 모듈의 정적 등록기만 매달린다.
        void* const pSharedHandle = ModuleLibraryUtil::loadDynamicLibrary( modulePath );
        if ( pSharedHandle == nullptr )
        {
            SW_LOG_ERROR( "Failed to load the shared module %#", modulePath );
            return false;
        }
        engine::registerModuleTypes( moduleName );
        _listSharedModule.push_back( SharedLibrary{ string{ moduleName }, pSharedHandle } );
        SW_LOG_INFO( "Shared module loaded: %#", moduleName );
        return true;
    }

    bool LiveReloadManager::registerModule( string_view moduleName, const vector<string>& listDependsOn )
    {
        // 공용 모듈(GameFramework)은 키트 · 게임보다 오래 산다는 전제가 그 함수 정적(데이터 표 캐시 · 카탈로그 · 서비스 표)의 근거다.
        // 리로드 대상(_mapModule)에 오르는 길은 여기 하나뿐이므로, 공용 모듈을 내리거나 다시 올리는 길을 여기서 막는다.
        for ( const SharedLibrary& shared : _listSharedModule )
        {
            if ( LiveReloadManagerInternal::isSameModuleName( shared._name, moduleName ) )
            {
                SW_LOG_ERROR( "Shared module %# outlives the modules that link it and cannot be registered for reload or unload", moduleName );
                return false;
            }
        }

        const string moduleDir = ModuleLibraryUtil::getModuleDirectory();
        if ( FileUtil::isDirectory( moduleDir ) == false )
        {
            SW_LOG_ERROR( "Module directory does not exist: %#", moduleDir );
            return false;
        }
        // 바깥 빌드의 신호는 모듈 폴더의 성공 스탬프다(`update`). 감시는 첫 등록이 한 번 건다 — 모듈이 없는 프로세스(테스트)는 감시 스레드를 띄우지 않는다.
        const bool bWatchModuleFolder = _fileWatcher != nullptr && _fileWatcher->isWatching() == false;
        if ( bWatchModuleFolder )
        {
            _buildStampPath = FileUtil::joinPath( moduleDir, config::kFileModuleBuildStamp );
            if ( FileUtil::getFileStamp( _buildStampPath, _seenBuildStamp ) == false )
                SW_LOG_WARNING( "%# is missing - outside builds are not reloaded until the build tree is configured (sw_addModuleBuildStamp)", _buildStampPath );
            if ( _fileWatcher->startWatching( moduleDir, false ) == false )
                SW_LOG_ERROR( "Failed to watch the module directory %# - outside builds are not reloaded", moduleDir );
        }

        // 의존 목록은 로드 **전에** 둔다 — 섀도 복사본이 의존 모듈의 import 를 그 의존의 지금 이름으로 바꿀 때 읽는다.
        _dependencyGraph.setDependsOn( moduleName, listDependsOn );
        ModuleRecord& moduleRecord       = getOrCreateRecord( moduleName );
        moduleRecord._moduleName         = moduleName;
        moduleRecord._tempModulePath     = "";
        moduleRecord._originalModulePath = ModuleLibraryUtil::findModuleLibraryPath( moduleName );
        if ( loadShadowCopyModule( moduleRecord ) )
        {
            if ( verifyModuleBindings() == false )
                markGraphBroken( "a module is bound to a stale dependency image after registration" );
            return true;
        }

        // commit 은 새 핸들을 컨텍스트에 넣은 뒤 onAfterReload 가 그래프를 막아도 실패를 돌려준다. 그대로 지우면 라이브러리 · 등록한 타입 ·
        // 섀도 파일이 프로세스 끝까지 남으므로, 등록을 거두기 전에 평소처럼 내린다.
        if ( moduleRecord._pLibraryModule != nullptr )
            unloadModule( moduleRecord );
        _mapModule.erase( string( moduleName ) );
        _dependencyGraph.removeModule( moduleName );
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

        ModuleRecord& context = iter->second;
        SW_LOG_INFO( "Manual Live Reload queued for %# (debounce %#ms)...",
                     moduleName, kMtimeDebounceMs );
        context._debounceMtime = FileUtil::getFileTimestamp( context._originalModulePath );
        context._debounceTimer.resetTimer();
        context._debounceTimer.startTimer();
        context._bMtimeDebouncing = true;
        context._bForceReload     = true;
    }

    void LiveReloadManager::notifyBuildStarted()
    {
        ScopedLock<mutex> lock{ _buildMutex };
        _buildNotice._bInProgress = true;
    }

    void LiveReloadManager::notifyBuildFinished( bool bSucceeded, string_view targetName )
    {
        ScopedLock<mutex> lock{ _buildMutex };
        _buildNotice._bInProgress      = false;
        _buildNotice._bFinishedPending = true;
        _buildNotice._bSucceeded       = bSucceeded;
        _buildNotice._targetName       = string( targetName );
    }

    bool LiveReloadManager::consumeBuildNotice()
    {
        BuildNotice notice;
        {
            ScopedLock<mutex> lock{ _buildMutex };
            notice                         = _buildNotice;
            _buildNotice._bFinishedPending = false;
        }
        if ( notice._bFinishedPending )
        {
            if ( notice._bSucceeded == false )
            {
                // 실패한 빌드가 다시 쓴 모듈은 반쯤 완성된 집합일 수 있으므로 로드하지 않는다. 파일은 그대로라 다음에 성공한 빌드의 변경과 함께 로드된다.
                for ( auto& [name, context] : _mapModule )
                {
                    if ( context._bMtimeDebouncing || context._bForceReload )
                        SW_LOG_WARNING( "Build failed - %# is not reloaded from its partial output", context._moduleName );
                    context._bMtimeDebouncing = false;
                    context._bForceReload     = false;
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
            {
                const FileWatchPollResult poll = _fileWatcher->pollEvents( listEvent );
                if ( poll._bWatchLost )
                    SW_LOG_ERROR( "Module directory watching stopped - outside builds are not reloaded until the process restarts" );
            }
        }

        // 바깥 빌드는 모듈 DLL 의 쓰기가 아니라 성공 스탬프로 본다 — 연쇄 빌드가 모듈을 하나씩 다시 쓰는 도중의 반쯤 링크된 집합을 올리지 않는다.
        // 이벤트는 깨우는 데만 쓰고 스탬프 자체를 비교한다(넘쳐서 합성 리스캔이 와도, 이름이 바뀌며 써도 같다).
        if ( listEvent.empty() == false )
        {
            FileStamp  currentStamp;
            const bool bStampChanged = FileUtil::getFileStamp( _buildStampPath, currentStamp ) && currentStamp != _seenBuildStamp;
            if ( bStampChanged )
            {
                _seenBuildStamp     = currentStamp;
                _bBuildStampPending = SW_TRUE;
            }
        }

        // 이 프로세스가 시작한 빌드가 도는 동안은 변경을 모아 두기만 한다. 연쇄 빌드의 반쯤 완성된 집합을 로드하지 않기 위해서다(`notifyBuildStarted`).
        const bool bBuildInProgress = consumeBuildNotice();
        if ( _bBuildStampPending == SW_TRUE && bBuildInProgress == false )
        {
            _bBuildStampPending = SW_FALSE;
            for ( auto& [name, context] : _mapModule )
            {
                if ( FileUtil::getFileTimestamp( context._originalModulePath ) <= context._loadedSourceMtime )
                    continue;
                SW_LOG_TRACE( "Module build stamp changed — queuing reload for %#", context._moduleName );
                context._bPendingReload = true;
            }
        }
        for ( auto& [name, context] : _mapModule )
        {
            if ( context._bMtimeDebouncing == false || bBuildInProgress )
                continue;

            context._debounceTimer.updateTimer();
            const float32 elapsedSec = context._debounceTimer.getTotalTime();
            if ( elapsedSec < static_cast<float32>( kMtimeDebounceMs ) / constant::kMillisecondsPerSecond )
                continue;

            const uint64 sourceMtime = FileUtil::getFileTimestamp( context._originalModulePath );
            if ( sourceMtime != context._debounceMtime )
            {
                context._debounceMtime = sourceMtime;
                context._debounceTimer.resetTimer();
                context._debounceTimer.startTimer();
                continue;
            }

            context._bMtimeDebouncing = false;
            const bool bForce         = context._bForceReload.exchange( false );
            if ( bForce || sourceMtime > context._loadedSourceMtime )
            {
                SW_LOG_TRACE( "FileWatcher settled — queuing reload for %#", context._moduleName );
                context._bPendingReload = true;
            }
        }

        if ( _bReloadGraphBroken == SW_TRUE )
            return;

        vector<string> listPendingRoot;
        BLOCK( "Collect Pending Reloads" )
        {
            for ( auto& [name, context] : _mapModule )
            {
                if ( context._bPendingReload )
                {
                    context._bPendingReload = false;
                    listPendingRoot.push_back( context._moduleName );
                }
            }
        }

        BLOCK( "Reload Cascade" )
        {
            vector<string> listSubgraph;
            for ( const string& root : listPendingRoot )
            {
                _dependencyGraph.collectDependentClosure( root, listSubgraph );
            }
            if ( listSubgraph.empty() == false )
                reloadCascade( listSubgraph );
        }
    }

    Subscription LiveReloadManager::setOnBeforeReload( string_view moduleName, OnBeforeReloadDelegate delegate )
    {
        getOrCreateRecord( moduleName )._onBeforeReload = std::move( delegate );
        return issueSlotSubscription( moduleName, CallbackSlot::BeforeReload );
    }

    Subscription LiveReloadManager::setOnAfterReload( string_view moduleName, OnAfterReloadDelegate delegate )
    {
        getOrCreateRecord( moduleName )._onAfterReload = std::move( delegate );
        return issueSlotSubscription( moduleName, CallbackSlot::AfterReload );
    }

    Subscription LiveReloadManager::setOnReloadFault( string_view moduleName, OnReloadFaultDelegate delegate )
    {
        getOrCreateRecord( moduleName )._onReloadFault = std::move( delegate );
        return issueSlotSubscription( moduleName, CallbackSlot::ReloadFault );
    }

    Subscription LiveReloadManager::setOnValidateLibrary( string_view moduleName, OnValidateLibraryDelegate delegate )
    {
        getOrCreateRecord( moduleName )._onValidateLibrary = std::move( delegate );
        return issueSlotSubscription( moduleName, CallbackSlot::ValidateLibrary );
    }

    Subscription LiveReloadManager::addOnBeforeCommitBatch( OnBeforeCommitBatchDelegate delegate )
    {
        const uint64 id = _nextCallbackID++;
        _listBeforeCommitBatch.push_back( { id, std::move( delegate ) } );
        _listCallbackRegistration.push_back( { id, string{}, CallbackSlot::BeforeCommitBatch } );
        return issueSubscription( id );
    }

    Subscription LiveReloadManager::setDrainWorkers( DrainWorkersDelegate delegate )
    {
        _drainWorkers = std::move( delegate );
        return issueSlotSubscription( string_view{}, CallbackSlot::DrainWorkers );
    }

    Subscription LiveReloadManager::issueSlotSubscription( string_view moduleName, CallbackSlot slot )
    {
        // 같은 칸을 다시 걸면 앞 등록은 표에서 빠진다 — 앞 구독이 나중에 풀려도 새로 건 것을 지우지 않는다.
        for ( size_t index = 0; index < _listCallbackRegistration.size(); ++index )
        {
            const CallbackRegistration& registration = _listCallbackRegistration[index];
            if ( registration._slot == slot && registration._moduleName == moduleName )
            {
                _listCallbackRegistration.erase( _listCallbackRegistration.begin() + static_cast<ptrdiff_t>( index ) );
                break;
            }
        }
        const uint64 id = _nextCallbackID++;
        _listCallbackRegistration.push_back( { id, string( moduleName ), slot } );
        return issueSubscription( id );
    }

    void LiveReloadManager::releaseSubscription( uint64 id )
    {
        for ( size_t index = 0; index < _listCallbackRegistration.size(); ++index )
        {
            if ( _listCallbackRegistration[index]._id != id )
                continue;
            const CallbackRegistration registration = _listCallbackRegistration[index];
            _listCallbackRegistration.erase( _listCallbackRegistration.begin() + static_cast<ptrdiff_t>( index ) );
            clearSlot( registration );
            return;
        }
    }

    void LiveReloadManager::clearSlot( const CallbackRegistration& registration )
    {
        if ( registration._slot == CallbackSlot::BeforeCommitBatch )
        {
            for ( size_t index = 0; index < _listBeforeCommitBatch.size(); ++index )
            {
                if ( _listBeforeCommitBatch[index]._id == registration._id )
                {
                    _listBeforeCommitBatch.erase( _listBeforeCommitBatch.begin() + static_cast<ptrdiff_t>( index ) );
                    return;
                }
            }
            return;
        }
        if ( registration._slot == CallbackSlot::DrainWorkers )
        {
            _drainWorkers = {};
            return;
        }
        // 기록이 이미 지워졌으면(그래프 비우기) 비울 칸도 없다.
        const auto recordIt = _mapModule.find( registration._moduleName );
        if ( recordIt == _mapModule.end() )
            return;
        ModuleRecord& record = recordIt->second;
        switch ( registration._slot )
        {
            case CallbackSlot::BeforeReload:
            {
                record._onBeforeReload = {};
                break;
            }
            case CallbackSlot::AfterReload:
            {
                record._onAfterReload = {};
                break;
            }
            case CallbackSlot::ReloadFault:
            {
                record._onReloadFault = {};
                break;
            }
            case CallbackSlot::ValidateLibrary:
            {
                record._onValidateLibrary = {};
                break;
            }
            case CallbackSlot::BeforeCommitBatch:
            case CallbackSlot::DrainWorkers:
            {
                break;
            }
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
        for ( const auto& [moduleName, moduleRecord] : _mapModule )
        {
            if ( moduleRecord._pLibraryModule == nullptr )
                continue;
            for ( const string& dependencyName : _dependencyGraph.getDependsOn( moduleName ) )
            {
                // 이 매니저가 로드하지 않은 의존(Engine 처럼 처음부터 링크된 것)은 교체되지 않으므로 검사할 것이 없다.
                const auto dependencyIt = _mapModule.find( dependencyName );
                if ( dependencyIt == _mapModule.end() || dependencyIt->second._pLibraryModule == nullptr )
                    continue;

                const void*                   pActual{ nullptr };
                const void*                   pExpected{ nullptr };
                const ShadowLibraryNameState& dependencyLibraryName = dependencyIt->second._libraryName;
                const string                  dependencyFileName    = dependencyLibraryName._loaded.empty() ? ModuleLibraryUtil::formatSharedLibraryName( dependencyName ) : dependencyLibraryName._loaded;
                if ( LiveReloadManagerInternal::isBoundTo( moduleRecord._pLibraryModule, dependencyName, dependencyFileName, dependencyIt->second._pLibraryModule, pActual,
                                                           pExpected ) )
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

    LiveReloadManager::ModuleRecord& LiveReloadManager::getOrCreateRecord( string_view moduleName )
    {
        _dependencyGraph.ensureModule( moduleName );
        return _mapModule[string( moduleName )];
    }

    void LiveReloadManager::clearRecords()
    {
        // `clear()` 는 용량을 남긴다. 이 관리자는 엔진의 종료 누수 보고 뒤에 사라지므로, 기준선 뒤 모듈 등록이 키운 표를 여기서 놓는다.
        _mapModule.clear();
        _mapModule.shrink_to_fit();
        _dependencyGraph.clear();
    }

    void LiveReloadManager::populateShadowCopyParams( const ModuleRecord& context, ShadowCopyParams& outParams ) const
    {
        outParams._moduleName              = context._moduleName;
        outParams._originalModulePath      = context._originalModulePath;
        outParams._bReplacingLoadedLibrary = context._pLibraryModule != nullptr;

        // 이 매니저가 올리지 않은 의존(공용 GameFramework · Engine)은 넣지 않는다 — 원래 이름 그대로 이미 올라온 라이브러리에 묶인다.
        for ( const string& dependencyName : _dependencyGraph.getDependsOn( context._moduleName ) )
        {
            const auto dependencyIt = _mapModule.find( dependencyName );
            if ( dependencyIt != _mapModule.end() )
                outParams._listDependencyLibrary.push_back( ShadowCopyModuleLibrary{ dependencyIt->first, &dependencyIt->second._libraryName } );
        }

        outParams._listRegisteredLibrary.reserve( _mapModule.size() );
        for ( const auto& [registeredName, registeredRecord] : _mapModule )
        {
            outParams._listRegisteredLibrary.push_back( ShadowCopyModuleLibrary{ registeredName, &registeredRecord._libraryName } );
        }
    }

    bool LiveReloadManager::loadShadowCopyModule( ModuleRecord& context )
    {
        PreparedShadow prepared;
        if ( prepareShadowCopy( context, prepared ) == false )
            return false;
        if ( commitShadowCopy( context, prepared ) )
            return true;
        abortShadowCopy( context, prepared );
        return false;
    }

    bool LiveReloadManager::prepareShadowCopy( ModuleRecord& context, PreparedShadow& outShadow )
    {
        outShadow = {};
        BLOCK( "Check Original Module" )
        {
            if ( FileUtil::exists( context._originalModulePath ) == false )
            {
                SW_LOG_ERROR( "Original module not found: %#", context._originalModulePath.c_str() );
                return false;
            }
        }

        outShadow._sourceMtime = FileUtil::getFileTimestamp( context._originalModulePath );

        BLOCK( "Create Shadow Copy" )
        {
            // 원본을 대조하고 세대 이름을 붙여 복사본으로 쓴다. 로드하기 **전이라** 대조에 걸린 모듈의 정적 초기화는 돌지 않는다.
            ShadowCopyParams params;
            populateShadowCopyParams( context, params );
            params._sourceMtime = outShadow._sourceMtime;
            if ( ModuleShadowCopy::writeShadowCopy( params, context._libraryName, outShadow._tempPath ) == false )
                return false;
        }

        BLOCK( "Load Dynamic Library" )
        {
            // 불변 조건: engine::registerModuleTypes 가 로드할 때마다 전역 헤드를 nullptr 로 비우므로, 여기에 들어올 때 세 헤드는
            // 항상 nullptr 이다(연쇄 교체의 두 번째 모듈 이후도 마찬가지다). abort 는 이 스냅샷을 되돌리므로, 정상 상태에서는
            // nullptr 로 되돌리는 것이 올바른 결과다. (검증: SmokeTest Architecture.LiveReloadRegistrarContentLifecycle)
            outShadow._previousHeads = engine::peekGlobalRegistrarHeads();

            // 교체 라이브러리의 정적 등록자는 옛 라이브러리가 도는 동안 돈다 — 같은 이름의 등록(개발 명령 · 감시 값 · 시나리오 단계 · 에디터 등록)을 거절하지 않고 대체하게 한다.
            // 첫 로드는 진짜 중복(두 모듈이 같은 이름을 정의)이 있을 수 있으니 거절을 그대로 둔다.
            if ( context._pLibraryModule != nullptr )
            {
                RegistrationReloadScope reloadScope;
                outShadow._pHandle = ModuleLibraryUtil::loadDynamicLibrary( outShadow._tempPath );
            }
            else
            {
                outShadow._pHandle = ModuleLibraryUtil::loadDynamicLibrary( outShadow._tempPath );
            }
            if ( outShadow._pHandle == nullptr )
            {
                context._libraryName._current = context._libraryName._loaded;
                SW_LOG_ERROR( "Failed to load dynamic library (keeping old): %#", outShadow._tempPath.c_str() );
                ModuleShadowCopy::removeArtifacts( outShadow._tempPath );
                outShadow = {};
                return false;
            }

            outShadow._heads = engine::peekGlobalRegistrarHeads();
            engine::setGlobalRegistrarHeads( engine::ModuleRegistrarHeads{} );
        }

        BLOCK( "Validate New Image" )
        {
            // 옛 라이브러리가 아직 도는 동안 호스트가 새 라이브러리를 거절할 수 있는 지점이다. 거절은 적용 전 실패라 옛 라이브러리를 두고 그래프를 막지 않는다.
            // 새 라이브러리의 코드가 불리므로 onAfterReload 와 같이 지킨다.
            if ( context._onValidateLibrary.isBound() )
            {
                bool        bAccepted{ false };
                uint32      faultCode{ 0 };
                void* const pNewHandle = outShadow._pHandle;
                const bool  bCompleted = ModuleCallGuard::run(
                    SW_DELEGATE_LAMBDA( Delegate<void()>, [&context, &bAccepted, pNewHandle]()
                 {
                    bAccepted = context._onValidateLibrary( pNewHandle );
                } ),
                    faultCode );
                if ( bCompleted == false || bAccepted == false )
                {
                    const utf8* pOutcome = ( context._pLibraryModule != nullptr ) ? "keeping the old module" : "it is not loaded";
                    if ( bCompleted == false )
                        SW_LOG_ERROR( "Module %# faulted (code 0x%#) while the host checked it — %#", context._moduleName,
                                      Fmt( faultCode, Format( 8, Format::Padding::Zero ).hex() ), pOutcome );
                    else
                        SW_LOG_ERROR( "Module %# was rejected by the host before it took over — %#", context._moduleName, pOutcome );
                    abortShadowCopy( context, outShadow );
                    outShadow = {};
                    return false;
                }
            }
        }

        return true;
    }

    bool LiveReloadManager::commitShadowCopy( ModuleRecord& context, PreparedShadow& prepared )
    {
        if ( prepared._pHandle == nullptr )
            return false;

        const string previousTempModule = context._tempModulePath;
        void*        pPreviousHandle    = context._pLibraryModule;
        bool         bKeepPreviousLibrary{ false };

        BLOCK( "Swap Module Handles" )
        {
            // onBeforeReload 가 모듈 자원을 건드리기 전에 워커가 옛 라이브러리에서 빠져나와 있어야 한다.
            if ( pPreviousHandle != nullptr && drainTasksBeforeUnload() == false )
                return false;

            if ( context._onBeforeReload.isBound() && pPreviousHandle != nullptr )
                context._onBeforeReload();

            if ( pPreviousHandle != nullptr )
            {
                // onBefore 뒤에 남은 작업을 비운다. 이미 모듈을 내렸으면 교체를 계속하고, 제한 시간을 넘기면 그래프를 깨진 상태로 표시만 한다.
                drainTasksBeforeUnload();
                engine::unregisterModuleTypes( context._moduleName );
                bKeepPreviousLibrary = ModuleLibraryUtil::releaseLibraryCode( context._moduleName, pPreviousHandle ) == false;
            }

            context._pLibraryModule      = prepared._pHandle;
            context._tempModulePath      = prepared._tempPath;
            context._loadedSourceMtime   = prepared._sourceMtime;
            context._libraryName._loaded = context._libraryName._current;
            prepared._pHandle            = nullptr;
            prepared._tempPath.clear();

            engine::registerModuleTypes( context._moduleName, prepared._heads );
            prepared._heads = engine::ModuleRegistrarHeads{};

            invokeAfterReload( context );

            // 옛 라이브러리는 바로 언로드하지 않고 언로드를 미룬다. 그래프가 깨진 경우도 같다(핸들을 잃지 않는다).
            if ( pPreviousHandle != nullptr )
                _deferredUnloadQueue.deferUnload( context._moduleName, pPreviousHandle, previousTempModule, bKeepPreviousLibrary );
        }

        if ( _bReloadGraphBroken == SW_TRUE )
        {
            SW_LOG_ERROR( "Module %# committed but onAfter marked the graph broken",
                          context._moduleName );
            return false;
        }

        SW_LOG_INFO( "Module loaded (shadow: %#)", context._tempModulePath.c_str() );
        return true;
    }

    void LiveReloadManager::invokeAfterReload( ModuleRecord& context )
    {
        // 새 라이브러리의 코드가 처음 도는 지점이다. 여기서 죽으면 에디터째 종료되어 저장하지 않은 작업을 잃으므로 보호한다.
        if ( context._onAfterReload.isBound() == false )
            return;
        uint32     faultCode{ 0 };
        const bool bCompleted = ModuleCallGuard::run(
            SW_DELEGATE_LAMBDA( Delegate<void()>, [&context]()
        {
            context._onAfterReload( context._pLibraryModule );
        } ),
            faultCode );
        if ( bCompleted == false )
        {
            SW_LOG_ERROR( "Module %# faulted (code 0x%#) while starting after the reload — dropping what it handed out; save your work and restart",
                          context._moduleName, Fmt( faultCode, Format( 8, Format::Padding::Zero ).hex() ) );
            markGraphBroken( "a module faulted in onAfterReload" );
            if ( context._onReloadFault.isBound() )
                context._onReloadFault( faultCode );
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

    void LiveReloadManager::abortShadowCopy( ModuleRecord& context, PreparedShadow& prepared )
    {
        context._libraryName._current = context._libraryName._loaded;
        if ( prepared._pHandle != nullptr )
        {
            engine::setGlobalRegistrarHeads( prepared._previousHeads );
            if ( ModuleLibraryUtil::releaseLibraryCode( context._moduleName, prepared._pHandle ) )
                ModuleLibraryUtil::unloadDynamicLibrary( prepared._pHandle );
            prepared._pHandle = nullptr;
        }

        prepared._heads         = engine::ModuleRegistrarHeads{};
        prepared._previousHeads = engine::ModuleRegistrarHeads{};
        ModuleShadowCopy::removeArtifacts( prepared._tempPath );
        prepared._tempPath.clear();
        prepared._sourceMtime = 0;
    }

    void LiveReloadManager::unloadModule( ModuleRecord& context )
    {
        if ( context._onBeforeReload.isBound() && context._pLibraryModule != nullptr )
            context._onBeforeReload();

        if ( context._pLibraryModule != nullptr )
        {
            SW_LOG_INFO( "Unloading module %# (handle=%#)", context._moduleName.c_str(), context._pLibraryModule );
            drainTasksBeforeUnload();

            engine::unregisterModuleTypes( context._moduleName );
            if ( ModuleLibraryUtil::releaseLibraryCode( context._moduleName, context._pLibraryModule ) )
                ModuleLibraryUtil::unloadDynamicLibrary( context._pLibraryModule );
            context._pLibraryModule = nullptr;
        }

        ModuleShadowCopy::removeArtifacts( context._tempModulePath );
        context._tempModulePath.clear();
    }

    void LiveReloadManager::unloadSharedModules()
    {
        if ( _listSharedModule.empty() )
            return;
        // 서비스가 이미 내려갔으면 등록을 제 이름으로 해제할 곳이 없다 — 라이브러리를 두면 정적 등록이 남은 채로 프로세스가 끝난다(예전 동작).
        if ( engine::areEngineServicesBound() == false )
        {
            _listSharedModule.clear();
            return;
        }
        drainTasksBeforeUnload();
        for ( auto it = _listSharedModule.rbegin(); it != _listSharedModule.rend(); ++it )
        {
            SW_LOG_INFO( "Unloading shared module %# (handle=%#)", it->_name.c_str(), it->_pHandle );
            engine::unregisterModuleTypes( it->_name );
            if ( ModuleLibraryUtil::releaseLibraryCode( it->_name, it->_pHandle ) )
                ModuleLibraryUtil::unloadDynamicLibrary( it->_pHandle );
            it->_pHandle = nullptr;
        }
        _listSharedModule.clear();
    }

    bool LiveReloadManager::drainTasksBeforeUnload()
    {
        // App 경로에서는 _drainWorkers(= GameModuleManager::drainRenderWorkers)가 실제로 비우고, 아래 폴백은 헤드리스 · 테스트에서만
        // 돈다. 제한 시간 상수는 양쪽이 함께 쓴다.
        constexpr uint32 kDrainTimeoutMs = LiveReloadManager::kModuleDrainTimeoutMs;

        // onBeforeReload 는 모듈에서 시작된 작업을 멈춰야 한다. 실행 중인 태스크를 비워 콜백이 옛 라이브러리에 들어가지 못하게 한다.
        // clear() 는 부르지 않는다. 그러면 관계없는 GPUScene · 씬 로드 작업까지 버리고 onTaskFinished 정리를 건너뛴다.
        if ( engine::areEngineServicesBound() )
            engine::getSubsystem<SceneManager>().cancelPendingAsyncLoads();

        // 렌더 스레드는 TaskManager 밖에서 돈다. Present 훅이 모듈 코드를 실행 중일 수 있으므로 먼저 비운다.
        if ( _drainWorkers.isBound() )
        {
            _drainWorkers();
            if ( _bReloadGraphBroken == SW_TRUE )
                return false;
            return true;
        }

        // 엔진 전체 대기가 맞다 — 모듈 코드는 어떤 태스크든 가상 함수 · 델리게이트로 들어올 수 있어 낸 쪽으로 가를 수 없다(`GameModuleManager::drainRenderWorkers` 의 같은 주석).
        if ( engine::areEngineServicesBound() )
        {
            TaskManager& taskManager = engine::getSubsystem<TaskManager>();
            if ( taskManager.waitAll( kDrainTimeoutMs ) == false )
            {
                SW_LOG_ERROR( "Task drain timeout (%# ms) before module unload (%# task(s) still running, first unfinished: '%#')", kDrainTimeoutMs, taskManager.getActiveTaskCount(),
                              taskManager.describeUnfinishedTask() );
                markGraphBroken( "task drain timeout before unload" );
                return false;
            }
        }
        return _bReloadGraphBroken == SW_FALSE;
    }

    bool LiveReloadManager::prepareCascade( const vector<string>& listOrder, vector<PreparedEntry>& outListPrepared )
    {
        outListPrepared.reserve( listOrder.size() );
        for ( const string& name : listOrder )
        {
            auto found = _mapModule.find( name );
            if ( found == _mapModule.end() )
                continue;
            PreparedEntry entry;
            entry._pRecord = &found->second;
            if ( prepareShadowCopy( *entry._pRecord, entry._shadow ) == false )
            {
                SW_LOG_ERROR( "Cascade prepare failed for %# — aborting, previous modules kept", name );
                return false;
            }
            outListPrepared.push_back( std::move( entry ) );
        }
        return true;
    }

    void LiveReloadManager::abortPrepared( vector<PreparedEntry>& inoutListPrepared, size_t firstIndex )
    {
        for ( size_t moduleIndex = firstIndex; moduleIndex < inoutListPrepared.size(); ++moduleIndex )
        {
            abortShadowCopy( *inoutListPrepared[moduleIndex]._pRecord, inoutListPrepared[moduleIndex]._shadow );
        }
    }

    bool LiveReloadManager::commitCascade( vector<PreparedEntry>& inoutListPrepared )
    {
        size_t committed{ 0 };
        for ( size_t moduleIndex = 0; moduleIndex < inoutListPrepared.size(); ++moduleIndex )
        {
            PreparedEntry& entry = inoutListPrepared[moduleIndex];
            if ( _bReloadGraphBroken == SW_TRUE )
            {
                SW_LOG_ERROR( "Cascade abort before commit of %# — graph already broken", entry._pRecord->_moduleName );
                abortPrepared( inoutListPrepared, moduleIndex );
                if ( committed > 0 )
                    markGraphBroken( "partial cascade commit" );
                return false;
            }

            if ( commitShadowCopy( *entry._pRecord, entry._shadow ) )
            {
                ++committed;
                continue;
            }

            SW_LOG_ERROR( "Cascade commit failed for %# — aborting remaining commits", entry._pRecord->_moduleName );
            abortPrepared( inoutListPrepared, moduleIndex );
            if ( committed > 0 || _bReloadGraphBroken == SW_FALSE )
                markGraphBroken( "partial cascade commit" );
            return false;
        }
        return true;
    }

    void LiveReloadManager::reloadCascade( const vector<string>& listSubgraphName )
    {
        vector<string> listOrder;
        if ( _dependencyGraph.sortByDependency( listSubgraphName, listOrder ) == false )
        {
            markGraphBroken( "dependency cycle" );
            return;
        }
        if ( listOrder.empty() )
            return;

        SW_LOG_TRACE( "Cascade reload count=%#", static_cast<uint32>( listOrder.size() ) );

        vector<PreparedEntry> listPrepared;
        if ( prepareCascade( listOrder, listPrepared ) == false )
        {
            abortPrepared( listPrepared, 0 );
            return;
        }

        // 배치 직전 콜백의 거절은 적용 전 실패다. 아무것도 언로드하지 않았으니 새 라이브러리만 버리고 옛 라이브러리로 계속 돈다.
        // 나중에 더한 것(바깥 층)부터 부른다. 콜백이 목록을 바꿔도 이번 차례는 지금 목록대로 돈다.
        const vector<BeforeCommitBatchEntry> listBeforeCommit = _listBeforeCommitBatch;
        bool                                 bCommitAccepted  = true;
        for ( auto it = listBeforeCommit.rbegin(); it != listBeforeCommit.rend() && bCommitAccepted; ++it )
        {
            bCommitAccepted = it->_delegate.isBound() == false || it->_delegate( listOrder );
        }
        if ( bCommitAccepted == false )
        {
            SW_LOG_ERROR( "Cascade reload refused before the first commit — keeping the previous modules" );
            abortPrepared( listPrepared, 0 );
            return;
        }

        _deferredUnloadQueue.beginBatch();
        if ( commitCascade( listPrepared ) == false )
            return;

        // 새 라이브러리가 모두 로드됐다. 의존 모듈이 옛 라이브러리에 바인딩돼 있으면, 옛 라이브러리를 언로드한 지금 거기로 점프하는 코드가 죽는다. 여기서 막는다.
        if ( verifyModuleBindings() == false )
        {
            markGraphBroken( "a module is bound to a stale dependency image after the cascade" );
            return;
        }
        if ( _bReloadGraphBroken == SW_FALSE ) // 리로드 직후 콜백의 결함도 그래프를 막는다 — 그런 리로드는 성공으로 세지 않는다
            ++_completedReloadCount;
    }

    LiveReloadManager::ModuleRecord::ModuleRecord() noexcept
        : _onBeforeReload{}
        , _onAfterReload{}
        , _onReloadFault{}
        , _onValidateLibrary{}
        , _moduleName{}
        , _originalModulePath{}
        , _tempModulePath{}
        , _libraryName{}
        , _pLibraryModule{ nullptr }
        , _loadedSourceMtime{ 0 }
        , _debounceMtime{ 0 }
        , _debounceTimer{}
        , _bPendingReload{ false }
        , _bMtimeDebouncing{ false }
        , _bForceReload{ false }
    {
    }
} // namespace sw
