#include "pch.h"

#include "Engine/Window/MainWindowSubsystem.h"

#include "Engine/Common/EngineSubsystemHost.h"
#include "Engine/Window/IWindow.h"

namespace sw
{
    MainWindowSubsystem::MainWindowSubsystem()
        : _window{ nullptr }
        , _baseTitle{}
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
