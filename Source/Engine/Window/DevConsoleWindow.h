/**
 * @file DevConsoleWindow.h
 * @brief 게임 창 개발 콘솔을 그리는 플랫폼 창의 인터페이스입니다. 입력은 모릅니다(판단은 `Input/DevConsoleController`). Shipping 에는 없습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Console/DevCommandRegistry.h"

#if SW_DEV_COMMANDS_ENABLED

namespace sw
{
    class IWindow;

    /**
     * @class IDevConsoleWindow
     * @brief 개발 콘솔 줄을 화면에 그리는 플랫폼 창입니다(Win32: 게임 창이 소유한 GDI 팝업, X11: 게임 창의 자식 창).
     * @details 키보드는 게임 창이 받아 `InputManager` 로 갑니다. 이 창은 그리기만 합니다.
     */
    class IDevConsoleWindow
    {
    public:
        virtual ~IDevConsoleWindow() = default;

        /** @brief 창을 보이거나 숨깁니다. 보일 때도 게임 창의 키보드 포커스는 빼앗지 않습니다. */
        virtual void setVisible( bool bVisible ) = 0;
        /** @brief 그릴 줄(위에서 아래로)을 넘기고 게임 창 위로 자리를 다시 맞춥니다. @p listErrorFlag 가 0 이 아닌 줄은 오류 색입니다. */
        virtual void present( const vector<string>& listLine, const vector<uint8>& listErrorFlag ) = 0;

        /** @brief 이 플랫폼의 창을 만듭니다. 만들지 못하면 nullptr(콘솔은 그리지 않고 입력만 받습니다). */
        static unique_ptr<IDevConsoleWindow> createPlatform( IWindow& owner );
    };
} // namespace sw

#endif
