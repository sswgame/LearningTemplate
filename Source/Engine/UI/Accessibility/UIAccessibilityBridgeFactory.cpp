#include "pch.h"

#include "Engine/UI/Accessibility/UIAccessibilityBridgeFactory.h"

#if defined( SW_PLATFORM_WINDOWS ) && defined( SW_WITH_CLIENT_CODE )
    #include "Engine/UI/Accessibility/Windows/UiaAccessibilityBridge.h"
#elif defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE ) && defined( SW_WITH_ATSPI )
    #include "Engine/UI/Accessibility/Linux/AtSpiAccessibilityBridge.h"
#endif

namespace sw
{
    unique_ptr<IUIAccessibilityBridge> UIAccessibilityBridgeFactory::createForWindow( [[maybe_unused]] UISystem& ui, [[maybe_unused]] IWindow& window )
    {
#if defined( SW_PLATFORM_WINDOWS ) && defined( SW_WITH_CLIENT_CODE )
        return UiaAccessibility::createBridge( ui, window );
#elif defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE ) && defined( SW_WITH_ATSPI )
        return AtSpiAccessibility::createBridge( ui, window );
#else
        return nullptr;
#endif
    }
} // namespace sw
