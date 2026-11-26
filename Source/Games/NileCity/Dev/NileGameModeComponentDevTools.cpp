/**
 * @file NileGameModeComponentDevTools.cpp
 * @brief `NileGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "Games/NileCity/NileGameModeComponent.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( nileMonth, "Nile.Month", "Months the city has run", NileGameModeComponent, &NileGameModeComponent::getMonthCount );
} // namespace sw
