#include "pch.h"

#include "Editor/Common/Backend/IImGuiPlatformBackend.h"

#if defined( SW_PLATFORM_LINUX )
    #include "Core/Container/string.h"
    #include "Core/Time/GameTimer.h"

    #include "Engine/Window/CursorShape.h"
    #include "Engine/Window/IWindow.h"
    #include "Engine/Window/Linux/Wayland/WaylandInputEvent.h"
    #include "Engine/Window/NativeWindowEvent.h"

    #include <imgui.h>

    #include <cfloat>

namespace sw::editor
{
    SW_LOG_CALLER( "ImGuiWayland" );

    namespace
    {
        /** @brief Wayland 입력 값 · 번호를 ImGui 로 옮기는 표입니다(이 파일은 Wayland · xkbcommon 헤더 없이 짓는다 — 번호만 상수로 둔다). */
        struct ImGuiWaylandInternal
        {
            // 리눅스 입력 이벤트 버튼 번호(`linux/input-event-codes.h` 의 BTN_*, WaylandInputEvent::_button).
            static constexpr uint32 kButtonLeft        = 0x110;
            static constexpr uint32 kButtonRight       = 0x111;
            static constexpr uint32 kButtonMiddle      = 0x112;
            static constexpr uint32 kButtonSide        = 0x113; ///< 뒤로
            static constexpr uint32 kButtonExtra       = 0x114; ///< 앞으로
            static constexpr int32  kImGuiMouseBack    = 3;     // ImGuiKey_MouseX1
            static constexpr int32  kImGuiMouseForward = 4;     // ImGuiKey_MouseX2

            // xkb keysym(= X11 KeySym, `xkbcommon-keysyms.h` 의 XKB_KEY_*)의 번호입니다.
            static constexpr uint32 kKeyBackSpace = 0xff08;
            static constexpr uint32 kKeyTab       = 0xff09;
            static constexpr uint32 kKeyReturn    = 0xff0d;
            static constexpr uint32 kKeyEscape    = 0xff1b;
            static constexpr uint32 kKeyHome      = 0xff50;
            static constexpr uint32 kKeyLeft      = 0xff51;
            static constexpr uint32 kKeyUp        = 0xff52;
            static constexpr uint32 kKeyRight     = 0xff53;
            static constexpr uint32 kKeyDown      = 0xff54;
            static constexpr uint32 kKeyPageUp    = 0xff55;
            static constexpr uint32 kKeyPageDown  = 0xff56;
            static constexpr uint32 kKeyEnd       = 0xff57;
            static constexpr uint32 kKeyInsert    = 0xff63;
            static constexpr uint32 kKeyKpEnter   = 0xff8d;
            static constexpr uint32 kKeyF1        = 0xffbe;
            static constexpr uint32 kKeyF12       = 0xffc9;
            static constexpr uint32 kKeyShiftL    = 0xffe1;
            static constexpr uint32 kKeyShiftR    = 0xffe2;
            static constexpr uint32 kKeyControlL  = 0xffe3;
            static constexpr uint32 kKeyControlR  = 0xffe4;
            static constexpr uint32 kKeyAltL      = 0xffe9;
            static constexpr uint32 kKeyAltR      = 0xffea;
            static constexpr uint32 kKeySuperL    = 0xffeb;
            static constexpr uint32 kKeySuperR    = 0xffec;
            static constexpr uint32 kKeyDelete    = 0xffff;
            static constexpr uint32 kKeySpace     = 0x0020;

