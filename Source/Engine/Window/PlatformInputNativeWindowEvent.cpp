/**
 * @file PlatformInputNativeWindowEvent.cpp
 * @brief 콜백 입력 창(웹 · Android)의 네이티브 창 이벤트 판정입니다(`NativeWindowEvent::isMouseInput` 등 — 플랫폼마다 한 벌인 함수).
 */
#include "pch.h"

#include "Engine/Window/NativeWindowEvent.h"
#include "Engine/Window/PlatformInputEvent.h"

#if defined( SW_PLATFORM_WEB ) || defined( SW_PLATFORM_ANDROID )

namespace sw
{
    namespace
    {
        const PlatformInputEvent* findPlatformInput( const NativeWindowEvent& event )
        {
            if ( event._message != NativeWindowEvent::kMessagePlatformInput || event._lParam == 0 )
                return nullptr;
            return reinterpret_cast<const PlatformInputEvent*>( event._lParam );
        }
    } // namespace

    bool NativeWindowEvent::isMouseInput() const
    {
        const PlatformInputEvent* const pInput = findPlatformInput( *this );
        if ( pInput == nullptr )
            return false;
        // 손가락도 포인터 입력이다 — 에디터 UI(ImGui)가 마우스로 받는다.
        const PlatformInputKind kind = pInput->_kind;
        return kind == PlatformInputKind::PointerMove || kind == PlatformInputKind::PointerDown || kind == PlatformInputKind::PointerUp || kind == PlatformInputKind::Wheel ||
               kind == PlatformInputKind::TouchDown || kind == PlatformInputKind::TouchMove || kind == PlatformInputKind::TouchUp;
    }

    bool NativeWindowEvent::isKeyboardInput() const
    {
        const PlatformInputEvent* const pInput = findPlatformInput( *this );
        return pInput != nullptr && ( pInput->_kind == PlatformInputKind::KeyDown || pInput->_kind == PlatformInputKind::KeyUp || pInput->_kind == PlatformInputKind::Text );
    }

    bool NativeWindowEvent::isInputRelease() const
    {
        const PlatformInputEvent* const pInput = findPlatformInput( *this );
        return pInput != nullptr && ( pInput->_kind == PlatformInputKind::KeyUp || pInput->_kind == PlatformInputKind::PointerUp || pInput->_kind == PlatformInputKind::TouchUp );
    }
} // namespace sw

#endif
