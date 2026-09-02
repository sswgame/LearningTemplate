#include "pch.h"

#include "Editor/Common/Backend/IImGuiPlatformBackend.h"

#if defined( SW_PLATFORM_LINUX )
    #include "Core/Time/CpuTimer.h"

    #include "Engine/Window/IWindow.h"
    #include "Engine/Window/NativeWindowEvent.h"

    #include <imgui.h>
    #include <X11/Xatom.h>
    #include <X11/keysym.h>

    #include "Core/Common/X11MacroUndef.h"

namespace sw::editor
{
    class ImGuiX11PlatformBackend : public IImGuiPlatformBackend
    {
    public:
        ImGuiX11PlatformBackend()
            : _pDisplay{ nullptr }
            , _mainWindow{ 0 }
            , _wmDelete{ 0 }
            , _timer{}
        {
        }

        bool initialize( IWindow* pWindow, RHIBackend backendType ) override
        {
            if ( pWindow == nullptr )
                return false;
            (void)backendType;

            Display* pDisplay = static_cast<Display*>( pWindow->getNativeDisplay() );
            Window   window   = static_cast<Window>( reinterpret_cast<uintptr_t>( pWindow->getNativeHandle() ) );
            if ( pDisplay == nullptr )
                return false;

            _pDisplay   = pDisplay;
            _mainWindow = window;
            _wmDelete   = XInternAtom( pDisplay, "WM_DELETE_WINDOW", 0 );
            _timer.resetTimer();
            _timer.startTimer();
            _s_active = this;

            ImGuiIO& io            = ImGui::GetIO();
            io.BackendPlatformName = "imgui_impl_x11";
            io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
            io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;

            ImGuiViewport* pMainViewport     = ImGui::GetMainViewport();
            pMainViewport->PlatformHandle    = pDisplay;
            pMainViewport->PlatformHandleRaw = reinterpret_cast<void*>( static_cast<uintptr_t>( window ) );

            ViewportData* pMainData         = IM_NEW( ViewportData )();
            pMainData->_windowHandle        = window;
            pMainData->_bOwned              = false;
            pMainViewport->PlatformUserData = pMainData;

            ImGuiPlatformIO& platformIO            = ImGui::GetPlatformIO();
            platformIO.Platform_CreateWindow       = createWindowThunk;
            platformIO.Platform_DestroyWindow      = destroyWindowThunk;
            platformIO.Platform_ShowWindow         = showWindowThunk;
            platformIO.Platform_SetWindowPos       = setWindowPosThunk;
            platformIO.Platform_GetWindowPos       = getWindowPosThunk;
            platformIO.Platform_SetWindowSize      = setWindowSizeThunk;
            platformIO.Platform_GetWindowSize      = getWindowSizeThunk;
            platformIO.Platform_SetWindowFocus     = setWindowFocusThunk;
            platformIO.Platform_GetWindowFocus     = getWindowFocusThunk;
            platformIO.Platform_GetWindowMinimized = getWindowMinimizedThunk;
            platformIO.Platform_SetWindowTitle     = setWindowTitleThunk;

            updateMonitors();
            return true;
        }

        void shutdown() override
        {
            ImGuiViewport* pMainViewport = ImGui::GetMainViewport();
            if ( pMainViewport != nullptr && pMainViewport->PlatformUserData != nullptr )
            {
                IM_DELETE( static_cast<ViewportData*>( pMainViewport->PlatformUserData ) );
                pMainViewport->PlatformUserData = nullptr;
            }
            _pDisplay   = nullptr;
            _mainWindow = 0;
            _wmDelete   = 0;
            _s_active   = nullptr;
        }

        void newFrame() override
        {
            ImGuiIO& io = ImGui::GetIO();
            if ( _pDisplay == nullptr || _mainWindow == 0 )
                return;

            if ( ImGui::GetPlatformIO().Monitors.empty() )
                updateMonitors();

            XWindowAttributes attrs{};
            XGetWindowAttributes( _pDisplay, _mainWindow, &attrs );
            io.DisplaySize = ImVec2( static_cast<float32>( attrs.width ), static_cast<float32>( attrs.height ) );

            _timer.updateTimer();
            io.DeltaTime = _timer.getDeltaTime();
            if ( io.DeltaTime <= 0.0f )
                io.DeltaTime = 0.00001f;

            Window rootReturn = 0, childReturn = 0;
            int32  rootX = 0, rootY = 0, winX = 0, winY = 0;
            uint32 maskReturn{ 0 };
            if ( XQueryPointer( _pDisplay, _mainWindow, &rootReturn, &childReturn, &rootX, &rootY, &winX, &winY, &maskReturn ) )
            {
                io.AddMousePosEvent( static_cast<float32>( winX ), static_cast<float32>( winY ) );
                io.AddMouseButtonEvent( 0, ( maskReturn & Button1Mask ) != 0 );
                io.AddMouseButtonEvent( 1, ( maskReturn & Button3Mask ) != 0 );
                io.AddMouseButtonEvent( 2, ( maskReturn & Button2Mask ) != 0 );
            }
        }

