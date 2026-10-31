#include "pch.h"

#include "ModuleManager/Dev/ModuleReloadDriver.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/formatString.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Module/ModuleCatalog.h"

#include "ModuleManager/LiveReload/LiveReloadManager.h"
#include "ModuleManager/ModuleCatalogLoader.h"
#include "ModuleManager/ModuleLoaderSubsystem.h"

// 개발 구성에만 있다(`ModuleManager/Dev/` — 배포는 폴더째 짓지 않는다). 호스트는 개발 계층(`IHostDevLayer`)으로만 닿는다.
namespace sw
{
    namespace
    {
        /** @brief 개발 명령 `module.reload` 와 감시 값 `Module.*` 의 처리 함수입니다. 리로드 구동기는 호스트 범위에 걸려 있어 엔진 범위에서 올라가 찾습니다. */
        struct ModuleReloadDriverInternal
        {
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
                utf8                arrReply[constant::kMaxBuffer256];
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
    SW_AUTOMATION_WATCH( moduleReloadCount, "Module.ReloadCount", "Hot reload cascades that finished successfully since the process started",
                         &ModuleReloadDriverInternal::readReloadCount );
    SW_AUTOMATION_WATCH( moduleReloadPending, "Module.ReloadPending", "1 while a reload queued by module.reload has not finished (0 once it finished, or the graph broke)",
                         &ModuleReloadDriverInternal::readReloadPending );
    SW_AUTOMATION_WATCH( moduleGraphBroken, "Module.GraphBroken", "1 when a failed reload blocked the module graph (restart needed), else 0",
                         &ModuleReloadDriverInternal::readGraphBroken );
    SW_AUTOMATION_WATCH( moduleBindingsOk, "Module.BindingsOk", "1 when every loaded module is bound to the dependency images the manager holds now, else 0",
                         &ModuleReloadDriverInternal::readBindingsOk );
} // namespace sw

namespace sw
{
    struct ModuleReloadDriver::State
    {
        unique_ptr<LiveReloadManager> _liveReloadManager;
        /** @brief 자신을 건 호스트 범위입니다(소유하지 않는다). 씬 뒤 훅이 여기서 자신을 풉니다. */
        SubsystemScope* _pHostScope = nullptr;
        /** @brief `queueReload` 가 예약한 리로드가 끝났다고 볼 완료 수입니다(예약 때의 완료 수 + 1). 0 이면 예약한 적이 없다. */
        uint32 _reloadTargetCount = 0;
        /** @brief 모듈 라이브러리 창구에 건 "씬이 사라졌다" 훅의 구독입니다(창구가 먼저 사라지면 무효가 된다). */
        Subscription _scenesReleasedHook;
    };

    ModuleReloadDriver::ModuleReloadDriver()
        : _pState{ make_unique<State>() }
    {
    }

    ModuleReloadDriver::~ModuleReloadDriver() = default;

    bool ModuleReloadDriver::initialize( uint8 targetMask, SubsystemScope& hostScope, ModuleLoaderSubsystem& moduleLibraryHost, ModuleCatalog& outCatalog,
                                         ModuleResolution& outResolution )
    {
        // 무엇을 로드할지는 모듈 매니페스트가 정한다(App 은 빌드가 담은 모든 대상, 전용 서버는 Server 대상).
        if ( ModuleCatalogLoader::loadAndResolve( targetMask, outCatalog, outResolution ) == false )
            return false;
        moduleLibraryHost.setResolution( outResolution );

        _pState->_liveReloadManager = make_unique<LiveReloadManager>();
        _pState->_pHostScope        = &hostScope;
        const bool bBound           = hostScope.bind( this ); // 개발 명령 `module.reload` · 감시 값 `Module.*` 가 엔진 범위에서 올라가 찾는다
        SW_ASSERT( bBound );
        (void)bBound;
        // 모듈 DLL 을 언로드할 수 있는 곳은 "씬은 사라졌고 서비스는 아직 있는" 좁은 구간뿐이다. 호스트 범위의 모듈 라이브러리 창구가 엔진 종료 안에서 그 자리를 부른다.
        _pState->_scenesReleasedHook = moduleLibraryHost.bindScenesReleasedHook( SW_DELEGATE_LAMBDA( Delegate<void()>, [this]()
        {
            if ( _pState->_pHostScope != nullptr )
                _pState->_pHostScope->unbind<ModuleReloadDriver>();
            _pState->_pHostScope = nullptr;
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
        if ( pManager == nullptr || pManager->getModuleHandle( pModuleName ) == nullptr )
            return false;
        _pState->_reloadTargetCount = pManager->getCompletedReloadCount() + 1;
        pManager->triggerReload( pModuleName );
        return true;
    }

    bool ModuleReloadDriver::isReloadPending() const
    {
        const LiveReloadManager* const pManager = _pState->_liveReloadManager.get();
        if ( pManager == nullptr || _pState->_reloadTargetCount == 0 )
            return false;
        return pManager->getCompletedReloadCount() < _pState->_reloadTargetCount && pManager->isGraphBroken() == false;
    }
} // namespace sw
