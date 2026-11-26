/**
 * @file SRPGCursorComponentDevTools.cpp
 * @brief `SRPGCursorComponent` 의 디버그 HUD 섹션 · 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Client/SRPGCursorComponent.h"
#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Component/SRPGGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Debug/GameplayDebugTarget.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    namespace
    {
        struct TacticsGrowthDebugSectionDevInternal
        {
            static void writeGrowth( DebugHUDSectionWriter& writer )
            {
                const GameObjectManager* const   pManager = GameplayDebugTarget::findActiveObjectManager();
                const SRPGCursorComponent* const pCursor  = pManager != nullptr ? GameplayDebugTarget::findTargetOrLowestIDComponent<SRPGCursorComponent>( writer.getSceneIterationKey(), *pManager ) : nullptr;
                if ( pCursor == nullptr )
                {
                    writer.addLine( "target", "none (no SRPG cursor in the active scene)" );
                    return;
                }
                pCursor->writeGrowthDebugLines( writer, *pManager );
            }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( srpgCursorX, "SRPG.CursorX", "Cursor cell X", SRPGCursorComponent, &SRPGCursorComponent::getCursorX );
    SW_AUTOMATION_GETTER_WATCH( srpgCursorY, "SRPG.CursorY", "Cursor cell Y", SRPGCursorComponent, &SRPGCursorComponent::getCursorY );
    SW_DEBUG_HUD_SECTION( tacticsGrowth, "tacticsgrowth", "Tactics growth (levels, xp, sources)", 290, DebugHUDSectionFlag::kNone, &TacticsGrowthDebugSectionDevInternal::writeGrowth );
} // namespace sw
