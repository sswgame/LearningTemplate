/**
 * @file UIEvents.h
 * @brief 위젯이 받는 이벤트 값입니다. 경로 단계, 포인터, 액션, 글자와 처리 결과(UIReply)를 정의합니다.
 * @details 이벤트는 위젯 경로(루트에서 잎)를 따라 터널링(미리보기) 다음 버블링으로 간다. 처리(`UIReply::makeHandled`)하면 거기서 멈춘다(언리얼의 FReply).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/UI/WidgetTree/WidgetTypes.h"

namespace sw
{
    /** @brief 이벤트가 경로를 지나는 단계입니다. 터널링(루트에서 대상, 미리보기)이 먼저이고, 버블링(대상에서 루트)이 다음입니다. */
    enum class UIRoutePhase : uint8
    {
        Tunnel,
        Bubble
    };

    /** @brief 포인터 이벤트의 종류입니다. Enter 와 Leave 는 경로를 타지 않는 알림(`Widget::onHoverChanged`)이라 여기에 없습니다. */
    enum class UIPointerEventKind : uint8
    {
        Down,
        Up,
        Move,
        Wheel
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIPointerEvent
     * @brief 포인터 이벤트입니다(UI 단위, 화면 좌표). 마우스는 포인터 0 입니다(터치를 추가하면 손가락마다 번호를 붙입니다).
     */
    struct UIPointerEvent
    {
        float2             _position{};
        float2             _delta{};
        float32            _wheel{ 0.0f };
        UIPointerEventKind _kind{ UIPointerEventKind::Move };
        MouseButton        _button{ MouseButton::Left };
        uint8              _pointerIndex{ 0 };
        uint8              _clickCount{ 1 }; ///< 두 번 누르면 2 입니다(InputMap doubleClickTime 과 같은 판정).
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIActionEvent
     * @brief 액션(입력 맵의 `UI.*` 액션) 이벤트입니다. 포커스 경로를 따라갑니다. 키, 패드, 가상 입력이 모두 이 경로로 들어옵니다.
     */
    struct UIActionEvent
    {
        hashed_string _action{};             ///< 액션 이름입니다(`UIActionName`, 예: `UI.Accept`, `UI.NavigateLeft`).
        float2        _value{};              ///< 축 값입니다(스틱 탐색과 스크롤이며, 위와 오른쪽이 +). 버튼이면 0 입니다.
        float32       _deltaSeconds{ 0.0f }; ///< 이번 입력 프레임의 시간입니다. 축 액션(스크롤)이 속도 × 시간으로 이동합니다.
        uint8         _bRepeat{ SW_FALSE };  ///< 누르고 있는 동안의 반복 발화이면 SW_TRUE 입니다.
    };
} // namespace sw

namespace sw
{
    /** @brief UI 액션 맵(`engine/input/ui.input.xml`)의 액션 이름입니다. `UISystem` 과 위젯(버튼, 슬라이더, 스크롤)이 같은 이름을 씁니다. */
    struct UIActionName
    {
        static constexpr utf8 kNavigateUp[]    = "UI.NavigateUp";
        static constexpr utf8 kNavigateDown[]  = "UI.NavigateDown";
        static constexpr utf8 kNavigateLeft[]  = "UI.NavigateLeft";
        static constexpr utf8 kNavigateRight[] = "UI.NavigateRight";
        static constexpr utf8 kNavigateStick[] = "UI.NavigateStick";
        static constexpr utf8 kFocusNext[]     = "UI.FocusNext";
        static constexpr utf8 kFocusPrevious[] = "UI.FocusPrevious";
        static constexpr utf8 kAccept[]        = "UI.Accept";
        static constexpr utf8 kBack[]          = "UI.Back";
        static constexpr utf8 kTabNext[]       = "UI.TabNext";
        static constexpr utf8 kTabPrevious[]   = "UI.TabPrevious";
        static constexpr utf8 kScroll[]        = "UI.Scroll";        ///< 패드 오른쪽 스틱이며 값을 가집니다.
        static constexpr utf8 kTextBackspace[] = "UI.TextBackspace"; ///< 텍스트 입력 위젯이 키보드 포커스를 가진 동안만 쓰며, 커서 앞 글자 하나를 지웁니다.
        static constexpr utf8 kPause[]         = "UI.Pause";         ///< 화면이 없을 때만 쓰며(레이어 `UIGlobal`) 일시정지 메뉴를 엽니다.

        // 글 편집 액션입니다. 모두 글 입력 위젯이 키보드 포커스를 가진 동안만 쓰고, 그 밖의 키 입력은 UI 의 것이 아닙니다.
        static constexpr utf8 kTextDelete[]      = "UI.TextDelete";      ///< 커서 뒤 글자 하나(선택이 있으면 선택)를 지웁니다.
        static constexpr utf8 kTextCaretLeft[]   = "UI.TextCaretLeft";   ///< 커서를 화면 기준 왼쪽 글자로 옮기고 선택을 풉니다.
        static constexpr utf8 kTextCaretRight[]  = "UI.TextCaretRight";  ///< 커서를 화면 기준 오른쪽 글자로 옮기고 선택을 풉니다.
        static constexpr utf8 kTextHome[]        = "UI.TextHome";        ///< 커서를 글 처음으로 옮깁니다.
        static constexpr utf8 kTextEnd[]         = "UI.TextEnd";         ///< 커서를 글 끝으로 옮깁니다.
        static constexpr utf8 kTextSelectLeft[]  = "UI.TextSelectLeft";  ///< 선택을 화면 기준 왼쪽 글자까지 넓힙니다.
        static constexpr utf8 kTextSelectRight[] = "UI.TextSelectRight"; ///< 선택을 화면 기준 오른쪽 글자까지 넓힙니다.
        static constexpr utf8 kTextSelectHome[]  = "UI.TextSelectHome";  ///< 선택을 글 처음까지 넓힙니다.
        static constexpr utf8 kTextSelectEnd[]   = "UI.TextSelectEnd";   ///< 선택을 글 끝까지 넓힙니다.
        static constexpr utf8 kTextSelectAll[]   = "UI.TextSelectAll";   ///< 글 전체를 선택합니다.
        static constexpr utf8 kTextCopy[]        = "UI.TextCopy";        ///< 선택을 클립보드로 복사합니다.
        static constexpr utf8 kTextCut[]         = "UI.TextCut";         ///< 선택을 클립보드로 옮기고 지웁니다.
        static constexpr utf8 kTextPaste[]       = "UI.TextPaste";       ///< 클립보드 글(한 줄)을 선택 자리에 넣습니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UITextEvent
     * @brief 글자 입력 이벤트입니다. 포커스 위젯 하나에만 보내며 경로를 타지 않습니다.
     */
    struct UITextEvent
    {
        string _text{};                   ///< UTF-8
        uint8  _bComposition{ SW_FALSE }; ///< IME 조합 중인(확정 전) 글자이면 SW_TRUE 입니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIReply
     * @brief 이벤트 처리 결과입니다(언리얼의 FReply). 처리되면 경로가 멈춥니다.
     */
    struct SW_API UIReply
    {
        WidgetID _focusRequest{ kInvalidWidgetID }; ///< 이 위젯으로 포커스를 옮겨 달라는 요청입니다(버튼을 마우스로 눌러도 포커스가 따라옵니다).
        uint8    _bHandled{ SW_FALSE };
        uint8    _bCapturePointer{ SW_FALSE }; ///< 이 위젯이 포인터를 잡습니다(슬라이더 끌기). 뗄 때까지 이벤트가 이 위젯에만 갑니다.
        uint8    _bReleasePointer{ SW_FALSE };

        static UIReply makeHandled();
        static UIReply makeUnhandled();
        UIReply&       capturePointer();
        UIReply&       releasePointer();
        UIReply&       requestFocus( WidgetID widget );
        bool           isHandled() const { return _bHandled == SW_TRUE; }
    };
} // namespace sw