            /** @brief keysym 을 ImGui 키로 바꿉니다. 모르는 키는 `ImGuiKey_None` 입니다(그 키는 ImGui 에 알리지 않는다). */
            static ImGuiKey mapKeySym( uint32 keySym )
            {
                if ( keySym >= 'a' && keySym <= 'z' )
                    return static_cast<ImGuiKey>( ImGuiKey_A + static_cast<int32>( keySym - 'a' ) );
                if ( keySym >= 'A' && keySym <= 'Z' )
                    return static_cast<ImGuiKey>( ImGuiKey_A + static_cast<int32>( keySym - 'A' ) ); // 시프트를 누른 글자 — 키는 같다
                if ( keySym >= '0' && keySym <= '9' )
                    return static_cast<ImGuiKey>( ImGuiKey_0 + static_cast<int32>( keySym - '0' ) );
                if ( keySym >= kKeyF1 && keySym <= kKeyF12 )
                    return static_cast<ImGuiKey>( ImGuiKey_F1 + static_cast<int32>( keySym - kKeyF1 ) );
                switch ( keySym )
                {
                    case kKeySpace:
                        return ImGuiKey_Space;
                    case kKeyBackSpace:
                        return ImGuiKey_Backspace;
                    case kKeyTab:
                        return ImGuiKey_Tab;
                    case kKeyReturn:
                        return ImGuiKey_Enter;
                    case kKeyKpEnter:
                        return ImGuiKey_KeypadEnter;
                    case kKeyEscape:
                        return ImGuiKey_Escape;
                    case kKeyHome:
                        return ImGuiKey_Home;
                    case kKeyEnd:
                        return ImGuiKey_End;
                    case kKeyPageUp:
                        return ImGuiKey_PageUp;
                    case kKeyPageDown:
                        return ImGuiKey_PageDown;
                    case kKeyInsert:
                        return ImGuiKey_Insert;
                    case kKeyDelete:
                        return ImGuiKey_Delete;
                    case kKeyLeft:
                        return ImGuiKey_LeftArrow;
                    case kKeyRight:
                        return ImGuiKey_RightArrow;
                    case kKeyUp:
                        return ImGuiKey_UpArrow;
                    case kKeyDown:
                        return ImGuiKey_DownArrow;
                    case kKeyShiftL:
                        return ImGuiKey_LeftShift;
                    case kKeyShiftR:
                        return ImGuiKey_RightShift;
                    case kKeyControlL:
                        return ImGuiKey_LeftCtrl;
                    case kKeyControlR:
                        return ImGuiKey_RightCtrl;
                    case kKeyAltL:
                        return ImGuiKey_LeftAlt;
                    case kKeyAltR:
                        return ImGuiKey_RightAlt;
                    case kKeySuperL:
                        return ImGuiKey_LeftSuper;
                    case kKeySuperR:
                        return ImGuiKey_RightSuper;
                    default:
                        break;
                }
                return ImGuiKey_None;
            }

            /** @brief 리눅스 버튼 번호를 ImGui 마우스 버튼 번호로 바꿉니다. 모르는 버튼은 -1 입니다. */
            static int32 mapButton( uint32 button )
            {
                switch ( button )
                {
                    case kButtonLeft:
                        return ImGuiMouseButton_Left;
                    case kButtonRight:
                        return ImGuiMouseButton_Right;
                    case kButtonMiddle:
                        return ImGuiMouseButton_Middle;
                    case kButtonSide:
                        return kImGuiMouseBack;
                    case kButtonExtra:
                        return kImGuiMouseForward;
                    default:
                        break;
                }
                return -1;
            }

            /** @brief ImGui 가 권하는 커서를 OS 커서 모양으로 바꿉니다(대응이 없는 모양은 화살표). */
            static CursorShape mapCursor( ImGuiMouseCursor cursor )
            {
                switch ( cursor )
                {
                    case ImGuiMouseCursor_TextInput:
                        return CursorShape::TextBeam;
                    case ImGuiMouseCursor_ResizeEW:
                        return CursorShape::ResizeHorizontal;
                    case ImGuiMouseCursor_ResizeNS:
                        return CursorShape::ResizeVertical;
                    case ImGuiMouseCursor_Hand:
                        return CursorShape::Hand;
                    case ImGuiMouseCursor_NotAllowed:
                        return CursorShape::NotAllowed;
                    default:
                        break;
                }
                return CursorShape::Arrow;
            }
        };
    } // namespace

    /**
     * @class ImGuiWaylandPlatformBackend
     * @brief Wayland 창(`WaylandWindow`)용 ImGui 플랫폼 백엔드입니다 — 창 하나만 다룬다.
     * @details Wayland 클라이언트는 창 자리를 정하지 못하고 최상위 창을 나란히 놓을 수도 없다(xdg-shell). 그래서 ImGui 멀티 뷰포트(패널을 OS 창으로 떼어 내기)는 켜지 않는다 —
     *          `PlatformHasViewports` 를 알리지 않으면 ImGui 가 `ViewportsEnable` 을 스스로 끄고 패널은 메인 창 안에서만 도킹한다.
     *          입력은 창이 `NativeWindowEvent::kMessageWayland` 로 넘기는 `WaylandInputEvent` 이고(키 · 포인터 · 휠 · 포커스), 클립보드는 창의 클립보드 능력(`IWindow::supportsClipboard`)에,
     *          커서 모양은 창의 커서 모양 능력(`supportsCursorShape`)에 잇는다.
     */
    class ImGuiWaylandPlatformBackend : public IImGuiPlatformBackend
    {
    public:
        /** @brief `ImGuiIO::BackendPlatformName` 에 거는 이름입니다. */
        static constexpr const utf8* kPlatformName = "imgui_impl_wayland";

        ImGuiWaylandPlatformBackend()
            : _pWindow{ nullptr }
            , _timer{}
            , _clipboardText{}
            , _lastCursor{ CursorShape::Arrow }
            , _bCursorKnown{ false }
        {
        }

