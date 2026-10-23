/**
 * @file WidgetProbes.cpp
 * @brief 화면 마커의 자동화 탐침입니다(`UI.*`). 탐침 레지스트리(`Engine/Probe`)가 UI 보다 아래라 UI 가 자기 탐침을 등록합니다.
 */
#include "pch.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Probe/AutomationProbe.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/World/WidgetComponent.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 탐침 `UI.MarkerVisible` 입니다. 대상 오브젝트의 위젯 컴포넌트(목표 마커 포함)가 화면 마커로 지금 보이면 1 입니다.
         * @details 보임은 마커 내용 위젯의 가시성이다(`applyPlacement` 가 숨길 때 접는다) — 카메라 밖 · 최대 거리 밖 · 코드가 숨김 · 목표가 닫힘이 모두 0 이다.
         *          Screen 공간만 뜻이 있다(World 위젯은 마커가 아니라 0 이다). 마커는 UI 갱신이 놓으므로 단언은 그 갱신이 지난 프레임에 둔다.
         */
        [[nodiscard]] bool readWidgetMarkerVisible( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
        {
            const GameObject* pTarget = query.findTargetObject();
            if ( pTarget == nullptr )
            {
                outValue.setText( query.getTargetProblem() );
                return false;
            }
            const WidgetComponent* pWidget = pTarget->getComponent<WidgetComponent>();
            if ( pWidget == nullptr )
            {
                outValue.setText( query.describeMissing( "widget component" ) );
                return false;
            }
            const Widget* pContent = pWidget->getSpace() == WidgetSpace::Screen ? pWidget->getContent() : nullptr;
            outValue.setNumber( pContent != nullptr && pContent->getVisibility() != WidgetVisibility::Collapsed ? 1.0 : 0.0 );
            return true;
        }
    } // namespace

    SW_AUTOMATION_QUERY_PROBE( widgetMarkerVisible, "UI.MarkerVisible", "1 while the target object's screen-space widget marker is shown (0: collapsed, world space or no content)",
                               &readWidgetMarkerVisible, true, false );
} // namespace sw
