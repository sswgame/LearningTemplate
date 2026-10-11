/**
 * @file UIPointerState.h
 * @brief 포인터 하나의 UI 상태입니다. 호버 경로와 포인터를 잡은 위젯을 가지며, 포인터 이벤트를 트리로 보내고 그 결과를 적용합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/UI/Base/UIEventRouter.h"
#include "Engine/UI/Base/UIEvents.h"
#include "Engine/UI/Base/WidgetTypes.h"

namespace sw
{
    class WidgetTree;

    /** @brief 포인터 이벤트 하나를 보낸 결과입니다. */
    struct UIPointerResult
    {
        WidgetID _handler{ kInvalidWidgetID };      ///< 처리한 위젯입니다(없으면 무효).
        WidgetID _focusRequest{ kInvalidWidgetID }; ///< 처리한 위젯이 포커스를 옮겨 달라고 요청했습니다.
        uint8    _bHandled{ SW_FALSE };             ///< 위젯이 처리했는지 나타냅니다. 처리했으면 그 마우스 버튼은 이번 프레임 UI 가 소비한 입력입니다.
        uint8    _bHitWidget{ SW_FALSE };           ///< 점 아래에 위젯이 있었는지 나타냅니다(처리하지 않았어도 true).
    };
} // namespace sw

namespace sw
{
    /**
     * @class UIPointerState
     * @brief 포인터 하나(마우스는 0)의 호버 경로와 포인터 잡기 상태를 가집니다(언리얼 FSlateUser 의 포인터 상태에 해당).
     * @details `process` 의 순서는 다음과 같다. (1) 잡은 위젯이 있으면 경로를 그 위젯까지의 조상 경로로 정하고(히트 테스트 안 함), 없으면 히트 테스트를 한다.
     *          (2) 기존 호버 경로와 비교해 빠진 쪽에 Leave(잎부터), 새로 들어온 쪽에 Enter(루트부터)를 알린다(경로를 타지 않는 알림 `Widget::onHoverChanged`).
     *          (3) 이벤트를 경로로 보내고 잡기와 놓기를 적용한다.
     *          상태는 번호와 트리 포인터를 가지므로, 트리를 지우기 전에 `forgetTree` 를 부른다(화면을 닫는 `UISystem` 이 한다).
     */
    class SW_API UIPointerState
    {
    public:
        UIPointerState();

        /** @brief 포인터 이벤트 하나를 @p tree 로 보냅니다(호버와 잡기 갱신 포함). */
        UIPointerResult process( WidgetTree& tree, const UIPointerEvent& event );
        /** @brief 호버를 모두 해제합니다(포인터가 창을 떠났거나 이 트리가 입력을 받지 못하게 됐을 때. 예를 들어 모달이 덮은 경우). */
        void clearHover();
        /** @brief @p tree 를 더 쓰지 않습니다. 그 트리의 호버와 잡기를 알림 없이 버립니다(트리를 지우기 직전에 부릅니다). */
        void forgetTree( const WidgetTree& tree );

        WidgetID            getCapturedWidget() const { return _captured; }
        const WidgetTree*   getCaptureTree() const { return _pCaptureTree; }
        WidgetID            getHoveredWidget() const { return _hoverPath.getLeaf(); }
        const UIWidgetPath& getHoverPath() const { return _hoverPath; }
        /** @brief 호버 경로가 속한 트리입니다(없으면 nullptr). */
        const WidgetTree* getHoverTree() const { return _pHoverTree; }

    private:
        /** @brief 호버 경로를 @p newPath(@p pTree 안)로 바꾸며 Leave 와 Enter 를 알립니다. */
        void updateHover( WidgetTree* pTree, const UIWidgetPath& newPath );

    private:
        UIWidgetPath _hoverPath;
        UIWidgetPath _scratchPath;
        WidgetTree*  _pHoverTree;
        WidgetTree*  _pCaptureTree;
        WidgetID     _captured;
    };
} // namespace sw
