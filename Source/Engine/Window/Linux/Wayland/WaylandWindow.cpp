#include "pch.h"

#include "Engine/Window/Linux/Wayland/WaylandWindow.h"

#include "Core/Common/Defines.h"
#include "Core/Container/vector.h"

#include "Engine/Window/Linux/Wayland/WaylandInputEvent.h"
#include "Engine/Window/NativeWindowEvent.h"

#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE ) && defined( SW_WITH_WAYLAND )
    #include <fcntl.h>
    #include <linux/input-event-codes.h>
    #include <poll.h>
    #include <sys/mman.h>
    #include <unistd.h>
    #include <wayland-client.h>
    #include <wayland-cursor.h>
    #include <xkbcommon/xkbcommon.h>

    // 구성 때 wayland-scanner 가 만든다(cmake/Modules/Platform/Linux.cmake 의 프로토콜 XML 목록)
    #include "cursor-shape-v1-client-protocol.h"
    #include "fractional-scale-v1-client-protocol.h"
    #include "pointer-constraints-unstable-v1-client-protocol.h"
    #include "relative-pointer-unstable-v1-client-protocol.h"
    #include "xdg-shell-client-protocol.h"

    #include <cerrno>
    #include <csignal>
    #include <cstring>
#endif

namespace sw
{
    SW_LOG_CALLER( "WaylandWindow" );
} // namespace sw

#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE ) && defined( SW_WITH_WAYLAND )
namespace sw
{
    /** @brief 바인드한 `wl_output` 하나와 그 정수 배율입니다. `_bEntered` 는 창(wl_surface)이 이 출력에 걸쳐 있는지입니다. */
    struct WaylandOutput
    {
        wl_output* _pOutput{ nullptr };
        int32      _scale{ 1 };
        bool       _bEntered{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief 라이브러리 객체와 리스너가 쌓은 것입니다. 리스너는 C 콜백이라 이 값의 주소를 사용자 값으로 받는다. */
    struct WaylandWindow::State
    {
        wl_display*       _pDisplay{ nullptr };
        wl_registry*      _pRegistry{ nullptr };
        wl_compositor*    _pCompositor{ nullptr };
        wl_subcompositor* _pSubcompositor{ nullptr }; ///< 개발 콘솔 덧그림(서브표면)이 쓴다 — 없으면 그 덧그림만 못 세운다
        wl_shm*           _pShm{ nullptr };
        xdg_wm_base*      _pWmBase{ nullptr };
        wl_seat*          _pSeat{ nullptr };
        wl_keyboard*      _pKeyboard{ nullptr };
        wl_pointer*       _pPointer{ nullptr };
        wl_surface*       _pSurface{ nullptr };
        xdg_surface*      _pXdgSurface{ nullptr };
        xdg_toplevel*     _pToplevel{ nullptr };
        xkb_context*      _pXkbContext{ nullptr };
        xkb_keymap*       _pKeymap{ nullptr };
        xkb_state*        _pXkbState{ nullptr };
        wl_cursor_theme*  _pCursorTheme{ nullptr };
        wl_surface*       _pCursorSurface{ nullptr };

        wl_data_device_manager* _pDataDeviceManager{ nullptr };
        wl_data_device*         _pDataDevice{ nullptr };
        wl_data_source*         _pDataSource{ nullptr };      ///< 우리가 내놓은 선택(없으면 nullptr). 글은 `_clipboardText`
        wl_data_offer*          _pSelectionOffer{ nullptr };  ///< 다른 프로그램이 내놓은 현재 선택
        wl_data_offer*          _pIntroducedOffer{ nullptr }; ///< 방금 소개된 제안(`selection` 이 오기 전까지 형식을 모은다)
        string                  _clipboardText;

        // 선택 확장(컴포지터에 없으면 nullptr — 없는 것이 정상인 길과 있어야 하는 길은 호출 쪽이 가른다: 소수 배율은 정수 배율로, 포인터 잠금은 호출 쪽이 오류로 알린다).
        wp_fractional_scale_manager_v1*  _pFractionalScaleManager{ nullptr };
        wp_fractional_scale_v1*          _pFractionalScale{ nullptr };
        uint32                           _fractionalScale120{ 0 }; ///< 컴포지터가 알린 선호 배율(1/120 단위, 0 이면 아직 못 받았다)
        zwp_pointer_constraints_v1*      _pPointerConstraints{ nullptr };
        zwp_relative_pointer_manager_v1* _pRelativePointerManager{ nullptr };
        zwp_relative_pointer_v1*         _pRelativePointer{ nullptr };
        zwp_locked_pointer_v1*           _pLockedPointer{ nullptr };
        zwp_confined_pointer_v1*         _pConfinedPointer{ nullptr };
        wp_cursor_shape_manager_v1*      _pCursorShapeManager{ nullptr };
        wp_cursor_shape_device_v1*       _pCursorShapeDevice{ nullptr };              ///< 포인터 장치가 생길 때 만든다
        CursorShape                      _cursorShape{ CursorShape::Arrow };          ///< 기억해 둔 모양(들어올 때 · 보일 때 적용한다)
        WindowPointerLock                _pointerLock{ WindowPointerLock::Released }; ///< 걸어 둔 방식
        bool                             _bPointerLocked{ false };                    ///< 컴포지터가 잠금을 켰다(`locked`) — 이 동안 포인터 움직임 대신 상대 이동이 온다

        vector<WaylandOutput>     _listOutput;                   ///< 바인드한 출력과 그 배율
        vector<WaylandInputEvent> _listPendingInput;             ///< 리스너가 쌓고 `processMessages` 가 넘긴다
        uint32                    _inputSerial{ 0 };             ///< 마지막 키 · 버튼 · 들어옴의 일련번호(선택을 내놓을 때 컴포지터가 요구한다)
        bool                      _bIntroducedHasUtf8{ false };  ///< `_pIntroducedOffer` 가 text/plain;charset=utf-8 을 내놓는지
        bool                      _bIntroducedHasPlain{ false }; ///< 위와 같이 text/plain
        bool                      _bSelectionHasUtf8{ false };   ///< `_pSelectionOffer` 가 내놓는 형식(선택이 된 순간 소개 때 모은 값을 옮긴다)
        bool                      _bSelectionHasPlain{ false };
        uint32                    _pointerSerial{ 0 };   ///< 마지막 포인터 들어옴의 일련번호(커서를 바꿀 때 쓴다)
        int32                     _configuredWidth{ 0 }; ///< 컴포지터가 정한 크기(0 이면 클라이언트가 정한다)
        int32                     _configuredHeight{ 0 };
        bool                      _bConfigured{ false };
        bool                      _bCloseRequested{ false };
        bool                      _bCursorVisible{ true };

        void pushInput( const WaylandInputEvent& event ) { _listPendingInput.push_back( event ); }

        WaylandOutput* findOutput( const wl_output* pOutput )
        {
            for ( WaylandOutput& output : _listOutput )
            {
                if ( output._pOutput == pOutput )
                    return &output;
            }
            return nullptr;
        }

        /** @brief 소개됐지만 선택이 되지 않은 제안(끌어다 놓기 따위)을 버립니다. */
        void dropIntroducedOffer()
        {
            if ( _pIntroducedOffer != nullptr && _pIntroducedOffer != _pSelectionOffer )
                wl_data_offer_destroy( _pIntroducedOffer );
            _pIntroducedOffer    = nullptr;
            _bIntroducedHasUtf8  = false;
            _bIntroducedHasPlain = false;
        }

        /** @brief 걸어 둔 잠금 · 가둠을 풉니다(없으면 아무것도 하지 않는다). */
        void releasePointerConstraint()
        {
            if ( _pLockedPointer != nullptr )
                zwp_locked_pointer_v1_destroy( _pLockedPointer );
            if ( _pConfinedPointer != nullptr )
                zwp_confined_pointer_v1_destroy( _pConfinedPointer );
            _pLockedPointer   = nullptr;
            _pConfinedPointer = nullptr;
            _pointerLock      = WindowPointerLock::Released;
            _bPointerLocked   = false;
        }

        void dropSelectionOffer()
        {
            if ( _pSelectionOffer != nullptr )
                wl_data_offer_destroy( _pSelectionOffer );
            if ( _pIntroducedOffer == _pSelectionOffer )
                _pIntroducedOffer = nullptr;
            _pSelectionOffer    = nullptr;
            _bSelectionHasUtf8  = false;
            _bSelectionHasPlain = false;
        }
    };
} // namespace sw

namespace sw
{
    namespace
    {
        /** @brief 리스너 묶음입니다(C 콜백 — 사용자 값이 `WaylandWindow::State*`). */
        struct WaylandWindowInternal
        {
            using State = WaylandWindow::State;

