#include "pch.h"

#include "Engine/Window/WindowPlatform.h"
#include "Engine/Window/Windows/Win32SplashWindow.h"
#include "Engine/Window/Windows/Win32Window.h"

namespace sw
{
    namespace
    {
        const WindowPlatformFactory s_win32WindowFactory{
            []() -> unique_ptr<IWindow>
        { return make_unique<Win32Window>(); },
            []() -> unique_ptr<ISplashWindow>
        { return make_unique<Win32SplashWindow>(); },
            []()
        { return true; } };
        const WindowPlatform::Registrar s_win32WindowRegistrar{ s_win32WindowFactory };
    } // namespace
} // namespace sw
