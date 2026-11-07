#include "pch.h"

#include "Engine/Window/MainWindowSubsystem.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Process/ProcessEnvironment.h"

#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Window/IWindow.h"

namespace sw
{
    bool MainWindowSubsystem::isUnwatched( const CommandLineManager& commandLine )
    {
        if ( commandLine.isArgumentProvided( CommandLineArgument::UNATTENDED ) == false )
            return false;
        string showWindows;
        return ( ProcessEnvironment::tryReadNonEmpty( "SW_SHOW_WINDOWS", showWindows ) && showWindows == "1" ) == false;
    }

    MainWindowSubsystem::MainWindowSubsystem()
        : _window{ nullptr }
        , _baseTitle{}
        , _framePacer{}
        , _titleTarget{ *this }
    {
    }

    MainWindowSubsystem::~MainWindowSubsystem() = default;

    void MainWindowSubsystem::shutdownSubsystem()
    {
        if ( _window == nullptr )
            return;
        _window->destroy();
        _window.reset();
    }

    SubsystemHostMask MainWindowSubsystem::getSubsystemHost() const
    {
        return EngineSubsystemHost::kClient;
    }

    const utf8* MainWindowSubsystem::getCurrentTitle() const
    {
        return _window->getTitle(); // getTitleTarget 이 창이 있을 때만 내놓는다
    }

    void MainWindowSubsystem::applyWindowTitle( const utf8* pTitle )
    {
        _window->setTitle( pTitle ); // 창 제목을 거는 유일한 곳(IWindow 의 friend)
    }

    bool MainWindowSubsystem::attach( unique_ptr<IWindow>& window )
    {
        if ( window == nullptr || _window != nullptr )
            return false;
        _window = std::move( window );
        return true;
    }
} // namespace sw

namespace sw
{
    // 주 창의 소유자 — RHI 가 기본 창을 만들면 여기에 넘기고, App 이 직접 만든 창도 여기로 간다. RHI 보다 먼저 서고 나중에 내려간다(RHI 가 의존한다). 렌더러를 두는 호스트만 —
    // 전용 서버 역할은 기동 설정이 렌더러를 끈다(`StartupSetup::_bRenderer`). 칸은 늘 있어 서버의 자동화 · 입력 코드가 `findSubsystem` 으로 묻고 nullptr 을 받는다.
    SW_ENGINE_SERVICE_SUBSYSTEM( MainWindowSubsystem, HostOnly, Startup, (), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        if ( build._pStartupSetup->_bRenderer == false )
            return;
        (void)scope.emplaceSubsystem<MainWindowSubsystem>();
    } );
} // namespace sw