            /** @brief 바인드할 판 상한입니다 — 리스너 구조체가 이 판까지의 이벤트만 채운다. */
            static constexpr uint32 kCompositorVersion    = 4;
            static constexpr uint32 kSeatVersion          = 5;
            static constexpr uint32 kShmVersion           = 1;
            static constexpr uint32 kSubcompositorVersion = 1;
            static constexpr uint32 kWmBaseVersion        = 1;
            static constexpr uint32 kOutputVersion        = 2; ///< 2 에서 `scale` 이벤트가 생긴다
            /** @brief 1 로 바인드해 끌어다 놓기(판 3) 이벤트가 오지 않게 한다 — 선택(클립보드)만 쓴다. */
            static constexpr uint32 kDataDeviceManagerVersion = 1;
            /** @brief 세 확장은 모두 판 1 만 있다 — 리스너 구조체도 판 1 의 이벤트를 모두 채운다. 판이 오르면 여기와 리스너를 함께 올린다(컴파일러가 알려 주지 않는다). */
            static constexpr uint32 kFractionalScaleVersion    = 1;
            static constexpr uint32 kPointerConstraintsVersion = 1;
            static constexpr uint32 kRelativePointerVersion    = 1;
            static constexpr uint32 kCursorShapeVersion        = 1; ///< 판 1 의 모양 목록만 쓴다(`mapCursorShape`)

            /** @brief 엔진 커서 모양을 `wp_cursor_shape_device_v1` 의 모양 번호로 바꿉니다. */
            static uint32 mapCursorShape( CursorShape shape )
            {
                switch ( shape )
                {
                    case CursorShape::Hand:
                        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;
                    case CursorShape::TextBeam:
                        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT;
                    case CursorShape::ResizeHorizontal:
                        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_EW_RESIZE;
                    case CursorShape::ResizeVertical:
                        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NS_RESIZE;
                    case CursorShape::NotAllowed:
                        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NOT_ALLOWED;
                    case CursorShape::Arrow:
                        break;
                }
                return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
            }
            /** @brief `wp_fractional_scale_v1.preferred_scale` 의 분모입니다(배율 = 값 / 120). */
            static constexpr float32 kFractionalScaleDenominator = 120.0f;
            /** @brief 클립보드 글 형식입니다(우선순위 순서). */
            static constexpr const utf8* kMimeUtf8  = "text/plain;charset=utf-8";
            static constexpr const utf8* kMimePlain = "text/plain";
            /** @brief 클립보드 읽기 · 쓰기가 상대를 기다리는 한 번의 한도(ms)입니다(X11 의 200 ms 와 같다). */
            static constexpr int32 kClipboardWaitMs = 200;
            /** @brief 클립보드 글 한 번에 받는 최대 바이트입니다(끝없이 보내는 상대를 끊는다). */
            static constexpr size_t kClipboardMaxBytes = 16u * constant::kBytesPerMegabyte;
            /** @brief 휠 한 칸에 해당하는 축 값입니다(컴포지터 관례 — 마우스 휠 한 칸이 10). */
            static constexpr float32 kAxisUnitsPerNotch = 10.0f;
            /** @brief 기본 커서 테마 크기(픽셀)입니다. */
            static constexpr int32 kCursorSize = 24;

            static void onRegistryGlobal( void* pData, wl_registry* pRegistry, uint32 name, const utf8* pInterface, uint32 version )
            {
                State& state = *static_cast<State*>( pData );
                if ( std::strcmp( pInterface, wl_compositor_interface.name ) == 0 )
                    state._pCompositor = static_cast<wl_compositor*>( wl_registry_bind( pRegistry, name, &wl_compositor_interface, version < kCompositorVersion ? version : kCompositorVersion ) );
                else if ( std::strcmp( pInterface, wl_subcompositor_interface.name ) == 0 )
                    state._pSubcompositor = static_cast<wl_subcompositor*>( wl_registry_bind( pRegistry, name, &wl_subcompositor_interface, kSubcompositorVersion ) );
                else if ( std::strcmp( pInterface, wl_shm_interface.name ) == 0 )
                    state._pShm = static_cast<wl_shm*>( wl_registry_bind( pRegistry, name, &wl_shm_interface, kShmVersion ) );
                else if ( std::strcmp( pInterface, xdg_wm_base_interface.name ) == 0 )
                    state._pWmBase = static_cast<xdg_wm_base*>( wl_registry_bind( pRegistry, name, &xdg_wm_base_interface, kWmBaseVersion ) );
                else if ( std::strcmp( pInterface, wl_seat_interface.name ) == 0 )
                    state._pSeat = static_cast<wl_seat*>( wl_registry_bind( pRegistry, name, &wl_seat_interface, version < kSeatVersion ? version : kSeatVersion ) );
                else if ( std::strcmp( pInterface, wl_data_device_manager_interface.name ) == 0 )
                    state._pDataDeviceManager = static_cast<wl_data_device_manager*>( wl_registry_bind( pRegistry, name, &wl_data_device_manager_interface, kDataDeviceManagerVersion ) );
                else if ( std::strcmp( pInterface, wp_fractional_scale_manager_v1_interface.name ) == 0 )
                    state._pFractionalScaleManager = static_cast<wp_fractional_scale_manager_v1*>( wl_registry_bind( pRegistry, name, &wp_fractional_scale_manager_v1_interface, kFractionalScaleVersion ) );
                else if ( std::strcmp( pInterface, wp_cursor_shape_manager_v1_interface.name ) == 0 )
                    state._pCursorShapeManager = static_cast<wp_cursor_shape_manager_v1*>( wl_registry_bind( pRegistry, name, &wp_cursor_shape_manager_v1_interface, kCursorShapeVersion ) );
                else if ( std::strcmp( pInterface, zwp_pointer_constraints_v1_interface.name ) == 0 )
                    state._pPointerConstraints = static_cast<zwp_pointer_constraints_v1*>( wl_registry_bind( pRegistry, name, &zwp_pointer_constraints_v1_interface, kPointerConstraintsVersion ) );
                else if ( std::strcmp( pInterface, zwp_relative_pointer_manager_v1_interface.name ) == 0 )
                    state._pRelativePointerManager = static_cast<zwp_relative_pointer_manager_v1*>( wl_registry_bind( pRegistry, name, &zwp_relative_pointer_manager_v1_interface, kRelativePointerVersion ) );
                else if ( std::strcmp( pInterface, wl_output_interface.name ) == 0 )
                {
                    WaylandOutput output;
                    output._pOutput = static_cast<wl_output*>( wl_registry_bind( pRegistry, name, &wl_output_interface, version < kOutputVersion ? version : kOutputVersion ) );
                    state._listOutput.push_back( output );
                    wl_output_add_listener( output._pOutput, &s_outputListener, &state );
                }
            }

            static void onOutputGeometry( void* pData, wl_output* pOutput, int32 x, int32 y, int32 physicalWidth, int32 physicalHeight, int32 subpixel, const utf8* pMake, const utf8* pModel, int32 transform )
            {
                (void)pData;
                (void)pOutput;
                (void)x;
                (void)y;
                (void)physicalWidth;
                (void)physicalHeight;
                (void)subpixel;
                (void)pMake;
                (void)pModel;
                (void)transform;
            }

            static void onOutputMode( void* pData, wl_output* pOutput, uint32 flags, int32 width, int32 height, int32 refresh )
            {
                (void)pData;
                (void)pOutput;
                (void)flags;
                (void)width;
                (void)height;
                (void)refresh;
            }

            static void onOutputDone( void* pData, wl_output* pOutput )
            {
                (void)pData;
                (void)pOutput;
            }

            static void onOutputScale( void* pData, wl_output* pOutput, int32 factor )
            {
                State&         state  = *static_cast<State*>( pData );
                WaylandOutput* pEntry = state.findOutput( pOutput );
                if ( pEntry != nullptr && factor > 0 )
                    pEntry->_scale = factor;
            }

            static void onSurfaceEnter( void* pData, wl_surface* pSurface, wl_output* pOutput )
            {
                (void)pSurface;
                State&         state  = *static_cast<State*>( pData );
                WaylandOutput* pEntry = state.findOutput( pOutput );
                if ( pEntry != nullptr )
                    pEntry->_bEntered = true;
            }

            static void onSurfaceLeave( void* pData, wl_surface* pSurface, wl_output* pOutput )
            {
                (void)pSurface;
                State&         state  = *static_cast<State*>( pData );
                WaylandOutput* pEntry = state.findOutput( pOutput );
                if ( pEntry != nullptr )
                    pEntry->_bEntered = false;
            }

            static void onDataOfferMime( void* pData, wl_data_offer* pOffer, const utf8* pMime )
            {
                State& state = *static_cast<State*>( pData );
                if ( pOffer != state._pIntroducedOffer )
                    return; // 낡은 제안의 형식이다 — 버려진 뒤에는 오지 않지만 순서가 뒤집힌 알림을 믿지 않는다
                if ( std::strcmp( pMime, kMimeUtf8 ) == 0 )
                    state._bIntroducedHasUtf8 = true;
                else if ( std::strcmp( pMime, kMimePlain ) == 0 )
                    state._bIntroducedHasPlain = true;
            }

