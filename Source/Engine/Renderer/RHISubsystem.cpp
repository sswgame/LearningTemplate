#include "pch.h"

#include "Engine/Renderer/RHISubsystem.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Diagnostics/CrashHandler.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Common/EngineSubsystemHost.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Renderer/Capture/RenderDocCapture.h"
#include "Engine/UserSettings/UserSettingsEngineHandlers.h"
#include "Engine/UserSettings/UserSettingsManager.h"
#include "Engine/Window/IWindow.h"

namespace sw
{
    /**
     * @brief `-gv_crashTest=N`: RHI 초기화 직후 일부러 크래시를 냅니다(리포트 경로 검증용). N 은 `CrashTestKind` 입니다.
     * @details 크래시 리포트는 크래시가 나야만 만들어집니다. 그래서 "덤프가 제대로 써지는가" 는 일부러
     *          죽여 보는 것 말고는 확인할 방법이 없습니다. 배포하고 나서 안 된다는 것을 알면 늦습니다. **죽는 방식마다** 따로 태워 봐야
     *          합니다 — 접근 위반으로 남는 리포트가 스택 오버플로 · abort 에서도 남는다는 보장은 없습니다.
     */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_crashTest, 0, "일부러 크래시를 내 리포트 경로를 검증합니다 (1=널 쓰기 2=스택 오버플로 3=작업 스레드 스택 오버플로 4=abort 5=순수 가상 호출)" );
} // namespace sw

namespace sw
{
    RHISubsystem::RHISubsystem()
        : _rhi{ nullptr }
        , _initResult{ RHIInitResult::NotStarted }
        , _bReady{ false }
    {
        SW_MEMORY_SCOPE( RenderCPU );
        _rhi = make_unique<RHI>();
    }

    RHISubsystem::~RHISubsystem() = default;

    SubsystemInitResult RHISubsystem::initializeSubsystem()
    {
        CommandLineManager& commandLine = engine::getCommandLineManager();
        RenderDocCapture::initialize( commandLine.isArgumentProvided( CommandLineArgument::RENDERDOC ) );

        RHIBackend          commandLineBackend{};
        const EngineConfig& engineConfig = EngineConfig::getActive();
        if ( RHIBackendUtil::findCommandLineBackend( commandLine, commandLineBackend ) == false )
            gv_rhiBackend = engineConfig._window._defaultRHI;

        const DisplaySettingsRequest& display = engine::getUserSettingsManager().getDisplayRequest();
        if ( IWindow::getActiveWindow() == nullptr )
        {
            uint32 windowWidth  = display._bHasResolution ? display._width : engineConfig._window._width;
            uint32 windowHeight = display._bHasResolution ? display._height : engineConfig._window._height;
            // 인자를 주지 않으면 getArgument 가 false 를 돌려주므로 설정값이 그대로 남는다.
            commandLine.getArgument( CommandLineArgument::WIDTH, windowWidth );
            commandLine.getArgument( CommandLineArgument::HEIGHT, windowHeight );

            unique_ptr<IWindow> defaultWindow = IWindow::createPlatformWindow();
            if ( defaultWindow != nullptr && defaultWindow->initializeWindow( GameConfig::getActive()._windowTitle.c_str(), windowWidth, windowHeight ) )
            {
                // 전체 화면은 스왑체인을 만들기 전에 고른다 — 스왑체인이 처음부터 모니터 크기다.
                if ( display._bHasMode && display._mode != WindowDisplayMode::Windowed )
                    (void)defaultWindow->setDisplayMode( display._mode, windowWidth, windowHeight );
                // 소유권은 App::initialize 가 IWindow::getActiveWindow() 로 넘겨받는다(App 이 없는 임베드라면 부르는 쪽이 직접 소유한다).
                IWindow::setActiveWindow( defaultWindow.release() );
            }
        }

        _rhi->setPreferredVSync( display._bHasVSync ? display._bVSync : engineConfig._window._bVSync );
        // RHI 는 창 시스템을 모른다. 표면(IRenderSurface)만 넘긴다. 창은 위에서 만들었거나 호스트가 들고 있다.
        const bool bReady = _rhi->initialize( IWindow::getActiveWindow() );
        _initResult       = _rhi->getInitResult();
        if ( bReady == false )
            return SubsystemInitResult::Failed;
        _bReady = true;
        // 백엔드가 정해졌으니 크래시 리포트에 남긴다 — "어느 백엔드에서 났는가" 가 범위를 좁히는 첫 질문이다.
        CrashHandler::setContextValue( "RHI", _rhi->getDevice().getBackendName() );
        // `-gv_crashTest=1`: 크래시 리포트 경로를 실제로 확인하는 유일한 방법이다(리포트는 크래시가 나야만 만들어진다).
        if ( gv_crashTest != 0 )
            CrashHandler::crashForTest( static_cast<CrashTestKind>( gv_crashTest ) );
        return SubsystemInitResult::Succeeded;
    }

    void RHISubsystem::shutdownSubsystem()
    {
        // 디바이스 생성이 실패하면 RHI 객체는 있어도 디바이스가 없다 — getDevice() 를 부르기 전에 hasDevice() 로 막는다.
        if ( _rhi->hasDevice() )
            _rhi->getDevice().waitIdle();
        _rhi->shutdown();
        _bReady = false;
    }

    span<const utf8* const> RHISubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { ConfigManager::kSubsystemName, UserSettingsManager::kSubsystemName };
        return kArrDependency;
    }

    SubsystemHostMask RHISubsystem::getSubsystemHost() const
    {
        return EngineSubsystemHost::kClient;
    }
} // namespace sw