        bool processEvent( const NativeWindowEvent& event ) override
        {
            if ( event._message != NativeWindowEvent::kMessageX11 )
                return false;

            XEvent* pEvent = reinterpret_cast<XEvent*>( event._lParam );
            if ( pEvent == nullptr )
                return false;

            ImGuiIO& io = ImGui::GetIO();
            switch ( pEvent->type )
            {
                case ClientMessage:
                {
                    if ( _wmDelete == 0 || pEvent->xclient.data.l[0] != static_cast<int32>( _wmDelete ) )
                        return false;

                    if ( pEvent->xclient.window == _mainWindow )
                        return false;

                    ImGuiViewport* pViewport = findViewportByWindow( pEvent->xclient.window );
                    if ( pViewport != nullptr )
                    {
                        pViewport->PlatformRequestClose = true;
                        return true;
                    }
                    return false;
                }
                case KeyPress:
                case KeyRelease:
                {
                    const bool   bIsDown = ( pEvent->type == KeyPress );
                    const KeySym sym     = XLookupKeysym( &pEvent->xkey, 0 );
                    if ( bIsDown && 0x20 <= sym && sym <= 0x7E )
                        io.AddInputCharacter( static_cast<uint32>( sym ) );
                    return io.WantCaptureKeyboard;
                }
                case ButtonPress:
                case ButtonRelease:
                {
                    const bool bIsDown = ( pEvent->type == ButtonPress );
                    int32      button{ 0 };
                    if ( pEvent->xbutton.button == Button1 )
                        button = 0;
                    else if ( pEvent->xbutton.button == Button3 )
                        button = 1;
                    else if ( pEvent->xbutton.button == Button2 )
                        button = 2;
                    else if ( pEvent->xbutton.button == Button4 )
                    {
                        if ( bIsDown )
                            io.AddMouseWheelEvent( 0, 1.0f );
                        return io.WantCaptureMouse;
                    }
                    else if ( pEvent->xbutton.button == Button5 )
                    {
                        if ( bIsDown )
                            io.AddMouseWheelEvent( 0, -1.0f );
                        return io.WantCaptureMouse;
                    }
                    io.AddMouseButtonEvent( button, bIsDown );
                    return io.WantCaptureMouse;
                }
                default:
                    break;
            }
            return false;
        }

    private:
        struct ViewportData
        {
            Window _windowHandle{ 0 };
            bool   _bOwned{ false };
        };

        static void createWindowThunk( ImGuiViewport* pViewport )
        {
            if ( _s_active != nullptr )
                _s_active->createWindow( pViewport );
        }
        static void destroyWindowThunk( ImGuiViewport* pViewport )
        {
            if ( _s_active != nullptr )
                _s_active->destroyWindow( pViewport );
        }
        static void showWindowThunk( ImGuiViewport* pViewport )
        {
            if ( _s_active != nullptr )
                _s_active->showWindow( pViewport );
        }
        static void setWindowPosThunk( ImGuiViewport* pViewport, ImVec2 pos )
        {
            if ( _s_active != nullptr )
                _s_active->setWindowPos( pViewport, pos );
        }
        static ImVec2 getWindowPosThunk( ImGuiViewport* pViewport )
        {
            if ( _s_active == nullptr )
                return ImVec2( 0, 0 );
            return _s_active->getWindowPos( pViewport );
        }
        static void setWindowSizeThunk( ImGuiViewport* pViewport, ImVec2 size )
        {
            if ( _s_active != nullptr )
                _s_active->setWindowSize( pViewport, size );
        }
        static ImVec2 getWindowSizeThunk( ImGuiViewport* pViewport )
        {
            if ( _s_active == nullptr )
                return ImVec2( 0, 0 );
            return _s_active->getWindowSize( pViewport );
        }
        static void setWindowTitleThunk( ImGuiViewport* pViewport, const utf8* pTitle )
        {
            if ( _s_active != nullptr )
                _s_active->setWindowTitle( pViewport, pTitle );
        }
        static void setWindowFocusThunk( ImGuiViewport* pViewport )
        {
            if ( _s_active != nullptr )
                _s_active->setWindowFocus( pViewport );
        }
        static bool getWindowFocusThunk( ImGuiViewport* pViewport )
        {
            if ( _s_active == nullptr )
                return false;
            return _s_active->getWindowFocus( pViewport );
        }
        static bool getWindowMinimizedThunk( ImGuiViewport* pViewport )
        {
            (void)pViewport;
            return false;
        }