            static void onDataOfferSourceActions( void* pData, wl_data_offer* pOffer, uint32 sourceActions )
            {
                (void)pData;
                (void)pOffer;
                (void)sourceActions; // 판 1 로 바인드해 오지 않는다 — 끌어다 놓기는 하지 않는다
            }

            static void onDataOfferAction( void* pData, wl_data_offer* pOffer, uint32 action )
            {
                (void)pData;
                (void)pOffer;
                (void)action;
            }

            static void onDataDeviceOffer( void* pData, wl_data_device* pDevice, wl_data_offer* pOffer )
            {
                (void)pDevice;
                State& state = *static_cast<State*>( pData );
                state.dropIntroducedOffer();
                state._pIntroducedOffer = pOffer;
                wl_data_offer_add_listener( pOffer, &s_dataOfferListener, &state );
            }

            static void onDataDeviceEnter( void* pData, wl_data_device* pDevice, uint32 serial, wl_surface* pSurface, wl_fixed_t x, wl_fixed_t y, wl_data_offer* pOffer )
            {
                (void)pData;
                (void)pDevice;
                (void)serial;
                (void)pSurface;
                (void)x;
                (void)y;
                (void)pOffer; // 끌어다 놓기는 받지 않는다(제안은 다음 `data_offer` 가 버린다)
            }

            static void onDataDeviceLeave( void* pData, wl_data_device* pDevice )
            {
                (void)pData;
                (void)pDevice;
            }

            static void onDataDeviceMotion( void* pData, wl_data_device* pDevice, uint32 time, wl_fixed_t x, wl_fixed_t y )
            {
                (void)pData;
                (void)pDevice;
                (void)time;
                (void)x;
                (void)y;
            }

            static void onDataDeviceDrop( void* pData, wl_data_device* pDevice )
            {
                (void)pData;
                (void)pDevice;
            }

            static void onDataDeviceSelection( void* pData, wl_data_device* pDevice, wl_data_offer* pOffer )
            {
                (void)pDevice;
                State& state = *static_cast<State*>( pData );
                if ( pOffer == state._pSelectionOffer )
                    return;
                state.dropSelectionOffer();
                if ( pOffer == nullptr )
                    return; // 선택이 비었다
                state._pSelectionOffer    = pOffer;
                state._bSelectionHasUtf8  = state._bIntroducedHasUtf8;
                state._bSelectionHasPlain = state._bIntroducedHasPlain;
            }

            static void onDataSourceTarget( void* pData, wl_data_source* pSource, const utf8* pMime )
            {
                (void)pData;
                (void)pSource;
                (void)pMime;
            }

            static void onDataSourceSend( void* pData, wl_data_source* pSource, const utf8* pMime, int32 fd )
            {
                State&     state      = *static_cast<State*>( pData );
                const bool bKnownMime = std::strcmp( pMime, kMimeUtf8 ) == 0 || std::strcmp( pMime, kMimePlain ) == 0;
                if ( pSource != state._pDataSource || bKnownMime == false )
                {
                    close( fd );
                    return;
                }
                // 상대가 느리면 주 스레드가 오래 서지 않도록 막지 않는 쓰기로 `kClipboardWaitMs` 안에 못 쓰면 포기한다.
                // 상대가 읽기를 닫았으면 쓰기가 SIGPIPE 로 프로세스를 죽인다 — 오류 코드로 받도록 무시로 둔다(프로세스 전역, `PosixProcess.cpp` 와 같다).
                (void)signal( SIGPIPE, SIG_IGN );
                const int32 flags = fcntl( fd, F_GETFL, 0 );
                if ( flags != -1 )
                    fcntl( fd, F_SETFL, flags | O_NONBLOCK );
                const utf8* pCursor    = state._clipboardText.data();
                size_t      remaining  = state._clipboardText.size();
                bool        bCompleted = true;
                while ( remaining > 0 )
                {
                    const ssize_t written = write( fd, pCursor, remaining );
                    if ( written > 0 )
                    {
                        pCursor += written;
                        remaining -= static_cast<size_t>( written );
                        continue;
                    }
                    pollfd     waitEntry{ fd, POLLOUT, 0 };
                    const bool bWouldBlock = written == -1 && ( errno == EAGAIN || errno == EWOULDBLOCK );
                    if ( bWouldBlock == false || poll( &waitEntry, 1, kClipboardWaitMs ) <= 0 )
                    {
                        bCompleted = false;
                        break;
                    }
                }
                close( fd );
                if ( bCompleted == false )
                    SW_LOG_WARNING( "Wayland clipboard: the reader stopped taking the text - %# bytes were not sent", static_cast<uint64>( remaining ) );
            }

            static void onDataSourceCancelled( void* pData, wl_data_source* pSource )
            {
                State& state = *static_cast<State*>( pData );
                if ( pSource == state._pDataSource )
                {
                    state._pDataSource = nullptr; // 다른 프로그램이 선택을 가져갔다
                    state._clipboardText.clear();
                }
                wl_data_source_destroy( pSource );
            }

            static void onFractionalScalePreferred( void* pData, wp_fractional_scale_v1* pFractionalScale, uint32 scale120 )
            {
                (void)pFractionalScale;
                static_cast<State*>( pData )->_fractionalScale120 = scale120;
            }

            static void onLockedPointerLocked( void* pData, zwp_locked_pointer_v1* pLockedPointer )
            {
                (void)pLockedPointer;
                static_cast<State*>( pData )->_bPointerLocked = true;
            }

            static void onLockedPointerUnlocked( void* pData, zwp_locked_pointer_v1* pLockedPointer )
            {
                (void)pLockedPointer;
                static_cast<State*>( pData )->_bPointerLocked = false; // 포커스를 잃었다 — 영속 수명이라 포커스가 돌아오면 `locked` 가 다시 온다
            }

            static void onConfinedPointerConfined( void* pData, zwp_confined_pointer_v1* pConfinedPointer )
            {
                (void)pData;
                (void)pConfinedPointer; // 가둠은 움직임을 그대로 받는다 — 알 일이 없다
            }

            static void onConfinedPointerUnconfined( void* pData, zwp_confined_pointer_v1* pConfinedPointer )
            {
                (void)pData;
                (void)pConfinedPointer;
            }

            static void onRelativePointerMotion( void* pData, zwp_relative_pointer_v1* pRelativePointer, uint32 timeHigh, uint32 timeLow, wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t dxUnaccelerated, wl_fixed_t dyUnaccelerated )
            {
                (void)pRelativePointer;
                (void)timeHigh;
                (void)timeLow;
                (void)dx;
                (void)dy;
                State& state = *static_cast<State*>( pData );
                if ( state._bPointerLocked == false )
                    return; // 잠기지 않았으면 같은 움직임이 `wl_pointer.motion` 으로 오고, 이중으로 세지 않는다
                WaylandInputEvent event;
                event._kind      = WaylandInputKind::PointerRelative;
                event._relativeX = static_cast<float32>( wl_fixed_to_double( dxUnaccelerated ) );
                event._relativeY = static_cast<float32>( wl_fixed_to_double( dyUnaccelerated ) );
                state.pushInput( event );
            }

            static void onRegistryGlobalRemove( void* pData, wl_registry* pRegistry, uint32 name )
            {
                (void)pData;
                (void)pRegistry;
                (void)name; // 합성기 · 셸이 사라지는 일은 세션 끝이다 — 다음 요청이 실패로 알린다
            }

            static void onWmBasePing( void* pData, xdg_wm_base* pWmBase, uint32 serial )
            {
                (void)pData;
                xdg_wm_base_pong( pWmBase, serial );
            }

            static void onXdgSurfaceConfigure( void* pData, xdg_surface* pXdgSurface, uint32 serial )
            {
                State& state = *static_cast<State*>( pData );
                xdg_surface_ack_configure( pXdgSurface, serial );
                state._bConfigured = true;
            }

            static void onToplevelConfigure( void* pData, xdg_toplevel* pToplevel, int32 width, int32 height, wl_array* pStates )
            {
                (void)pToplevel;
                (void)pStates;
                State& state            = *static_cast<State*>( pData );
                state._configuredWidth  = width;
                state._configuredHeight = height;
            }

            static void onToplevelClose( void* pData, xdg_toplevel* pToplevel )
            {
                (void)pToplevel;
                static_cast<State*>( pData )->_bCloseRequested = true;
            }

