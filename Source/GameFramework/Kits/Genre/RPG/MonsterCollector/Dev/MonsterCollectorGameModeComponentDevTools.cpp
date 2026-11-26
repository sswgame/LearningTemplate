/**
 * @file MonsterCollectorGameModeComponentDevTools.cpp
 * @brief `MonsterCollectorGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Serializer/StateIO.h"

#include "GameFramework/Kits/Genre/RPG/MonsterCollector/Component/MonsterCollectorGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/World/Query/WalkSweep.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( monsterCollectorPhase, "MonsterCollector.Phase", "Monster collector phase (0 explore, 1 battle, 2 victory, 3 defeat)", MonsterCollectorGameModeComponent,
                                &MonsterCollectorGameModeComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( monsterCollectorFoeHP, "MonsterCollector.FoeHP", "HP of the wild monster (0 outside a battle)", MonsterCollectorGameModeComponent,
                                &MonsterCollectorGameModeComponent::getFoeHP );
    SW_AUTOMATION_GETTER_WATCH( monsterCollectorBallCount, "MonsterCollector.BallCount", "Balls left", MonsterCollectorGameModeComponent, &MonsterCollectorGameModeComponent::getRemainingBallCount );
    SW_AUTOMATION_GETTER_WATCH( monsterCollectorCaptures, "MonsterCollector.CaptureCount", "Monsters captured in this outing", MonsterCollectorGameModeComponent,
                                &MonsterCollectorGameModeComponent::getCaptureCount );
    SW_AUTOMATION_GETTER_WATCH( monsterCollectorBattleCount, "MonsterCollector.BattleCount", "Battles opened in this outing", MonsterCollectorGameModeComponent,
                                &MonsterCollectorGameModeComponent::getBattleCount );
    SW_AUTOMATION_GETTER_WATCH( monsterCollectorPartySize, "MonsterCollector.PartySize", "Monsters in the party", MonsterCollectorGameModeComponent,
                                &MonsterCollectorGameModeComponent::getPartySize );
    SW_AUTOMATION_GETTER_WATCH( monsterCollectorLeaderXP, "MonsterCollector.LeaderXP", "Total experience of the first party monster", MonsterCollectorGameModeComponent,
                                &MonsterCollectorGameModeComponent::getLeaderXP );
    SW_AUTOMATION_GETTER_WATCH( monsterCollectorPlayerX, "MonsterCollector.PlayerX", "Absolute ground-plane X (m) of the player", MonsterCollectorGameModeComponent,
                                &MonsterCollectorGameModeComponent::getPlayerX );
} // namespace sw
