#include "pch.h"

#include "Engine/Window/IWindow.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Window/NativeWindowEvent.h"
#include "Engine/Window/WindowMonitor.h"
#include "Engine/Window/WindowPlatform.h"

#include <cstdlib>
#include <cstring>

namespace sw
{
    IWindow::IWindow()
        : _arrTitle{}
        , _customHandler{}
        , _accessibilityQuery{}
        , _onResize{}
        , _onLifecycle{}
        , _closeQuery{}
        , _width{ 1280 }
        , _height{ 720 }
        , _bShouldClose{ SW_FALSE }
        , _bRecreating{ SW_FALSE }
        , _bVisibleRequested{ SW_FALSE }
        , _bMinimized{ SW_FALSE }
        , _bFocused{ SW_FALSE }
        , _bSurfaceLost{ SW_FALSE }
        , _bAppSuspended{ SW_FALSE }
        , _reserved{ 0 }
        , _displayMode{ WindowDisplayMode::Windowed }
        , _textInputClientMask{ 0 }
        , _arrReserved{}
        , _textInputArea{}
        , _restoreX{ 0 }
        , _restoreY{ 0 }
        , _minClientWidth{ 0 }
        , _minClientHeight{ 0 }
        , _monitorGeneration{ 0 }
        , _renderPausedTickCount{ 0 }
        , _simulatedInterruptionTicks{ 0 }
    {
    }

    void IWindow::notifyLifecycleEvent( const WindowLifecycleEvent event )
    {
        const bool bWasLost      = _bSurfaceLost == SW_TRUE;
        const bool bWasSuspended = _bAppSuspended == SW_TRUE;
        switch ( event )
        {
            case WindowLifecycleEvent::SurfaceLost:
                _bSurfaceLost = SW_TRUE;
                break;
            case WindowLifecycleEvent::SurfaceRestored:
                _bSurfaceLost = SW_FALSE;
                break;
            case WindowLifecycleEvent::AppSuspended:
                _bAppSuspended = SW_TRUE;
                break;
            case WindowLifecycleEvent::AppResumed:
                _bAppSuspended = SW_FALSE;
                break;
        }
        const bool bChanged = bWasLost != ( _bSurfaceLost == SW_TRUE ) || bWasSuspended != ( _bAppSuspended == SW_TRUE );
        if ( bChanged && _onLifecycle.isBound() )
            _onLifecycle.get()( event );
    }

    void IWindow::advanceLifecycleTick()
    {
        if ( isRenderPaused() )
            ++_renderPausedTickCount;
        if ( _simulatedInterruptionTicks == 0 )
            return;
        --_simulatedInterruptionTicks;
        if ( _simulatedInterruptionTicks == 0 )
        {
            notifyLifecycleEvent( WindowLifecycleEvent::SurfaceRestored );
            notifyLifecycleEvent( WindowLifecycleEvent::AppResumed );
        }
    }

    void IWindow::simulateAppInterruption( const uint32 tickCount )
    {
        _simulatedInterruptionTicks = tickCount;
        notifyLifecycleEvent( WindowLifecycleEvent::AppSuspended );
        notifyLifecycleEvent( WindowLifecycleEvent::SurfaceLost );
    }

    bool IWindow::tryGetMonitors( vector<WindowMonitor>& outListMonitor ) const
    {
        outListMonitor.clear();
        return false;
    }

    void IWindow::requestTextInput( WindowTextInputClient client, bool bWanted )
    {
        const bool  bWasWanted = isTextInputWanted();
        const uint8 clientBit  = static_cast<uint8>( 1u << static_cast<uint32>( client ) );
        _textInputClientMask   = bWanted ? static_cast<uint8>( _textInputClientMask | clientBit ) : static_cast<uint8>( _textInputClientMask & ~clientBit );
        if ( isTextInputWanted() != bWasWanted )
            applyTextInputWanted( isTextInputWanted() );
    }

    void IWindow::setTextInputArea( const WindowTextInputArea& area )
    {
        if ( area == _textInputArea )
            return;
        _textInputArea = area;
        applyTextInputArea( area );
    }

