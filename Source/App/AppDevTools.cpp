#include "pch.h"

#include "App/AppDevTools.h"

#include "App/RHIBackendSwitcher.h"

#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "DevTools/AssetReload/ContentFolderWatchService.h"
#include "DevTools/DevToolsHost.h"

#include "Engine/Engine.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Input/DebugKeyActions.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Renderer/RenderThread.h"

#include "ModuleManager/ModuleReloadDriver.h"

#include "sw/config/ConfigConstants.h"

// 개발 빌드의 구현이다 — 배포는 `AppDevToolsShipping.cpp` 를 CMake 가 고른다(`Source/App/CMakeLists.txt`).
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

    void AppDevTools::addToHostScope( SubsystemScope& host, Engine& engine )
    {
        // 개발 도구 — 엔진 기동 확장(델리게이트)으로 걸어, 엔진이 기동 단계 범위를 채울 때 자기 서브시스템을 더한다(엔진은 무엇인지 모른다).
        {
            SW_MEMORY_SCOPE( EngineMisc );
            _pDevTools = &host.addSubsystem( make_unique<DevToolsHost>() );
        }
        engine.setStartupExtension( _pDevTools->makeStartupExtension() );
        // 콘텐츠 폴더 감시 — 기능 · 게임의 데이터 다시 읽기가 범위에서 찾는다(배포에는 없다).
        const bool bFolderWatchBound = host.bind<IContentFolderWatch>( &_pDevTools->getContentFolderWatch() );
        SW_ASSERT( bFolderWatchBound );
        (void)bFolderWatchBound;
    }

    void AppDevTools::attachFrameHooks( Engine& engine )
    {
        _frameSubscription = engine.getFrameHooks().add( _pDevTools->makeFrameHooks() );
    }

    void AppDevTools::startConsole( bool bEnableEditor )
    {
        _pDevTools->startConsole( bEnableEditor );
    }

    void AppDevTools::stopConsole()
    {
        _pDevTools->stopConsole();
    }

    void AppDevTools::bindRenderThreadHooks( RenderThread& renderThread, RHIBackendSwitcher& backendSwitcher )
    {
        renderThread.setBeginFrameHook( SW_DELEGATE_METHOD( BeginFrameHookDelegate, &RHIBackendSwitcher::onBeginFrameOnRenderThread, &backendSwitcher ) ); // 개발 명령 `rhi.removeDevice <ms>`
    }

    void AppDevTools::pollShellActions( float32 deltaTime, ModuleReloadDriver& moduleReload, bool bEnableEditor )
    {
        DebugKeyActions& debugKeys = engine::getSubsystem<DebugKeyActions>();
        debugKeys.update( engine::getSubsystem<InputManager>(), deltaTime );

        // 모듈을 다시 로드하는 장치는 호스트(App)에 있다. 디버그 키 맵에는 "이 액션이 눌렸나"만 묻는다.
        if ( debugKeys.wasActionTriggered( hashed_string( InputMapDefaults::kReloadGameAction ) ) )
        {
            moduleReload.requestReload( config::kTargetGameModule );
            SW_LOG_INFO( "%#: force SWGame reload", InputMapDefaults::kReloadGameAction );
        }
        if ( bEnableEditor && debugKeys.wasActionTriggered( hashed_string( AppDevToolsInternal::kReloadEditorAction ) ) )
        {
            moduleReload.requestReload( config::kTargetEditorModule );
            SW_LOG_INFO( "%#: force EditorModule reload", AppDevToolsInternal::kReloadEditorAction );
        }
    }

    void AppDevTools::detach()
    {
        _frameSubscription.reset();
    }
} // namespace sw
