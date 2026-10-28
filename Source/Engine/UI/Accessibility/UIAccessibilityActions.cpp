#include "pch.h"

#include "Engine/UI/Accessibility/UIAccessibilityActions.h"

#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/WidgetTree/UIEventRouter.h"
#include "Engine/UI/WidgetTree/UIEvents.h"
#include "Engine/UI/WidgetTree/Widget.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"
#include "Engine/UI/Widgets/SliderWidget.h"

namespace sw
{
    namespace
    {
        /** @brief (화면, 위젯)을 찾습니다. 없거나 꺼졌으면 nullptr 입니다. 꺼진 위젯은 사람도 누르지 못한다. */
        Widget* findEnabledAccessibleWidget( UISystem& ui, uint32 screen, WidgetID widget )
        {
            UIScreen* pScreen = ui.findScreen( screen );
            if ( pScreen == nullptr || pScreen->isClosing() )
                return nullptr;
            Widget* pWidget = pScreen->getTree().findWidgetByID( widget );
            return pWidget != nullptr && pWidget->isEnabledInHierarchy() ? pWidget : nullptr;
        }
    } // namespace

    bool UIAccessibilityActions::invoke( UISystem& ui, uint32 screen, WidgetID widget )
    {
        Widget* pWidget = findEnabledAccessibleWidget( ui, screen, widget );
        if ( pWidget == nullptr )
            return false;
        const WidgetTree& tree = ui.findScreen( screen )->getTree();
        UIWidgetPath      path{};
        if ( UIEventRouter::makePathTo( tree, widget, path ) == false )
            return false;
        UIActionEvent event{};
        event._action = hashed_string( UIActionName::kAccept );
        WidgetID handler{ kInvalidWidgetID };
        return UIEventRouter::routeActionEvent( tree, path, event, handler ).isHandled();
    }

    bool UIAccessibilityActions::focus( UISystem& ui, uint32 screen, WidgetID widget )
    {
        UIScreen* pScreen = ui.findScreen( screen );
        if ( pScreen == nullptr || pScreen->isClosing() )
            return false;
        return ui.getInputRouter().getFocusManager().setFocus( pScreen->getTree(), widget );
    }

    bool UIAccessibilityActions::setRangeValue( UISystem& ui, uint32 screen, WidgetID widget, float32 value )
    {
        SliderWidget* pSlider = castTo<SliderWidget>( findEnabledAccessibleWidget( ui, screen, widget ) );
        if ( pSlider == nullptr )
            return false;
        pSlider->setValueFromUser( value );
        return true;
    }
} // namespace sw