            static void onKeyboardKeymap( void* pData, wl_keyboard* pKeyboard, uint32 format, int32 fd, uint32 size )
            {
                (void)pKeyboard;
                State& state = *static_cast<State*>( pData );
                if ( format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 )
                {
                    close( fd );
                    SW_LOG_ERROR( "Wayland keyboard sent a keymap that is not XKB v1 - keys are not read" );
                    return;
                }
                void* const pMapped = mmap( nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0 );
                close( fd );
                if ( pMapped == MAP_FAILED )
                {
                    SW_LOG_ERROR( "Wayland keymap could not be mapped - keys are not read" );
                    return;
                }
                xkb_keymap* const pKeymap = xkb_keymap_new_from_string( state._pXkbContext, static_cast<const utf8*>( pMapped ), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS );
                munmap( pMapped, size );
                if ( pKeymap == nullptr )
                {
                    SW_LOG_ERROR( "Wayland keymap could not be compiled - keys are not read" );
                    return;
                }
                xkb_state_unref( state._pXkbState );
                xkb_keymap_unref( state._pKeymap );
                state._pKeymap   = pKeymap;
                state._pXkbState = xkb_state_new( pKeymap );
            }

            static void onKeyboardEnter( void* pData, wl_keyboard* pKeyboard, uint32 serial, wl_surface* pSurface, wl_array* pKeys )
            {
                (void)pKeyboard;
                (void)pSurface;
                (void)pKeys;
                static_cast<State*>( pData )->_inputSerial = serial;
                WaylandInputEvent event;
                event._kind = WaylandInputKind::FocusGained;
                static_cast<State*>( pData )->pushInput( event );
            }

            static void onKeyboardLeave( void* pData, wl_keyboard* pKeyboard, uint32 serial, wl_surface* pSurface )
            {
                (void)pKeyboard;
                (void)serial;
                (void)pSurface;
                WaylandInputEvent event;
                event._kind = WaylandInputKind::FocusLost;
                static_cast<State*>( pData )->pushInput( event );
            }

            static void onKeyboardKey( void* pData, wl_keyboard* pKeyboard, uint32 serial, uint32 time, uint32 key, uint32 keyState )
            {
                (void)pKeyboard;
                (void)time;
                State& state       = *static_cast<State*>( pData );
                state._inputSerial = serial;
                if ( state._pXkbState == nullptr )
                    return;
                // evdev 키 번호 + 8 이 XKB 키 코드다(xkbcommon 규약).
                const xkb_keycode_t keyCode = key + 8;
                const bool          bDown   = keyState == WL_KEYBOARD_KEY_STATE_PRESSED;
                WaylandInputEvent   event;
                event._kind   = bDown ? WaylandInputKind::KeyDown : WaylandInputKind::KeyUp;
                event._keySym = xkb_state_key_get_one_sym( state._pXkbState, keyCode );
                state.pushInput( event );
                if ( bDown == false )
                    return;
                WaylandInputEvent text;
                text._kind             = WaylandInputKind::Text;
                const int32 byteCount  = xkb_state_key_get_utf8( state._pXkbState, keyCode, text._arrText, WaylandInputEvent::kTextCapacity );
                const bool  bPrintable = byteCount > 0 && static_cast<uint8>( text._arrText[0] ) >= 0x20 && text._arrText[0] != 0x7F;
                if ( bPrintable )
                    state.pushInput( text );
            }

            static void onKeyboardModifiers( void* pData, wl_keyboard* pKeyboard, uint32 serial, uint32 depressed, uint32 latched, uint32 locked, uint32 group )
            {
                (void)pKeyboard;
                (void)serial;
                State& state = *static_cast<State*>( pData );
                if ( state._pXkbState != nullptr )
                    xkb_state_update_mask( state._pXkbState, depressed, latched, locked, 0, 0, group );
            }

            static void onKeyboardRepeatInfo( void* pData, wl_keyboard* pKeyboard, int32 rate, int32 delay )
            {
                (void)pData;
                (void)pKeyboard;
                (void)rate;
                (void)delay; // 자동 반복은 엔진이 하지 않는다(X11 의 감지 가능한 자동 반복과 같다 — 누름은 한 번)
            }

            static void onPointerEnter( void* pData, wl_pointer* pPointer, uint32 serial, wl_surface* pSurface, wl_fixed_t x, wl_fixed_t y )
            {
                (void)pPointer;
                (void)pSurface;
                State& state         = *static_cast<State*>( pData );
                state._pointerSerial = serial;
                WaylandInputEvent enter;
                enter._kind = WaylandInputKind::PointerEnter;
                state.pushInput( enter );
                WaylandInputEvent move;
                move._kind = WaylandInputKind::PointerMove;
                move._x    = wl_fixed_to_int( x );
                move._y    = wl_fixed_to_int( y );
                state.pushInput( move );
                if ( state._bCursorVisible == false )
                    wl_pointer_set_cursor( state._pPointer, serial, nullptr, 0, 0 );
                else if ( state._pCursorShapeDevice != nullptr )
                    wp_cursor_shape_device_v1_set_shape( state._pCursorShapeDevice, serial, mapCursorShape( state._cursorShape ) ); // 들어올 때마다 커서를 다시 정해야 한다
            }

            static void onPointerLeave( void* pData, wl_pointer* pPointer, uint32 serial, wl_surface* pSurface )
            {
                (void)pPointer;
                (void)serial;
                (void)pSurface;
                WaylandInputEvent event;
                event._kind = WaylandInputKind::PointerLeave;
                static_cast<State*>( pData )->pushInput( event );
            }

            static void onPointerMotion( void* pData, wl_pointer* pPointer, uint32 time, wl_fixed_t x, wl_fixed_t y )
            {
                (void)pPointer;
                (void)time;
                WaylandInputEvent event;
                event._kind = WaylandInputKind::PointerMove;
                event._x    = wl_fixed_to_int( x );
                event._y    = wl_fixed_to_int( y );
                static_cast<State*>( pData )->pushInput( event );
            }

            static void onPointerButton( void* pData, wl_pointer* pPointer, uint32 serial, uint32 time, uint32 button, uint32 buttonState )
            {
                (void)pPointer;
                (void)time;
                static_cast<State*>( pData )->_inputSerial = serial;
                WaylandInputEvent event;
                event._kind   = buttonState == WL_POINTER_BUTTON_STATE_PRESSED ? WaylandInputKind::PointerDown : WaylandInputKind::PointerUp;
                event._button = button;
                static_cast<State*>( pData )->pushInput( event );
            }

            static void onPointerAxis( void* pData, wl_pointer* pPointer, uint32 time, uint32 axis, wl_fixed_t value )
            {
                (void)pPointer;
                (void)time;
                // 축 값은 아래(세로) · 오른쪽(가로)이 + 다. 엔진 휠은 위가 + 라 세로는 부호를 뒤집는다(X11 Button4 = +1 과 같은 뜻).
                const float32     notches = static_cast<float32>( wl_fixed_to_double( value ) ) / kAxisUnitsPerNotch;
                WaylandInputEvent event;
                event._kind = WaylandInputKind::PointerWheel;
                if ( axis == WL_POINTER_AXIS_VERTICAL_SCROLL )
                    event._wheel = -notches;
                else
                    event._horizontalWheel = notches;
                static_cast<State*>( pData )->pushInput( event );
            }

            static void onPointerFrame( void* pData, wl_pointer* pPointer )
            {
                (void)pData;
                (void)pPointer; // 이벤트를 하나씩 넘긴다 — 묶음 경계는 쓰지 않는다
            }

            static void onPointerAxisSource( void* pData, wl_pointer* pPointer, uint32 source )
            {
                (void)pData;
                (void)pPointer;
                (void)source;
            }

            static void onPointerAxisStop( void* pData, wl_pointer* pPointer, uint32 time, uint32 axis )
            {
                (void)pData;
                (void)pPointer;
                (void)time;
                (void)axis;
            }

            static void onPointerAxisDiscrete( void* pData, wl_pointer* pPointer, uint32 axis, int32 discrete )
            {
                (void)pData;
                (void)pPointer;
                (void)axis;
                (void)discrete; // 칸 수는 축 값(`onPointerAxis`)으로 이미 센다
            }

            static void onSeatCapabilities( void* pData, wl_seat* pSeat, uint32 capabilities );

            static void onSeatName( void* pData, wl_seat* pSeat, const utf8* pName )
            {
                (void)pData;
                (void)pSeat;
                (void)pName;
            }

            static wl_registry_listener makeRegistryListener()
            {
                wl_registry_listener listener{};
                listener.global        = &onRegistryGlobal;
                listener.global_remove = &onRegistryGlobalRemove;
                return listener;
            }

            static xdg_wm_base_listener makeWmBaseListener()
            {
                xdg_wm_base_listener listener{};
                listener.ping = &onWmBasePing;
                return listener;
            }

            static xdg_surface_listener makeXdgSurfaceListener()
            {
                xdg_surface_listener listener{};
                listener.configure = &onXdgSurfaceConfigure;
                return listener;
            }

