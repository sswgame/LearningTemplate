/**
 * @file UIEvents.h
 * @brief 위젯이 받는 사건 값입니다 — 경로 단계 · 포인터 · 행동 · 글자와 처리 결과(UIReply).
 * @details 사건은 위젯 경로(뿌리 → 잎)를 따라 터널링(미리보기) 다음 버블링으로 갑니다. 처리(`UIReply::makeHandled`)하면 거기서 멈춥니다(언리얼 FReply).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/UI/Base/WidgetTypes.h"

namespace sw
{
    /** @brief 사건이 경로를 지나는 단계입니다. 터널링(뿌리 → 대상, 미리보기)이 먼저, 버블링(대상 → 뿌리)이 다음입니다. */
    enum class UIRoutePhase : uint8
    {
        Tunnel,
        Bubble
    };

    /** @brief 포인터 사건의 종류입니다. Enter · Leave 는 경로를 타지 않는 알림(`Widget::onHoverChanged`)이라 여기 없습니다. */
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
     * @brief 포인터 사건입니다(UI 단위, 화면 좌표). 마우스는 포인터 0 입니다(터치를 더하면 손가락마다 번호).
     */
    struct UIPointerEvent
    {
        float2             _position{};
        float2             _delta{};
        float32            _wheel{ 0.0f };
        UIPointerEventKind _kind{ UIPointerEventKind::Move };
        MouseButton        _button{ MouseButton::Left };
        uint8              _pointerIndex{ 0 };
        uint8              _clickCount{ 1 }; ///< 두 번 누름이면 2(InputMap doubleClickTime 과 같은 판정)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIActionEvent
     * @brief 행동(입력 맵의 `UI.*` 액션) 사건입니다 — 포커스 경로를 탑니다. 키 · 패드 · 가상 입력이 이 한 길로 옵니다.
     */
    struct UIActionEvent
    {
        hashed_string _action{};             ///< 액션 이름(`UIActionName` — `UI.Accept` · `UI.NavigateLeft` …)
        float2        _value{};              ///< 축 값(스틱 탐색 · 스크롤 — 위 · 오른쪽이 +) — 버튼이면 0
        float32       _deltaSeconds{ 0.0f }; ///< 이번 입력 프레임의 시간 — 축 행동(스크롤)이 속도 × 시간으로 옮긴다
        uint8         _bRepeat{ SW_FALSE };  ///< 누르고 있는 동안의 반복 발화이면 SW_TRUE
    };
} // namespace sw

namespace sw
{
    /** @brief UI 행동 맵(`engine/input/ui.input.xml`)의 행동 이름입니다 — `UISystem` 과 위젯(버튼 · 슬라이더 · 스크롤)이 같은 이름을 씁니다. */
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
        static constexpr utf8 kScroll[]        = "UI.Scroll";        ///< 패드 오른쪽 스틱 — 값을 든다
        static constexpr utf8 kTextBackspace[] = "UI.TextBackspace"; ///< 글 입력 칸이 키보드 포커스를 쥔 동안만 — 커서 앞 글자 하나 지우기
        static constexpr utf8 kPause[]         = "UI.Pause";         ///< 화면이 없을 때만(레이어 `UIGlobal`) — 일시정지 메뉴를 연다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UITextEvent
     * @brief 글자 입력 사건입니다 — 포커스 위젯 하나에만 갑니다(경로를 타지 않는다).
     */
    struct UITextEvent
    {
        string _text{};                   ///< UTF-8
        uint8  _bComposition{ SW_FALSE }; ///< IME 조합 중인 글(확정 전)이면 SW_TRUE
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIReply
     * @brief 사건 처리 결과입니다(언리얼 FReply). 처리되면 경로가 멈춥니다.
     */
    struct SW_API UIReply
    {
        WidgetID _focusRequest{ kInvalidWidgetID }; ///< 이 위젯으로 포커스를 옮겨 달라(버튼을 마우스로 눌러도 포커스가 따라온다)
        uint8    _bHandled{ SW_FALSE };
        uint8    _bCapturePointer{ SW_FALSE }; ///< 이 위젯이 포인터를 잡는다(슬라이더 끌기) — 뗄 때까지 사건이 이 위젯에게만
        uint8    _bReleasePointer{ SW_FALSE };

        static UIReply makeHandled();
        static UIReply makeUnhandled();
        UIReply&       capturePointer();
        UIReply&       releasePointer();
        UIReply&       requestFocus( WidgetID widget );
        bool           isHandled() const { return _bHandled == SW_TRUE; }
    };
} // namespace sw