        bool initialize( IWindow* pWindow, RHIBackend backendType ) override
        {
            if ( pWindow == nullptr )
                return false;
            (void)backendType;

            // 이 백엔드는 Wayland 창 전용이다 — 다른 창이면 입력 이벤트가 오지 않으므로 오류로 멈춘다.
            if ( pWindow->getSurfaceKind() != NativeSurfaceKind::Wayland )
            {
                SW_LOG_ERROR( "The ImGui Wayland platform backend needs a Wayland window" );
                return false;
            }
            _pWindow = pWindow;
            _timer.resetTimer();
            _timer.startTimer();
            ImGui::GetIO().BackendPlatformUserData = this; // 활성 백엔드의 주인은 ImGui 컨텍스트다(콜백이 거기서 찾는다) - 프로세스 정적 포인터를 두지 않는다

            ImGuiIO& io            = ImGui::GetIO();
            io.BackendPlatformName = kPlatformName;
            io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors; // 창이 커서 모양을 스스로 바꾼다(없으면 `newFrame` 이 건너뛴다)

            // 메인 뷰포트의 `PlatformHandle` 은 비워 둔다 — Wayland 의 연결 · 표면은 ImGui 뷰포트로 내보내지 않는다(렌더러는 메인 창의 스왑체인을 쓰고,
            // 활성 백엔드가 X11 이 아니라 `tryGetViewportX11Surface` 는 false 다).
            ImGuiPlatformIO& platformIO            = ImGui::GetPlatformIO();
            platformIO.Platform_GetClipboardTextFn = &getClipboardTextThunk;
            platformIO.Platform_SetClipboardTextFn = &setClipboardTextThunk;

            updateMonitor();
            return true;
        }

        void shutdown() override
        {
            // 초기화한 적이 없어도(실패를 수습하는 경로) 안전해야 한다 — 활성 백엔드가 이 백엔드일 때만 ImGui 쪽 끈을 푼다.
            ImGuiIO& io = ImGui::GetIO();
            if ( io.BackendPlatformUserData == this )
            {
                ImGuiPlatformIO& platformIO            = ImGui::GetPlatformIO();
                platformIO.Platform_GetClipboardTextFn = nullptr;
                platformIO.Platform_SetClipboardTextFn = nullptr;
                io.BackendPlatformUserData             = nullptr;
            }
            _pWindow = nullptr;
        }

        void newFrame() override
        {
            if ( _pWindow == nullptr )
                return;
            ImGuiIO& io    = ImGui::GetIO();
            io.DisplaySize = ImVec2( static_cast<float32>( _pWindow->getWidth() ), static_cast<float32>( _pWindow->getHeight() ) );
            updateMonitor();

            _timer.updateTimer();
            io.DeltaTime = _timer.getDeltaTime();
            if ( io.DeltaTime <= 0.0f )
                io.DeltaTime = 0.00001f;

            updateCursor( io );
        }

        bool processEvent( const NativeWindowEvent& event ) override
        {
            if ( event._message != NativeWindowEvent::kMessageWayland || event._lParam == 0 )
                return false;
            const WaylandInputEvent& input = *reinterpret_cast<const WaylandInputEvent*>( event._lParam );
            ImGuiIO&                 io    = ImGui::GetIO();
            switch ( input._kind )
            {
                case WaylandInputKind::PointerMove:
                {
                    io.AddMousePosEvent( static_cast<float32>( input._x ), static_cast<float32>( input._y ) );
                    return io.WantCaptureMouse;
                }
                case WaylandInputKind::PointerDown:
                case WaylandInputKind::PointerUp:
                {
                    const int32 button = ImGuiWaylandInternal::mapButton( input._button );
                    if ( button < 0 )
                        return false;
                    io.AddMouseButtonEvent( button, input._kind == WaylandInputKind::PointerDown );
                    return io.WantCaptureMouse;
                }
                case WaylandInputKind::PointerWheel:
                {
                    io.AddMouseWheelEvent( input._horizontalWheel, input._wheel );
                    return io.WantCaptureMouse;
                }
                case WaylandInputKind::PointerLeave:
                {
                    io.AddMousePosEvent( -FLT_MAX, -FLT_MAX ); // 창 밖 — 호버가 남지 않게 한다
                    return false;
                }
                case WaylandInputKind::FocusGained:
                case WaylandInputKind::FocusLost:
                {
                    io.AddFocusEvent( input._kind == WaylandInputKind::FocusGained );
                    return false;
                }
                case WaylandInputKind::KeyDown:
                case WaylandInputKind::KeyUp:
                {
                    const ImGuiKey key = ImGuiWaylandInternal::mapKeySym( input._keySym );
                    if ( key != ImGuiKey_None )
                        io.AddKeyEvent( key, input._kind == WaylandInputKind::KeyDown );
                    return io.WantCaptureKeyboard;
                }
                case WaylandInputKind::Text:
                {
                    io.AddInputCharactersUTF8( input._arrText );
                    return io.WantCaptureKeyboard;
                }
                case WaylandInputKind::PointerEnter:
                case WaylandInputKind::PointerRelative:
                {
                    break; // 들어옴은 이어 오는 움직임이 알리고, 상대 이동은 포인터를 잠근 게임 입력용이다
                }
            }
            return false;
        }

