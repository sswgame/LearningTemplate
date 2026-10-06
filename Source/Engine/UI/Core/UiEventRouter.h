/**
 * @file UiEventRouter.h
 * @brief 히트 테스트와 사건 경로 — 화면 점 아래의 위젯 경로를 찾고, 경로를 따라 터널링 다음 버블링으로 사건을 보냅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/UI/Core/UiEvents.h"
#include "Engine/UI/Core/WidgetTypes.h"

namespace sw
{
    class WidgetTree;

    /**
     * @struct UiWidgetPath
     * @brief 위젯 경로입니다(뿌리 → 잎). 번호를 들므로 사건을 보내는 사이 떨어진 위젯은 보내는 쪽이 건너뜁니다.
     */
    struct UiWidgetPath
    {
        vector<WidgetId> _listWidget{};

        bool     isEmpty() const { return _listWidget.empty(); }
        WidgetId getLeaf() const { return _listWidget.empty() ? kInvalidWidgetId : _listWidget.back(); }
        bool     contains( WidgetId id ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UiEventRouter
     * @brief 히트 테스트 · 경로 만들기 · 경로로 사건 보내기입니다(언리얼 FSlateApplication 의 widget path · FReply, 유니티 TrickleDown/BubbleUp).
     * @details 꺼진 위젯(자기나 조상이 `setEnabled( false )`)은 히트 테스트에 걸려 아래로 클릭이 새지 않게 막지만 사건은 받지 않습니다.
     */
    struct SW_API UiEventRouter
    {
        /**
         * @brief 그리기 역순(위에 그린 것 먼저)으로 내려가며 점이 든 가장 깊은 위젯 경로를 찾습니다. 못 찾으면 false 이고 경로는 빕니다.
         * @details 보임: Collapsed · Hidden · HitTestInvisible 은 자기와 자식 모두 빠지고, SelfHitTestInvisible 은 자식만 받습니다.
         *          렌더 변환은 역변환으로 따르고, 자르는 패널 밖의 점은 그 자식도 받지 않습니다.
         */
        static bool hitTest( const WidgetTree& tree, const float2& screenPoint, UiWidgetPath& outPath );
        /** @brief @p widget 까지의 조상 경로(뿌리 → @p widget)를 만듭니다. 트리에 없으면 false 이고 경로는 빕니다. */
        static bool makePathTo( const WidgetTree& tree, WidgetId widget, UiWidgetPath& outPath );
        /**
         * @brief 경로로 포인터 사건을 보냅니다 — 터널링(앞에서부터) 뒤 버블링(뒤에서부터). 처리한 위젯이 나오면 멈춥니다.
         * @param outHandler 처리한 위젯 번호(없으면 무효).
         */
        static UiReply routePointerEvent( const WidgetTree& tree, const UiWidgetPath& path, const UiPointerEvent& event, WidgetId& outHandler );
        /** @brief 경로로 행동 사건을 보냅니다(포커스 경로). 규칙은 포인터와 같습니다. */
        static UiReply routeActionEvent( const WidgetTree& tree, const UiWidgetPath& path, const UiActionEvent& event, WidgetId& outHandler );
    };
} // namespace sw