            static xdg_toplevel_listener makeToplevelListener()
            {
                // 판 1 의 두 이벤트만 채운다 — 판 1 로 바인드해 그 뒤 이벤트(configure_bounds · wm_capabilities)는 오지 않는다.
                xdg_toplevel_listener listener{};
                listener.configure = &onToplevelConfigure;
                listener.close     = &onToplevelClose;
                return listener;
            }

            static wl_keyboard_listener makeKeyboardListener()
            {
                wl_keyboard_listener listener{};
                listener.keymap      = &onKeyboardKeymap;
                listener.enter       = &onKeyboardEnter;
                listener.leave       = &onKeyboardLeave;
                listener.key         = &onKeyboardKey;
                listener.modifiers   = &onKeyboardModifiers;
                listener.repeat_info = &onKeyboardRepeatInfo;
                return listener;
            }

            static wl_pointer_listener makePointerListener()
            {
                // 자리 판 5 까지의 이벤트를 채운다(바인드도 5 까지 — `kSeatVersion`).
                wl_pointer_listener listener{};
                listener.enter         = &onPointerEnter;
                listener.leave         = &onPointerLeave;
                listener.motion        = &onPointerMotion;
                listener.button        = &onPointerButton;
                listener.axis          = &onPointerAxis;
                listener.frame         = &onPointerFrame;
                listener.axis_source   = &onPointerAxisSource;
                listener.axis_stop     = &onPointerAxisStop;
                listener.axis_discrete = &onPointerAxisDiscrete;
                return listener;
            }

            static wl_output_listener makeOutputListener()
            {
                wl_output_listener listener{};
                listener.geometry = &onOutputGeometry;
                listener.mode     = &onOutputMode;
                listener.done     = &onOutputDone;
                listener.scale    = &onOutputScale;
                return listener;
            }

            static wl_surface_listener makeSurfaceListener()
            {
                // 판 4 로 바인드해 판 6 의 선호 배율 이벤트는 오지 않는다.
                wl_surface_listener listener{};
                listener.enter = &onSurfaceEnter;
                listener.leave = &onSurfaceLeave;
                return listener;
            }

            static wl_data_offer_listener makeDataOfferListener()
            {
                wl_data_offer_listener listener{};
                listener.offer          = &onDataOfferMime;
                listener.source_actions = &onDataOfferSourceActions;
                listener.action         = &onDataOfferAction;
                return listener;
            }

            static wl_data_device_listener makeDataDeviceListener()
            {
                wl_data_device_listener listener{};
                listener.data_offer = &onDataDeviceOffer;
                listener.enter      = &onDataDeviceEnter;
                listener.leave      = &onDataDeviceLeave;
                listener.motion     = &onDataDeviceMotion;
                listener.drop       = &onDataDeviceDrop;
                listener.selection  = &onDataDeviceSelection;
                return listener;
            }

            static wl_data_source_listener makeDataSourceListener()
            {
                wl_data_source_listener listener{};
                listener.target    = &onDataSourceTarget;
                listener.send      = &onDataSourceSend;
                listener.cancelled = &onDataSourceCancelled;
                return listener;
            }

            static wl_seat_listener makeSeatListener()
            {
                wl_seat_listener listener{};
                listener.capabilities = &onSeatCapabilities;
                listener.name         = &onSeatName;
                return listener;
            }

            static wp_fractional_scale_v1_listener makeFractionalScaleListener()
            {
                wp_fractional_scale_v1_listener listener{};
                listener.preferred_scale = &onFractionalScalePreferred;
                return listener;
            }

            static zwp_locked_pointer_v1_listener makeLockedPointerListener()
            {
                zwp_locked_pointer_v1_listener listener{};
                listener.locked   = &onLockedPointerLocked;
                listener.unlocked = &onLockedPointerUnlocked;
                return listener;
            }

            static zwp_confined_pointer_v1_listener makeConfinedPointerListener()
            {
                zwp_confined_pointer_v1_listener listener{};
                listener.confined   = &onConfinedPointerConfined;
                listener.unconfined = &onConfinedPointerUnconfined;
                return listener;
            }

            static zwp_relative_pointer_v1_listener makeRelativePointerListener()
            {
                zwp_relative_pointer_v1_listener listener{};
                listener.relative_motion = &onRelativePointerMotion;
                return listener;
            }

            // 리스너는 객체가 사는 동안 그 자리에 있어야 한다 — 정적 상수로 둔다(상태를 갖지 않는다).
            static const wl_registry_listener             s_registryListener;
            static const xdg_wm_base_listener             s_wmBaseListener;
            static const xdg_surface_listener             s_xdgSurfaceListener;
            static const xdg_toplevel_listener            s_toplevelListener;
            static const wl_keyboard_listener             s_keyboardListener;
            static const wl_pointer_listener              s_pointerListener;
            static const wl_seat_listener                 s_seatListener;
            static const wl_output_listener               s_outputListener;
            static const wl_surface_listener              s_surfaceListener;
            static const wl_data_offer_listener           s_dataOfferListener;
            static const wl_data_device_listener          s_dataDeviceListener;
            static const wl_data_source_listener          s_dataSourceListener;
            static const wp_fractional_scale_v1_listener  s_fractionalScaleListener;
            static const zwp_locked_pointer_v1_listener   s_lockedPointerListener;
            static const zwp_confined_pointer_v1_listener s_confinedPointerListener;
            static const zwp_relative_pointer_v1_listener s_relativePointerListener;
        };

        const wl_registry_listener             WaylandWindowInternal::s_registryListener        = WaylandWindowInternal::makeRegistryListener();
        const xdg_wm_base_listener             WaylandWindowInternal::s_wmBaseListener          = WaylandWindowInternal::makeWmBaseListener();
        const xdg_surface_listener             WaylandWindowInternal::s_xdgSurfaceListener      = WaylandWindowInternal::makeXdgSurfaceListener();
        const xdg_toplevel_listener            WaylandWindowInternal::s_toplevelListener        = WaylandWindowInternal::makeToplevelListener();
        const wl_keyboard_listener             WaylandWindowInternal::s_keyboardListener        = WaylandWindowInternal::makeKeyboardListener();
        const wl_pointer_listener              WaylandWindowInternal::s_pointerListener         = WaylandWindowInternal::makePointerListener();
        const wl_seat_listener                 WaylandWindowInternal::s_seatListener            = WaylandWindowInternal::makeSeatListener();
        const wl_output_listener               WaylandWindowInternal::s_outputListener          = WaylandWindowInternal::makeOutputListener();
        const wl_surface_listener              WaylandWindowInternal::s_surfaceListener         = WaylandWindowInternal::makeSurfaceListener();
        const wl_data_offer_listener           WaylandWindowInternal::s_dataOfferListener       = WaylandWindowInternal::makeDataOfferListener();
        const wl_data_device_listener          WaylandWindowInternal::s_dataDeviceListener      = WaylandWindowInternal::makeDataDeviceListener();
        const wl_data_source_listener          WaylandWindowInternal::s_dataSourceListener      = WaylandWindowInternal::makeDataSourceListener();
        const wp_fractional_scale_v1_listener  WaylandWindowInternal::s_fractionalScaleListener = WaylandWindowInternal::makeFractionalScaleListener();
        const zwp_locked_pointer_v1_listener   WaylandWindowInternal::s_lockedPointerListener   = WaylandWindowInternal::makeLockedPointerListener();
        const zwp_confined_pointer_v1_listener WaylandWindowInternal::s_confinedPointerListener = WaylandWindowInternal::makeConfinedPointerListener();
        const zwp_relative_pointer_v1_listener WaylandWindowInternal::s_relativePointerListener = WaylandWindowInternal::makeRelativePointerListener();

        void WaylandWindowInternal::onSeatCapabilities( void* pData, wl_seat* pSeat, uint32 capabilities )
        {
            State&     state     = *static_cast<State*>( pData );
            const bool bKeyboard = ( capabilities & WL_SEAT_CAPABILITY_KEYBOARD ) != 0;
            const bool bPointer  = ( capabilities & WL_SEAT_CAPABILITY_POINTER ) != 0;
            if ( bKeyboard && state._pKeyboard == nullptr )
            {
                state._pKeyboard = wl_seat_get_keyboard( pSeat );
                wl_keyboard_add_listener( state._pKeyboard, &s_keyboardListener, &state );
            }
            if ( bPointer && state._pPointer == nullptr )
            {
                state._pPointer = wl_seat_get_pointer( pSeat );
                wl_pointer_add_listener( state._pPointer, &s_pointerListener, &state );
                if ( state._pRelativePointerManager != nullptr )
                {
                    state._pRelativePointer = zwp_relative_pointer_manager_v1_get_relative_pointer( state._pRelativePointerManager, state._pPointer );
                    zwp_relative_pointer_v1_add_listener( state._pRelativePointer, &s_relativePointerListener, &state );
                }
                if ( state._pCursorShapeManager != nullptr )
                    state._pCursorShapeDevice = wp_cursor_shape_manager_v1_get_pointer( state._pCursorShapeManager, state._pPointer );
            }
        }
    } // namespace
} // namespace sw

namespace sw
{
    WaylandWindow::WaylandWindow()
        : _pState{ make_unique<State>() }
    {
        clearRestorePosition();
    }

