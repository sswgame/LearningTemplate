#include "pch.h"

#include "ModuleManager/ModuleReloadDriver.h"

#include "Core/Subsystem/SubsystemScope.h"

#include "ModuleManager/ModuleLoaderSubsystem.h"

#if !defined( SW_SHIPPING )
    #include "ModuleManager/LiveReloadManager.h"

    #include "Core/Console/DevCommandRegistry.h"
    #include "Core/Container/formatString.h"
    #include "Core/Container/string.h"
    #include "Core/Container/vector.h"

    #include "Engine/Automation/Watch/AutomationWatch.h"
    #include "Engine/Common/EngineServices.h"
#endif

#if !defined( SW_SHIPPING )
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
#endif

namespace sw
{
    ModuleReloadDriver::ModuleReloadDriver()
#if !defined( SW_SHIPPING )
        : _liveReloadManager{ nullptr }
        , _pHostScope{ nullptr }
        , _reloadTargetCount{ 0 }
        , _scenesReleasedHook{}
#endif
    {
    }

    ModuleReloadDriver::~ModuleReloadDriver() = default;

    void ModuleReloadDriver::initialize( [[maybe_unused]] SubsystemScope& hostScope, [[maybe_unused]] ModuleLoaderSubsystem& moduleLibraryHost )
    {
#if !defined( SW_SHIPPING )
        _liveReloadManager = make_unique<LiveReloadManager>();
        _pHostScope        = &hostScope;
        const bool bBound  = hostScope.bind( this ); // 개발 명령 `module.reload` · 감시 값 `Module.*` 가 엔진 범위에서 올라가 찾는다
        SW_ASSERT( bBound );
        (void)bBound;
        // 모듈 DLL 을 언로드할 수 있는 곳은 "씬은 사라졌고 서비스는 아직 있는" 좁은 구간뿐이다. 호스트 범위의 모듈 라이브러리 창구가 엔진 종료 안에서 그 자리를 부른다.
        _scenesReleasedHook = moduleLibraryHost.bindScenesReleasedHook( SW_DELEGATE_LAMBDA( Delegate<void()>, [this]()
        {
            if ( _pHostScope != nullptr )
                _pHostScope->unbind<ModuleReloadDriver>();
            _pHostScope = nullptr;
            if ( _liveReloadManager != nullptr )
                _liveReloadManager->shutdown();
        } ) );
#endif
    }

    void ModuleReloadDriver::requestReload( [[maybe_unused]] const utf8* pModuleName )
    {
#if !defined( SW_SHIPPING )
        if ( _liveReloadManager != nullptr )
            _liveReloadManager->triggerReload( pModuleName );
#endif
    }

    void ModuleReloadDriver::update()
    {
#if !defined( SW_SHIPPING )
        if ( _liveReloadManager != nullptr )
            _liveReloadManager->update();
#endif
    }

    LiveReloadManager* ModuleReloadDriver::getLiveReloadManager() const
    {
#if defined( SW_SHIPPING )
        return nullptr;
#else
        return _liveReloadManager.get();
#endif
    }

    bool ModuleReloadDriver::queueReload( [[maybe_unused]] const utf8* pModuleName )
    {
#if defined( SW_SHIPPING )
        return false;
#else
        if ( _liveReloadManager == nullptr || _liveReloadManager->getModuleHandle( pModuleName ) == nullptr )
            return false;
        _reloadTargetCount = _liveReloadManager->getCompletedReloadCount() + 1;
        _liveReloadManager->triggerReload( pModuleName );
        return true;
#endif
    }

    bool ModuleReloadDriver::isReloadPending() const
    {
#if defined( SW_SHIPPING )
        return false;
#else
        if ( _liveReloadManager == nullptr || _reloadTargetCount == 0 )
            return false;
        return _liveReloadManager->getCompletedReloadCount() < _reloadTargetCount && _liveReloadManager->isGraphBroken() == false;
#endif
    }
} // namespace sw
