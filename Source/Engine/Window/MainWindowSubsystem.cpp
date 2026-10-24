#include "pch.h"

#include "Engine/Window/MainWindowSubsystem.h"

#include "Engine/Common/EngineSubsystemHost.h"
#include "Engine/Window/IWindow.h"

namespace sw
{
    MainWindowSubsystem::MainWindowSubsystem()
        : _window{ nullptr }
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

    bool MainWindowSubsystem::adopt( unique_ptr<IWindow>& window )
    {
        if ( window == nullptr || _window != nullptr )
            return false;
        _window = std::move( window );
        return true;
    }
} // namespace sw