    void IWindow::dispatchTextInput( WindowTextInputKind kind, string_view text )
    {
        if ( _customHandler.isBound() == false )
            return;
        WindowTextInputEvent input;
        input._kind = kind;
        input._text = text;
        NativeWindowEvent event{};
        event.setNativeWindow( getSurfaceKind(), getNativeHandle() );
        event._message = NativeWindowEvent::kMessageTextInput;
        event._lParam  = reinterpret_cast<intptr_t>( &input );
        (void)_customHandler.get()( event ); // 글은 소비되어도 창이 달리 할 일이 없다
    }

    void IWindow::setMinimumClientSize( uint32 width, uint32 height )
    {
        _minClientWidth  = width;
        _minClientHeight = height;
        applyMinimumClientSize();

        // 바닥은 사용자가 줄일 때만 막는다. 이미 바닥보다 작은 창은 여기서 키운다.
        const bool bSmaller = _width < width || _height < height;
        if ( _displayMode == WindowDisplayMode::Windowed && bSmaller )
            (void)setDisplayMode( WindowDisplayMode::Windowed, _width < width ? width : _width, _height < height ? height : _height );
    }

    void IWindow::computeTilePosition( const WindowTileSlot& slot, uint32 clientWidth, uint32 clientHeight, const WindowFrameExtents& extents, const WindowMonitorRect& workArea, int32& outX,
                                       int32& outY )
    {
        SW_ASSERT( slot._columnCount > 0 && slot._index >= 0 ); // `createMainWindow` 가 막는다
        const int32 column      = slot._index % slot._columnCount;
        const int32 row         = slot._index / slot._columnCount;
        const int32 frameWidth  = static_cast<int32>( clientWidth ) + extents._left + extents._right;
        const int32 frameHeight = static_cast<int32>( clientHeight ) + extents._top + extents._bottom;
        // 가로: 열이 작업 영역 너비에 다 들어가게 간격을 좁힌다(겹침을 고르게 나눈다). 한 열이거나 넉넉하면 틀 간격 그대로다.
        int32 strideX = frameWidth;
        if ( slot._columnCount > 1 && slot._columnCount * frameWidth > workArea._size._x )
            strideX = MathUtil::max( 0, ( workArea._size._x - frameWidth ) / ( slot._columnCount - 1 ) );
        outX = workArea._position._x + column * strideX;
        // 세로: 행 수는 이 창이 모른다 — 틀 간격으로 쌓고 아래로 넘치면 작업 영역 아래끝에 붙인다(작업 표시줄에 가리지 않는다).
        const int32 bottomY = workArea._position._y + MathUtil::max( 0, workArea._size._y - frameHeight );
        outY                = MathUtil::min( workArea._position._y + row * frameHeight, bottomY );
    }

