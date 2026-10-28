#include "pch.h"

#include "Engine/Window/Linux/Wayland/WaylandDevConsoleWindow.h"

#if SW_DEV_COMMANDS_ENABLED && defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE )

    #include "Core/Log/Logger.h"

    #include "Engine/Window/Linux/Wayland/WaylandWindow.h"

    #if defined( SW_WITH_WAYLAND )
        #include "Core/Container/string.h"
        #include "Core/Container/vector.h"

        #include "Engine/Window/Linux/Wayland/WaylandShmBuffer.h"
        #include "Engine/Window/Linux/Wayland/WaylandTextPainter.h"

        #include <wayland-client.h>
    #endif

namespace sw
{
    SW_LOG_CALLER( "DevConsole" );
} // namespace sw

    #if defined( SW_WITH_WAYLAND )

namespace sw
{
    namespace
    {
        struct WaylandDevConsoleWindowInternal
        {
            static constexpr uint32 kPixelSize       = 14;
            static constexpr int32  kPadding         = 6;
            static constexpr uint32 kBackgroundColor = 0x000000;
            static constexpr uint32 kTextColor       = 0xFFFFFF;
            static constexpr uint32 kErrorTextColor  = 0xFF7070;
        };
    } // namespace

    /**
     * @class WaylandDevConsoleWindow
     * @brief 게임 창 표면의 서브표면입니다. 열려 있는 동안 `present` 가 프레임마다 다시 그린다(서브표면은 `desync` 라 이 표면의 commit 이 곧바로 반영된다).
     */
    class WaylandDevConsoleWindow final : public IDevConsoleWindow
    {
    public:
        explicit WaylandDevConsoleWindow( WaylandWindow& owner )
            : _owner{ owner }
            , _handles{}
            , _pSurface{ nullptr }
            , _pSubsurface{ nullptr }
            , _painter{}
            , _pool{}
            , _listLine{}
            , _listErrorFlag{}
            , _bVisible{ false }
        {
        }

        ~WaylandDevConsoleWindow() override
        {
            if ( _pSubsurface != nullptr )
                wl_subsurface_destroy( _pSubsurface );
            if ( _pSurface != nullptr )
                wl_surface_destroy( _pSurface );
            if ( _handles._pDisplay != nullptr )
                wl_display_flush( _handles._pDisplay );
        }

        WaylandDevConsoleWindow( const WaylandDevConsoleWindow& )            = delete;
        WaylandDevConsoleWindow& operator=( const WaylandDevConsoleWindow& ) = delete;

        [[nodiscard]] bool initialize()
        {
            if ( _owner.tryGetOverlayHandles( _handles ) == false )
            {
                SW_LOG_WARNING( "Dev console: the compositor offers no wl_subcompositor or wl_shm - the console window is not created (input only)" );
                return false;
            }
            if ( _painter.initialize( WaylandDevConsoleWindowInternal::kPixelSize ) == false )
                return false; // 이유는 painter 가 남겼다
            _pSurface = wl_compositor_create_surface( _handles._pCompositor );
            if ( _pSurface == nullptr )
                return false;
            _pSubsurface = wl_subcompositor_get_subsurface( _handles._pSubcompositor, _pSurface, _handles._pParentSurface );
            if ( _pSubsurface == nullptr )
                return false;
            wl_subsurface_set_position( _pSubsurface, 0, 0 );
            wl_subsurface_set_desync( _pSubsurface ); // 게임 창 표면의 commit 을 기다리지 않는다
            // 입력 영역을 비운다 — 포인터가 이 표면에 들어가 게임 창의 포인터 이벤트가 끊기지 않게 한다.
            wl_region* const pEmptyRegion = wl_compositor_create_region( _handles._pCompositor );
            if ( pEmptyRegion != nullptr )
            {
                wl_surface_set_input_region( _pSurface, pEmptyRegion );
                wl_region_destroy( pEmptyRegion );
            }
            return true;
        }