    WaylandWindow::~WaylandWindow()
    {
        WaylandWindow::destroy();
    }

    void WaylandWindow::clearRestorePosition()
    {
        _restoreX = 0;
        _restoreY = 0;
    }

    bool WaylandWindow::initializeWindow( const utf8* pTitle, uint32 width, uint32 height )
    {
        using Internal = WaylandWindowInternal;
        _width         = width;
        _height        = height;
        if ( storeTitle( pTitle ) == false )
            return false; // 오류는 storeTitle 이 남겼다

        State& state    = *_pState;
        state           = State{};
        state._pDisplay = wl_display_connect( nullptr );
        if ( state._pDisplay == nullptr )
        {
            SW_LOG_ERROR( "Wayland: no compositor to connect to (WAYLAND_DISPLAY) - the window is not created" );
            return false;
        }
        state._pXkbContext = xkb_context_new( XKB_CONTEXT_NO_FLAGS );
        state._pRegistry   = wl_display_get_registry( state._pDisplay );
        wl_registry_add_listener( state._pRegistry, &Internal::s_registryListener, &state );
        wl_display_roundtrip( state._pDisplay );
        const bool bMissingGlobals = state._pCompositor == nullptr || state._pWmBase == nullptr;
        if ( bMissingGlobals )
        {
            SW_LOG_ERROR( "Wayland: the compositor has no wl_compositor or xdg_wm_base - the window is not created" );
            destroy();
            return false;
        }
        xdg_wm_base_add_listener( state._pWmBase, &Internal::s_wmBaseListener, &state );
        if ( state._pSeat != nullptr )
            wl_seat_add_listener( state._pSeat, &Internal::s_seatListener, &state );
        if ( state._pSeat != nullptr && state._pDataDeviceManager != nullptr )
        {
            state._pDataDevice = wl_data_device_manager_get_data_device( state._pDataDeviceManager, state._pSeat );
            wl_data_device_add_listener( state._pDataDevice, &Internal::s_dataDeviceListener, &state );
        }

        state._pSurface = wl_compositor_create_surface( state._pCompositor );
        wl_surface_add_listener( state._pSurface, &Internal::s_surfaceListener, &state );
        if ( state._pFractionalScaleManager != nullptr )
        {
            // 첫 commit 전에 만든다 — 컴포지터는 객체를 만들 때와 배율이 바뀔 때 `preferred_scale` 을 보낸다.
            state._pFractionalScale = wp_fractional_scale_manager_v1_get_fractional_scale( state._pFractionalScaleManager, state._pSurface );
            wp_fractional_scale_v1_add_listener( state._pFractionalScale, &Internal::s_fractionalScaleListener, &state );
        }
        state._pXdgSurface = xdg_wm_base_get_xdg_surface( state._pWmBase, state._pSurface );
        xdg_surface_add_listener( state._pXdgSurface, &Internal::s_xdgSurfaceListener, &state );
        state._pToplevel = xdg_surface_get_toplevel( state._pXdgSurface );
        xdg_toplevel_add_listener( state._pToplevel, &Internal::s_toplevelListener, &state );
        xdg_toplevel_set_app_id( state._pToplevel, "sw.engine" );
        applyTitle();
        applyMinimumClientSize();
        wl_surface_commit( state._pSurface );
        // 첫 configure 를 기다린다 — 그 전에는 그림을 붙이면 안 된다(xdg-shell 규약).
        int32 dispatchResult = 0;
        while ( state._bConfigured == false && dispatchResult != -1 )
        {
            dispatchResult = wl_display_dispatch( state._pDisplay );
        }
        if ( state._bConfigured == false )
        {
            SW_LOG_ERROR( "Wayland: the compositor closed the connection before configuring the window" );
            destroy();
            return false;
        }
        if ( state._configuredWidth > 0 && state._configuredHeight > 0 )
        {
            _width  = static_cast<uint32>( state._configuredWidth );
            _height = static_cast<uint32>( state._configuredHeight );
        }
        if ( state._pShm != nullptr )
        {
            state._pCursorTheme   = wl_cursor_theme_load( nullptr, Internal::kCursorSize, state._pShm );
            state._pCursorSurface = wl_compositor_create_surface( state._pCompositor );
        }
        _bShouldClose = SW_FALSE;
        SW_LOG_INFO( "Native Wayland Window created successfully! (%#×%#)", _width, _height );
        // 없는 확장은 오류가 아니라 능력 차이다 — 소수 배율이 없으면 정수 배율을 쓰고, 포인터 잠금이 없으면 요청하는 쪽(입력 관리자)이 알린다.
        SW_LOG_INFO( "Wayland extensions: fractional scale %#, cursor shape %#, pointer constraints %#, relative pointer %#", state._pFractionalScaleManager != nullptr ? "yes" : "no",
                     state._pCursorShapeManager != nullptr ? "yes" : "no", state._pPointerConstraints != nullptr ? "yes" : "no", state._pRelativePointerManager != nullptr ? "yes" : "no" );
        return true;
    }

    void WaylandWindow::destroy()
    {
        State& state = *_pState;
        state.dropIntroducedOffer();
        state.dropSelectionOffer();
        if ( state._pDataSource != nullptr )
            wl_data_source_destroy( state._pDataSource );
        if ( state._pDataDevice != nullptr )
            wl_data_device_destroy( state._pDataDevice );
        if ( state._pDataDeviceManager != nullptr )
            wl_data_device_manager_destroy( state._pDataDeviceManager );
        for ( const WaylandOutput& output : state._listOutput )
        {
            wl_output_destroy( output._pOutput );
        }
        state.releasePointerConstraint();
        if ( state._pCursorShapeDevice != nullptr )
            wp_cursor_shape_device_v1_destroy( state._pCursorShapeDevice );
        if ( state._pCursorShapeManager != nullptr )
            wp_cursor_shape_manager_v1_destroy( state._pCursorShapeManager );
        if ( state._pRelativePointer != nullptr )
            zwp_relative_pointer_v1_destroy( state._pRelativePointer );
        if ( state._pRelativePointerManager != nullptr )
            zwp_relative_pointer_manager_v1_destroy( state._pRelativePointerManager );
        if ( state._pPointerConstraints != nullptr )
            zwp_pointer_constraints_v1_destroy( state._pPointerConstraints );
        if ( state._pFractionalScale != nullptr )
            wp_fractional_scale_v1_destroy( state._pFractionalScale );
        if ( state._pFractionalScaleManager != nullptr )
            wp_fractional_scale_manager_v1_destroy( state._pFractionalScaleManager );
        if ( state._pCursorSurface != nullptr )
            wl_surface_destroy( state._pCursorSurface );
        if ( state._pCursorTheme != nullptr )
            wl_cursor_theme_destroy( state._pCursorTheme );
        if ( state._pToplevel != nullptr )
            xdg_toplevel_destroy( state._pToplevel );
        if ( state._pXdgSurface != nullptr )
            xdg_surface_destroy( state._pXdgSurface );
        if ( state._pSurface != nullptr )
            wl_surface_destroy( state._pSurface );
        if ( state._pPointer != nullptr )
            wl_pointer_destroy( state._pPointer );
        if ( state._pKeyboard != nullptr )
            wl_keyboard_destroy( state._pKeyboard );
        if ( state._pSeat != nullptr )
            wl_seat_destroy( state._pSeat );
        if ( state._pWmBase != nullptr )
            xdg_wm_base_destroy( state._pWmBase );
        if ( state._pShm != nullptr )
            wl_shm_destroy( state._pShm );
        if ( state._pSubcompositor != nullptr )
            wl_subcompositor_destroy( state._pSubcompositor );
        if ( state._pCompositor != nullptr )
            wl_compositor_destroy( state._pCompositor );
        if ( state._pRegistry != nullptr )
            wl_registry_destroy( state._pRegistry );
        xkb_state_unref( state._pXkbState );
        xkb_keymap_unref( state._pKeymap );
        xkb_context_unref( state._pXkbContext );
        if ( state._pDisplay != nullptr )
            wl_display_disconnect( state._pDisplay );
        state = State{};
    }