    unique_ptr<IWindow> IWindow::createMainWindow( const utf8* pTitle, uint32 width, uint32 height, const CommandLineManager& commandLine )
    {
        // 위치 인자는 둘 중 하나다 — 좌표(`-WinX` · `-WinY`, 둘 다)나 바둑판 칸(`-WinTile` · `-WinTileColumns`, 둘 다). 반쪽이나 둘을 함께 주면 플랫폼 값으로 채우지 않고 멈춘다.
        const bool bHasX           = commandLine.isArgumentProvided( CommandLineArgument::WINDOW_X );
        const bool bHasY           = commandLine.isArgumentProvided( CommandLineArgument::WINDOW_Y );
        const bool bHasTile        = commandLine.isArgumentProvided( CommandLineArgument::WINDOW_TILE );
        const bool bHasTileColumns = commandLine.isArgumentProvided( CommandLineArgument::WINDOW_TILE_COLUMNS );
        int32      x{ 0 };
        int32      y{ 0 };
        const bool bPosition = bHasX || bHasY;
        if ( bPosition && ( bHasX && bHasY && commandLine.getArgument( CommandLineArgument::WINDOW_X, x ) && commandLine.getArgument( CommandLineArgument::WINDOW_Y, y ) ) == false )
        {
            SW_LOG_ERROR_ID( "Window.WindowPositionNeedsBoth", "Window position needs both -WinX=<x> and -WinY=<y> as numbers - the window is not created" );
            return nullptr;
        }
        WindowTileSlot slot;
        const bool     bTile = bHasTile || bHasTileColumns;
        if ( bTile && ( bHasTile && bHasTileColumns && commandLine.getArgument( CommandLineArgument::WINDOW_TILE, slot._index ) &&
                        commandLine.getArgument( CommandLineArgument::WINDOW_TILE_COLUMNS, slot._columnCount ) && slot._index >= 0 && slot._columnCount > 0 ) == false )
        {
            SW_LOG_ERROR_ID( "Window.WindowTileNeedsBoth", "Window tile needs both -WinTile=<index from 0> and -WinTileColumns=<count from 1> - the window is not created" );
            return nullptr;
        }
        if ( bPosition && bTile )
        {
            SW_LOG_ERROR_ID( "Window.WindowPositionAndTile", "Window position (-WinX/-WinY) and tile (-WinTile) are both given - pick one; the window is not created" );
            return nullptr;
        }

        unique_ptr<IWindow> pWindow = createPlatformWindow();
        if ( pWindow == nullptr )
            return nullptr;
        const bool bPlace = pWindow->canPlaceWindow();
        if ( ( bPosition || bTile ) && bPlace == false )
            SW_LOG_INFO_ID( "Window.WindowPlacedByCompositor", "This window system places windows itself - -WinX/-WinTile are not applied" );
        if ( bPosition && bPlace )
            pWindow->setInitialPosition( x, y );
        if ( pWindow->initializeWindow( pTitle, width, height ) == false )
            return nullptr;
        if ( bTile == false || bPlace == false )
            return pWindow;

        // 칸 간격은 만든 창 자신의 틀 두께다 — 틀은 창 스타일 · DPI · 창 관리자 테마가 정해서 창을 띄우는 쪽(부모 프로세스)은 모른다.
        WindowFrameExtents extents;
        if ( pWindow->tryGetFrameExtents( extents ) == false )
            return nullptr; // 오류는 플랫폼이 남겼다
        // 원점은 주 모니터의 작업 영역이다 — 화면 (0, 0) 에서 쌓으면 작은 모니터에서 작업 표시줄 아래 · 화면 밖으로 나갔다.
        vector<WindowMonitor> listMonitor;
        if ( pWindow->tryGetMonitors( listMonitor ) == false || listMonitor.empty() )
        {
            SW_LOG_ERROR_ID( "Window.WindowTileNoMonitor", "Window tile %# needs the primary monitor's work area but the window system reported no monitor - the window is not created", slot._index );
            return nullptr;
        }
        const WindowMonitor* pPrimary = &listMonitor[0]; // 주 모니터가 0 번(`tryGetMonitors` 약속)
        computeTilePosition( slot, pWindow->getWidth(), pWindow->getHeight(), extents, pPrimary->_workArea, x, y );
        if ( pWindow->setFramePosition( x, y ) == false )
        {
            SW_LOG_ERROR_ID( "Window.WindowTileMoveFailed", "Window tile %# could not move the window to (%#, %#)", slot._index, x, y );
            return nullptr;
        }
        SW_LOG_INFO_ID( "Window.WindowTilePlaced", "Window tile %# of %# column(s) at (%#, %#) - frame left %# top %# right %# bottom %#", slot._index, slot._columnCount, x, y, extents._left,
                        extents._top, extents._right, extents._bottom );
        return pWindow;
    }

    void IWindow::setTitle( const utf8* pTitle )
    {
        if ( StringUtil::isNullOrEmpty( pTitle ) )
            return;
        if ( std::strcmp( pTitle, _arrTitle ) == 0 )
            return;
        if ( storeTitle( pTitle ) == false )
            return; // 오류는 storeTitle 이 남겼다 — 지금 제목이 그대로다
        applyTitle();
    }

