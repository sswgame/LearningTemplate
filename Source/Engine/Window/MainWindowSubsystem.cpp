#include "pch.h"

#include "Engine/Window/MainWindowSubsystem.h"

#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Window/IWindow.h"

namespace sw
{
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

    bool MainWindowSubsystem::adopt( unique_ptr<IWindow>& window )
    {
        if ( window == nullptr || _window != nullptr )
            return false;
        _window = std::move( window );
        return true;
    }
} // namespace sw

namespace sw
{
    // 주 창의 소유자 — RHI 가 기본 창을 만들면 여기에 넘기고, App 이 직접 만든 창도 여기로 간다. RHI 보다 먼저 서고 나중에 내려간다(RHI 가 의존한다). 렌더러를 두는 호스트만.
#if defined( SW_WITH_CLIENT_CODE )
    SW_ENGINE_SERVICE_SUBSYSTEM( MainWindowSubsystem, HostOnly, Startup, (), []( SubsystemScope& scope, engine::EngineSubsystemBuild& build )
    {
        if ( build._pStartupSetup->_bRenderer == false )
            return;
        (void)scope.emplaceSubsystem<MainWindowSubsystem>();
    } );
#else
    // 창 없는 전용 서버 빌드 — 만들지 않지만 칸은 둔다(자동화 · 입력 코드가 서버에서도 `engine::findSubsystem<MainWindowSubsystem>()` 으로 묻고 nullptr 을 받는다).
    SW_ENGINE_SERVICE( MainWindowSubsystem, HostOnly );
#endif
} // namespace sw