        void setVisible( bool bVisible ) override
        {
            if ( _bVisible == bVisible )
                return;
            _bVisible = bVisible;
            if ( bVisible )
            {
                redraw();
            }
            else
            {
                wl_surface_attach( _pSurface, nullptr, 0, 0 ); // 버퍼를 떼면 표면이 사라진다
                wl_surface_commit( _pSurface );
                wl_display_flush( _handles._pDisplay );
            }
        }

        void present( const vector<string>& listLine, const vector<uint8>& listErrorFlag ) override
        {
            _listLine      = listLine;
            _listErrorFlag = listErrorFlag;
            if ( _bVisible )
                redraw();
        }

    private:
        void redraw()
        {
            const uint32 width = _owner.getWidth();
            if ( width == 0 )
                return;
            const int32  lineCount = static_cast<int32>( _listLine.empty() ? 1 : _listLine.size() );
            const uint32 height    = static_cast<uint32>( lineCount * _painter.getLineHeight() + WaylandDevConsoleWindowInternal::kPadding * 2 );
            // 전부 쓰는 중이면 이번 그리기를 건너뛴다 — 열려 있는 동안 프레임마다 다시 부르므로 다음 프레임에 그려진다.
            WaylandShmBuffer* const pBuffer = _pool.acquire( _handles._pShm, width, height );
            if ( pBuffer == nullptr )
                return;
            uint32* const pPixels    = pBuffer->getPixels();
            const size_t  pixelCount = static_cast<size_t>( width ) * static_cast<size_t>( height );
            for ( size_t index = 0; index < pixelCount; ++index )
            {
                pPixels[index] = WaylandDevConsoleWindowInternal::kBackgroundColor;
            }
            int32 baselineY = WaylandDevConsoleWindowInternal::kPadding + _painter.getAscent();
            for ( size_t index = 0; index < _listLine.size(); ++index )
            {
                const bool   bError = index < _listErrorFlag.size() && _listErrorFlag[index] != 0;
                const uint32 color  = bError ? WaylandDevConsoleWindowInternal::kErrorTextColor : WaylandDevConsoleWindowInternal::kTextColor;
                (void)_painter.drawText( pPixels, width, height, WaylandDevConsoleWindowInternal::kPadding, baselineY, _listLine[index], color );
                baselineY += _painter.getLineHeight();
            }
            wl_surface_attach( _pSurface, pBuffer->getBuffer(), 0, 0 );
            wl_surface_damage( _pSurface, 0, 0, static_cast<int32>( width ), static_cast<int32>( height ) );
            pBuffer->markBusy();
            wl_surface_commit( _pSurface );
            wl_display_flush( _handles._pDisplay );
        }

    private:
        WaylandWindow&        _owner;
        WaylandOverlayHandles _handles;
        wl_surface*           _pSurface;
        wl_subsurface*        _pSubsurface;
        WaylandTextPainter    _painter;
        WaylandShmBufferPool  _pool;
        vector<string>        _listLine;
        vector<uint8>         _listErrorFlag;
        bool                  _bVisible;
    };

    unique_ptr<IDevConsoleWindow> createWaylandDevConsoleWindow( WaylandWindow& owner )
    {
        unique_ptr<WaylandDevConsoleWindow> pWindow = make_unique<WaylandDevConsoleWindow>( owner );
        if ( pWindow->initialize() == false )
        {
            SW_LOG_WARNING( "Dev console: could not create the Wayland console window (no subsurface, shm or font) - input only" );
            return nullptr;
        }
        return pWindow;
    }
} // namespace sw

    #else

namespace sw
{
    unique_ptr<IDevConsoleWindow> createWaylandDevConsoleWindow( WaylandWindow& owner )
    {
        (void)owner;
        return nullptr; // Wayland 를 짓지 않은 구성에는 Wayland 창이 없다(이 함수는 불리지 않는다)
    }
} // namespace sw

    #endif

#endif
