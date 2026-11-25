/**
 * @file UIScreenTypes.h
 * @brief 화면을 가리키는 번호 · 명령 함수 형식 · 화면 성질(`UIScreenDef`)입니다. 화면을 번호로만 들고 있는 쪽(서비스 · 컨트롤러 · 패널)이 `UIScreen.h`(위젯 트리 · 애니메이션 · 바인딩)를 끌지 않게 뗐다.
 */
#pragma once
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/TypedID.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Presentation/UILayer.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class Widget;

    /** @brief 화면을 가리키는 번호입니다. 다시 쓰지 않습니다(0 이면 없음). */
    using UIScreenHandle = TypedID<struct UIScreenHandleTag, uint32>;
    /** @brief 화면이 없음을 뜻하는 번호입니다. */
    inline constexpr UIScreenHandle kInvalidUIScreenHandle{};

    /** @brief 위젯이 낸 명령(버튼의 `_command`)을 받는 함수입니다(`UIScreen::registerCommand`). */
    using UICommandDelegate = Delegate<void( const hashed_string&, Widget& )>;
} // namespace sw

namespace sw
{
    /** @struct UIScreenDef @brief 화면 하나의 성질입니다(문서의 루트 요소 속성으로도 적습니다). */
    REFLECT()
    struct SW_API UIScreenDef
    {
        REFLECT_BODY();

        PROPERTY( DisplayName = "Layer" )
        UILayer _layer{ UILayer::Menu };
        PROPERTY( DisplayName = "Modal", Tooltip = "Blocks input to screens below and to the game" )
        bool _bModal{ false };
        PROPERTY( DisplayName = "Takes Focus", Tooltip = "Receives focus and UI actions (never on the HUD and Overlay layers)" )
        bool _bTakesFocus{ true };
        PROPERTY( DisplayName = "Show Cursor", Tooltip = "Show the OS cursor while this screen is the active screen" )
        bool _bShowCursor{ true };
        PROPERTY( DisplayName = "Pauses Game", Tooltip = "Pause game time while this screen is open" )
        bool _bPausesGame{ false };
        PROPERTY( DisplayName = "Default Focus", Tooltip = "Widget to focus when the screen opens in navigation mode" )
        hashed_string _defaultFocus{};
    };
} // namespace sw
