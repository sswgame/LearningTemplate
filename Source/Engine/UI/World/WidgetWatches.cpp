/**
 * @file WidgetWatches.cpp
 * @brief 화면 마커의 자동화 감시 값입니다(`UI.*`). 감시 값 레지스트리(`Engine/Automation/Watch`)가 UI 보다 아래라 UI 가 자기 감시 값을 등록합니다.
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/World/WidgetComponent.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 감시 값 `UI.MarkerVisible` 입니다. 대상 오브젝트의 위젯 컴포넌트(목표 마커 포함)가 화면 마커로 지금 보이면 1 입니다.
         * @details 보임은 마커 내용 위젯의 가시성이다(`applyPlacement` 가 숨길 때 접는다) — 카메라 밖 · 최대 거리 밖 · 코드가 숨김 · 목표가 닫힘이 모두 0 이다.
         *          Screen 공간만 뜻이 있다(World 위젯은 마커가 아니라 0 이다). 마커는 UI 갱신이 놓으므로 단언은 그 갱신이 지난 프레임에 둔다.
         */
        [[nodiscard]] bool readWidgetMarkerVisible( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
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

        /**
         * @brief 감시 값 `UI.DeferredGlyphs` 입니다. 지난 UI 그리기가 글리프 래스터화 예산 때문에 미룬 글리프 수이며 0 이면 화면의 글자가 다 그려졌다.
         * @details 예산이 다음 프레임에 살아나지 않거나 미룬 위젯이 다시 칠해지지 않으면 이 값이 0 이 되지 않아 `WaitUntil` 이 시한으로 지고, 글자가 빈 화면이 통과하지 못한다.
         */
        [[nodiscard]] bool readDeferredGlyphs( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            const UISystem* pUI = engine::findSubsystem<UISystem>();
            outValue            = pUI != nullptr ? static_cast<float64>( pUI->getDeferredGlyphCount() ) : 0.0;
            return pUI != nullptr && pUI->isInitialized();
        }
    } // namespace

    SW_AUTOMATION_WATCH( uiDeferredGlyphs, "UI.DeferredGlyphs", "Glyphs the last UI paint left for the next frame because the glyph raster budget ran out (0: all text is drawn)", &readDeferredGlyphs );
    SW_AUTOMATION_QUERY_WATCH( widgetMarkerVisible, "UI.MarkerVisible", "1 while the target object's screen-space widget marker is shown (0: collapsed, world space or no content)",
                               &readWidgetMarkerVisible, true, false );
} // namespace sw