        ImGuiViewport* findViewportByWindow( Window window ) const
        {
            if ( window == 0 )
                return nullptr;

            ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
            for ( int32 viewportIndex = 0; viewportIndex < platformIO.Viewports.Size; ++viewportIndex )
            {
                ImGuiViewport* pViewport = platformIO.Viewports[viewportIndex];
                ViewportData*  pData     = static_cast<ViewportData*>( pViewport->PlatformUserData );
                if ( pData != nullptr && pData->_windowHandle == window )
                    return pViewport;
            }
            return nullptr;
        }

        void createWindow( ImGuiViewport* pViewport )
        {
            if ( _pDisplay == nullptr || pViewport == nullptr )
                return;

            ViewportData* pData         = IM_NEW( ViewportData )();
            pData->_bOwned              = true;
            pViewport->PlatformUserData = pData;

            const int32 width    = static_cast<int32>( pViewport->Size.x ) > 0 ? static_cast<int32>( pViewport->Size.x ) : 1;
            const int32 height   = static_cast<int32>( pViewport->Size.y ) > 0 ? static_cast<int32>( pViewport->Size.y ) : 1;
            Window      root     = DefaultRootWindow( _pDisplay );
            pData->_windowHandle = XCreateSimpleWindow(
                _pDisplay, root, static_cast<int32>( pViewport->Pos.x ), static_cast<int32>( pViewport->Pos.y ), static_cast<uint32>( width ), static_cast<uint32>( height ), 1,
                BlackPixel( _pDisplay, DefaultScreen( _pDisplay ) ),
                WhitePixel( _pDisplay, DefaultScreen( _pDisplay ) ) );

            XSelectInput( _pDisplay, pData->_windowHandle,
                          ExposureMask | StructureNotifyMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | FocusChangeMask | KeyPressMask | KeyReleaseMask );
            if ( _wmDelete == 0 )
                _wmDelete = XInternAtom( _pDisplay, "WM_DELETE_WINDOW", 0 );
            XSetWMProtocols( _pDisplay, pData->_windowHandle, &_wmDelete, 1 );
            XMapWindow( _pDisplay, pData->_windowHandle );
            XFlush( _pDisplay );

            pViewport->PlatformHandle    = _pDisplay;
            pViewport->PlatformHandleRaw = reinterpret_cast<void*>( static_cast<uintptr_t>( pData->_windowHandle ) );
        }

        void destroyWindow( ImGuiViewport* pViewport )
        {
            if ( pViewport == nullptr )
                return;

            ViewportData* pData = static_cast<ViewportData*>( pViewport->PlatformUserData );
            if ( pData != nullptr )
            {
                if ( pData->_bOwned && pData->_windowHandle != 0 && _pDisplay != nullptr )
                {
                    XDestroyWindow( _pDisplay, pData->_windowHandle );
                    XFlush( _pDisplay );
                }
                IM_DELETE( pData );
                pViewport->PlatformUserData  = nullptr;
                pViewport->PlatformHandle    = nullptr;
                pViewport->PlatformHandleRaw = nullptr;
            }
        }

        void showWindow( ImGuiViewport* pViewport )
        {
            ViewportData* pData = static_cast<ViewportData*>( pViewport->PlatformUserData );
            if ( pData != nullptr && _pDisplay != nullptr && pData->_windowHandle != 0 )
                XMapRaised( _pDisplay, pData->_windowHandle );
        }

        void setWindowPos( ImGuiViewport* pViewport, ImVec2 pos )
        {
            ViewportData* pData = static_cast<ViewportData*>( pViewport->PlatformUserData );
            if ( pData != nullptr && _pDisplay != nullptr && pData->_windowHandle != 0 )
                XMoveWindow( _pDisplay, pData->_windowHandle, static_cast<int32>( pos.x ), static_cast<int32>( pos.y ) );
        }

        ImVec2 getWindowPos( ImGuiViewport* pViewport )
        {
            ViewportData* pData = static_cast<ViewportData*>( pViewport->PlatformUserData );
            if ( pData == nullptr || _pDisplay == nullptr || pData->_windowHandle == 0 )
                return ImVec2( 0, 0 );

            Window child{ 0 };
            int32  x = 0;
            int32  y = 0;
            XTranslateCoordinates( _pDisplay, pData->_windowHandle, DefaultRootWindow( _pDisplay ), 0, 0, &x, &y, &child );
            return ImVec2( static_cast<float32>( x ), static_cast<float32>( y ) );
        }

