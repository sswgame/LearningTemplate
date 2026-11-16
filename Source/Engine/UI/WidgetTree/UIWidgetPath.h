/**
 * @file UIWidgetPath.h
 * @brief 히트 테스트와 이벤트 경로를 정의합니다. 화면 점 아래의 위젯 경로를 찾고, 경로를 따라 터널링 다음 버블링으로 이벤트를 보냅니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/UI/WidgetTree/UIEvents.h"
#include "Engine/UI/WidgetTree/WidgetTypes.h"

namespace sw
{
    class WidgetTree;

    /**
     * @struct SW_API UIWidgetPath
     * @brief 위젯 경로입니다(루트에서 잎). 번호를 가지므로 이벤트를 보내는 사이 떨어진 위젯은 보내는 쪽이 건너뜁니다.
     */
    struct UIWidgetPath
    {
        vector<WidgetID> _listWidget{};

        bool     isEmpty() const { return _listWidget.empty(); }
        WidgetID getLeaf() const { return _listWidget.empty() ? kInvalidWidgetID : _listWidget.back(); }
        bool     contains( WidgetID id ) const;

        // 히트 테스트, 경로 만들기, 경로로 이벤트 보내기를 담당합니다(언리얼 FSlateApplication 의 widget path 와 FReply, 유니티의 TrickleDown/BubbleUp 에 해당).
        // 꺼진 위젯(자기나 조상이 `setEnabled( false )`)은 히트 테스트에는 걸려 아래로 클릭이 새지 않게 막지만 이벤트는 받지 않는다.
        /**
         * @brief 그리기 역순(위에 그린 것이 먼저이며, z 순서 패널은 `PanelWidget::collectPaintOrder` 의 역순)으로 내려가며 점을 포함하는 가장 깊은 위젯 경로를 찾습니다.
         *        못 찾으면 false 이고 경로는 비어 있습니다.
         * @details 보임: Collapsed, Hidden, HitTestInvisible 은 자기와 자식 모두 제외하고, SelfHitTestInvisible 은 자식만 받는다.
         *          렌더 변환은 역변환으로 따르고, 자르는 패널 밖의 점은 그 자식도 받지 않는다.
         */
        static bool hitTest( const WidgetTree& tree, const float2& screenPoint, UIWidgetPath& outPath );
        /**
         * @brief 손가락 히트 테스트입니다. 점이 조작(포커스를 받는 위젯)에 바로 닿으면 `hitTest` 와 같고, 아니면 상자를 @p minTargetSize 까지 넓힌 조작 중 중심이 가장 가까운 것을 고릅니다.
         * @details 작은 단추를 손가락으로 누르기 어렵게 하지 않는다 — Android `TouchDelegate` · Material 의 48dp 터치 영역과 같은 뜻이다(기억). 넓혀도 닿는 조작이 없으면 `hitTest` 의 결과다.
         *          @p minTargetSize 가 0 이하이면 넓히지 않는다.
         */
        static bool hitTestTouch( const WidgetTree& tree, const float2& screenPoint, float32 minTargetSize, UIWidgetPath& outPath );
        /** @brief @p widget 까지의 조상 경로(루트에서 @p widget)를 만듭니다. 트리에 없으면 false 이고 경로는 비어 있습니다. */
        static bool makePathTo( const WidgetTree& tree, WidgetID widget, UIWidgetPath& outPath );
        /**
         * @brief 경로로 포인터 이벤트를 보냅니다. 터널링(앞에서부터) 뒤 버블링(뒤에서부터) 순서이며, 처리한 위젯이 나오면 멈춥니다.
         *        보내는 동안 트리는 전달 중(`WidgetTree::DispatchScope`)이라 핸들러가 떼어 낸 위젯은 끝난 뒤 지워집니다.
         * @param outHandler 처리한 위젯 번호입니다(없으면 무효).
         */
        static UIReply routePointerEvent( WidgetTree& tree, const UIWidgetPath& path, const UIPointerEvent& event, WidgetID& outHandler );
        /** @brief 경로로 액션 이벤트를 보냅니다(포커스 경로). 규칙은 포인터와 같습니다. */
        static UIReply routeActionEvent( WidgetTree& tree, const UIWidgetPath& path, const UIActionEvent& event, WidgetID& outHandler );
    };
} // namespace sw
