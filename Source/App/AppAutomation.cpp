#include "pch.h"

#include "App/AppAutomation.h"

#if !defined( SW_SHIPPING )

    #include "Core/Container/string.h"
    #include "Core/Container/vector.h"

    #include "Engine/Automation/AutomationProbe.h"
    #include "Core/Console/DevCommandRegistry.h"

    #include "ModuleHost/LiveReloadManager.h"

namespace sw
{
    namespace
    {
        /** @brief 개발 명령과 탐침이 읽는 매니저입니다. 게임 스레드에서만 읽고 쓴다. */
        LiveReloadManager* s_pLiveReloadManager = nullptr;
        /** @brief `module.reload` 가 예약한 리로드가 끝났다고 볼 완료 수입니다(예약 때의 완료 수 + 1). 0 이면 예약한 적이 없다. */
        uint32 s_reloadTargetCount = 0;

        struct AppAutomationInternal
        {
            /** @brief `module.reload <모듈>` — 그 모듈과 그것에 의존하는 모듈의 리로드를 예약합니다(리로드는 이후 프레임에서 끝납니다. 끝은 `Module.ReloadCount` 로 봅니다). */
            static bool runReloadModule( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                if ( s_pLiveReloadManager == nullptr || s_pLiveReloadManager->getModuleHandle( listArgument[0] ) == nullptr )
                {
                    outReply = "no loaded module '" + listArgument[0] + "'";
                    return false;
                }
                s_reloadTargetCount = s_pLiveReloadManager->getCompletedReloadCount() + 1;
                s_pLiveReloadManager->triggerReload( listArgument[0] );
                outReply = "reload of " + listArgument[0] + " queued";
                return true;
            }

            [[nodiscard]] static bool readReloadCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                if ( s_pLiveReloadManager == nullptr )
                    return false;
                outValue = static_cast<float64>( s_pLiveReloadManager->getCompletedReloadCount() );
                return true;
            }

            /** @brief 탐침 `Module.ReloadPending` — `module.reload` 로 예약한 리로드가 아직 끝나지 않았으면 1 입니다(`WaitUntil … equals="0"` 으로 기다린다). 앞선 리로드 횟수와 무관하다. */
            [[nodiscard]] static bool readReloadPending( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                if ( s_pLiveReloadManager == nullptr )
                    return false;
                const bool bPending = s_reloadTargetCount != 0 && s_pLiveReloadManager->getCompletedReloadCount() < s_reloadTargetCount && s_pLiveReloadManager->isGraphBroken() == false;
                outValue            = bPending ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readGraphBroken( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                if ( s_pLiveReloadManager == nullptr )
                    return false;
                outValue = s_pLiveReloadManager->isGraphBroken() ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readBindingsOk( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                if ( s_pLiveReloadManager == nullptr )
                    return false;
                outValue = s_pLiveReloadManager->verifyModuleBindings() ? 1.0 : 0.0;
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( ReloadModule, "module.reload", "module.reload <module>", "Queue a hot reload of a loaded module and the modules that depend on it (e.g. SWGame)",
                    &AppAutomationInternal::runReloadModule );
    SW_AUTOMATION_PROBE( moduleReloadCount, "Module.ReloadCount", "Hot reload cascades that finished successfully since the process started",
                         &AppAutomationInternal::readReloadCount );
    SW_AUTOMATION_PROBE( moduleReloadPending, "Module.ReloadPending", "1 while a reload queued by module.reload has not finished (0 once it finished, or the graph broke)",
                         &AppAutomationInternal::readReloadPending );
    SW_AUTOMATION_PROBE( moduleGraphBroken, "Module.GraphBroken", "1 when a failed reload blocked the module graph (restart needed), else 0",
                         &AppAutomationInternal::readGraphBroken );
    SW_AUTOMATION_PROBE( moduleBindingsOk, "Module.BindingsOk", "1 when every loaded module is bound to the dependency images the manager holds now, else 0",
                         &AppAutomationInternal::readBindingsOk );

    void AppAutomation::bindLiveReloadManager( LiveReloadManager* pManager )
    {
        s_pLiveReloadManager = pManager;
    }
} // namespace sw

#else

namespace sw
{
    void AppAutomation::bindLiveReloadManager( LiveReloadManager* /*pManager*/ )
    {
    }
} // namespace sw

#endif
