/**
 * @file OverworldGameModeComponentDevTools.cpp
 * @brief `OverworldGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/TileMap/TileMapComponent.h"

#include "GameFramework/Kits/Feature/World/Overworld/Component/OverworldGameModeComponent.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/Dialogue/DialogueRunnerComponent.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( overworldHeroTileX, "Overworld.HeroTileX", "Tile column of the hero", OverworldGameModeComponent, &OverworldGameModeComponent::getHeroTileX );
    SW_AUTOMATION_GETTER_WATCH( overworldHeroTileY, "Overworld.HeroTileY", "Tile row of the hero (rows grow down)", OverworldGameModeComponent, &OverworldGameModeComponent::getHeroTileY );
    SW_AUTOMATION_GETTER_WATCH( overworldHeroWalking, "Overworld.HeroWalking", "1 while the hero walks between tiles (0 once it stands on a tile)", OverworldGameModeComponent, &OverworldGameModeComponent::isHeroWalking );
    SW_AUTOMATION_GETTER_WATCH( overworldHeroFacing, "Overworld.HeroFacing", "Hero facing (0 down, 1 left, 2 right, 3 up)", OverworldGameModeComponent, &OverworldGameModeComponent::getHeroFacing );
    SW_AUTOMATION_GETTER_WATCH( overworldTalking, "Overworld.Talking", "1 while a talk with a villager is open", OverworldGameModeComponent, &OverworldGameModeComponent::isTalking );
    SW_AUTOMATION_GETTER_WATCH( overworldShownLines, "Overworld.ShownLines", "Dialogue lines shown this session", OverworldGameModeComponent, &OverworldGameModeComponent::getShownLineCount );
    SW_AUTOMATION_GETTER_WATCH( overworldFinishedTalks, "Overworld.FinishedTalks", "Talks finished this session", OverworldGameModeComponent, &OverworldGameModeComponent::getFinishedTalkCount );
} // namespace sw
