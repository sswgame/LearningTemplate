#include "pch.h"

#include "DevTools/ModuleReload/ModuleReloadDriver.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/String/formatString.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "DevTools/ModuleReload/LiveReload/LiveReloadManager.h"

#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/Commandlet.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Input/DebugKeyActions.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Module/ModuleCatalog.h"

#include "ModuleAPI/Service/IModuleCompiler.h"

#include "ModuleManager/ModuleCatalogLoader.h"
#include "ModuleManager/ModuleLoaderSubsystem.h"

#include "sw/config/ConfigConstants.h"

// 개발 도구(DevTools.dll)에만 있다 — 배포는 DevTools 를 짓지 않는다. 호스트는 개발 계층(`IHostDevLayer`)으로만 닿는다.
namespace sw
{
    namespace
    {
        /** @brief 개발 명령 `module.reload` 와 감시 값 `Module.*` 의 처리 함수입니다. 리로드 구동기는 호스트 범위에 걸려 있어 엔진 범위에서 올라가 찾습니다. */
        struct ModuleReloadDriverInternal
        {
            /** @brief 에디터 모듈(EditorModule) 핫 리로드 단축키의 액션 이름(`default.input.xml` 의 Debug 레이어). 에디터를 올리는 App 만 부른다(배포본엔 리로드가 없다). */
            static constexpr const utf8* kReloadEditorAction = "ReloadEditor";

            /** @brief 매니저가 살아 있는 구동기입니다. 범위에 없거나(엔진 밖) 씬 뒤 훅이 매니저를 내렸으면 nullptr 입니다. */
            static const LiveReloadManager* findManager()
            {
                const ModuleReloadDriver* pDriver = engine::findSubsystem<ModuleReloadDriver>();
                return pDriver != nullptr ? pDriver->getLiveReloadManager() : nullptr;
            }

            /** @brief `module.reload <모듈>` — 그 모듈과 그것에 의존하는 모듈의 리로드를 예약합니다(리로드는 이후 프레임에서 끝납니다. 끝은 `Module.ReloadPending` 으로 봅니다). */
            static bool runReloadModule( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                ModuleReloadDriver* pDriver = engine::findSubsystem<ModuleReloadDriver>();
                const bool          bQueued = pDriver != nullptr && pDriver->queueReload( listArgument[0].c_str() );
                utf8                arrReply[constant::buffer::kMax256];
                formatstring( arrReply, sizeof( arrReply ), bQueued ? "reload of %# queued" : "no loaded module '%#'", listArgument[0].c_str() );
                outReply = arrReply;
                return bQueued;
            }

            [[nodiscard]] static bool readReloadCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const LiveReloadManager* pManager = findManager();
                if ( pManager == nullptr )
                    return false;
                outValue = static_cast<float64>( pManager->getCompletedReloadCount() );
                return true;
            }

            /** @brief 감시 값 `Module.ReloadPending` — `module.reload` 로 예약한 리로드가 아직 끝나지 않았으면 1 입니다(`WaitUntil … equals="0"` 으로 기다린다). */
            [[nodiscard]] static bool readReloadPending( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ModuleReloadDriver* pDriver = engine::findSubsystem<ModuleReloadDriver>();
                if ( pDriver == nullptr || pDriver->getLiveReloadManager() == nullptr )
                    return false;
                outValue = pDriver->isReloadPending() ? 1.0 : 0.0;
                return true;
            }

            /** @brief 예약한 리로드가 끝나지 않은 동안 시나리오 시계를 잡는다 — 리로드는 벽시계 디바운스(300 ms)와 빌드 · 복사를 거쳐 끝나 프레임 수로는 기다릴 수 없다(빠른 Release 는 600 프레임이 300 ms 안에 지나갔다). */
            static AutomationReadiness readReloadReadiness()
            {
                const ModuleReloadDriver* pDriver  = engine::findSubsystem<ModuleReloadDriver>();
                const bool                bPending = pDriver != nullptr && pDriver->getLiveReloadManager() != nullptr && pDriver->isReloadPending();
                return bPending ? AutomationReadiness::waiting( 0, "a queued module reload has not finished" ) : AutomationReadiness::ready();
            }

