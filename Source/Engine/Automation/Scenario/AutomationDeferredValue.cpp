#include "pch.h"

#include "Engine/Automation/Scenario/AutomationDeferredValue.h"

namespace sw
{
    shared_ptr<AutomationDeferredValue> AutomationDeferredValue::create()
    {
        return make_shared<AutomationDeferredValue>();
    }
} // namespace sw