    bool WaylandWindow::processMessages()
    {
        State& state = *_pState;
        if ( state._pDisplay == nullptr )
            return _bShouldClose == SW_FALSE;

        // 막지 않고 읽는다: 쌓인 것을 처리 → 읽을 준비 → 보낼 것을 보냄 → 읽을 것이 있으면 읽음(없으면 취소) → 다시 처리.
        wl_display_dispatch_pending( state._pDisplay );
        while ( wl_display_prepare_read( state._pDisplay ) != 0 )
        {
            wl_display_dispatch_pending( state._pDisplay );
        }
        wl_display_flush( state._pDisplay );
        pollfd pollEntry{ wl_display_get_fd( state._pDisplay ), POLLIN, 0 };
        if ( poll( &pollEntry, 1, 0 ) > 0 )
            (void)wl_display_read_events( state._pDisplay ); // 실패는 다음 dispatch 가 -1 로 알린다
        else
            wl_display_cancel_read( state._pDisplay );
        if ( wl_display_dispatch_pending( state._pDisplay ) == -1 )
        {
            SW_LOG_ERROR( "Wayland: the connection to the compositor broke - closing the window" );
            return false;
        }

        // 입력은 X11 과 같이 처리기(입력 관리자 · 에디터)에 하나씩 넘긴다.
        vector<WaylandInputEvent> listInput;
        listInput.swap( state._listPendingInput );
        for ( const WaylandInputEvent& input : listInput )
        {
            // 포커스는 처리기가 없어도 적는다(앱 루프의 프레임 페이싱이 읽는다).
            if ( input._kind == WaylandInputKind::FocusGained || input._kind == WaylandInputKind::FocusLost )
                setFocusState( input._kind == WaylandInputKind::FocusGained );
            if ( _customHandler.isBound() == false )
                break;
            NativeWindowEvent windowEvent{};
            windowEvent.setNativeWindow( NativeSurfaceKind::Wayland, state._pSurface );
            windowEvent._message = NativeWindowEvent::kMessageWayland;
            windowEvent._lParam  = reinterpret_cast<intptr_t>( &input );
            (void)_customHandler.get()( windowEvent ); // 받은 쪽이 먹었는지는 Wayland 에서 할 일을 바꾸지 않는다
        }

        const bool bResized = state._configuredWidth > 0 && state._configuredHeight > 0 &&
                              ( static_cast<uint32>( state._configuredWidth ) != _width || static_cast<uint32>( state._configuredHeight ) != _height );
        if ( bResized )
        {
            _width  = static_cast<uint32>( state._configuredWidth );
            _height = static_cast<uint32>( state._configuredHeight );
            if ( _onResize.isBound() )
                _onResize.get()( _width, _height );
        }
        if ( state._bCloseRequested && _bRecreating == SW_FALSE )
        {
            state._bCloseRequested = false;
            if ( tryBeginClose() )
                return false;
        }
        return _bShouldClose == SW_FALSE;
    }

    void WaylandWindow::waitForMessages( uint32 timeoutMilliseconds )
    {
        State& state = *_pState;
        if ( state._pDisplay == nullptr )
            return;
        // `processMessages` 가 매 프레임 쌓인 것을 모두 처리하므로 큐는 비어 있다 — 소켓만 기다리고, 읽기는 다음 `processMessages` 가 한다.
        wl_display_flush( state._pDisplay );
        pollfd pollEntry{ wl_display_get_fd( state._pDisplay ), POLLIN, 0 };
        (void)poll( &pollEntry, 1, static_cast<int32>( timeoutMilliseconds ) );
    }

    void WaylandWindow::applyWindowVisibility( bool bShow )
    {
        if ( bShow == false )
            SW_LOG_ERROR( "Wayland has no way for a client to hide its window - the window stays mapped" );
    }

    bool WaylandWindow::isVisible() const
    {
        return _pState->_bConfigured;
    }

    bool WaylandWindow::setDisplayMode( WindowDisplayMode mode, uint32 width, uint32 height )
    {
        State& state = *_pState;
        if ( state._pToplevel == nullptr )
            return false;
        _displayMode = mode;
        if ( mode == WindowDisplayMode::BorderlessFullscreen )
        {
            xdg_toplevel_set_fullscreen( state._pToplevel, nullptr ); // 크기는 다음 configure 가 준다
            wl_surface_commit( state._pSurface );
            return true;
        }
        xdg_toplevel_unset_fullscreen( state._pToplevel );
        wl_surface_commit( state._pSurface );
        // 창 모드의 크기는 클라이언트가 정한다 — 그 크기로 그린다고 알린다.
        _width  = width;
        _height = height;
        if ( _onResize.isBound() )
            _onResize.get()( _width, _height );
        return true;
    }

    void* WaylandWindow::getNativeHandle() const
    {
        return _pState->_pSurface;
    }

    void* WaylandWindow::getNativeDisplay() const
    {
        return _pState->_pDisplay;
    }

    float32 WaylandWindow::getContentScale() const
    {
        // 소수 배율을 알린 컴포지터는 그 값이 정답이다(wl_output 의 정수 배율은 올림한 값이라 소수 배율에서는 어긋난다).
        if ( _pState->_fractionalScale120 != 0 )
            return static_cast<float32>( _pState->_fractionalScale120 ) / WaylandWindowInternal::kFractionalScaleDenominator;
        // 소수 배율 프로토콜이 없는 컴포지터는 정상이다 — 정수 배율이 그 길이다.
        int32 scale = 1;
        for ( const WaylandOutput& output : _pState->_listOutput )
        {
            if ( output._bEntered && output._scale > scale )
                scale = output._scale;
        }
        return static_cast<float32>( scale );
    }

    bool WaylandWindow::supportsFractionalScale() const
    {
        return _pState->_pFractionalScaleManager != nullptr;
    }

    bool WaylandWindow::tryGetOverlayHandles( WaylandOverlayHandles& outHandles ) const
    {
        outHandles                = WaylandOverlayHandles{};
        const State& state        = *_pState;
        const bool   bUnavailable = state._pDisplay == nullptr || state._pCompositor == nullptr || state._pSubcompositor == nullptr || state._pShm == nullptr || state._pSurface == nullptr;
        if ( bUnavailable )
            return false;
        outHandles._pDisplay       = state._pDisplay;
        outHandles._pCompositor    = state._pCompositor;
        outHandles._pSubcompositor = state._pSubcompositor;
        outHandles._pShm           = state._pShm;
        outHandles._pParentSurface = state._pSurface;
        return true;
    }

    bool WaylandWindow::supportsPointerLock() const
    {
        return _pState->_pPointerConstraints != nullptr && _pState->_pRelativePointerManager != nullptr && _pState->_pPointer != nullptr;
    }

    bool WaylandWindow::setPointerLock( WindowPointerLock lock )
    {
        State& state = *_pState;
        if ( lock == state._pointerLock )
            return true;
        state.releasePointerConstraint();
        if ( lock == WindowPointerLock::Released )
            return true;
        // 가둠은 상대 이동이 필요 없다 — 잠금만 상대 포인터를 요구한다(잠긴 포인터는 움직임이 오지 않는다).
        const bool bSupported = lock == WindowPointerLock::Locked ? supportsPointerLock() : ( state._pPointerConstraints != nullptr && state._pPointer != nullptr );
        if ( bSupported == false )
            return false;
        using Internal = WaylandWindowInternal;
        if ( lock == WindowPointerLock::Locked )
        {
            state._pLockedPointer = zwp_pointer_constraints_v1_lock_pointer( state._pPointerConstraints, state._pSurface, state._pPointer, nullptr, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT );
            zwp_locked_pointer_v1_add_listener( state._pLockedPointer, &Internal::s_lockedPointerListener, &state );
        }
        else
        {
            state._pConfinedPointer = zwp_pointer_constraints_v1_confine_pointer( state._pPointerConstraints, state._pSurface, state._pPointer, nullptr, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT );
            zwp_confined_pointer_v1_add_listener( state._pConfinedPointer, &Internal::s_confinedPointerListener, &state );
        }
        state._pointerLock = lock;
        wl_display_flush( state._pDisplay );
        return true;
    }

    bool WaylandWindow::supportsClipboard() const
    {
        return _pState->_pDataDevice != nullptr;
    }

