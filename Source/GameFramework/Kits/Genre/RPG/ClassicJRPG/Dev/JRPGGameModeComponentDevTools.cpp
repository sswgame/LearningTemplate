/**
 * @file JRPGGameModeComponentDevTools.cpp
 * @brief `JRPGGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Serializer/StateIO.h"

#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/Component/JRPGGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"
#include "GameFramework/Runtime/World/Query/WalkSweep.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( jrpgPhase, "JRPG.Phase", "JRPG phase (0 explore, 1 battle, 2 victory, 3 defeat, 4 timing)", JRPGGameModeComponent, &JRPGGameModeComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( jrpgCommandMember, "JRPG.CommandMember", "Party member whose battle command is awaited (-1 = none)", JRPGGameModeComponent,
                                &JRPGGameModeComponent::getCommandMember );
    SW_AUTOMATION_GETTER_WATCH( jrpgEnemies, "JRPG.EnemiesAlive", "Living enemies of the current battle (0 outside a battle)", JRPGGameModeComponent, &JRPGGameModeComponent::getEnemiesAlive );
    SW_AUTOMATION_GETTER_WATCH( jrpgBattleCount, "JRPG.BattleCount", "Battles opened in this run", JRPGGameModeComponent, &JRPGGameModeComponent::getBattleCount );
    SW_AUTOMATION_GETTER_WATCH( jrpgBattleRound, "JRPG.BattleRound", "Rounds resolved in the current or last battle", JRPGGameModeComponent, &JRPGGameModeComponent::getBattleRound );
    SW_AUTOMATION_GETTER_WATCH( jrpgTimingStep, "JRPG.TimingStep", "Timed swing awaited (member index, member count = block, -1 = none)", JRPGGameModeComponent, &JRPGGameModeComponent::getTimingStep );
    SW_AUTOMATION_GETTER_WATCH( jrpgTimingElapsed, "JRPG.TimingElapsed", "Seconds since the awaited swing started", JRPGGameModeComponent, &JRPGGameModeComponent::getTimingElapsed );
    SW_AUTOMATION_GETTER_WATCH( jrpgTimedCount, "JRPG.TimedCount", "Timed hits and blocks landed in this run", JRPGGameModeComponent, &JRPGGameModeComponent::getTimedCount );
    SW_AUTOMATION_GETTER_WATCH( jrpgLeaderAttack, "JRPG.LeaderAttack", "Attack of the first party member (strength and gear)", JRPGGameModeComponent,
                                &JRPGGameModeComponent::getLeaderAttack );
    SW_AUTOMATION_GETTER_WATCH( jrpgClassChanges, "JRPG.ClassChanges", "Class changes in this run", JRPGGameModeComponent, &JRPGGameModeComponent::getClassChangeCount );
    SW_AUTOMATION_GETTER_WATCH( jrpgGold, "JRPG.Gold", "Gold held by the party", JRPGGameModeComponent, &JRPGGameModeComponent::getGold );
    SW_AUTOMATION_GETTER_WATCH( jrpgLeaderHP, "JRPG.LeaderHP", "HP of the first party member", JRPGGameModeComponent, &JRPGGameModeComponent::getLeaderHP );
    SW_AUTOMATION_GETTER_WATCH( jrpgLeaderXP, "JRPG.LeaderXP", "Experience of the first party member", JRPGGameModeComponent, &JRPGGameModeComponent::getLeaderXP );
    SW_AUTOMATION_GETTER_WATCH( jrpgPlayerX, "JRPG.PlayerX", "Absolute ground-plane X (m) of the party", JRPGGameModeComponent, &JRPGGameModeComponent::getPlayerX );
} // namespace sw
