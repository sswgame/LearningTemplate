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
    // 서브시스템 자리 등록(Engine/Foundation/EngineSubsystemRegistration.h) — 만드는 순서는 자리 표가 정한다.
    // 주 창의 소유자 — RHI 가 기본 창을 만들면 여기에 넘기고, App 이 직접 만든 창도 여기로 간다. RHI 보다 먼저 서고 나중에 내려간다(RHI 가 의존한다). 렌더러를 두는 호스트만.
#if defined( SW_WITH_CLIENT_CODE ) // 클라이언트 전용 칸이다 — 창 없는 전용 서버는 이 자리를 올리지 않는다
    SW_ENGINE_SUBSYSTEM( MainWindowSubsystem, []( SubsystemScope& scope, engine::EngineSubsystemBuild& build )
    {
        if ( build._pStartupSetup->_bRenderer == false )
            return;
        (void)scope.emplaceSubsystem<MainWindowSubsystem>();
    } );
#endif
} // namespace sw