            [[nodiscard]] static bool readGraphBroken( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const LiveReloadManager* pManager = findManager();
                if ( pManager == nullptr )
                    return false;
                outValue = pManager->isGraphBroken() ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readBindingsOk( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const LiveReloadManager* pManager = findManager();
                if ( pManager == nullptr )
                    return false;
                outValue = pManager->verifyModuleBindings() ? 1.0 : 0.0;
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( ReloadModule, "module.reload", "module.reload <module>", "Queue a hot reload of a loaded module and the modules that depend on it (e.g. SWGame)",
                    &ModuleReloadDriverInternal::runReloadModule );
    // 활동 수 없음(둘 다): 리로드는 원본이 조용해지기(디바운스)를 기다린 뒤 `update` 한 번에 섀도 복사 · 로드 · 재생성을 동기로 끝낸다 — 프레임 사이에 보일 중간 진척이 없다.
    SW_AUTOMATION_WALL_WATCH( moduleReloadCount, "Module.ReloadCount", "Hot reload cascades that finished successfully since the process started",
                              &ModuleReloadDriverInternal::readReloadCount, nullptr );
    SW_AUTOMATION_WALL_WATCH( moduleReloadPending, "Module.ReloadPending", "1 while a reload queued by module.reload has not finished (0 once it finished, or the graph broke)",
                              &ModuleReloadDriverInternal::readReloadPending, nullptr );
    SW_AUTOMATION_CLOCK_HOLDER( moduleReloadPending, "ModuleReload", &ModuleReloadDriverInternal::readReloadReadiness );
    SW_AUTOMATION_WATCH( moduleGraphBroken, "Module.GraphBroken", "1 when a failed reload blocked the module graph (restart needed), else 0",
                         &ModuleReloadDriverInternal::readGraphBroken );
    SW_AUTOMATION_WATCH( moduleBindingsOk, "Module.BindingsOk", "1 when every loaded module is bound to the dependency images the manager holds now, else 0",
                         &ModuleReloadDriverInternal::readBindingsOk );
} // namespace sw

namespace sw
{
    namespace
    {
        /** @brief `ModuleReloadDriver` 가 예약한 일의 종류입니다(교체만 · 다시 지은 뒤 교체). */
        enum class ReloadRequestKind : uint8
        {
            Reload,  ///< `queueReload` — 이미 지은 이미지를 갈아 끼운다
            Rebuild, ///< `queueRebuild` — 다시 짓고, 빌드가 성공하면 갈아 끼운다
        };
    } // namespace

    struct ModuleReloadDriver::State
    {
        unique_ptr<LiveReloadManager> _liveReloadManager;
        /** @brief 자신을 건 호스트 범위입니다(소유하지 않는다). 씬 뒤 훅이 여기서 자신을 풉니다. */
        SubsystemScope* _pHostScope = nullptr;
        /** @brief `queueReload` 가 예약한 리로드가 끝났다고 볼 완료 수입니다(예약 때의 완료 수 + 1). 0 이면 예약한 적이 없다. */
        uint32 _reloadTargetCount = 0;
        /** @brief 모듈 라이브러리 창구에 건 "씬이 사라졌다" 훅의 구독입니다(창구가 먼저 사라지면 무효가 된다). */
        Subscription _scenesReleasedHook;
        /** @brief `queueRebuild` 가 처음 부를 때 매니저에게서 받는 컴파일러입니다(빌드 시작 · 끝을 매니저에 알린다). 매니저보다 먼저 놓는다(소멸이 도는 빌드를 기다린다). */
        unique_ptr<IModuleCompiler> _rebuildCompiler;
        /** @brief 마지막 예약의 종류입니다 — 다시 짓기(`queueRebuild`)면 컴파일러의 빌드 상태가 끝을 정한다. */
        ReloadRequestKind _requestKind = ReloadRequestKind::Reload;
    };

    ModuleReloadDriver::ModuleReloadDriver()
        : _pState{ make_unique<State>() }
    {
    }

    ModuleReloadDriver::~ModuleReloadDriver() = default;

    bool ModuleReloadDriver::initialize( uint8 targetMask, SubsystemScope& hostScope, ModuleLoaderSubsystem& moduleLibraryHost, FileWatchService& fileWatch, ModuleCatalog& outCatalog,
                                         ModuleResolution& outResolution )
    {
        // 무엇을 로드할지는 모듈 매니페스트가 정한다(App 은 빌드가 담은 모든 대상, 전용 서버는 Server 대상).
        if ( ModuleCatalogLoader::loadAndResolve( targetMask, outCatalog, outResolution ) == false )
            return false;
        const CommandLineManager& commandLine = engine::getSubsystem<CommandLineManager>();
        // 테스트 자식(자식 역할 · 격리 케이스)은 그 케이스가 등록된 모듈과 그 의존만 연다 — 엔진 · 개발 도구의 케이스면 하나도 열지 않는다.
        string childModule;
        if ( commandLine.getArgument( CommandLineArgument::TEST_CHILD_MODULE, childModule ) && childModule.empty() == false )
            outResolution.keepOnly( outCatalog, childModule );
        moduleLibraryHost.setResolution( outResolution );

        // 헤드리스 작업은 프레임을 돌지 않아 리로드할 자리가 없다 — 모듈을 복사 없이 바로 연다.
        const ModuleOpenMode openMode = CommandletRegistration::isAnyRequested( commandLine ) ? ModuleOpenMode::Direct : ModuleOpenMode::ShadowCopy;
        _pState->_liveReloadManager   = make_unique<LiveReloadManager>( fileWatch, openMode );
        _pState->_pHostScope          = &hostScope;
        const bool bBound             = hostScope.bind( this ); // 개발 명령 `module.reload` · 감시 값 `Module.*` 가 엔진 범위에서 올라가 찾는다
        SW_ASSERT( bBound );
        (void)bBound;
        // 모듈 DLL 을 언로드할 수 있는 곳은 "씬은 사라졌고 서비스는 아직 있는" 좁은 구간뿐이다. 호스트 범위의 모듈 라이브러리 창구가 엔진 종료 안에서 그 자리를 부른다.
        _pState->_scenesReleasedHook = moduleLibraryHost.bindScenesReleasedHook( SW_DELEGATE_LAMBDA( Delegate<void()>, [this]()
        {
            if ( _pState->_pHostScope != nullptr )
                _pState->_pHostScope->unbind<ModuleReloadDriver>();
            _pState->_pHostScope = nullptr;
            _pState->_rebuildCompiler.reset(); // 매니저에 빌드 끝을 알리는 컴파일러를 매니저보다 먼저 내린다
            if ( _pState->_liveReloadManager != nullptr )
                _pState->_liveReloadManager->shutdown();
        } ) );
        return true;
    }

    Subscription ModuleReloadDriver::addOnBeforeReloadBatch( BeforeReloadBatchDelegate onBeforeBatch )
    {
        if ( _pState->_liveReloadManager == nullptr )
            return Subscription{};
        return _pState->_liveReloadManager->addOnBeforeCommitBatch( std::move( onBeforeBatch ) );
    }

    void ModuleReloadDriver::requestReload( const utf8* pModuleName )
    {
        if ( _pState->_liveReloadManager != nullptr )
            _pState->_liveReloadManager->triggerReload( pModuleName );
    }

    void ModuleReloadDriver::pollReloadActions( const DebugKeyActions& debugKeys, bool bEnableEditor )
    {
        // 디버그 키는 개발 계층(`HostDevLayer::pollShellActions`)이 이번 프레임 것으로 갱신했다. 모듈을 다시 로드하는 장치는 호스트에 있다. 디버그 키 맵에는 "이 액션이 눌렸나"만 묻는다.
        if ( debugKeys.wasActionTriggered( hashed_string( InputMapDefaults::kReloadGameAction ) ) )
        {
            requestReload( config::kTargetGameModule );
            SW_LOG_INFO_ID( "App.ForceSWGameReload", "%#: force SWGame reload", InputMapDefaults::kReloadGameAction );
        }
        if ( bEnableEditor && debugKeys.wasActionTriggered( hashed_string( ModuleReloadDriverInternal::kReloadEditorAction ) ) )
        {
            requestReload( config::kTargetEditorModule );
            SW_LOG_INFO_ID( "App.ForceEditorModuleReload", "%#: force EditorModule reload", ModuleReloadDriverInternal::kReloadEditorAction );
        }
    }

    void ModuleReloadDriver::update()
    {
        if ( _pState->_liveReloadManager != nullptr )
            _pState->_liveReloadManager->update();
    }

    LiveReloadManager* ModuleReloadDriver::getLiveReloadManager() const
    {
        return _pState->_liveReloadManager.get();
    }

    bool ModuleReloadDriver::queueReload( const utf8* pModuleName )
    {
        LiveReloadManager* const pManager = _pState->_liveReloadManager.get();
        if ( pManager == nullptr || pManager->getModuleHandle( pModuleName ).isValid() == false )
            return false;
        _pState->_reloadTargetCount = pManager->getCompletedReloadCount() + 1;
        _pState->_requestKind       = ReloadRequestKind::Reload;
        pManager->triggerReload( pModuleName );
        return true;
    }

    bool ModuleReloadDriver::queueRebuild( const utf8* pModuleName )
    {
        LiveReloadManager* const pManager = _pState->_liveReloadManager.get();
        if ( pManager == nullptr || pManager->getModuleHandle( pModuleName ).isValid() == false )
            return false;
        if ( _pState->_rebuildCompiler == nullptr )
            _pState->_rebuildCompiler = pManager->createModuleCompiler();
        // 빌드 성공은 다음 `update` 에서 교체로 이어진다(매니저가 빌드 끝을 받아 그 모듈을 다시 올린다) — 끝을 볼 완료 수는 빌드를 시작한 때의 수 + 1 이다.
        const uint32 targetCount = pManager->getCompletedReloadCount() + 1;
        if ( _pState->_rebuildCompiler->compileModule( pModuleName ) == false )
            return false; // 이미 빌드 중이다 — 예약을 바꾸지 않는다
        _pState->_reloadTargetCount = targetCount;
        _pState->_requestKind       = ReloadRequestKind::Rebuild;
        return true;
    }

    bool ModuleReloadDriver::isReloadPending() const
    {
        const LiveReloadManager* const pManager = _pState->_liveReloadManager.get();
        if ( pManager == nullptr || _pState->_reloadTargetCount == 0 )
            return false;
        if ( _pState->_requestKind == ReloadRequestKind::Rebuild && _pState->_rebuildCompiler != nullptr )
        {
            if ( _pState->_rebuildCompiler->isCompiling() )
                return true;
            if ( _pState->_rebuildCompiler->getBuildState() != BuildState::Success )
                return false; // 다시 짓기가 실패했다(또는 취소) — 교체는 없고 옛 이미지로 돈다
        }
        return pManager->getCompletedReloadCount() < _pState->_reloadTargetCount && pManager->isGraphBroken() == false;
    }
} // namespace sw
