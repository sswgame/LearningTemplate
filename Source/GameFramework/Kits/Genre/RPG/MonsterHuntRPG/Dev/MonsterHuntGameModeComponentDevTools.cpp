/**
 * @file MonsterHuntGameModeComponentDevTools.cpp
 * @brief `MonsterHuntGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Component/MonsterHuntGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/GameData/DepletingValue.h"
#include "GameFramework/Runtime/Foundation/GameData/StatBlock.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/World/Query/WalkSweep.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( monsterHuntPhase, "MonsterHunt.Phase", "Monster hunt phase (0 haggle, 1 investigate, 2 hunt, 3 victory, 4 defeat)", MonsterHuntGameModeComponent,
                                &MonsterHuntGameModeComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( monsterHuntReward, "MonsterHunt.Reward", "Current offer, or the settled reward once the haggling closed", MonsterHuntGameModeComponent, &MonsterHuntGameModeComponent::getReward );
    SW_AUTOMATION_GETTER_WATCH( monsterHuntClues, "MonsterHunt.FoundClues", "Clues investigated", MonsterHuntGameModeComponent, &MonsterHuntGameModeComponent::getFoundClueCount );
    SW_AUTOMATION_GETTER_WATCH( monsterHuntKnowledge, "MonsterHunt.Knowledge", "Bestiary knowledge level of the hunted monster", MonsterHuntGameModeComponent, &MonsterHuntGameModeComponent::getKnowledge );
    SW_AUTOMATION_GETTER_WATCH( monsterHuntPlayerHP, "MonsterHunt.PlayerHP", "HP of the hunter", MonsterHuntGameModeComponent, &MonsterHuntGameModeComponent::getPlayerHP );
    SW_AUTOMATION_GETTER_WATCH( monsterHuntMonsterHP, "MonsterHunt.MonsterHP", "HP of the monster", MonsterHuntGameModeComponent, &MonsterHuntGameModeComponent::getMonsterHP );
    SW_AUTOMATION_GETTER_WATCH( monsterHuntOilHits, "MonsterHunt.OilHits", "Hits left on the blade oil", MonsterHuntGameModeComponent, &MonsterHuntGameModeComponent::getOilHits );
    SW_AUTOMATION_GETTER_WATCH( monsterHuntToxicity, "MonsterHunt.Toxicity", "Potion toxicity", MonsterHuntGameModeComponent, &MonsterHuntGameModeComponent::getToxicity );
    SW_AUTOMATION_GETTER_WATCH( monsterHuntAttackReady, "MonsterHunt.AttackReady", "1 when the attack cooldown is over", MonsterHuntGameModeComponent, &MonsterHuntGameModeComponent::getAttackReady );
    SW_AUTOMATION_GETTER_WATCH( monsterHuntInReach, "MonsterHunt.MonsterInReach", "1 when the monster is within the attack reach", MonsterHuntGameModeComponent,
                                &MonsterHuntGameModeComponent::getMonsterInReach );
    SW_AUTOMATION_GETTER_WATCH( monsterHuntQuest, "MonsterHunt.QuestDone", "1 when the quest of the contract is completed", MonsterHuntGameModeComponent, &MonsterHuntGameModeComponent::getQuestDone );
    SW_AUTOMATION_GETTER_WATCH( monsterHuntSkills, "MonsterHunt.LearnedSkills", "Skill ranks learned in this hunt", MonsterHuntGameModeComponent, &MonsterHuntGameModeComponent::getLearnedSkillCount );
    SW_AUTOMATION_GETTER_WATCH( monsterHuntIntensity, "MonsterHunt.SignIntensity", "Sign intensity (percent) from skills and mutagens", MonsterHuntGameModeComponent,
                                &MonsterHuntGameModeComponent::getSignIntensity );
    SW_AUTOMATION_GETTER_WATCH( monsterHuntPlayerX, "MonsterHunt.PlayerX", "Absolute ground-plane X (m) of the hunter", MonsterHuntGameModeComponent, &MonsterHuntGameModeComponent::getPlayerX );
} // namespace sw