        float32 getDPIScale() const override
        {
            if ( _pWindow == nullptr )
                return 1.0f;
            const float32 scale = _pWindow->getContentScale();
            return scale > 0.0f ? scale : 1.0f;
        }

    private:
        /** @brief 지금 ImGui 컨텍스트에 걸린 이 백엔드입니다(`initialize` 가 `BackendPlatformUserData` 에 건다). 걸려 있지 않으면 nullptr 입니다. */
        static ImGuiWaylandPlatformBackend* getActive() { return static_cast<ImGuiWaylandPlatformBackend*>( ImGui::GetIO().BackendPlatformUserData ); }

        static const utf8* getClipboardTextThunk( ImGuiContext* pContext )
        {
            (void)pContext;
            ImGuiWaylandPlatformBackend* pBackend     = getActive();
            const bool                   bNoClipboard = pBackend == nullptr || pBackend->_pWindow == nullptr || pBackend->_pWindow->supportsClipboard() == false;
            if ( bNoClipboard )
                return nullptr; // 글이 없음 — ImGui 는 안 붙여 넣는다
            if ( pBackend->_pWindow->readClipboardText( pBackend->_clipboardText ) == false )
                return nullptr;
            return pBackend->_clipboardText.c_str();
        }

        static void setClipboardTextThunk( ImGuiContext* pContext, const utf8* pText )
        {
            (void)pContext;
            ImGuiWaylandPlatformBackend* pBackend     = getActive();
            const bool                   bCannotWrite = pBackend == nullptr || pBackend->_pWindow == nullptr || pText == nullptr || pBackend->_pWindow->supportsClipboard() == false;
            if ( bCannotWrite )
                return;
            // 마지막 입력 일련번호가 없으면(입력 없이 복사를 시킨 자동화) 창이 경고를 남기고 false 를 돌려준다 — 여기서 다시 알리지 않는다.
            (void)pBackend->_pWindow->writeClipboardText( pText );
        }

        /** @brief ImGui 가 권하는 커서를 창에 알립니다(바뀔 때만). 창이 커서 모양을 못 바꾸면 하지 않는다. */
        void updateCursor( const ImGuiIO& io )
        {
            const bool bCannotChange = ( io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange ) != 0 || _pWindow->supportsCursorShape() == false;
            if ( bCannotChange )
                return;
            const CursorShape shape = ImGuiWaylandInternal::mapCursor( ImGui::GetMouseCursor() );
            if ( _bCursorKnown && shape == _lastCursor )
                return;
            if ( _pWindow->setCursorShape( shape ) )
            {
                _lastCursor   = shape;
                _bCursorKnown = true;
            }
        }

        /** @brief 모니터 목록을 창 크기의 하나로 둡니다 — Wayland 클라이언트는 출력(모니터)의 전역 자리를 모른다. */
        void updateMonitor()
        {
            ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
            platformIO.Monitors.resize( 0 );
            ImGuiPlatformMonitor monitor{};
            monitor.MainPos  = ImVec2( 0.0f, 0.0f );
            monitor.MainSize = ImVec2( static_cast<float32>( _pWindow->getWidth() ), static_cast<float32>( _pWindow->getHeight() ) );
            monitor.WorkPos  = monitor.MainPos;
            monitor.WorkSize = monitor.MainSize;
            monitor.DpiScale = getDPIScale();
            platformIO.Monitors.push_back( monitor );
        }

    private:
        IWindow*    _pWindow;
        GameTimer   _timer;
        string      _clipboardText; ///< `Platform_GetClipboardTextFn` 이 돌려주는 글(다음 호출까지 살아 있어야 한다)
        CursorShape _lastCursor;
        bool        _bCursorKnown;
    };

    unique_ptr<IImGuiPlatformBackend> IImGuiPlatformBackend::createPlatformBackendFor( const IWindow& window )
    {
        if ( window.getSurfaceKind() == NativeSurfaceKind::Wayland )
            return make_unique<ImGuiWaylandPlatformBackend>();
        return createPlatformBackend();
    }
} // namespace sw::editor
#endif
