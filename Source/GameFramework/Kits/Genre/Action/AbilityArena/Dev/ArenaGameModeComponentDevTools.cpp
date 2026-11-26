/**
 * @file ArenaGameModeComponentDevTools.cpp
 * @brief `ArenaGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Action/AbilityArena/ArenaProjectileQuery.h"
#include "GameFramework/Kits/Genre/Action/AbilityArena/Component/ArenaGameModeComponent.h"
#include "GameFramework/Kits/Genre/Action/AbilityArena/Component/ArenaUnitComponent.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileDef.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ResolvedShot.h"
#include "GameFramework/Runtime/Character/Control/ControlSystem.h"
#include "GameFramework/Runtime/Character/Control/Controller/AIControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilitySystemComponent.h"
#include "GameFramework/Runtime/Gameplay/Ability/CombatAttributeSet.h"

namespace sw
{
    namespace
    {
        struct ArenaGameModeComponentDevInternal
        {
            [[nodiscard]] static bool readPlayerX( const ArenaGameModeComponent& gameMode, float64& outValue )
            {
                float3 position{};
                if ( gameMode.findUnitPosition( gameMode.getPlayerObject(), position ) == false )
                    return false;
                outValue = static_cast<float64>( position._x );
                return true;
            }

            [[nodiscard]] static bool readPlayerZ( const ArenaGameModeComponent& gameMode, float64& outValue )
            {
                float3 position{};
                if ( gameMode.findUnitPosition( gameMode.getPlayerObject(), position ) == false )
                    return false;
                outValue = static_cast<float64>( position._z );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_COMPONENT_WATCH( arenaPlayerX, "Arena.PlayerX", "World X of the arena player", ArenaGameModeComponent, &ArenaGameModeComponentDevInternal::readPlayerX );
    SW_AUTOMATION_COMPONENT_WATCH( arenaPlayerZ, "Arena.PlayerZ", "World Z of the arena player", ArenaGameModeComponent, &ArenaGameModeComponentDevInternal::readPlayerZ );
    SW_AUTOMATION_GETTER_WATCH( arenaPlayerShotCount, "Arena.PlayerShotCount", "Projectiles the player team fired this game", ArenaGameModeComponent, &ArenaGameModeComponent::getPlayerShotCount );
    SW_AUTOMATION_GETTER_WATCH( arenaKillCount, "Arena.KillCount", "Enemies felled this game", ArenaGameModeComponent, &ArenaGameModeComponent::getKillCount );
    SW_AUTOMATION_GETTER_WATCH( arenaPlayerControllerKind, "Arena.PlayerControllerKind", "0 = the player controller holds the player, 1 = the auto battle AI", ArenaGameModeComponent, &ArenaGameModeComponent::isPlayerDrivenByAI );
} // namespace sw
