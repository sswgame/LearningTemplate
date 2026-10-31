#include "pch.h"

#include "App/AppDevTools.h"

#include "App/RHIBackendSwitcher.h"

#include "Core/Foundation/BuildInfo.h"
#include "Core/Log/Logger.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Input/DebugKeyActions.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Renderer/RenderThread.h"

#include "ModuleManager/HostDevLayer.h"

#include "sw/config/ConfigConstants.h"

// 배포에서도 지어진다 — 렌더 스레드 훅은 `build::kShipping` 이면 걸지 않고, 리로드 단축키는 개발 계층이 있을 때만 App 이 부른다.
namespace sw
{
    namespace
    {
        struct AppDevToolsInternal
        {
            /** @brief 에디터 모듈(EditorModule) 핫 리로드 단축키의 액션 이름(`default.input.xml` 의 Debug 레이어). 에디터를 아는 App 만 쓴다(배포본엔 리로드가 없다). */
            static constexpr const utf8* kReloadEditorAction = "ReloadEditor";
        };
    } // namespace

    SW_LOG_CALLER( "App" );

    void AppDevTools::bindRenderThreadHooks( RenderThread& renderThread, RHIBackendSwitcher& backendSwitcher )
    {
        if constexpr ( build::kShipping ) // 배포는 개발 명령이 없어 장치 제거 요청이 오지 않는다
            return;
        renderThread.setBeginFrameHook( SW_DELEGATE_METHOD( BeginFrameHookDelegate, &RHIBackendSwitcher::onBeginFrameOnRenderThread, &backendSwitcher ) ); // 개발 명령 `rhi.removeDevice <ms>`
    }

    void AppDevTools::pollShellActions( float32 deltaTime, IHostDevLayer& devLayer, bool bEnableEditor )
    {
        DebugKeyActions& debugKeys = engine::getSubsystem<DebugKeyActions>();
        debugKeys.update( engine::getSubsystem<InputManager>(), deltaTime );

        // 모듈을 다시 로드하는 장치는 호스트(App)에 있다. 디버그 키 맵에는 "이 액션이 눌렸나"만 묻는다.
        if ( debugKeys.wasActionTriggered( hashed_string( InputMapDefaults::kReloadGameAction ) ) )
        {
            devLayer.requestReload( config::kTargetGameModule );
            SW_LOG_INFO( "%#: force SWGame reload", InputMapDefaults::kReloadGameAction );
        }
        if ( bEnableEditor && debugKeys.wasActionTriggered( hashed_string( AppDevToolsInternal::kReloadEditorAction ) ) )
        {
            devLayer.requestReload( config::kTargetEditorModule );
            SW_LOG_INFO( "%#: force EditorModule reload", AppDevToolsInternal::kReloadEditorAction );
        }
    }

} // namespace sw