    bool IWindow::storeTitle( const utf8* pTitle )
    {
        const utf8* const pSource = pTitle != nullptr ? pTitle : "";
        const size_t      length  = std::strlen( pSource );
        if ( length >= kTitleCapacity )
        {
            SW_LOG_ERROR_ID( "Window.WindowTitleBytesMust", "Window title is %# bytes - it must be shorter than %# bytes (IWindow::kTitleCapacity)", length, kTitleCapacity );
            return false;
        }
        std::memcpy( _arrTitle, pSource, length + 1 );
        return true;
    }

    void* IWindow::tryGetWin32Handle() const
    {
        return getSurfaceKind() == NativeSurfaceKind::Win32 ? getNativeHandle() : nullptr;
    }

    bool IWindow::tryGetX11Surface( X11SurfaceHandles& outHandles ) const
    {
        outHandles = X11SurfaceHandles{};
        if ( getSurfaceKind() != NativeSurfaceKind::X11 )
            return false;
        X11SurfaceHandles handles;
        handles._pDisplay = getNativeDisplay();
        handles._window   = static_cast<uint64>( reinterpret_cast<uintptr_t>( getNativeHandle() ) );
        if ( handles._pDisplay == nullptr || handles._window == 0 )
            return false;
        outHandles = handles;
        return true;
    }

    void IWindow::requestClose()
    {
        _bShouldClose = SW_TRUE;
    }

    bool IWindow::tryBeginClose()
    {
        if ( _bShouldClose == SW_TRUE )
            return true;
        if ( _closeQuery.isBound() && _closeQuery.get()() == false )
            return false;
        _bShouldClose = SW_TRUE;
        return true;
    }

    IWindow::~IWindow() = default;

    bool IWindow::recreate()
    {
        if ( _arrTitle[0] == '\0' )
            return false;

        // 다시 만들어도 **보이던 창은 보여야 한다**. 빠뜨리면 백엔드 교체 뒤 창이 사라진다.
        //
        // **묻는 것은 화면 상태가 아니라 의도다.** `isVisible()` 은 X11 에서 "창 관리자가 이미 매핑했는가" 를 묻는다.
        // 방금 `showWindow(true)` 한 창도 아직 아니고, 최소화됐거나 다른 워크스페이스에 있어도 아니다. 그 값으로 다시 만들면
        // **창이 사라진다**(윈도우에서는 `IsWindowVisible` 이 WS_VISIBLE 스타일이라 둘이 같아 리눅스에서만 드러난다.
        // `WindowTest.RecreateKeepsVisibilityAndSize` 참고).
        const bool bWasVisible = isVisibleRequested();
        captureRestorePosition();

        const uint32 width  = _width;
        const uint32 height = _height;

        // 이 구간의 `destroy()` 는 앱 종료가 아니다. 플랫폼 메시지 처리기가 이 깃발을 보고 삼킨다.
        _bRecreating = SW_TRUE;
        destroy();
        _bShouldClose = SW_FALSE;

        // 플랫폼의 `initializeWindow` 가 제목을 `_arrTitle` 에 다시 담으므로 사본으로 넘긴다(같은 버퍼를 읽으며 쓰지 않게).
        utf8 arrTitle[kTitleCapacity];
        std::memcpy( arrTitle, _arrTitle, kTitleCapacity );
        const bool bOk = initializeWindow( arrTitle, width, height );
        // 새 창은 늘 창 모드로 선다. 전체 화면이던 창은 보이기 전에 같은 방식으로 되돌린다(크기 통보는 `_bRecreating` 이 삼킨다).
        if ( bOk && _displayMode != WindowDisplayMode::Windowed )
            (void)setDisplayMode( _displayMode, width, height );
        if ( bOk && bWasVisible )
            showWindow( true );

        _bRecreating = SW_FALSE;
        clearRestorePosition();
        return bOk;
    }

    unique_ptr<IWindow> IWindow::createPlatformWindow()
    {
        const WindowPlatformFactory* const pFactory = WindowPlatform::find();
        return pFactory != nullptr ? pFactory->_pfnCreateWindow() : nullptr;
    }
} // namespace sw
