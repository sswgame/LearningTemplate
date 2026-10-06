/**
 * @file UiPointerState.h
 * @brief 포인터 하나의 UI 상태입니다 — 호버 경로 · 포인터를 잡은 위젯. 포인터 사건을 트리로 보내고 그 결과를 적용합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/UI/Core/UiEventRouter.h"
#include "Engine/UI/Core/UiEvents.h"
#include "Engine/UI/Core/WidgetTypes.h"

namespace sw
{
    class WidgetTree;

    /** @brief 포인터 사건 하나를 보낸 결과입니다. */
    struct UiPointerResult
    {
        WidgetId _handler{ kInvalidWidgetId };      ///< 처리한 위젯(없으면 무효)
        WidgetId _focusRequest{ kInvalidWidgetId }; ///< 처리한 위젯이 포커스를 옮겨 달라고 했다
        uint8    _bHandled{ SW_FALSE };             ///< 위젯이 처리했다 — 그 마우스 버튼은 이번 프레임 UI 가 먹은 입력이다
        uint8    _bHitWidget{ SW_FALSE };           ///< 점 아래에 위젯이 있었다(처리하지 않았어도)
    };
} // namespace sw

namespace sw
{
    /**
     * @class UiPointerState
     * @brief 포인터 하나(마우스는 0)의 호버 경로와 잡기를 듭니다(언리얼 FSlateUser 의 포인터 칸).
     * @details `process`: (1) 잡은 위젯이 있으면 경로 = 그 위젯까지의 조상 경로(히트 테스트 안 함), 없으면 히트 테스트 (2) 옛 호버 경로와 견줘 빠진 쪽에
     *          Leave(잎부터), 새로 든 쪽에 Enter(뿌리부터) — 경로를 타지 않는 알림(`Widget::onHoverChanged`) (3) 사건을 경로로 보내고 잡기 · 놓기를 적용.
     *          상태는 번호와 트리 포인터를 듭니다 — 트리를 지우기 전에 `forgetTree` 를 부릅니다(화면을 닫는 `UiSystem` 이 한다).
     */
    class SW_API UiPointerState
    {
    public:
        UiPointerState();

        /** @brief 포인터 사건 하나를 @p tree 로 보냅니다(호버 · 잡기 갱신 포함). */
        UiPointerResult process( WidgetTree& tree, const UiPointerEvent& event );
        /** @brief 호버를 모두 내립니다(포인터가 창을 떠났거나 이 트리가 입력을 못 받게 됐다 — 모달이 덮었다). */
        void clearHover();
        /** @brief @p tree 를 더 쓰지 않습니다 — 그 트리의 호버 · 잡기를 알림 없이 버립니다(트리를 지우기 직전). */
        void forgetTree( const WidgetTree& tree );

        WidgetId            getCapturedWidget() const { return _captured; }
        const WidgetTree*   getCaptureTree() const { return _pCaptureTree; }
        WidgetId            getHoveredWidget() const { return _hoverPath.getLeaf(); }
        const UiWidgetPath& getHoverPath() const { return _hoverPath; }

    private:
        /** @brief 호버 경로를 @p newPath(@p pTree 안)로 바꾸며 Leave · Enter 를 알립니다. */
        void updateHover( WidgetTree* pTree, const UiWidgetPath& newPath );

    private:
        UiWidgetPath _hoverPath;
        UiWidgetPath _scratchPath;
        WidgetTree*  _pHoverTree;
        WidgetTree*  _pCaptureTree;
        WidgetId     _captured;
    };
} // namespace sw