        void setWindowSize( ImGuiViewport* pViewport, ImVec2 size )
        {
            ViewportData* pData = static_cast<ViewportData*>( pViewport->PlatformUserData );
            if ( pData != nullptr && _pDisplay != nullptr && pData->_windowHandle != 0 )
            {
                XResizeWindow( _pDisplay, pData->_windowHandle,
                               static_cast<uint32>( size.x > 1 ? size.x : 1 ),
                               static_cast<uint32>( size.y > 1 ? size.y : 1 ) );
            }
        }

        ImVec2 getWindowSize( ImGuiViewport* pViewport )
        {
            ViewportData* pData = static_cast<ViewportData*>( pViewport->PlatformUserData );
            if ( pData == nullptr || _pDisplay == nullptr || pData->_windowHandle == 0 )
                return ImVec2( 0, 0 );

            XWindowAttributes attrs{};
            XGetWindowAttributes( _pDisplay, pData->_windowHandle, &attrs );
            return ImVec2( static_cast<float32>( attrs.width ), static_cast<float32>( attrs.height ) );
        }

        void setWindowTitle( ImGuiViewport* pViewport, const utf8* pTitle )
        {
            ViewportData* pData = static_cast<ViewportData*>( pViewport->PlatformUserData );
            if ( pData != nullptr && _pDisplay != nullptr && pData->_windowHandle != 0 && pTitle != nullptr )
                XStoreName( _pDisplay, pData->_windowHandle, pTitle );
        }

        void setWindowFocus( ImGuiViewport* pViewport )
        {
            ViewportData* pData = static_cast<ViewportData*>( pViewport->PlatformUserData );
            if ( pData != nullptr && _pDisplay != nullptr && pData->_windowHandle != 0 )
                XSetInputFocus( _pDisplay, pData->_windowHandle, RevertToParent, CurrentTime );
        }

        bool getWindowFocus( ImGuiViewport* pViewport )
        {
            ViewportData* pData = static_cast<ViewportData*>( pViewport->PlatformUserData );
            if ( pData == nullptr || _pDisplay == nullptr || pData->_windowHandle == 0 )
                return false;

            Window focused{ 0 };
            int32  revert{ 0 };
            XGetInputFocus( _pDisplay, &focused, &revert );
            return focused == pData->_windowHandle;
        }

        void updateMonitors()
        {
            ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
            platformIO.Monitors.resize( 0 );
            if ( _pDisplay == nullptr )
                return;

            const int32 screenCount = ScreenCount( _pDisplay );
            for ( int32 screen = 0; screen < screenCount; ++screen )
            {
                ImGuiPlatformMonitor monitor{};
                monitor.MainPos  = ImVec2( 0.0f, 0.0f );
                monitor.MainSize = ImVec2( static_cast<float32>( DisplayWidth( _pDisplay, screen ) ), static_cast<float32>( DisplayHeight( _pDisplay, screen ) ) );
                monitor.WorkPos  = monitor.MainPos;
                monitor.WorkSize = monitor.MainSize;
                monitor.DpiScale = 1.0f;

                const int32 widthMm = DisplayWidthMM( _pDisplay, screen );
                if ( widthMm > 0 && monitor.MainSize.x > 0.0f )
                {
                    const float32 dpi = ( monitor.MainSize.x * 25.4f ) / static_cast<float32>( widthMm );
                    if ( dpi > 0.0f )
                        monitor.DpiScale = dpi / 96.0f;
                }
                if ( monitor.DpiScale <= 0.0f )
                    continue;

                monitor.PlatformHandle = reinterpret_cast<void*>( static_cast<uintptr_t>( screen ) );
                platformIO.Monitors.push_back( monitor );
            }

            if ( platformIO.Monitors.empty() )
            {
                ImGuiPlatformMonitor fallback{};
                fallback.MainSize = fallback.WorkSize = ImVec2( 1280.0f, 720.0f );
                fallback.DpiScale                     = 1.0f;
                platformIO.Monitors.push_back( fallback );
            }
        }

    private:
        Display* _pDisplay;
        Window   _mainWindow;
        Atom     _wmDelete;
        CpuTimer _timer;

        static ImGuiX11PlatformBackend* _s_active;
    };

    ImGuiX11PlatformBackend* ImGuiX11PlatformBackend::_s_active{ nullptr };

    unique_ptr<IImGuiPlatformBackend> IImGuiPlatformBackend::createPlatformBackend()
    {
        return make_unique<ImGuiX11PlatformBackend>();
    }
} // namespace sw::editor
#endif