    bool WaylandWindow::readClipboardText( string& outText )
    {
        using Internal = WaylandWindowInternal;
        State& state   = *_pState;
        if ( state._pDataSource != nullptr )
        {
            outText = state._clipboardText; // 우리가 주인이다 — 컴포지터를 거치지 않는다
            return true;
        }
        const utf8* pMime = state._bSelectionHasUtf8 ? Internal::kMimeUtf8 : ( state._bSelectionHasPlain ? Internal::kMimePlain : nullptr );
        if ( state._pSelectionOffer == nullptr || pMime == nullptr )
            return false; // 선택이 없거나 글 형식이 없다(이미지 등)

        int32 arrPipeEnd[2]{ -1, -1 };
        if ( pipe( arrPipeEnd ) != 0 )
        {
            SW_LOG_WARNING( "Wayland clipboard: a pipe could not be made - nothing pasted" );
            return false;
        }
        wl_data_offer_receive( state._pSelectionOffer, pMime, arrPipeEnd[1] );
        close( arrPipeEnd[1] ); // 보내기 요청이 쓰기 끝을 복제해 들고 있다
        wl_display_flush( state._pDisplay );

        string bytes;
        bool   bCompleted = false;
        utf8   arrBuffer[4096];
        while ( bytes.size() <= Internal::kClipboardMaxBytes )
        {
            pollfd waitEntry{ arrPipeEnd[0], POLLIN, 0 };
            if ( poll( &waitEntry, 1, Internal::kClipboardWaitMs ) <= 0 )
                break;
            const ssize_t count = read( arrPipeEnd[0], arrBuffer, sizeof( arrBuffer ) );
            if ( count < 0 )
                break;
            if ( count == 0 )
            {
                bCompleted = true;
                break;
            }
            bytes.append( arrBuffer, static_cast<size_t>( count ) );
        }
        close( arrPipeEnd[0] );
        if ( bCompleted == false )
        {
            SW_LOG_WARNING( "Wayland clipboard: the owner did not finish sending within %# ms (%# bytes received) - nothing pasted", Internal::kClipboardWaitMs, static_cast<uint64>( bytes.size() ) );
            return false;
        }
        outText = std::move( bytes );
        return true;
    }

    bool WaylandWindow::writeClipboardText( string_view text )
    {
        using Internal = WaylandWindowInternal;
        State& state   = *_pState;
        // 선택을 내놓으려면 사용자 입력에서 온 일련번호가 있어야 한다(컴포지터가 거절한다).
        // 데이터 장치는 관리자가 있을 때만 만들어진다(`initializeWindow`) — 장치 하나로 둘을 본다.
        if ( state._pDataDevice == nullptr || state._inputSerial == 0 )
            return false;
        wl_data_source* const pSource = wl_data_device_manager_create_data_source( state._pDataDeviceManager );
        wl_data_source_add_listener( pSource, &Internal::s_dataSourceListener, &state );
        wl_data_source_offer( pSource, Internal::kMimeUtf8 );
        wl_data_source_offer( pSource, Internal::kMimePlain );
        // 이전 원천은 컴포지터가 `cancelled` 로 알려 콜백이 지운다(현재 원천이 아니라 글을 건드리지 않는다).
        state._pDataSource = pSource;
        state._clipboardText.assign( text.data(), text.size() );
        wl_data_device_set_selection( state._pDataDevice, pSource, state._inputSerial );
        wl_display_flush( state._pDisplay );
        return true;
    }

    bool WaylandWindow::supportsCursorShape() const
    {
        return _pState->_pCursorShapeDevice != nullptr;
    }

    bool WaylandWindow::setCursorShape( CursorShape shape )
    {
        State& state       = *_pState;
        state._cursorShape = shape;
        if ( state._pCursorShapeDevice == nullptr )
            return false;
        // 숨긴 동안과 포인터가 들어오기 전에는 일련번호가 없거나 보이면 안 된다 — 기억만 하고 들어올 때(`onPointerEnter`) · 보일 때(`setCursorVisible`) 적용한다.
        if ( state._bCursorVisible && state._pointerSerial != 0 )
        {
            wp_cursor_shape_device_v1_set_shape( state._pCursorShapeDevice, state._pointerSerial, WaylandWindowInternal::mapCursorShape( shape ) );
            wl_display_flush( state._pDisplay );
        }
        return true;
    }

    bool WaylandWindow::setCursorVisible( bool bVisible )
    {
        State& state          = *_pState;
        state._bCursorVisible = bVisible;
        if ( state._pPointer == nullptr || state._pointerSerial == 0 )
            return true; // 포인터가 들어올 때(`onPointerEnter`) 적용한다
        if ( bVisible == false )
        {
            wl_pointer_set_cursor( state._pPointer, state._pointerSerial, nullptr, 0, 0 );
            return true;
        }
        if ( state._pCursorShapeDevice != nullptr )
        {
            wp_cursor_shape_device_v1_set_shape( state._pCursorShapeDevice, state._pointerSerial, WaylandWindowInternal::mapCursorShape( state._cursorShape ) );
            return true;
        }
        wl_cursor* const pCursor        = state._pCursorTheme != nullptr ? wl_cursor_theme_get_cursor( state._pCursorTheme, "left_ptr" ) : nullptr;
        const bool       bNoCursorImage = pCursor == nullptr || pCursor->image_count == 0 || state._pCursorSurface == nullptr;
        if ( bNoCursorImage )
            return false; // 커서 테마가 없다 — 호출 쪽이 알린다
        wl_cursor_image* const pImage  = pCursor->images[0];
        wl_buffer* const       pBuffer = wl_cursor_image_get_buffer( pImage );
        wl_pointer_set_cursor( state._pPointer, state._pointerSerial, state._pCursorSurface, static_cast<int32>( pImage->hotspot_x ), static_cast<int32>( pImage->hotspot_y ) );
        wl_surface_attach( state._pCursorSurface, pBuffer, 0, 0 );
        wl_surface_damage( state._pCursorSurface, 0, 0, static_cast<int32>( pImage->width ), static_cast<int32>( pImage->height ) );
        wl_surface_commit( state._pCursorSurface );
        return true;
    }

    void WaylandWindow::applyMinimumClientSize()
    {
        if ( _pState->_pToplevel != nullptr )
            xdg_toplevel_set_min_size( _pState->_pToplevel, static_cast<int32>( _minClientWidth ), static_cast<int32>( _minClientHeight ) );
    }

    void WaylandWindow::applyTitle()
    {
        if ( _pState->_pToplevel != nullptr )
            xdg_toplevel_set_title( _pState->_pToplevel, getTitle() );
    }
} // namespace sw
#else
namespace sw
{
    /** @brief Wayland 를 짓지 않은 구성의 빈 상태입니다 — 이 구성은 `createPlatformWindow` 가 이 창을 만들지 않는다. */
    struct WaylandWindow::State
    {
    };

    WaylandWindow::WaylandWindow()
        : _pState{ make_unique<State>() }
    {
        clearRestorePosition();
    }

    WaylandWindow::~WaylandWindow() = default;

    void WaylandWindow::clearRestorePosition()
    {
        _restoreX = 0;
        _restoreY = 0;
    }

    bool WaylandWindow::initializeWindow( const utf8* pTitle, uint32 width, uint32 height )
    {
        (void)pTitle;
        (void)width;
        (void)height;
        SW_LOG_ERROR( "This build has no Wayland window layer (configure with -DSW_ENABLE_WAYLAND=ON on Linux)" );
        return false;
    }

    void WaylandWindow::destroy()
    {
    }

    bool WaylandWindow::processMessages()
    {
        return _bShouldClose == SW_FALSE;
    }

    void WaylandWindow::waitForMessages( uint32 timeoutMilliseconds )
    {
        // 이 빌드에는 Wayland 창이 없다 — 기다릴 것이 없다.
        (void)timeoutMilliseconds;
    }

    void WaylandWindow::applyWindowVisibility( bool bShow )
    {
        (void)bShow;
    }

    bool WaylandWindow::isVisible() const
    {
        return false;
    }

    bool WaylandWindow::setDisplayMode( WindowDisplayMode mode, uint32 width, uint32 height )
    {
        (void)mode;
        (void)width;
        (void)height;
        return false;
    }

    void* WaylandWindow::getNativeHandle() const
    {
        return nullptr;
    }

    void* WaylandWindow::getNativeDisplay() const
    {
        return nullptr;
    }

    float32 WaylandWindow::getContentScale() const
    {
        return 1.0f;
    }

    bool WaylandWindow::supportsFractionalScale() const
    {
        return false;
    }

    bool WaylandWindow::tryGetOverlayHandles( WaylandOverlayHandles& outHandles ) const
    {
        outHandles = WaylandOverlayHandles{};
        return false;
    }

    bool WaylandWindow::supportsPointerLock() const
    {
        return false;
    }

    bool WaylandWindow::setPointerLock( WindowPointerLock lock )
    {
        return lock == WindowPointerLock::Released;
    }

    bool WaylandWindow::supportsClipboard() const
    {
        return false;
    }

    bool WaylandWindow::readClipboardText( string& outText )
    {
        (void)outText;
        return false;
    }

    bool WaylandWindow::writeClipboardText( string_view text )
    {
        (void)text;
        return false;
    }

    bool WaylandWindow::supportsCursorShape() const
    {
        return false;
    }

    bool WaylandWindow::setCursorShape( CursorShape shape )
    {
        (void)shape;
        return false;
    }

    bool WaylandWindow::setCursorVisible( bool bVisible )
    {
        (void)bVisible;
        return false;
    }

    void WaylandWindow::applyMinimumClientSize()
    {
    }

    void WaylandWindow::applyTitle()
    {
    }
} // namespace sw
#endif
